// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm-19/llvm/ADT/ArrayRef.h>
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
#include <memory>

constexpr uint32_t Bits = 128;

using namespace llvm;
namespace fs = std::filesystem;

static cl::OptionCategory VectorizerCategory("Vectorizer options");
static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"),
             cl::cat(VectorizerCategory));

static cl::opt<std::string>
    OutputDir(cl::Positional, cl::desc("<directory for LLVM IR outputs>"),
              cl::Required, cl::value_desc("output"),
              cl::cat(VectorizerCategory));

static uint32_t getTypeBits(Type *Ty, const DataLayout &DL) {
  if (Ty->isIntegerTy())
    return Ty->getScalarSizeInBits();

  if (Ty->isFloatTy())
    return sizeof(float);

  if (Ty->isDoubleTy())
    return sizeof(double);

  // Pointer types and GEP are not supported.
  //   if (Ty->isPointerTy())
  //     return DL.getPointerSizeInBits();

  return 0;
}

static uint32_t getElementCount(Type *Ty, const DataLayout &DL) {
  if (Ty->isVoidTy() || Ty->isIntegerTy(1))
    return 255U;
  if (auto Size = getTypeBits(Ty, DL))
    return Size <= 64U ? Bits / Size : 0U;
  return 0U;
}

static uint32_t getMaxElementCount(Function &F, const DataLayout &DL) {
  uint32_t MaxElementCount = 2U;

  auto Update = [&](uint32_t Count) {
    MaxElementCount = std::min(MaxElementCount, Count);
  };

  Update(getElementCount(F.getReturnType(), DL));
  for (Value &Arg : F.args())
    Update(getElementCount(Arg.getType(), DL));
  for (BasicBlock &BB : F)
    for (Instruction &I : BB) {
      if (I.isTerminator())
        continue;
      Update(getElementCount(I.getType(), DL));
      for (auto &Op : I.operands()) {
        if (isa<Function>(Op))
          continue;
        if (isa<ConstantExpr>(Op))
          return 0U;
        Update(getElementCount(Op->getType(), DL));
      }
    }
  return MaxElementCount;
}

static Type *getVectorType(Type *Ty, uint32_t Count) {
  if (Ty->isVoidTy())
    return Ty;
  if (auto *FnTy = dyn_cast<FunctionType>(Ty)) {
    auto RetTy = getVectorType(FnTy->getReturnType(), Count);
    SmallVector<Type *, 4> ParamTy;
    for (auto *Param : FnTy->params())
      ParamTy.push_back(getVectorType(Param, Count));
    return FunctionType::get(RetTy, ParamTy, FnTy->isVarArg());
  }
  return FixedVectorType::get(Ty, Count);
}

class Vectorizer final : public InstVisitor<Vectorizer, Value *> {
private:
  Module &Mod;
  IRBuilder<> Builder;
  SmallDenseMap<Value *, Value *> ValueMap;
  SmallDenseMap<BasicBlock *, BasicBlock *> BBMap;
  uint32_t ElementCount;
  bool Mixed = false;

  Value *getMappedValue(Value *V) {
    if (auto *C = dyn_cast<Constant>(V)) {
      SmallVector<Constant *, 4> Elts;
      for (uint32_t I = 0; I != ElementCount; ++I) {
        if (I == ElementCount - 1)
          Elts.push_back(PoisonValue::get(C->getType()));
        else
          Elts.push_back(C);
      }
      Mixed = true;
      return ConstantVector::get(Elts);
      // return ConstantVector::getSplat(ElementCount::getFixed(ElementCount),
      // C);
    }
    Value *Mapped = ValueMap.lookup(V);
    if (!Mapped) {
      // errs() << "Invalid value " << *V << '\n';
      // std::abort();
      return PoisonValue::get(getVectorType(V->getType(), ElementCount));
    }
    return Mapped;
  }

  Value *getReducedValue(Value *V) { return Builder.CreateAndReduce(V); }

public:
  explicit Vectorizer(Module &M, uint32_t Count)
      : Mod{M}, Builder{M.getContext()}, ElementCount{Count} {}

