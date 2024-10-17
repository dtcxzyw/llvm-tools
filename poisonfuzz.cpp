// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include "llvm/Analysis/ValueTracking.h"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/InstSimplifyFolder.h>
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
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include "pcg_random.hpp"
#include <cassert>
#include <cstdlib>
#include <random>

using namespace llvm;
pcg64 Rng(pcg_extras::seed_seq_from<std::random_device>{});

static uint32_t randomUInt(uint32_t Min, uint32_t Max) {
  return std::uniform_int_distribution<uint32_t>(Min, Max)(Rng);
}
static uint32_t randomUInt(uint32_t Max) { return randomUInt(0, Max); }
static bool randomBool() {
  return std::uniform_int_distribution<uint32_t>(0, 1)(Rng);
}

static bool mutate(Function &F) {
  bool Changed = false;
  for (auto &BB : F) {
    for (auto &I : BB) {
      // if (auto CB = dyn_cast<IntrinsicInst>(&I)) {
      //   if (CB->getType()->isIntOrIntVectorTy() &&
      //       !CB->hasRetAttr(Attribute::NoUndef) && randomBool()) {
      //     CB->addRetAttr(Attribute::NoUndef);
      //     Changed = true;
      //   }
      //   if (CB->getType()->isIntOrIntVectorTy() &&
      //       !CB->hasRetAttr(Attribute::Range) && randomBool()) {
      //     ConstantRange CR = computeConstantRange(&I, /*ForSigned=*/false);
      //     APInt Zero = APInt::getZero(CR.getBitWidth());
      //     if (CR.contains(Zero) && randomBool()) {
      //       CB->addRangeRetAttr(
      //           CR.intersectWith(ConstantRange(Zero).inverse()));
      //       Changed = true;
      //       continue;
      //     }
      //     APInt One = APInt(CR.getBitWidth(), 1);
      //     if (CR.contains(One) && randomBool()) {
      //       CB->addRangeRetAttr(CR.intersectWith(ConstantRange(One).inverse()));
      //       Changed = true;
      //       continue;
      //     }
      //     APInt Min = APInt::getSignedMinValue(CR.getBitWidth());
      //     if (CR.contains(Min) && randomBool()) {
      //       CB->addRangeRetAttr(CR.intersectWith(ConstantRange(Min).inverse()));
      //       Changed = true;
      //       continue;
      //     }
      //     APInt AllOnes = APInt::getAllOnes(CR.getBitWidth());
      //     if (CR.contains(AllOnes) && randomBool()) {
      //       CB->addRangeRetAttr(
      //           CR.intersectWith(ConstantRange(AllOnes).inverse()));
      //       Changed = true;
      //       continue;
      //     }
      //   }
      //   Intrinsic::ID IID = CB->getIntrinsicID();
      //   if ((IID == Intrinsic::ctlz || IID == Intrinsic::cttz) &&
      //       randomBool()) {
      //     CB->setArgOperand(
      //         1, ConstantExpr::getNot(cast<Constant>(CB->getArgOperand(1))));
      //     Changed = true;
      //   }
      // }

      if (auto *ICmp = dyn_cast<ICmpInst>(&I)) {
        if ( // ICmp->isUnsigned() &&
            !ICmp->hasSameSign() && randomBool()) {
          ICmp->setSameSign();
          Changed = true;
        }
      }
    }
  }
  return Changed;
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};

  std::error_code ec;
  LLVMContext Ctx;
  SMDiagnostic Err;
  auto M = parseIRFile(argv[1], Err, Ctx);
  if (!M)
    return EXIT_FAILURE;
  M->setSourceFileName("");
  M->setModuleIdentifier("");

  bool Changed = false;
  for (auto &F : *M) {
    if (F.empty())
      continue;
    uint32_t E = randomUInt(1, 4);
    for (uint32_t I = 0; I != E; ++I)
      Changed |= mutate(F);
  }

  if (!Changed)
    return EXIT_SUCCESS;

  std::string Out = argv[2];
  auto OutSrc = std::make_unique<llvm::ToolOutputFile>(Out + ".src", ec,
                                                       llvm::sys::fs::OF_None);
  auto OutTgt = std::make_unique<llvm::ToolOutputFile>(Out + ".tgt", ec,
                                                       llvm::sys::fs::OF_None);
  M->print(OutSrc->os(), nullptr);
  OutSrc->keep();
  OutSrc.reset();
  PassBuilder PB;

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModulePassManager MPM;
  ModuleAnalysisManager MAM;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  MPM.addPass(createModuleToFunctionPassAdaptor(InstCombinePass{}));
  MPM.run(*M, MAM);
  M->print(OutTgt->os(), nullptr);
  OutTgt->keep();
  return EXIT_SUCCESS;
}
