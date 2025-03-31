import os
import requests
from openai import OpenAI

token = os.environ["DS_TOKEN"]
client = OpenAI(api_key=token, base_url="https://api.deepseek.com")
# pr_id = 120905
# patch = requests.get(f"https://github.com/llvm/llvm-project/pull/{pr_id}.diff").text

PROMPT = """
You are a senior LLVM maintainer.
Condider the following LLVM IR:
```
define i1 @test(i16 %n) {
entry:
  %cond = icmp slt i16 %n, 1
  br i1 %cond, label %exit, label %loop.preheader

loop.preheader:
  %sub = add nsw i16 %n, -1
  %ext = zext nneg i16 %sub to i32
  br label %loop

loop:
  %indvar = phi i32 [ %indvar.inc, %loop.latch ], [ 0, %loop.preheader ]
  %12 = icmp eq i32 %indvar, %ext
  br i1 %12, label %exit, label %loop.latch

loop.latch:
  %indvar.inc = add nuw nsw i32 %indvar, 1
  br label %loop

exit:
  %cmp = icmp sgt i16 %n, 0
  ret i1 %cmp
}
```

Is `%indvar ule %ext` a valid dominating condition for the context instruction `%cmp = icmp sgt i16 %n, 0`?

This condition is added by the following code snippet in the ConstraintElimination pass:
```
  // Try to add condition from header to the exit blocks. When exiting either
  // with EQ or NE in the header, we know that the induction value must be u<=
  // B, as other exits may only exit earlier.
  assert(!StepOffset.isNegative() && "induction must be increasing");
  assert((Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_NE) &&
         "unsupported predicate");
  ConditionTy Precond = {CmpInst::ICMP_ULE, StartValue, B};
  SmallVector<BasicBlock *> ExitBBs;
  L->getExitBlocks(ExitBBs);
  for (BasicBlock *EB : ExitBBs) {
    WorkList.emplace_back(FactOrCheck::getConditionFact(
        DT.getNode(EB), CmpInst::ICMP_ULE, A, B, Precond));
  }
```

If not, please explain why and tell me what should we check on EB to make sure that this optimization is valid.
"""

completion = client.chat.completions.create(
    model="deepseek-reasoner",
    messages=[
        {"role": "system", "content": "You are a senior LLVM maintainer."},
        # {"role": "user", "content": "What is the difference between deferred UB and immediate UB in LLVM?"},
        # {"role": "user", "content": "What is the meaning of `samesign` flag in LLVM?"},
        # {"role": "user", "content": "What is the meaning of `disjoint` flag in LLVM?"},
        # {
        #     "role": "user",
        #     "content": "Please list all the poison generating flags in LLVM.",
        # },
        # {
        #     "role": "user",
        #     "content": "Please list all the flags that can be used to optimize the code in LLVM.",
        # },
        # {"role": "user", "content": f"Please make three short, concrete and targeted suggestions on the following patch. Don't output in Markdown format. Patch:\n{patch}"},
        {"role": "user", "content": PROMPT},
    ],
    stream=True,
    seed=19260817,
    timeout=60,
)
is_thinking = False
for chunk in completion:
    delta = chunk.choices[0].delta
    if hasattr(delta, "reasoning_content") and delta.reasoning_content != None:
        if not is_thinking:
            print("Thinking:")
            is_thinking = True
        print(delta.reasoning_content, end="", flush=True)
    else:
        if delta.content is not None:
            print(delta.content, end="", flush=True)
print("")

# print(response.choices[0].message.reasoning_content)
# print(response.choices[0].message.content)
