// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include "llvm/IR/CmpPredicate.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
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
#include <memory>
#include <set>

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "scanner\n");

  std::vector<std::string> BlockList{};

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
  uint32_t PatternCount = 0;
  uint32_t ConstantRHSCount = 0;
  std::set<std::string> Patterns;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    auto &DL = M->getDataLayout();

    for (auto &F : *M) {
      if (F.empty())
        continue;

      for (auto &BB : F) {
        for (auto &I : BB) {
          auto VisitCond = [&](Value *Cond) {
            Value *X, *RHS;
            CmpPredicate Pred;
            if (match(Cond, m_ICmp(Pred, m_NUWMul(m_Value(X), m_Deferred(X)),
                                   m_Value(RHS)))) {
              // errs() << "Found: " << *X << ' ' << *RHS << '\n';
              PatternCount += 1;
              if (isa<Constant>(RHS)) {
                ConstantRHSCount += 1;
              }
              Patterns.insert(
                  fs::relative(Path, std::string{InputDir}).string());
            }
          };
          if (auto *AI = dyn_cast<AssumeInst>(&I)) {
            Value *Cond = AI->getArgOperand(0);
            VisitCond(Cond);
          }

          if (auto *BI = dyn_cast<BranchInst>(&I)) {
            if (BI->isConditional()) {
              Value *Cond = BI->getCondition();
              VisitCond(Cond);
            }
          }
        }
      }
    }
    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';
  errs() << "Pattern count: " << PatternCount << '\n';
  errs() << "Constant RHS count: " << ConstantRHSCount << '\n';
  for (auto &Path : Patterns)
    errs() << Path << '\n';

  return EXIT_SUCCESS;
}