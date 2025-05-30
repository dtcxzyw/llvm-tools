// SPDX-License-Identifier: MIT License
// Copyright (c) 2025 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/STLExtras.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

using namespace llvm;
using namespace llvm::PatternMatch;

template <typename IRUnitT> const IRUnitT *unwrapIR(Any IR) {
  const IRUnitT **IRPtr = llvm::any_cast<const IRUnitT *>(&IR);
  return IRPtr ? *IRPtr : nullptr;
}

constexpr StringRef InterestingPasses[] = {
    "SimplifyCFGPass",
    "JumpThreadingPass",
};

static bool isInterestingPass(StringRef PassName) {
  return is_contained(InterestingPasses, PassName);
}

static std::unordered_map<const Function *, uint32_t> Rank;

class RankVisitor : public InstVisitor<RankVisitor> {
  uint32_t &Rank;

public:
  explicit RankVisitor(uint32_t &R) : Rank(R) {}

  void visitIntToPtrInst(IntToPtrInst &I) {
    if (match(I.getOperand(0), m_Zero()))
      ++Rank;
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    if (I.isInBounds() && isa<ConstantPointerNull>(I.getPointerOperand()))
      ++Rank;
  }

  void visitBranchInst(BranchInst &I) {
    if (I.isConditional() && isa<UndefValue>(I.getCondition()))
      ++Rank;
  }

  void visitSwitchInst(SwitchInst &I) {
    if (isa<UndefValue>(I.getCondition()))
      ++Rank;
  }

  void visitReturnInst(ReturnInst &I) {
    if (I.getReturnValue() && isa<UndefValue>(I.getReturnValue()))
      ++Rank;
    if (I.getReturnValue() && isa<ConstantPointerNull>(I.getReturnValue()) &&
        I.getFunction()->hasRetAttribute(Attribute::NonNull))
      ++Rank;
  }

  void visitMemTransferInst(MemTransferInst &I) {
    if (isa<ConstantPointerNull>(I.getRawDest()))
      ++Rank;
    if (isa<ConstantPointerNull>(I.getRawSource()))
      ++Rank;
  }
  void visitMemIntrinsic(MemIntrinsic &I) {
    if (isa<ConstantPointerNull>(I.getRawDest()))
      ++Rank;
  }

  void visitLoadInst(LoadInst &I) {
    if (isa<ConstantPointerNull>(I.getPointerOperand()))
      ++Rank;
  }

  void visitStoreInst(StoreInst &I) {
    if (isa<ConstantPointerNull>(I.getPointerOperand()))
      ++Rank;
    if (isa<UndefValue>(I.getValueOperand()))
      ++Rank;
  }
  void visitSubInst(BinaryOperator &I) {
    if (match(I.getOperand(0), m_Zero()) && I.hasNoUnsignedWrap())
      ++Rank;
  }

  void visitCallBase(CallBase &I) {
    if (isa<ConstantPointerNull>(I.getCalledOperand()))
      ++Rank;
    if (isa<UndefValue>(I.getCalledOperand()))
      ++Rank;

    for (auto &Op : I.args()) {
      if (isa<UndefValue>(Op) && I.isPassingUndefUB(Op.getOperandNo()))
        ++Rank;
      if (isa<ConstantPointerNull>(Op) &&
          I.paramHasNonNullAttr(Op.getOperandNo(), true))
        ++Rank;
    }
  }

  void visitIntrinsicInst(IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::assume: {
      if (match(I.getArgOperand(0), m_Zero()))
        ++Rank;
      break;
    }
    case Intrinsic::expect: {
      if (isa<Constant>(I.getArgOperand(0)) &&
          I.getArgOperand(0) != I.getArgOperand(1))
        ++Rank;
      break;
    }
    default:
      break;
    }
  }

  void visitSDivInst(BinaryOperator &I) {
    if (match(I.getOperand(0), m_Zero()))
      ++Rank;
  }
  void visitUDivInst(BinaryOperator &I) {
    if (match(I.getOperand(0), m_Zero()))
      ++Rank;
  }
  void visitSRemInst(BinaryOperator &I) {
    if (match(I.getOperand(0), m_Zero()))
      ++Rank;
  }
  void visitURemInst(BinaryOperator &I) {
    if (match(I.getOperand(0), m_Zero()))
      ++Rank;
  }
};

static uint32_t getUBRank(const Function &F) {
  uint32_t Rank = 0;
  RankVisitor RV(Rank);
  for (auto &BB : F)
    for (auto &I : BB) {
      uint32_t OldRank = Rank;
      // poison values
      for (auto &Op : I.operands()) {
        if (isa<PoisonValue>(Op) && propagatesPoison(Op))
          ++Rank;
      }

      RV.visit(const_cast<Instruction &>(I));
      if (OldRank != Rank) {
        // errs() << I << '\n';
      }
    }
  return Rank;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "speculate-ub", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            auto *PIC = PB.getPassInstrumentationCallbacks();
            PIC->registerBeforeNonSkippedPassCallback(
                [](StringRef PassName, Any IRUnit) {
                  auto *Func = unwrapIR<Function>(IRUnit);
                  if (!Func || Func->empty())
                    return;
                  if (!isInterestingPass(PassName))
                    return;
                  Rank[Func] = getUBRank(*Func);
                  // if (Rank[Func] != 0)
                  //   errs() << PassName << ' ' << Func->getName()
                  //          << ": rank = " << Rank[Func] << '\n';
                });
            PIC->registerAfterPassCallback([](StringRef PassName, Any IRUnit,
                                              const PreservedAnalyses &PA) {
              if (PA.areAllPreserved())
                return;
              auto *Func = unwrapIR<Function>(IRUnit);
              if (!Func || Func->empty())
                return;
              if (!isInterestingPass(PassName))
                return;

              auto OldRank = Rank.at(Func);
              auto NewRank = getUBRank(*Func);
              if (NewRank > OldRank) {
                errs() << PassName << ' ' << Func->getName()
                       << ": rank increased from " << OldRank << " to "
                       << NewRank << '\n';
              }
            });
          }};
}
