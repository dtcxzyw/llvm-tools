// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include "llvm/IR/InstrTypes.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/ValueTracking.h>
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
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;
namespace fs = std::filesystem;
using namespace PatternMatch;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

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
  LLVMContext Context;
  uint32_t Count = 0;
  uint32_t AssumeCount = 0;
  uint32_t DCCount = 0;
  std::unordered_set<std::string> AssumeSet;
  std::unordered_set<std::string> DCSet;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    auto &DL = M->getDataLayout();

    for (auto &F : *M) {
      if (F.empty())
        continue;

      SmallVector<Instruction *, 16> AssumeInsts;
      for (auto &BB : F) {
        for (auto &I : BB) {
          if (isa<AssumeInst>(&I)) {
            AssumeInsts.push_back(&I);
          }
        }
      }

      DominatorTree DT{F};

      auto IsImpliedByAssumes = [&](Instruction *I, Value *X, Value *Y) {
        for (auto *Assume : AssumeInsts) {
          Value *Cond = Assume->getOperand(0);
          if (!match(Cond, m_SpecificICmp(ICmpInst::ICMP_EQ,
                                          m_IRem(m_Specific(X), m_Specific(Y)),
                                          m_Zero())))
            continue;
          if (isValidAssumeForContext(Assume, I, &DT))
            return true;
        }
        return false;
      };

      auto IsImpliedByDominatingConditions = [&](Instruction *I, Value *X,
                                                 Value *Y) {
        auto *BB = I->getParent();
        if (!DT.isReachableFromEntry(BB))
          return false;
        auto *Node = DT.getNode(BB);
        while (Node->getIDom()) {
          auto *Pred = Node->getIDom();

          if (auto *BI =
                  dyn_cast<BranchInst>(Pred->getBlock()->getTerminator());
              BI && BI->isConditional()) {
            auto *Cond = BI->getCondition();

            if (BI->getSuccessor(0) != BI->getSuccessor(1)) {
              ICmpInst::Predicate Pred;
              if (BI->getSuccessor(0) == Node->getBlock())
                Pred = ICmpInst::ICMP_EQ;
              else
                Pred = ICmpInst::ICMP_NE;
              if (match(Cond, m_SpecificICmp(
                                  Pred, m_IRem(m_Specific(X), m_Specific(Y)),
                                  m_Zero())))
                return true;
            }
          }

          Node = Pred;
        }
        return false;
      };

      for (auto &BB : F) {
        for (auto &I : BB) {
          Value *X, *Y;
          if (match(&I, m_IDiv(m_Value(X), m_Value(Y))) && !I.isExact()) {
            if (IsImpliedByAssumes(&I, X, Y)) {
              ++AssumeCount;
              AssumeSet.insert(Path.string());
              continue;
            }
            if (IsImpliedByDominatingConditions(&I, X, Y)) {
              ++DCCount;
              DCSet.insert(Path.string());
              continue;
            }
          }
        }
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  errs() << "Assume: " << AssumeCount << '\n';
  for (auto &Path : AssumeSet)
    errs() << Path << '\n';

  errs() << "DC: " << DCCount << '\n';
  for (auto &Path : DCSet)
    errs() << Path << '\n';

  return EXIT_SUCCESS;
}
