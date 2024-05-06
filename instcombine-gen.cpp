// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/InstSimplifyFolder.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
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

constexpr uint32_t MinArgs = 1U;
constexpr uint32_t MaxArgs = 3U;
constexpr uint32_t MinInsts = 3U;
constexpr uint32_t MaxInsts = 5U;
constexpr uint32_t BatchSize = 1024U;

class FuncGenerator final {
  pcg64 Rng;
  LLVMContext Ctx;
  Module M;
  SmallVector<IntegerType *, 4> Types;
  uint32_t randomUInt(uint32_t Min, uint32_t Max) {
    return std::uniform_int_distribution<uint32_t>(Min, Max)(Rng);
  }
  uint32_t randomUInt(uint32_t Max) { return randomUInt(0, Max); }
  bool randomBool() { return randomUInt(1); }
  IntegerType *randomType() { return Types[randomUInt(Types.size() - 1)]; }
  IntegerType *randomNonBoolType() {
    return Types[randomUInt(1, Types.size() - 1)];
  }

  DenseMap<Type *, SmallVector<Value *, MaxArgs + MaxInsts>> TypedValues;
  SmallVector<Value *, MaxArgs + MaxInsts> Values;
  IRBuilder<ConstantFolder> Builder;

  void addValue(Value *V) {
    TypedValues[V->getType()].push_back(V);
    Values.push_back(V);
  }
  Value *selectUnused(ArrayRef<Value *> Set) {
    SmallVector<Value *, MaxArgs + MaxInsts> Unused;
    for (auto *V : Set)
      if (V->use_empty())
        Unused.push_back(V);
    if (!Unused.empty())
      return Unused[randomUInt(Unused.size() - 1)];
    return nullptr;
  }
  Value *selectTypedVal(IntegerType *Ty) {
    auto &Set = TypedValues[Ty];
    if (Set.empty() || randomBool()) {
      uint32_t Bits = Ty->getScalarSizeInBits();
      return ConstantInt::get(Ty, APInt(Bits, randomUInt((1U << Bits) - 1)));
    }
    if (randomBool()) {
      if (auto *V = selectUnused(Set))
        return V;
    }
    return Set[randomUInt(Set.size() - 1)];
  }
  Value *selectVal() {
    if (randomBool()) {
      if (auto *V = selectUnused(Values))
        return V;
    }
    return Values[randomUInt(Values.size() - 1)];
  }
  Value *selectInst() {
    switch (randomUInt(8)) {
    case 0: {
      auto Val = selectVal();
      if (isa<FreezeInst>(Val) || isa<Argument>(Val))
        return nullptr;
      return Builder.CreateFreeze(Val);
    }
    case 1: {
      if (auto *Val = selectVal(); Val->getType()->isIntegerTy()) {
        auto TgtTy = randomType();
        if (TgtTy == Val->getType())
          return nullptr;
        auto *Cast = Builder.CreateIntCast(Val, TgtTy, randomBool());
        if (auto *Trunc = dyn_cast<TruncInst>(Cast)) {
          Trunc->setHasNoSignedWrap(randomBool());
          Trunc->setHasNoUnsignedWrap(randomBool());
        }
        if (auto *NNeg = dyn_cast<PossiblyNonNegInst>(Cast))
          NNeg->setNonNeg(randomBool());
        return Cast;
      }
      break;
    }
    case 2: {
      auto *Ty = randomType();
      auto *LHS = selectTypedVal(Ty);
      auto *RHS = selectTypedVal(Ty);
      static constexpr Instruction::BinaryOps BinOps[] = {
          Instruction::Add,  Instruction::Sub,  Instruction::Mul,
          Instruction::SDiv, Instruction::UDiv, Instruction::SRem,
          Instruction::URem, Instruction::And,  Instruction::Or,
          Instruction::Xor,  Instruction::Shl,  Instruction::LShr,
          Instruction::AShr,
      };
      auto BinOp = BinOps[randomUInt(std::size(BinOps) - 1U)];
      if (Ty->isIntegerTy(1) && BinOp != Instruction::And &&
          BinOp != Instruction::Or && BinOp != Instruction::Xor)
        return nullptr;

      auto *Inst = dyn_cast<Instruction>(Builder.CreateBinOp(BinOp, LHS, RHS));
      if (!Inst)
        return nullptr;
      if (isa<OverflowingBinaryOperator>(Inst)) {
        Inst->setHasNoSignedWrap(randomBool());
        Inst->setHasNoUnsignedWrap(randomBool());
      } else if (isa<PossiblyExactOperator>(Inst))
        Inst->setIsExact(randomBool());
      else if (auto *Disjoint = dyn_cast<PossiblyDisjointInst>(Inst))
        Disjoint->setIsDisjoint(randomBool());
      return Inst;
    }
    case 3: {
      auto *Ty = randomNonBoolType();
      auto *LHS = selectTypedVal(Ty);
      if (isa<Constant>(LHS))
        return nullptr;
      auto *RHS = selectTypedVal(Ty);
      auto Pred = static_cast<ICmpInst::Predicate>(randomUInt(
          ICmpInst::FIRST_ICMP_PREDICATE, ICmpInst::LAST_ICMP_PREDICATE));
      if (isa<Constant>(RHS) && ICmpInst::isNonStrictPredicate(Pred))
        return nullptr;
      return Builder.CreateICmp(Pred, LHS, RHS);
    }
    case 4: {
      auto *Ty = randomNonBoolType();
      auto *Val = selectTypedVal(Ty);
      if (isa<Constant>(Val))
        return nullptr;
      static constexpr Intrinsic::ID IntArith[] = {
          Intrinsic::bswap, Intrinsic::ctpop,      Intrinsic::ctlz,
          Intrinsic::cttz,  Intrinsic::bitreverse, Intrinsic::abs,
      };
      Intrinsic::ID IID = IntArith[randomUInt(std::size(IntArith) - 1U)];
      if (IID == Intrinsic::bswap) {
        if (!(Ty->getScalarSizeInBits() > 8 &&
              Ty->getScalarSizeInBits() % 8 == 0))
          return nullptr;
      }

      if (IID == Intrinsic::bswap || IID == Intrinsic::bitreverse ||
          IID == Intrinsic::ctpop)
        return Builder.CreateUnaryIntrinsic(IID, Val);
      return Builder.CreateBinaryIntrinsic(IID, Val,
                                           Builder.getInt1(randomBool()));
    }
    case 5: {
      auto *Ty = randomNonBoolType();
      auto *LHS = selectTypedVal(Ty);
      auto *RHS = selectTypedVal(Ty);
      if (isa<Constant>(LHS) && isa<Constant>(RHS))
        return nullptr;
      static constexpr Intrinsic::ID IntArith[] = {
          Intrinsic::sadd_with_overflow, Intrinsic::ssub_with_overflow,
          Intrinsic::smul_with_overflow, Intrinsic::uadd_with_overflow,
          Intrinsic::usub_with_overflow, Intrinsic::umul_with_overflow,
          Intrinsic::sadd_sat,           Intrinsic::ssub_sat,
          Intrinsic::uadd_sat,           Intrinsic::usub_sat,
          Intrinsic::sshl_sat,           Intrinsic::ushl_sat,
      };
      Intrinsic::ID IID = IntArith[randomUInt(std::size(IntArith) - 1U)];
      return Builder.CreateBinaryIntrinsic(IID, LHS, RHS);
    }
    case 6: {
      auto *Ty = randomNonBoolType();
      auto *X = selectTypedVal(Ty);
      auto *Y = selectTypedVal(Ty);
      auto *Z = selectTypedVal(Ty);
      if (isa<Constant>(X) && isa<Constant>(Y) && isa<Constant>(Z))
        return nullptr;
      static constexpr Intrinsic::ID IntArith[] = {Intrinsic::fshl,
                                                   Intrinsic::fshr};
      Intrinsic::ID IID = IntArith[randomUInt(std::size(IntArith) - 1U)];
      return Builder.CreateIntrinsic(IID, X->getType(), {X, Y, Z});
    }
    case 7: {
      auto *Ty = randomType();
      auto *Cond = selectTypedVal(Builder.getInt1Ty());
      if (isa<Constant>(Cond))
        return nullptr;
      auto *TrueVal = selectTypedVal(Ty);
      auto *FalseVal = selectTypedVal(Ty);
      return Builder.CreateSelect(Cond, TrueVal, FalseVal);
    }
    case 8: {
      auto *Ty = randomNonBoolType();
      auto *WithOverflowTy = StructType::get(Ty, Builder.getInt1Ty());
      auto &Set = TypedValues[WithOverflowTy];
      if (Set.empty())
        return nullptr;
      auto *Val = Set[randomUInt(Set.size() - 1)];
      return Builder.CreateExtractValue(Val, randomUInt(1));
    }
    default:
      break;
    }
    return nullptr;
  }

public:
  explicit FuncGenerator()
      : Rng(pcg_extras::seed_seq_from<std::random_device>{}), M("", Ctx),
        Builder(Ctx) {
    Types.push_back(IntegerType::get(Ctx, 1));
    Types.push_back(IntegerType::get(Ctx, 4));  // Non-legal
    Types.push_back(IntegerType::get(Ctx, 8));  // Legal
    Types.push_back(IntegerType::get(Ctx, 16)); // Legal
  }
  bool addFunc(uint32_t Idx) {
    Values.clear();
    TypedValues.clear();

    uint32_t ArgNum = randomUInt(MinArgs, MaxArgs);
    SmallVector<Type *, MaxArgs> argTypes;
    for (uint32_t I = 0; I < ArgNum; ++I)
      argTypes.push_back(randomType());
    // Type *RetType = randomType();
    Type *RetType = Builder.getInt16Ty();
    auto F = Function::Create(FunctionType::get(RetType, argTypes,
                                                /*isVarArg=*/false),
                              GlobalValue::ExternalLinkage,
                              "func" + std::to_string(Idx), M);
    for (auto &Arg : F->args()) {
      if (randomBool())
        Arg.addAttr(Attribute::NoUndef);
      addValue(&Arg);
    }
    auto Entry = BasicBlock::Create(Ctx, "", F);
    Builder.SetInsertPoint(Entry);
    uint32_t ExpectedInsts = randomUInt(MinInsts, MaxInsts);

    auto IsReady = [&]() -> Value * {
      if (Entry->size() != ExpectedInsts)
        return nullptr;
      for (auto &Arg : F->args())
        if (Arg.use_empty())
          return nullptr;
      Value *Ret = nullptr;
      for (auto &I : *Entry) {
        if (I.use_empty()) {
          if (I.getType()->isIntegerTy()) {
            if (Ret)
              return nullptr;
            Ret = &I;
          } else
            return nullptr;
        }
      }
      return Ret;
    };

    do {
      if (auto Ret = IsReady()) {
        Builder.CreateRet(Builder.CreateIntCast(Ret, RetType, randomBool()));
        return true;
      }
      if (Entry->size() == ExpectedInsts) {
        F->eraseFromParent();
        return false;
      }

      if (auto *V = selectInst()) {
        // Not supported by alive2
        // if (auto *II = dyn_cast<IntrinsicInst>(V)) {
        //   for (uint32_t I = 0; I < II->arg_size(); ++I)
        //     if (randomBool())
        //       II->addParamAttr(I, Attribute::NoUndef);
        //   if (randomBool())
        //     II->addRetAttr(Attribute::NoUndef);
        // }
        if (isa<Instruction>(V))
          addValue(V);
      }
    } while (true);
  }
  void dump(raw_ostream &out) const {
    assert(!verifyModule(M, &errs()) && "Module is broken");
    M.print(out, nullptr);
  }
  void runInstCombine() {
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
    MPM.run(M, MAM);
  }
};

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};

  std::string Name = argv[1];
  std::error_code ec;
  auto OutSrc = std::make_unique<llvm::ToolOutputFile>(Name + ".src", ec,
                                                       llvm::sys::fs::OF_None);
  auto OutTgt = std::make_unique<llvm::ToolOutputFile>(Name + ".tgt", ec,
                                                       llvm::sys::fs::OF_None);
  FuncGenerator Gen;
  for (uint32_t I = 0; I < BatchSize; ++I)
    while (!Gen.addFunc(I))
      ;
  Gen.dump(OutSrc->os());
  OutSrc->keep();
  OutSrc.reset();
  Gen.runInstCombine();
  Gen.dump(OutTgt->os());
  OutTgt->keep();
  return EXIT_SUCCESS;
}
