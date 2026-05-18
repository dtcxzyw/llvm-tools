// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace llvm;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<input directory>"), cl::Required,
             cl::value_desc("dir"));

static cl::opt<std::string> Passes("passes", cl::desc("Pipeline to run"),
                                   cl::init("default<O3>"));

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "batchopt\n");

  std::vector<fs::path> InputFiles;
  std::error_code EC;
  for (auto &SubEntry : fs::directory_iterator(std::string(InputDir), EC)) {
    if (!SubEntry.is_directory())
      continue;
    auto OriginalDir = SubEntry.path() / "original";
    if (!fs::is_directory(OriginalDir))
      continue;
    for (auto &BCEntry : fs::directory_iterator(OriginalDir, EC)) {
      if (!BCEntry.is_regular_file())
        continue;
      if (BCEntry.path().extension() == ".bc")
        InputFiles.push_back(BCEntry.path());
      if (InputFiles.size() >= 100)
        break;
    }
    if (InputFiles.size() >= 100)
      break;
  }

  errs() << "Input files: " << InputFiles.size() << '\n';

  uint32_t Count = 0;
  for (auto &Path : InputFiles) {
    LLVMContext Context;
    Context.setDiagnosticHandlerCallBack(
        [](const DiagnosticInfo *, void *) {});

    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M) {
      errs() << "\rProgress: " << ++Count << "/" << InputFiles.size();
      continue;
    }

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    if (auto Err = PB.parsePassPipeline(MPM, Passes)) {
      errs() << "Failed to parse pass pipeline: " << toString(std::move(Err))
             << '\n';
      return EXIT_FAILURE;
    }

    MPM.run(*M, MAM);

    errs() << "\rProgress: " << ++Count << "/" << InputFiles.size();
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
