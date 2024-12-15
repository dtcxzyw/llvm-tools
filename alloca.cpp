// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

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
#include <unordered_map>

using namespace llvm;
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
  uint32_t FindCount = 0;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    auto &DL = M->getDataLayout();
    bool Found = false;
    for (auto &F : *M) {
      if (F.empty())
        continue;

      for (auto &I : F.getEntryBlock()) {
        if (auto *AI = dyn_cast<AllocaInst>(&I)) {
          if (!AI->getAllocatedType()->isIntOrPtrTy() &&
              !AI->getAllocatedType()->isFloatTy())
            continue;
          if (auto Size = AI->getAllocationSizeInBits(DL);
              Size.value_or(TypeSize::getFixed(128)) > 64)
            continue;

          SmallVector<Instruction *, 16> WorkList;
          WorkList.push_back(AI);
          SmallPtrSet<Instruction *, 16> Visited;
          bool UsedByOther = false;
          while (!WorkList.empty()) {
            auto *I = WorkList.pop_back_val();
            if (!Visited.insert(I).second)
              continue;
            for (auto *U : I->users()) {
              if (auto *UI = dyn_cast<Instruction>(U)) {
                if (isa<CallBase>(UI) || isa<PtrToIntInst>(UI) ||
                    isa<PHINode>(UI) || isa<SelectInst>(UI)) {
                  UsedByOther = true;
                  break;
                }

                if (auto *LI = dyn_cast<LoadInst>(UI)) {
                  if (!LI->isSimple() ||
                      UI->getAccessType() != AI->getAllocatedType()) {
                    UsedByOther = true;
                    break;
                  }
                  continue;
                }

                if (auto *SI = dyn_cast<StoreInst>(UI)) {
                  if (!SI->isSimple() || SI->getValueOperand() == I ||
                      UI->getAccessType() != AI->getAllocatedType()) {
                    UsedByOther = true;
                    break;
                  }
                  continue;
                }
                WorkList.push_back(UI);
              }
            }
          }
          if (UsedByOther)
            continue;
          errs() << "Found alloca: " << *AI << ' ' << Path.string() << '\n';
          Found = true;
          break;
        } else
          break;
      }

      if (Found) {
        ++FindCount;
        break;
      }
    }

    if (FindCount >= 20)
      break;

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
