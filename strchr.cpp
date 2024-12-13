// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/ValueTracking.h>
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
#include <unordered_map>

using namespace llvm;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

static uint32_t foldStrChr(CallInst *Call, LibFunc Func) {
  if (isa<Constant>(Call->getArgOperand(1)))
    return 0;

  StringRef Str;
  Value *Base = Call->getArgOperand(0);
  if (!getConstantStringInfo(Base, Str, /*TrimAtNul=*/Func == LibFunc_strchr))
    return 0;

  uint64_t N = Str.size();
  if (Func == LibFunc_memchr) {
    if (auto *ConstInt = dyn_cast<ConstantInt>(Call->getArgOperand(2))) {
      uint64_t Val = ConstInt->getZExtValue();
      // Ignore the case that n is larger than the size of string.
      if (Val > N)
        return 0;
      N = Val;
    } else
      return 0;
  }

  // return N;
  std::set<char> Chars;
  for (uint64_t I = 0; I < N; ++I)
    Chars.insert(Str[I]);

  return Chars.size();
}

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
  std::map<uint32_t, uint32_t> LenDist;
  uint32_t Count = 0;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    TargetLibraryInfoImpl TLIImpl(Triple(M->getTargetTriple()));

    for (auto &F : *M) {
      if (F.empty())
        continue;

      TargetLibraryInfo TLI(TLIImpl, &F);

      for (auto &BB : F) {
        for (auto &I : BB) {
          if (auto *Call = dyn_cast<CallInst>(&I)) {
            LibFunc Func;
            if (TLI.getLibFunc(*Call, Func) && (Func == LibFunc_memchr)) {
              if (auto Len = foldStrChr(Call, Func))
                ++LenDist[Len];
            }
          }
        }
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  for (auto [K, V] : LenDist)
    errs() << K << ' ' << V << '\n';

  return EXIT_SUCCESS;
}
