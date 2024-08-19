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

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "scanner\n");

  std::vector<std::string> BlockList{
      "ruby/optimized/vm.ll",
      "/regexec.ll",
      "quickjs/optimized/quickjs.ll",
      "/redis/",
      "typst-rs",
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

  std::vector<std::string> BlockKeyList{
      "EE8write_toERNS0_7ContextIS2_EEPh", "get_symbols_v1",
      "EE14get_thunk_addrEl", "$", "toml_edit2de5Error6custom",
      "_ZNK8DfgConst7srcNameB5cxx11Em", "zim_DOM_HTMLDocument___construct",
      "facebook5velox", "_ZN3syn", "_ZN8rawspeed10DngOpcodes",
      "hermes3hbc7HBCISel", "_ZN4enttL8meta_argINS_9type_list",
      "_ZN5serde3ser12SerializeMap", "arena_", "prof_", "extent_", "bt_init",
      // https://github.com/nodejs/node/pull/54325
      "_ZN4nodeL13CauseSegfaultERKN2v820FunctionCallbackInfoINS0_5ValueEEE"};

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    LLVMContext Context;
    // errs() << Path.string() << '\n';
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    // auto &DL = M->getDataLayout();
    // errs() << DL.getStringRepresentation() << '\n';

    bool Contains = false;
    for (auto &F : *M) {
      if (F.empty())
        continue;

      if (F.size() != 1)
        continue;
      auto &BB = F.getEntryBlock();
      if (BB.size() != 1)
        continue;
      auto *Term = BB.getTerminator();
      if (auto *Ret = dyn_cast<ReturnInst>(Term)) {
        auto *RetVal = Ret->getReturnValue();
        if (RetVal && isa<PoisonValue>(RetVal)) {
          errs() << F.getName() << ' ' << fs::absolute(Path) << '\n';
        }
      } else if (isa<UnreachableInst>(Term)) {
        bool Blocked = false;
        for (auto Key : BlockKeyList)
          if (F.getName().contains(Key)) {
            Blocked = true;
            break;
          }
        if (Blocked)
          continue;

        errs() << ' ' << F.getName() << ' ' << fs::absolute(Path) << '\n';
        return EXIT_SUCCESS;
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