  Value *visitUnaryOperator(UnaryInstruction &I) {
    return Builder.CreateUnOp(static_cast<Instruction::UnaryOps>(I.getOpcode()),
                              getMappedValue(I.getOperand(0)), I.getName());
  }
  Value *visitBinaryOperator(BinaryOperator &I) {
    return Builder.CreateBinOp(I.getOpcode(), getMappedValue(I.getOperand(0)),
                               getMappedValue(I.getOperand(1)), I.getName());
  }
  Value *visitCastInst(CastInst &I) {
    return Builder.CreateCast(I.getOpcode(), getMappedValue(I.getOperand(0)),
                              getVectorType(I.getDestTy(), ElementCount),
                              I.getName());
  }
  Value *visitCmpInst(CmpInst &I) {
    return Builder.CreateCmp(I.getPredicate(), getMappedValue(I.getOperand(0)),
                             getMappedValue(I.getOperand(1)), I.getName());
  }
  Value *visitIntrinsicInst(IntrinsicInst &I) {
    Intrinsic::ID IID = I.getIntrinsicID();
    switch (IID) {
    case Intrinsic::abs:
    case Intrinsic::ctlz:
    case Intrinsic::cttz:
    case Intrinsic::is_fpclass: {
      Value *Src = getMappedValue(I.getArgOperand(0));
      Value *Scalar = I.getArgOperand(1);
      return Builder.CreateBinaryIntrinsic(
          IID, Src, Scalar, isa<FPMathOperator>(I) ? &I : nullptr, I.getName());
    }
    case Intrinsic::smul_fix:
    case Intrinsic::smul_fix_sat:
    case Intrinsic::umul_fix:
    case Intrinsic::umul_fix_sat:
    case Intrinsic::sdiv_fix:
    case Intrinsic::sdiv_fix_sat:
    case Intrinsic::udiv_fix:
    case Intrinsic::udiv_fix_sat: {
      Value *LHS = getMappedValue(I.getArgOperand(0));
      Value *RHS = getMappedValue(I.getArgOperand(1));
      Value *Scalar = I.getArgOperand(2);
      return Builder.CreateIntrinsic(IID, {LHS->getType()}, {LHS, RHS, Scalar},
                                     nullptr, I.getName());
    }
    case Intrinsic::assume: {
      Value *Src = getReducedValue(getMappedValue(I.getArgOperand(0)));
      return Builder.CreateAssumption(Src);
    }
    case Intrinsic::experimental_guard:
    case Intrinsic::experimental_deoptimize:
    case Intrinsic::experimental_widenable_condition:
    case Intrinsic::coro_size:
    case Intrinsic::vscale:
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::ptrmask:
    case Intrinsic::is_constant:
    case Intrinsic::convert_from_fp16:
    case Intrinsic::convert_to_fp16:
    case Intrinsic::pseudoprobe:
    case Intrinsic::expect:
    case Intrinsic::expect_with_probability:
      return nullptr;
    case Intrinsic::ldexp:
    case Intrinsic::powi: {
      SmallVector<Type *, 4> Types;
      SmallVector<Value *, 4> Args;
      for (Value *Arg : I.args()) {
        Args.push_back(getMappedValue(Arg));
        Types.push_back(Args.back()->getType());
      }

      return Builder.CreateIntrinsic(
          IID, Types, Args, isa<FPMathOperator>(I) ? &I : nullptr, I.getName());
    }
    case Intrinsic::lround:
    case Intrinsic::llround:
    case Intrinsic::lrint:
    case Intrinsic::llrint:
    case Intrinsic::fptosi_sat:
    case Intrinsic::fptoui_sat: {
      Value *Src = getMappedValue(I.getArgOperand(0));
      return Builder.CreateIntrinsic(
          IID, {getVectorType(I.getType(), ElementCount), Src->getType()},
          {Src}, isa<FPMathOperator>(I) ? &I : nullptr, I.getName());
    }
    default: {
      if (IID > Intrinsic::xray_typedevent)
        return nullptr;
      SmallVector<Value *, 4> Args;
      for (Value *Arg : I.args())
        Args.push_back(getMappedValue(Arg));

      return Builder.CreateIntrinsic(
          IID,
          Args.empty() ? std::nullopt
                       : ArrayRef<Type *>{Args.front()->getType()},
          Args, isa<FPMathOperator>(I) ? &I : nullptr, I.getName());
    }
    }
  }
  Value *visitSelectInst(SelectInst &I) {
    return Builder.CreateSelect(
        getMappedValue(I.getOperand(0)), getMappedValue(I.getOperand(1)),
        getMappedValue(I.getOperand(2)), I.getName(), &I);
  }
  Value *visitFreezeInst(FreezeInst &I) {
    return Builder.CreateFreeze(getMappedValue(I.getOperand(0)), I.getName());
  }
  Value *visitReturnInst(ReturnInst &I) {
    if (I.getReturnValue())
      return Builder.CreateRet(getMappedValue(I.getReturnValue()));
    return Builder.CreateRetVoid();
  }
  Value *visitLoadInst(LoadInst &I) {
    return Builder.CreateLoad(getVectorType(I.getType(), ElementCount),
                              getMappedValue(I.getPointerOperand()),
                              I.isVolatile(), I.getName());
  }
  Value *visitStoreInst(StoreInst &I) {
    return Builder.CreateStore(getMappedValue(I.getValueOperand()),
                               getMappedValue(I.getPointerOperand()),
                               I.isVolatile());
  }
  Value *visitFenceInst(FenceInst &I) {
    return Builder.CreateFence(I.getOrdering(), I.getSyncScopeID(),
                               I.getName());
  }
  Value *visitUnreachableInst(UnreachableInst &I) {
    return Builder.CreateUnreachable();
  }
  Value *visitBranchInst(BranchInst &I) {
    if (I.isConditional())
      return Builder.CreateCondBr(
          getReducedValue(getMappedValue(I.getCondition())),
          BBMap.lookup(I.getSuccessor(0)), BBMap.lookup(I.getSuccessor(1)));
    return Builder.CreateBr(BBMap.lookup(I.getSuccessor(0)));
  }
  Value *visitSwitchInst(SwitchInst &I) { return nullptr; }
  Value *visitPHINode(PHINode &PHI) {
    PHINode *NewPHI = cast<PHINode>(getMappedValue(&PHI));
    for (uint32_t I = 0; I != PHI.getNumIncomingValues(); ++I) {
      NewPHI->addIncoming(getMappedValue(PHI.getIncomingValue(I)),
                          BBMap.lookup(PHI.getIncomingBlock(I)));
    }
    return NewPHI;
  }
  Value *visitCallBase(CallBase &I) { return nullptr; }
  Value *visitIndirectBrInst(IndirectBrInst &I) { return nullptr; }

