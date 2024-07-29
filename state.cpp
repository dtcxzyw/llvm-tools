// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/ConstantFolding.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
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
#include <map>
#include <memory>
#include <set>

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

std::map<uint32_t, uint32_t> PhiCountTable;

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

    bool Contains = false;
    for (auto &F : *M) {
      if (F.empty())
        continue;

      for (auto &BB : F) {
        uint32_t PhiCount = 0;
        for (auto &PHI : BB.phis()) {
          if (!PHI.getType()->isIntegerTy(1))
            continue;
          bool AllConst = true;
          for (Value *V : PHI.incoming_values())
            if (!match(V, m_Zero()) && !match(V, m_One())) {
              AllConst = false;
              break;
            }
          if (AllConst)
            PhiCount++;
        }
        if (PhiCount) {
          PhiCountTable[PhiCount]++;
          if (PhiCount >= 8)
            Contains = true;
        }
      }
    }

    if (Contains) {
      Names.insert(fs::relative(fs::absolute(Path), BaseDir));
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  errs() << Names.size() << '\n';
  for (auto &Name : Names)
    errs() << Name << '\n';

  for (auto [K, V] : PhiCountTable)
    errs() << K << ' ' << V << '\n';

  return EXIT_SUCCESS;
}
