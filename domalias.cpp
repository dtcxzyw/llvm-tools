// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/DomConditionCache.h>
#include <llvm/Analysis/SimplifyQuery.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Pass.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MathExtras.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>

constexpr uint32_t MaxDepth = 6;

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::OptionCategory ExtractorCategory("Extractor options");
static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"),
             cl::cat(ExtractorCategory));

static bool visitFunc(Function &F) {
  DominatorTree DT(F);
  //   AssumptionCache AC(F);
  //   DomConditionCache DC;
  //   SimplifyQuery SQ{F.getParent()->getDataLayout(), nullptr, &DT, &AC};
  //   SQ.DC = &DC;
  //   ReversePostOrderTraversal<Function *> RPOT(&F);

  for (auto &BB : F) {
    for (auto &I : make_early_inc_range(BB)) {
      if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
        auto *LHS = dyn_cast<LoadInst>(Cmp->getOperand(0));
        auto *RHS = dyn_cast<LoadInst>(Cmp->getOperand(1));
        if (LHS && RHS && LHS->isSimple() && RHS->isSimple()) {
          auto *DTN = DT.getNode(&BB);

          while (DTN && DTN->getIDom()) {
            auto *DomBB = DTN->getIDom()->getBlock();
            if (auto *Branch = dyn_cast<BranchInst>(DomBB->getTerminator())) {
              CmpPredicate Pred;
              if (Branch->isConditional() &&
                  match(Branch->getCondition(),
                        m_c_ICmp(Pred, m_Specific(LHS->getPointerOperand()),
                                 m_Specific(RHS->getPointerOperand()))) &&
                  ICmpInst::isEquality(Pred)) {
                BasicBlockEdge Edge(DomBB, Pred == ICmpInst::ICMP_EQ
                                               ? Branch->getSuccessor(0)
                                               : Branch->getSuccessor(1));
                if (DT.dominates(Edge, &BB))
                  return true;
              }
            }

            DTN = DTN->getIDom();
          }
        }
      }
    }

    // auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
    // if (!BI || BI->isUnconditional())
    //   continue;
    // DC.registerBranch(BI);
  }
  return false;
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "potential dead code extractor\n");
  std::vector<std::string> BlockList{};

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.string().find("/optimized/") == std::string::npos)
        continue;
      if (Path.extension() == ".ll") {
        bool Blocked = false;
        for (auto &Pattern : BlockList)
          if (Path.string().find(Pattern) != std::string::npos) {
            Blocked = true;
            break;
          }
        if (!Blocked)
          InputFiles.push_back(Path);
      }
    }
  }
  errs() << "Input files: " << InputFiles.size() << '\n';
  LLVMContext Context;
  Context.setDiagnosticHandlerCallBack([](const DiagnosticInfo *DI, void *) {});
  uint32_t Count = 0;
  std::set<std::string> Interesting;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    // errs() << Path << '\n';
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    for (auto &F : *M) {
      if (F.empty())
        continue;
      if (visitFunc(F)) {
        Interesting.insert(
            fs::relative(fs::absolute(Path), InputDir.c_str()).string());
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  errs() << Interesting.size() << "\n";
  for (auto &Path : Interesting) {
    errs() << Path << "\n";
  }

  return EXIT_SUCCESS;
}
