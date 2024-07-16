// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

// Match a simple increment by constant operation.  Note that if a sub is
// matched, the step is negated (as if the step had been canonicalized to
// an add, even though we leave the instruction alone.)
static bool matchIncrement(const Instruction *IVInc, Instruction *&LHS,
                           Constant *&Step) {
  if (match(IVInc, m_Add(m_Instruction(LHS), m_Constant(Step))) ||
      match(IVInc, m_ExtractValue<0>(m_Intrinsic<Intrinsic::uadd_with_overflow>(
                       m_Instruction(LHS), m_Constant(Step)))))
    return true;
  if (match(IVInc, m_Sub(m_Instruction(LHS), m_Constant(Step))) ||
      match(IVInc, m_ExtractValue<0>(m_Intrinsic<Intrinsic::usub_with_overflow>(
                       m_Instruction(LHS), m_Constant(Step))))) {
    Step = ConstantExpr::getNeg(Step);
    return true;
  }
  return false;
}

/// If given \p PN is an inductive variable with value IVInc coming from the
/// backedge, and on each iteration it gets increased by Step, return pair
/// <IVInc, Step>. Otherwise, return std::nullopt.
static std::optional<std::pair<Instruction *, Constant *>>
getIVIncrement(const PHINode *PN, const LoopInfo *LI) {
  const Loop *L = LI->getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent() || !L->getLoopLatch())
    return std::nullopt;
  auto *IVInc =
      dyn_cast<Instruction>(PN->getIncomingValueForBlock(L->getLoopLatch()));
  if (!IVInc || LI->getLoopFor(IVInc->getParent()) != L)
    return std::nullopt;
  Instruction *LHS = nullptr;
  Constant *Step = nullptr;
  if (matchIncrement(IVInc, LHS, Step) && LHS == PN)
    return std::make_pair(IVInc, Step);
  return std::nullopt;
}

static bool isRemOfLoopIncrementWithLoopInvariant(
    Value *Rem, const LoopInfo *LI, Value *&RemAmtOut,
    std::optional<bool> &AddOrSubOut, Value *&AddOrSubOffsetOut,
    PHINode *&LoopIncrPNOut) {
  Value *Incr, *RemAmt;
  if (!isa<Instruction>(Rem))
    return false;
  // NB: If RemAmt is a power of 2 it *should* have been transformed by now.
  if (!match(Rem, m_URem(m_Value(Incr), m_Value(RemAmt))))
    return false;

  // Only trivially analyzable loops.
  Loop *L = LI->getLoopFor(cast<Instruction>(Rem)->getParent());
  if (L == nullptr || L->getLoopPreheader() == nullptr ||
      L->getLoopLatch() == nullptr)
    return false;

  std::optional<bool> AddOrSub;
  Value *AddOrSubOffset;
  // Find out loop increment PHI.
  PHINode *PN = dyn_cast<PHINode>(Incr);
  if (PN != nullptr) {
    AddOrSub = std::nullopt;
    AddOrSubOffset = nullptr;
  } else {
    // Search through a NUW add/sub.
    Value *V0, *V1;
    if (match(Incr, m_NUWAddLike(m_Value(V0), m_Value(V1))))
      AddOrSub = true;
    else if (match(Incr, m_NUWSub(m_Value(V0), m_Value(V1))))
      AddOrSub = false;
    else
      return false;

    PN = dyn_cast<PHINode>(V0);
    if (PN != nullptr) {
      AddOrSubOffset = V1;
    } else if (*AddOrSub) {
      PN = dyn_cast<PHINode>(V1);
      AddOrSubOffset = V0;
    }
  }

  if (PN == nullptr)
    return false;

  // This isn't strictly necessary, what we really need is one increment and any
  // amount of initial values all being the same.
  if (PN->getNumIncomingValues() != 2)
    return false;

  // Only works if the remainder amount is a loop invaraint
  if (!L->isLoopInvariant(RemAmt))
    return false;

  // Is the PHI a loop increment?
  auto LoopIncrInfo = getIVIncrement(PN, LI);
  if (!LoopIncrInfo.has_value())
    return false;

  // We need remainder_amount % increment_amount to be zero. Increment of one
  // satisfies that without any special logic and is overwhelmingly the common
  // case.
  if (!match(LoopIncrInfo->second, m_One()))
    return false;

  // Need the increment to not overflow.
  if (!match(LoopIncrInfo->first, m_NUWAdd(m_Value(), m_Value())))
    return false;

  if (PN->getBasicBlockIndex(L->getLoopLatch()) < 0 ||
      PN->getBasicBlockIndex(L->getLoopPreheader()) < 0)
    return false;

  // Set output variables.
  RemAmtOut = RemAmt;
  LoopIncrPNOut = PN;
  AddOrSubOut = AddOrSub;
  AddOrSubOffsetOut = AddOrSubOffset;

  return true;
}

