// SPDX-License-Identifier: MIT License
// Copyright (c) 2025 Yingwei Zheng
// This file is licensed under the MIT License.
// See the LICENSE file for more information.

#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/CmpInstAnalysis.h>
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
#include <unordered_map>

using namespace llvm;
using namespace PatternMatch;
namespace fs = std::filesystem;

static cl::opt<std::string>
    InputDir(cl::Positional, cl::desc("<directory for input LLVM IR files>"),
             cl::Required, cl::value_desc("inputdir"));

using NodeIndex = uint32_t;
using Graph = std::vector<std::vector<NodeIndex>>;

std::pair<NodeIndex, std::vector<NodeIndex>> calcSCC(const Graph &graph) {
  const auto size = graph.size();
  std::vector<NodeIndex> dfn(size), low(size), st(size), col(size);
  NodeIndex top = 0, ccnt = 0, icnt = 0;
  std::vector<bool> flag(size);
  const auto dfs = [&](auto &&self, NodeIndex u) -> void {
    dfn[u] = low[u] = ++icnt;
    flag[u] = true;
    st[top++] = u;
    for (auto v : graph[u]) {
      if (dfn[v]) {
        if (flag[v])
          low[u] = std::min(low[u], dfn[v]);
      } else {
        self(self, v);
        low[u] = std::min(low[u], low[v]);
      }
    }
    if (dfn[u] == low[u]) {
      NodeIndex c = ccnt++, v;
      do {
        v = st[--top];
        flag[v] = false;
        col[v] = c;
      } while (u != v);
    }
  };

  for (NodeIndex i = 0; i < size; ++i)
    if (!dfn[i])
      dfs(dfs, i);
  return {ccnt, std::move(col)};
}

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  cl::ParseCommandLineOptions(argc, argv, "scanner\n");

  std::vector<std::string> BlockList{};

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
  std::map<uint32_t, uint32_t> Dist;

  for (auto &Path : InputFiles) {
    SMDiagnostic Err;
    LLVMContext Context;
    // errs() << Path.string() << '\n';
    auto M = parseIRFile(Path.string(), Err, Context);
    if (!M)
      continue;
    // auto &DL = M->getDataLayout();
    // errs() << DL.getStringRepresentation() << '\n';

    for (auto &F : *M) {
      if (F.empty())
        continue;

      std::unordered_map<PHINode *, uint32_t> PHIs;
      std::vector<PHINode *> PHIList;
      uint32_t PhiCount = 0;
      for (auto &BB : F)
        for (auto &I : BB.phis()) {
          PHIs[&I] = PhiCount++;
          PHIList.push_back(&I);
        }

      Graph G;
      G.resize(PhiCount);
      for (auto &BB : F)
        for (auto &I : BB.phis()) {
          auto IdxU = PHIs.at(&I);
          for (auto &V : I.incoming_values()) {
            if (auto *PN = dyn_cast<PHINode>(V)) {
              auto IdxV = PHIs.at(PN);
              if (IdxU != IdxV)
                G[IdxU].push_back(IdxV);
            }
          }
        }

      auto [CCnt, Col] = calcSCC(G);
      std::vector<std::vector<PHINode *>> SCC(CCnt);
      for (auto &I : PHIList) {
        auto Idx = PHIs.at(I);
        auto CIdx = Col[Idx];
        SCC[CIdx].push_back(I);
      }
      uint32_t CIdx = 0;
      for (auto &C : SCC) {
        if (C.size() < 2)
          continue;

        Value *CommonV = nullptr;
        bool Valid = true;
        for (auto *PHI : C) {
          for (auto &V : PHI->incoming_values()) {
            if (auto *PN = dyn_cast<PHINode>(V)) {
              auto IdxV = PHIs.at(PN);
              if (Col[IdxV] == CIdx)
                continue;
              if (CommonV == nullptr)
                CommonV = V.get();
              else if (CommonV != V.get()) {
                Valid = false;
                break;
              }
            }
          }
        }
        ++CIdx;

        if (Valid) {
          Dist[C.size()]++;
        }
      }
    }

    errs() << "\rProgress: " << ++Count;
  }
  errs() << '\n';

  for (auto [K, V] : Dist)
    outs() << K << ' ' << V << '\n';

  return EXIT_SUCCESS;
}
