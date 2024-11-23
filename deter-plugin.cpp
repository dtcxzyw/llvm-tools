// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/Hashing.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

using namespace llvm;

class ModulePointerSummaryPass
    : public PassInfoMixin<ModulePointerSummaryPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    hash_code summary = hash_value(&M);

    for (auto &G : M.globals())
      summary = hash_combine(summary, hash_value(&G));

    for (auto &F : M.functions()) {
      for (auto &BB : F) {
        summary = hash_combine(summary, hash_value(&BB));
        for (auto &I : BB) {
          summary = hash_combine(summary, hash_value(&I));
          for (auto &U : I.operands())
            summary = hash_combine(summary, hash_value(&U));
          for (auto &U : I.uses())
            summary = hash_combine(summary, hash_value(&U));
        }
      }
    }

    errs() << "ModulePointerSummary: " << summary << "\n";

    return PreservedAnalyses::all();
  }
};

static PassPluginLibraryInfo getDeterPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DeterministicCheck", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback([](ModulePassManager &PM,
                                                  OptimizationLevel Level,
                                                  ThinOrFullLTOPhase) {
              PM.addPass(ModulePointerSummaryPass());
            });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getDeterPluginInfo();
}
