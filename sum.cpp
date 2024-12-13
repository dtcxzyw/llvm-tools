// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/InstSimplifyFolder.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/ConstantRange.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
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
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
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
#include <filesystem>

namespace fs = std::filesystem;
using namespace llvm;

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};

  std::vector<fs::path> InputFiles;
  std::vector<std::string> BlockList{};
  for (auto &Entry : fs::recursive_directory_iterator(std::string(argv[1]))) {
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
  uint32_t Count = 0;
  uint32_t ModuleCount = 0;
  uint32_t FuncCount = 0;
  uint32_t BBCount = 0;
  uint32_t InstrCount = 0;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    LLVMContext Context;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    ++ModuleCount;
    for (auto &F : *M) {
      if (F.empty())
        continue;
      ++FuncCount;
      for (auto &BB : F) {
        ++BBCount;
        InstrCount += BB.size();
      }
    }

    errs() << "\rProgress: " << ++Count;
  }

  errs() << "\n";
  errs() << "Module " << ModuleCount << '\n';
  errs() << "Func " << FuncCount << '\n';
  errs() << "BB " << BBCount << '\n';
  errs() << "Instr " << InstrCount << '\n';

  return EXIT_SUCCESS;
}
