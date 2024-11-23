// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/DomConditionCache.h>
#include <llvm/Analysis/SimplifyQuery.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
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
#include <llvm/Pass.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
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

constexpr uint32_t MaxDepth = 3;

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::OptionCategory ExtractorCategory("Extractor options");
static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"),
             cl::cat(ExtractorCategory));

static cl::opt<std::string>
    OutputDir(cl::Positional, cl::desc("<directory for LLVM IR outputs>"),
              cl::Required, cl::value_desc("output"),
              cl::cat(ExtractorCategory));

static bool isLikelyToBeDead(BasicBlock &BB) {
  for (auto &I : BB) {
    if (isa<UnreachableInst>(&I))
      return true;
    if (auto *Call = dyn_cast<CallBase>(&I)) {
      if (Call->doesNotReturn())
        return true;
      if (auto *F = Call->getCalledFunction()) {
        auto Name = F->getName();
        if (Name.contains_insensitive("panic"))
          return true;
        if (Name.contains_insensitive("err"))
          return true;
        if (Name.contains_insensitive("fatal"))
          return true;
        if (Name.contains_insensitive("fail"))
          return true;
        if (Name.contains_insensitive("terminate"))
          return true;
        if (Name.contains_insensitive("abort"))
          return true;
        if (Name.contains_insensitive("assert"))
          return true;
        if (Name.contains_insensitive("error"))
          return true;
        if (Name.contains_insensitive("throw"))
          return true;
        if (Name.contains_insensitive("exception"))
          return true;
        if (!F->isIntrinsic()) {
          for (auto &Arg : Call->args()) {
            StringRef S;
            if (getConstantStringInfo(Arg, S))
              return true;
          }
        }
      }
    }
  }
  return false;
}

uint32_t Idx;

static void visit(Instruction *I, DenseSet<Value *> &Visited,
                  DenseSet<Instruction *> &NonTerminal, uint32_t Depth) {
  if (!Visited.insert(I).second)
    return;

  if (Depth++ > MaxDepth)
    return;

  switch (I->getOpcode()) {
  case Instruction::Call: {
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::abs:
      case Intrinsic::ctlz:
      case Intrinsic::cttz:
      case Intrinsic::ctpop:
      case Intrinsic::bswap:
      case Intrinsic::bitreverse:
      case Intrinsic::fshl:
      case Intrinsic::fshr:
      case Intrinsic::smax:
      case Intrinsic::smin:
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::sadd_sat:
      case Intrinsic::uadd_sat:
      case Intrinsic::ssub_sat:
      case Intrinsic::usub_sat:
      case Intrinsic::sshl_sat:
      case Intrinsic::ushl_sat:
      case Intrinsic::scmp:
      case Intrinsic::ucmp:
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::usub_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::umul_with_overflow:
        break;
      default:
        return;
      }
    } else
      return;
    break;
  }
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    //   case Instruction::UDiv:
    //   case Instruction::SDiv:
    //   case Instruction::URem:
    //   case Instruction::SRem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Select:
    //   case Instruction::GetElementPtr:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::ICmp:
  case Instruction::ExtractElement:
    break;
  default:
    return;
  }

  NonTerminal.insert(I);
  for (auto &Op : I->operands()) {
    if (auto *Inst = dyn_cast<Instruction>(Op))
      visit(Inst, Visited, NonTerminal, Depth);
  }
}
static bool isValidCond(Value *V, DenseSet<Instruction *> &NonTerminal,
                        DenseSet<Value *> &Terminals,
                        DenseSet<Instruction *> &NewNonTerminal,
                        uint32_t Depth) {
  if (match(V, m_ImmConstant()))
    return !isa<GlobalValue>(V);
  if (Terminals.contains(V))
    return true;
  if (Depth > MaxDepth)
    return false;
  if (auto *Inst = dyn_cast<Instruction>(V)) {
    if (NonTerminal.contains(Inst))
      return true;
    if (isa<PHINode>(Inst) || isa<AllocaInst>(Inst) || isa<InvokeInst>(Inst))
      return false;
    if (auto *II = dyn_cast<IntrinsicInst>(Inst)) {
      for (auto &Op : II->args())
        if (!isValidCond(Op, NonTerminal, Terminals, NewNonTerminal, Depth + 1))
          return false;
    } else {
      for (auto &Op : Inst->operands())
        if (!isValidCond(Op, NonTerminal, Terminals, NewNonTerminal, Depth + 1))
          return false;
    }
    NewNonTerminal.insert(Inst);
    return true;
  }
  return false;
}

