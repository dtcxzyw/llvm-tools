// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
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
#include <llvm/TargetParser/Triple.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

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
  unsigned Count = 0;
  std::set<std::string> Interesting;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    for (auto &F : *M) {
      if (F.empty())
        continue;

      for (auto &BB : F) {
        for (auto &I : BB) {
          Value *X;
          const APFloat *FC1, *FC2, *FC3;
          CmpPredicate Pred;
          auto MaxMin = m_CombineOr(
              m_CombineOr(
                  m_CombineOr(m_FMinNum(m_Deferred(X), m_APFloat(FC2)),
                              m_FMaxNum(m_Deferred(X), m_APFloat(FC2))),
                  m_CombineOr(m_FMinimum(m_Deferred(X), m_APFloat(FC2)),
                              m_FMaximum(m_Deferred(X), m_APFloat(FC2)))),
              m_CombineOr(m_FMinimumNum(m_Deferred(X), m_APFloat(FC2)),
                          m_FMaximumNum(m_Deferred(X), m_APFloat(FC2))));
          if (match(&I, m_Select(m_FCmp(Pred, m_Value(X), m_APFloat(FC1)),
                                 MaxMin, m_APFloat(FC3))) ||
              match(&I, m_Select(m_FCmp(Pred, m_Value(X), m_APFloat(FC1)),
                                 m_APFloat(FC3), MaxMin))) {
            if (FC1->bitwiseIsEqual(*FC3)) {
              Interesting.insert(
                  fs::relative(fs::absolute(Path), InputDir.c_str()).string());
              break;
            }
          }
        }
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
