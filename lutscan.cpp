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

std::map<uint64_t, uint32_t> Cost;
std::map<uint64_t, uint32_t> Distrib;

static bool matchLoadLUT(LoadInst &LI) {
  if (LI.isVolatile())
    return false;

  auto *GEP = dyn_cast<GetElementPtrInst>(LI.getPointerOperand());
  if (!GEP)
    return false;

  auto *GV = dyn_cast<GlobalVariable>(GEP->getPointerOperand());
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return false;

  Constant *Init = GV->getInitializer();
  auto &DL = LI.getDataLayout();

  uint64_t IndexBW = DL.getIndexTypeSizeInBits(GEP->getType());
  APInt ConstOffset(IndexBW, 0);
  MapVector<Value *, APInt> VariableOffsets;
  if (!GEP->collectOffset(DL, IndexBW, VariableOffsets, ConstOffset))
    return false;

  if (!ConstOffset.isZero() || VariableOffsets.size() != 1)
    return false;

  auto &Step = VariableOffsets.front().second;
  if (Step.isNonPositive())
    return false;
  uint64_t ArraySize = DL.getTypeAllocSize(Init->getType()).getFixedValue();

  Value *Index = VariableOffsets.front().first;
  if (Index->getType()->getScalarSizeInBits() != IndexBW)
    return false;
  auto &CostCounter = Cost[ArraySize];

  Type *LoadTy = LI.getType();
  SmallMapVector<Constant *, uint64_t, 2> ValueMap;
  // MultiMapIdx indicates that this value occurs more than once in the array.
  constexpr uint64_t MultiMapIdx = static_cast<uint64_t>(-1);
  uint32_t MultiMapElts = 0;
  APInt Offset(IndexBW, 0);
  for (uint64_t I = 0; Offset.getZExtValue() < ArraySize; ++I, Offset += Step) {
    ++CostCounter;
    Constant *Elt = ConstantFoldLoadFromConst(Init, LoadTy, Offset, DL);

    if (!Elt)
      return false;

    // bail out if the array contains undef values
    if (isa<UndefValue>(Elt))
      return false;

    if (auto It = ValueMap.find(Elt); It != ValueMap.end()) {
      if (It->second == MultiMapIdx)
        continue;
      if (++MultiMapElts == 2)
        return false;
      It->second = MultiMapIdx;
    } else {
      if (ValueMap.size() == 2)
        return false;
      ValueMap.insert(std::make_pair(Elt, I));
    }
  }

  if (ValueMap.size() != 1 && ValueMap.size() != 2)
    std::abort();

  Distrib[ArraySize]++;

  //   LI.print(errs() << "\nLoad: ");
  //   GEP->print(errs() << "\nGEP: ");
  //   Init->print(errs() << "\nInit: ");
  //   errs() << '\n';
  return true;
}

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
    // auto &DL = M->getDataLayout();
    // errs() << DL.getStringRepresentation() << '\n';

    bool Contains = false;
    for (auto &F : *M) {
      if (F.empty())
        continue;

      for (auto &BB : F) {
        for (auto &I : BB) {
          if (auto *Load = dyn_cast<LoadInst>(&I)) {
            if (matchLoadLUT(*Load)) {
              Contains = true;
            }
          }
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

  //   for (auto &[Size, Count] : Distrib)
  //     errs() << Size << ": " << Count << '\n';
  //   for (auto &Name : Names)
  //     errs() << Name << '\n';

  uint32_t CostAcc = 0;
  uint32_t FoldAcc = 0;

  errs() << "Thres(Byte) ScanCount FoldCount\n";
  for (uint32_t Thres = 0; Thres < 100; ++Thres) {
    CostAcc += Cost[Thres];
    FoldAcc += Distrib[Thres];
    if (Distrib[Thres])
      errs() << Thres << ": " << CostAcc << ' ' << FoldAcc << '\n';
  }

  return EXIT_SUCCESS;
}