static void extractCond(Instruction *Root, bool IsCondTrue, Module &NewM,
                        const SimplifyQuery &Q) {
  DenseSet<Value *> Visited;
  DenseSet<Instruction *> NonTerminal;
  visit(Root, Visited, NonTerminal, /*Depth=*/0);
  if (NonTerminal.size() <= 1)
    return;
  DenseSet<Value *> Terminals;

  DenseMap<Instruction *, uint32_t> Degree;
  for (auto *I : NonTerminal) {
    for (Value *Op : I->operands()) {
      if (auto *Inst = dyn_cast<Instruction>(Op);
          Inst && NonTerminal.contains(Inst))
        Degree[Inst]++;
      else if ((!match(Op, m_ImmConstant()) || isa<GlobalValue>(Op)) &&
               !Op->getType()->isFunctionTy())
        Terminals.insert(Op);
    }
    // Disallow external uses
    if (I != Root)
      for (auto *User : I->users())
        if (auto *Inst = dyn_cast<Instruction>(User))
          if (!NonTerminal.contains(Inst))
            return;
  }

  // Add conditions
  DenseMap<Value *, bool> PreConditions;
  auto addCondFor = [&](Value *Cond, bool CondIsTrue) {
    if (!isa<Instruction>(Cond))
      return;
    DenseSet<Instruction *> NewNonTerminal;
    if (!isValidCond(Cond, NonTerminal, Terminals, NewNonTerminal, /*Depth=*/0))
      return;

    for (auto *I : NewNonTerminal) {
      if (NonTerminal.insert(I).second) {
        for (Value *Op : I->operands()) {
          if (auto *Inst = dyn_cast<Instruction>(Op)) {
            if (NonTerminal.contains(Inst) || NewNonTerminal.contains(Inst))
              Degree[Inst]++;
          }
        }
      }
    }
    PreConditions[Cond] = CondIsTrue;
  };
  auto addCond = [&](Value *V) {
    // TODO: use idoms instead?
    auto DTN = Q.DT->getNode(Root->getParent());
    while (DTN) {
      auto IDom = DTN->getIDom();
      if (!IDom)
        break;
      auto *BI = dyn_cast<BranchInst>(IDom->getBlock()->getTerminator());
      if (BI && BI->isConditional()) {
        auto Edge1 = BasicBlockEdge(BI->getParent(), BI->getSuccessor(0));
        if (Q.DT->dominates(Edge1, Q.CxtI->getParent()))
          addCondFor(BI->getCondition(), /*CondIsTrue=*/true);
        auto Edge2 = BasicBlockEdge(BI->getParent(), BI->getSuccessor(1));
        if (Q.DT->dominates(Edge2, Q.CxtI->getParent()))
          addCondFor(BI->getCondition(), /*CondIsTrue=*/false);
      }
      DTN = IDom;
    }
    // for (auto BI : Q.DC->conditionsFor(V)) {
    //   auto Edge1 = BasicBlockEdge(BI->getParent(), BI->getSuccessor(0));
    //   if (Q.DT->dominates(Edge1, Q.CxtI->getParent()))
    //     addCondFor(BI->getCondition(), /*CondIsTrue=*/true);
    //   auto Edge2 = BasicBlockEdge(BI->getParent(), BI->getSuccessor(1));
    //   if (Q.DT->dominates(Edge2, Q.CxtI->getParent()))
    //     addCondFor(BI->getCondition(), /*CondIsTrue=*/false);
    // }
    for (auto &AssumeVH : Q.AC->assumptionsFor(V)) {
      if (!AssumeVH)
        continue;
      CallInst *I = cast<CallInst>(AssumeVH);
      if (!isValidAssumeForContext(I, Q.CxtI, Q.DT))
        continue;

      auto *Cond = I->getArgOperand(0);
      addCondFor(Cond, /*CondIsTrue=*/true);
    }
  };
  for (auto *V : Terminals)
    addCond(V);
  for (auto *I : NonTerminal)
    addCond(I);
  if (PreConditions.empty())
    return;

  SmallVector<Instruction *, 16> Queue;
  SmallVector<Instruction *, 16> WorkList;
  for (auto *I : NonTerminal) {
    if (Degree[I] == 0)
      WorkList.push_back(I);
  }

  while (!WorkList.empty()) {
    auto *I = WorkList.pop_back_val();
    Queue.push_back(I);
    for (Value *Op : I->operands()) {
      if (auto *Inst = dyn_cast<Instruction>(Op);
          Inst && NonTerminal.count(Inst) && --Degree[Inst] == 0)
        WorkList.push_back(Inst);
    }
  }

  SmallVector<Type *, 16> Types;
  for (auto *I : Terminals)
    Types.push_back(I->getType());
  auto *FTy = FunctionType::get(Type::getInt1Ty(NewM.getContext()), Types,
                                /*isVarArg=*/false);
  ++Idx;
  auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage,
                             "src" + std::to_string(Idx), NewM);
  auto *BB = BasicBlock::Create(NewM.getContext(), "entry", F);
  DenseMap<Value *, Value *> ValueMap;
  uint32_t ArgIdx = 0;
  for (auto *Val : Terminals) {
    ValueMap[Val] = F->getArg(ArgIdx);
    if (isGuaranteedNotToBePoison(Val, Q.AC, Q.CxtI, Q.DT))
      F->addParamAttr(ArgIdx, Attribute::NoUndef);
    if (Val->getType()->isPtrOrPtrVectorTy() && isKnownNonZero(Val, Q))
      F->addParamAttr(ArgIdx, Attribute::NonNull);
    // TODO: range/knownbits
    ArgIdx++;
  }
  while (!Queue.empty()) {
    auto *I = Queue.pop_back_val();
    auto *NewI = I->clone();
    if (auto *II = dyn_cast<IntrinsicInst>(NewI)) {
      auto *CalledFunction = II->getCalledFunction();
      II->setCalledFunction(NewM.getOrInsertFunction(
          CalledFunction->getName(), CalledFunction->getFunctionType(),
          CalledFunction->getAttributes()));
    }
    // Check external uses
    // if (I != Root)
    //   for (auto *User : I->users())
    //     if (auto *Inst = dyn_cast<Instruction>(User))
    //       if (!NonTerminal.count(Inst)) {
    //         NewI->setName("use");
    //         break;
    //       }
    NewI->insertInto(BB, BB->end());
    auto Iter = PreConditions.find(I);
    if (Iter != PreConditions.end()) {
      IRBuilder<> Builder(BB, BB->end());
      if (Iter->second)
        Builder.CreateAssumption(NewI);
      else
        Builder.CreateAssumption(Builder.CreateNot(NewI));
    }
    ValueMap[I] = NewI;
  }

  for (auto &I : *BB) {
    for (Use &Op : I.operands()) {
      if (ValueMap.count(Op))
        Op.set(ValueMap[Op]);
    }
  }

  auto *Cond = ValueMap[Root];
  assert(Cond);
  ReturnInst::Create(NewM.getContext(), Cond, BB);
  //   F->dump();
  //   Root->getParent()->getParent()->dump();
  //   errs().flush();
  if (verifyFunction(*F, &errs())) {
    errs() << *F;
    abort();
  }

  auto *TgtF = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                "tgt" + std::to_string(Idx), NewM);
  auto *TgtBB = BasicBlock::Create(NewM.getContext(), "entry", TgtF);
  ReturnInst::Create(NewM.getContext(),
                     ConstantInt::getBool(NewM.getContext(), IsCondTrue),
                     TgtBB);
}

