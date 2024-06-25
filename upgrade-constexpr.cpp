// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
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
#include <string>

using namespace llvm;
namespace fs = std::filesystem;

static cl::OptionCategory UpgraderCategory("Upgrader options");
static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"),
             cl::cat(UpgraderCategory));

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(
      argc, argv, "upgrade-constexpr LLVM constexpr -> inst upgrader\n");

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.string().find("/original/") == std::string::npos)
        continue;
      if (Path.extension() == ".ll")
        InputFiles.push_back(Path);
    }
  }
  errs() << "Input files: " << InputFiles.size() << '\n';
  uint32_t Count = 0;

  for (auto &Path : InputFiles) {
    LLVMContext Context;
    Context.setDiagnosticHandlerCallBack(
        [](const DiagnosticInfo *DI, void *) {});
    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    bool Changed = true;
    bool Dirty = false;

    while (Changed) {
      Changed = false;
      for (auto &F : *M) {
        for (auto &BB : F) {
          for (auto &I : BB) {
            DenseMap<BasicBlock *, Value *> Cache;
            for (auto &Op : I.operands()) {
              if (auto *CE = dyn_cast<ConstantExpr>(Op.get())) {
                if (I.getOpcode() == Instruction::PHI) {
                  auto *Phi = cast<PHINode>(&I);
                  auto *PredBB = Phi->getIncomingBlock(Op);

                  if (Cache.contains(PredBB)) {
                    Op.set(Cache.at(PredBB));
                  } else {
                    auto *Inst = CE->getAsInstruction();
                    Inst->insertBefore(PredBB->getTerminator());
                    Cache.insert({PredBB, Inst});
                    Op.set(Inst);
                  }
                } else {
                  auto *Inst = CE->getAsInstruction();
                  Inst->insertBefore(&I);
                  Op.set(Inst);
                }
                Changed = true;
              }
            }
          }
        }
      }
      Dirty |= Changed;
    }

    assert(!verifyModule(*M, &errs()) && "Module verification failed");

    if (Dirty) {
      std::error_code EC;
      auto Out =
          std::make_unique<ToolOutputFile>(Path.string(), EC, sys::fs::OF_Text);
      if (EC) {
        errs() << EC.message() << '\n';
        abort();
      }
      M->print(Out->os(), /*AAW=*/nullptr);
      Out->keep();
      int unused = system(("sed -i \"1,2d\" " + Path.string()).c_str());
      (void)unused;
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