static bool foldURemOfLoopIncrement(Instruction *Rem, const LoopInfo *LI) {
  std::optional<bool> AddOrSub;
  Value *AddOrSubOffset, *RemAmt;
  PHINode *LoopIncrPN;
  if (!isRemOfLoopIncrementWithLoopInvariant(Rem, LI, RemAmt, AddOrSub,
                                             AddOrSubOffset, LoopIncrPN))
    return false;

  // Only non-constant remainder as the extra IV is is probably not profitable
  // in that case. Further, since remainder amount is non-constant, only handle
  // case where `IncrLoopInvariant` and `Start` are 0 to entirely eliminate the
  // rem (as opposed to just hoisting it outside of the loop).
  //
  // Potential TODO: Should we have a check for how "nested" this remainder
  // operation is? The new code runs every iteration so if the remainder is
  // guarded behind unlikely conditions this might not be worth it.
  if (AddOrSub.has_value() || match(RemAmt, m_ImmConstant()))
    return false;
  Loop *L = LI->getLoopFor(Rem->getParent());
  if (!match(LoopIncrPN->getIncomingValueForBlock(L->getLoopPreheader()),
             m_Zero()))
    return false;
  return true;
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "scanner\n");

  std::vector<std::string> BlockList{
      "ruby/optimized/vm.ll",
      "/regexec.ll",
      "quickjs/optimized/quickjs.ll",
  };

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.extension() == ".ll" &&
          Path.string().find("/optimized/") != std::string::npos) {
        auto Str = Path.string();
        bool Blocked = false;
        for (auto &Pattern : BlockList)
          if (Str.find(Pattern) != std::string::npos) {
            Blocked = true;
            break;
          }
        if (!Blocked)
          InputFiles.push_back(Path);
      }
    }
  }
  errs() << "Input files: " << InputFiles.size() << '\n';
  auto BaseDir = fs::absolute(std::string(InputDir));
  uint32_t Count = 0;
  std::set<std::string> Names;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    LLVMContext Context;
    // errs() << Path.string() << '\n';
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    for (auto &F : *M) {
      if (F.empty())
        continue;

      FunctionAnalysisManager FAM;
      FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
      FAM.registerPass([&] { return DominatorTreeAnalysis(); });
      FAM.registerPass([&] { return LoopAnalysis(); });
      auto &LI = FAM.getResult<LoopAnalysis>(F);

      if (LI.empty())
        continue;

      bool Contains = false;

      for (auto &BB : F) {
        for (auto &I : BB) {
          if (I.getOpcode() != Instruction::URem)
            continue;

          if (foldURemOfLoopIncrement(&I, &LI)) {
            Contains = true;
            break;
          }
        }
        if (Contains)
          break;
      }

      if (Contains) {
        Names.insert(fs::relative(fs::absolute(Path), BaseDir));
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  for (auto &Name : Names)
    errs() << Name << '\n';

  return EXIT_SUCCESS;
}