static void visitFunc(Function &F, Module &NewM) {
  DenseMap<BranchInst *, uint32_t> Visited;
  auto AddEdge = [&](BranchInst *BI, bool IsCondTrue, const SimplifyQuery &Q) {
    auto *Cond = dyn_cast<Instruction>(BI->getCondition());
    if (!Cond)
      return;
    auto &Count = Visited[BI];
    if (IsCondTrue) {
      if (Count & 1)
        return;
      Count |= 1;
    } else {
      if (Count & 2)
        return;
      Count |= 2;
    }

    extractCond(Cond, !IsCondTrue, NewM, Q);
  };

  DominatorTree DT(F);
  AssumptionCache AC(F);
  DomConditionCache DC;
  SimplifyQuery SQ{F.getParent()->getDataLayout(), nullptr, &DT, &AC};
  SQ.DC = &DC;
  ReversePostOrderTraversal<Function *> RPOT(&F);

  DenseSet<BasicBlock *> InterestingBBs;
  for (auto &BB : F)
    if (isLikelyToBeDead(BB))
      InterestingBBs.insert(&BB);

  for (auto *BB : RPOT) {
    // for (auto &I : make_early_inc_range(*BB)) {
    //   if (auto *II = dyn_cast<MinMaxIntrinsic>(&I)) {
    //     if (II->getType()->isVectorTy())
    //       continue;
    //     auto Pred = ICmpInst::getNonStrictPredicate(II->getPredicate());
    //     auto *Cmp = ICmpInst::Create(Instruction::ICmp, Pred,
    //     II->getOperand(0),
    //                                  II->getOperand(1), "", II);
    //     auto Q = SQ.getWithInstruction(Cmp);
    //     extractCond(Cmp, true, NewM, Q);
    //     Cmp->setPredicate(ICmpInst::getSwappedPredicate(Pred));
    //     extractCond(Cmp, true, NewM, Q);
    //     Cmp->eraseFromParent();
    //   }
    // }

    auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (!BI || BI->isUnconditional())
      continue;
    DC.registerBranch(BI);
    auto Q = SQ.getWithInstruction(BB->getFirstNonPHI());
    if (auto *Cond = dyn_cast<Instruction>(BI->getCondition())) {
      // if (InterestingBBs.count(BI->getSuccessor(0)))
      AddEdge(BI, /*IsCondTrue=*/true, Q);
      // if (InterestingBBs.count(BI->getSuccessor(1)))
      AddEdge(BI, /*IsCondTrue=*/false, Q);
    }
  }
}

