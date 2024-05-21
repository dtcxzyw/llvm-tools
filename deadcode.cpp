// SPDX-License-Identifier: MIT License
// Copyright (c) 2024 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm-19/llvm/IR/GlobalValue.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallVector.h>
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

constexpr uint32_t MaxDepth = 6;

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

static bool hasInterestingCall(BasicBlock &BB) {
  for (auto &I : BB) {
    if (isa<UnreachableInst>(&I))
      return true;
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

static void extractCond(Instruction *Root, bool IsCondTrue, Module &NewM,
                        const SimplifyQuery &Q) {
  DenseSet<Value *> Visited;
  DenseSet<Instruction *> NonTerminal;
  visit(Root, Visited, NonTerminal, /*Depth=*/0);
  if (NonTerminal.empty())
    return;
  DenseSet<Value *> Terminals;

  DenseMap<Instruction *, uint32_t> Degree;
  for (auto *I : NonTerminal) {
    for (Value *Op : I->operands()) {
      if (auto *Inst = dyn_cast<Instruction>(Op);
          Inst && NonTerminal.count(Inst))
        Degree[Inst]++;
      else if ((!match(Op, m_ImmConstant()) || isa<GlobalValue>(Op)) &&
               !Op->getType()->isFunctionTy())
        Terminals.insert(Op);
    }
  }
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
    if (I != Root)
      for (auto *User : I->users())
        if (auto *Inst = dyn_cast<Instruction>(User))
          if (!NonTerminal.count(Inst)) {
            NewI->setName("use");
            break;
          }
    NewI->insertInto(BB, BB->end());
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
  for (auto *BB : RPOT) {
    if (!hasInterestingCall(*BB))
      continue;
    auto *Node = DT.getNode(BB);
    if (!Node)
      continue;
    Node = Node->getIDom();
    if (!Node)
      continue;
    auto *DomBB = Node->getBlock();
    auto *BI = dyn_cast<BranchInst>(DomBB->getTerminator());
    if (!BI || BI->isUnconditional())
      continue;

    auto Q = SQ.getWithInstruction(BB->getFirstNonPHI());
    BasicBlockEdge Edge0(BI->getParent(), BI->getSuccessor(0));
    if (DT.dominates(Edge0, BB))
      AddEdge(BI, /*IsCondTrue=*/true, Q);

    BasicBlockEdge Edge1(BI->getParent(), BI->getSuccessor(1));
    if (DT.dominates(Edge1, BB))
      AddEdge(BI, /*IsCondTrue=*/false, Q);

    if (auto *BI = dyn_cast<BranchInst>(BB->getTerminator()))
      if (BI->isConditional())
        DC.registerBranch(BI);
  }
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "potential dead code extractor\n");

  std::vector<fs::path> InputFiles;
  for (auto &Entry : fs::recursive_directory_iterator(std::string(InputDir))) {
    if (Entry.is_regular_file()) {
      auto &Path = Entry.path();
      if (Path.string().find("/optimized/") == std::string::npos)
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