  Value *visitInstruction(Instruction &I) {
    errs() << I << '\n';
    llvm_unreachable("Unhandled instruction type");
  }

  bool run(Function &OldF, Function &NewF) {
    for (uint32_t I = 0, E = OldF.arg_size(); I != E; ++I)
      ValueMap.insert({OldF.getArg(I), NewF.getArg(I)});
    for (BasicBlock &BB : OldF) {
      BasicBlock &NewBB =
          *BasicBlock::Create(Mod.getContext(), BB.getName(), &NewF);
      BBMap.insert({&BB, &NewBB});
      Builder.SetInsertPoint(&NewBB);
      for (auto &PHI : BB.phis()) {
        PHINode *NewPHI =
            Builder.CreatePHI(getVectorType(PHI.getType(), ElementCount),
                              PHI.getNumIncomingValues());
        ValueMap.insert({&PHI, NewPHI});
      }
    }

    for (auto &BB : OldF) {
      Builder.SetInsertPoint(BBMap.lookup(&BB));
      for (Instruction &I : BB) {
        Value *V = visit(I);
        if (!V)
          return false;
        if (auto *NewI = dyn_cast<Instruction>(V))
          NewI->copyIRFlags(&I);
        ValueMap.insert({&I, V});
      }
    }
    return Mixed;
  }
};

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(
      argc, argv, "vectorizer LLVM scalar -> vectorize type converter\n");

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.string().find("Verifier") != std::string::npos)
        continue;
      if (Path.extension() == ".ll")
        InputFiles.push_back(Path);
    }
  }
  errs() << "Input files: " << InputFiles.size() << '\n';
  LLVMContext Context;
  Context.setDiagnosticHandlerCallBack([](const DiagnosticInfo *DI, void *) {});
  uint32_t Count = 0;
  auto OutputBase = fs::path{std::string{OutputDir}};

  if (fs::exists(OutputBase))
    fs::remove_all(OutputBase);
  fs::create_directories(OutputBase);

  for (auto &Path : InputFiles) {
    // errs() << Path.string() << '\n';

    SMDiagnostic Err;
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    Module NewM("", Context);
    for (auto &F : *M) {
      if (F.empty())
        continue;
      uint32_t MaxElementCount = getMaxElementCount(F, M->getDataLayout());
      if (MaxElementCount < 2U)
        continue;
      auto NewF = cast<Function>(
          NewM.getOrInsertFunction(F.getName(),
                                   cast<FunctionType>(getVectorType(
                                       F.getFunctionType(), MaxElementCount)))
              .getCallee());
      Vectorizer Builder{NewM, MaxElementCount};
      if (!Builder.run(F, *NewF)) {
        NewF->eraseFromParent();
        continue;
      }
      F.removeRetAttr(Attribute::ZExt);
      F.removeRetAttr(Attribute::SExt);
      for (uint32_t I = 0; I != F.arg_size(); ++I) {
        F.removeParamAttr(I, Attribute::ZExt);
        F.removeParamAttr(I, Attribute::SExt);
      }
      F.clearMetadata();
      F.setPersonalityFn(nullptr);
      NewF->copyAttributesFrom(&F);
      if (verifyFunction(*NewF, &errs())) {
        NewF->dump();
        std::abort();
      }
    }

    bool Valid = false;
    for (auto &F : NewM)
      if (!F.empty()) {
        Valid = true;
        break;
      }

    if (!Valid)
      continue;

    // bool DbgInfoBroken = false;
    // if (verifyModule(NewM, &errs(), &DbgInfoBroken) || DbgInfoBroken) {
    //   NewM.dump();
    //   std::abort();
    // }

    std::error_code EC;
    auto OutPath = OutputBase / fs::relative(Path, std::string(InputDir));
    fs::create_directories(OutPath.parent_path());
    auto Out = std::make_unique<ToolOutputFile>(OutPath.string(), EC,
                                                sys::fs::OF_Text);
    if (EC) {
      errs() << EC.message() << '\n';
      abort();
    }

    NewM.print(Out->os(), /*AAW=*/nullptr);
    Out->keep();

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