static void cleanup(Module &M) {
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

  ModulePassManager MPM = PB.buildModuleSimplificationPipeline(
      OptimizationLevel::O3, ThinOrFullLTOPhase::None);

  MPM.run(M, MAM);

  std::vector<std::string> DeadFuncs;
  for (auto &F : M) {
    if (F.empty())
      continue;
    if (F.getName().starts_with("src")) {
      auto *RetValue =
          cast<ReturnInst>(F.getEntryBlock().getTerminator())->getReturnValue();
      if (isa<Constant>(RetValue) || F.getEntryBlock().size() > 5)
        DeadFuncs.push_back(F.getName().str());
    }
  }

  for (auto &Name : DeadFuncs) {
    auto *F = M.getFunction(Name);
    F->eraseFromParent();
    auto *TgtF = M.getFunction("tgt" + Name.substr(3));
    TgtF->eraseFromParent();
  }
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "potential dead code extractor\n");
  std::vector<std::string> BlockList{
      "ruby/optimized/vm.ll",
      "/regexec.ll",
      "quickjs/optimized/quickjs.ll",
  };

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.string().find("/optimized/") == std::string::npos)
        continue;
      if (Path.extension() == ".ll") {
        bool Blocked = false;
        for (auto &Pattern : BlockList)
          if (Path.string().find(Pattern) != std::string::npos) {
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
  Context.setDiagnosticHandlerCallBack([](const DiagnosticInfo *DI, void *) {});
  uint32_t Count = 0;
  auto OutputBase = fs::path{std::string{OutputDir}};

  if (fs::exists(OutputBase))
    fs::remove_all(OutputBase);
  fs::create_directories(OutputBase);

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    // errs() << Path << '\n';
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;

    Idx = 0;
    Module NewM("", Context);
    for (auto &F : *M) {
      if (F.empty())
        continue;
      visitFunc(F, NewM);
    }
    cleanup(NewM);

    bool Valid = false;
    for (auto &F : NewM) {
      if (!F.empty()) {
        Valid = true;
        break;
      }
    }
    if (Valid) {
      if (verifyModule(NewM, &errs()))
        abort();

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
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  return EXIT_SUCCESS;
}
