import os
import requests
from openai import OpenAI

token = os.environ["DS_TOKEN"]
client = OpenAI(api_key=token, base_url="https://api.deepseek.com")
# pr_id = 120905
# patch = requests.get(f"https://github.com/llvm/llvm-project/pull/{pr_id}.diff").text

PROMPT = """
Condider the following LLVM IR:
```
define i32 @widget() {
b:
  br label %b1

b1:                                              ; preds = %b5, %b
  %phi = phi i32 [ 0, %b ], [ %udiv6, %b5 ]
  %phi2 = phi i32 [ 1, %b ], [ %add, %b5 ]
  %icmp = icmp eq i32 %phi, 0
  br i1 %icmp, label %b3, label %b8

b3:                                              ; preds = %b1
  %udiv = udiv i32 10, %phi2
  %urem = urem i32 %udiv, 10
  %icmp4 = icmp eq i32 %urem, 0
  br i1 %icmp4, label %b7, label %b5

b5:                                              ; preds = %b3
  %udiv6 = udiv i32 %phi2, 0
  %add = add i32 %phi2, 1
  br label %b1

b7:                                              ; preds = %b3
  ret i32 5

b8:                                              ; preds = %b1
  ret i32 7
}
```

Please check the following SCEV expressions. Are they all correct? If not, please tell me which one is incorrect and why.

%phi2 = phi i32 [ 1, %b ], [ %add, %b5 ] --> {1,+,1}<%b1>
%udiv6 = udiv i32 %phi2, 0 --> ({1,+,1}<%b1> /u 0)
%phi = phi i32 [ 0, %b ], [ %udiv6, %b5 ] --> ({0,+,1}<%b1> /u 0)

Hint:
```cpp
// Otherwise, this could be a loop like this:
//     i = 0;  for (j = 1; ..; ++j) { ....  i = j; }
// In this case, j = {1,+,1}  and BEValue is j.
// Because the other in-value of i (0) fits the evolution of BEValue
// i really is an addrec evolution.
//
// We can generalize this saying that i is the shifted value of BEValue
// by one iteration:
//   PHI(f(0), f({1,+,1})) --> f({0,+,1})

// Do not allow refinement in rewriting of BEValue.
const SCEV *Shifted = SCEVShiftRewriter::rewrite(BEValue, L, *this);
const SCEV *Start = SCEVInitRewriter::rewrite(Shifted, L, *this, false);
if (Shifted != getCouldNotCompute() && Start != getCouldNotCompute() &&
    isGuaranteedNotToCauseUB(Shifted) && ::impliesPoison(Shifted, Start)) {
    const SCEV *StartVal = getSCEV(StartValueV);
    if (Start == StartVal) {
    // Okay, for the entire analysis of this edge we assumed the PHI
    // to be symbolic.  We now need to go back and purge all of the
    // entries for the scalars that use the symbolic expression.
    forgetMemoizedResults(SymbolicName);
    insertValueToMap(PN, Shifted);
    return Shifted;
    }
}
```
"""

response = client.chat.completions.create(
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
    stream=False,
    seed=19260817,
    timeout=60,
)

print(response.choices[0].message.reasoning_content)
print(response.choices[0].message.content)
