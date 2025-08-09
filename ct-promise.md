[RFC] Constant Time Execution Guarantees in LLVM

# Motivation

Constant-time execution is crucial for cryptographic algorithms to protect against timing attacks[6][12]. However, C/C++ standard does not talk about this topic, and optimizing compilers like GCC/Clang may introduce branches even if the library developers write expressions in a "constant-time manner"[4]. Although it is allowed by the "as-if rule"[15], I think the compiler should always provide a way to avoid surprises to users. It was discussed on the forum[7][8][9] but no substantive action has been taken yet.

This RFC proposes a new function attribute `timing_attack_hardening` to guarantee that the compiler will not introduce new non-constant-time operations when optimizing a constant-time instruction sequence. It is convenient for crypto library developers since it can be simply enabled by adding `__attribute__(timing_attack_hardening)` to function definitions or by passing `-mtiming-attack-hardening` to the compiler.

# Suggested Changes to LLVM LangRef

This RFC suggests adding a new function attribute `timing_attack_hardening`. If it is set, the optimizer guarantees not to introduce non-constant-time operations when optimizing a constant-time instruction sequence. If the ISel cannot select constant-time instructions, an error diagnostic will be emitted.

The following LLVM IR instructions are considered constant-time:

```

add/sub/mul

and/or/xor

shl/lshr/ashr

zext/sext/trunc

icmp

freeze

bitcast

llvm.abs

llvm.smin/smax/umin/umax

llvm.ssa.copy

llvm.fshl/fshr

llvm.bswap

llvm.bitreverse

shufflevector

insertvalue

extractvalue

select

```

The list above is determined according to the constant-time primitives used in crypto libraries (e.g., OpenSSL[10], BoringSSL[11]), and the instruction lists with data-independent execution latency on different architectures (e.g., RISC-V Zkt[1], ARM DIT[2], Intel DOIT[3]).

## Constant-Time Selection

On RISC-V (without Zicond+Zkr), constant-time conditional moves are unavailable. So we may lower `SELECT_CC(LHS, RHS, TV, FV, Pred)` into:

```

CMP = SET_CC LHS, RHS, Pred

MASK = ADD CMP, -1

NOT_MASK = XOR MASK, -1

TV_MASKED = AND FREEZE(TV), NOT_MASK

FV_MASKED = AND FREEZE(FV), MASK

SELECTED = OR disjoint TV_MASKED, FV_MASKED

```

## Conditional Loads

This is a bit off-topic, but I think it is still worth mentioning. GVN/JumpThreading insert conditional loads when the loaded value is partially available. It may expose the risk of timing attacks as only one of the paths performs the load.

# Previous Solutions

Here are some previous solutions discussed in a recent survey[6] of constant-time crypto library implementations:

+ The most robust way to implement constant-time crypto algorithms is to use assembly. However, this method is not portable and requires more efforts to maintain implementations for different platforms.

+ In well-known crypto libraries which are written in C, constant-time primitives are implemented with the help of value barriers, which blocks the optimizer from recognizing and "optimizing" patterns. This is a rabbit hole as the compiler may optimize more aggressively in the future, and force crypto library developers to insert more barriers :(

+ __builtin_ct_choose[4][7][13] was proposed to force the compiler to emit constant-time CMOV code. However, it is not upstreamed to LLVM yet. Additionally, it is still not enough as the compiler even introduces branches for x < y[6].

# Implementation

I haven't implemented a prototype yet. If you think this RFC is pratical and the maintainance burden is limited, I am willing to implement it on mainstream platforms (X86/AArch64/RISC-V) and upstream the patch series. We can perform a end-to-end test on crypto libraries without value barriers, with the help of IR-level verifiers[5] or other binary-level analysis tools[14]. If it is ready for production, I can also help to set up a CI and fuzzing tools to avoid regressions in the future.

# References

1. RISC-V Zkt: https://dtcxzyw.github.io/riscv-isa-manual-host/unpriv-isa-asciidoc.html#crypto_scalar_zkt

2. ARM DIT: https://developer.arm.com/documentation/ddi0601/2025-03/AArch64-Registers/DIT--Data-Independent-Timing

3. Intel DOIT: https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/best-practices/data-operand-independent-timing-isa-guidance.html

4. D. J. Bernstein's blog: https://blog.cr.yp.to/20240803-clang.html

5. CT-LLVM https://eprint.iacr.org/2025/338.pdf

6. The cryptoint library: https://cr.yp.to/papers/cryptoint-20250424.pdf

7. https://discourse.llvm.org/t/side-channel-resistant-values/53045

8. https://discourse.llvm.org/t/the-as-if-rule-perf-vs-security/40326

9. https://discourse.llvm.org/t/optimizing-math-code/31090

10. OpenSSL's CT primitives: https://github.com/openssl/openssl/blob/master/include/internal/constant_time.h

11. BoringSSL's CT primitives: https://github.com/google/boringssl/blob/main/crypto/internal.h

12. BearSSL's docs https://www.bearssl.org/constanttime.html

13. What You Get is What You C: Controlling Side Effects in Mainstream C Compilers https://ieeexplore.ieee.org/document/8406587

14. ctgrind. https://github.com/agl/ctgrind

15. as-if rule: https://cppreference.com/w/cpp/language/as_if.html


Reply:

Thank you for your feedback! I agree that a prototype is essential to validate the feasibility of this proposal. I will start working on it and share my implementation experience.

> I don’t see how this is feasible without major work requiring multiple people. Most optimizations would need to be changed.
Most instruction movements, foldings, etc, could potentially be wrong.
Imagine you have a if-then-else. InstSimplify wants to simplify something in one branch. Oops, wrong optimization because the other branch was not simplified and now it’s not constant time anymore.

This proposal only focuses on preventing optimizers from introducing non-constant-time operations in "constant-time code". In your counterexample, if the optimizer breaks the constant-time guarantee, it implies that the original IR branches on a "secret" condition. That is the responsibility of library developers.

> I would be mostly concerned that it would be difficult to ensure this works reliably. Every transform would be forced to dance around this in what should otherwise be straightforward code. And we’d inevitably screw this up, so nobody would trust it.

I would expect that the maintainance burden is limited to the same level as coroutines/convergence. I saw many optimizations bail out on functions with `convergent/presplitcoroutine`.

> Applying the attribute function-wide could also make it difficult to write performant code in some cases: on targets where mul isn’t constant-time, we really don’t want to use a constant-time mul replacement for address arithmetic.

Scalar multiplication is always assumed to be constant-time (e.g., RISC-V Zkt). If not, an error diagnostic should be emitted.
For performance degradation caused by making address arithmetic constant-time, I need a prototype to evaluate the impact.

> If the proposal was to add, for example, llvm.constanttime.add, I would be less skeptical: that’s an intrinsic with specific semantics, which almost all transforms would naturally preserve. But you can already basically do that using inline asm, so I’m not sure how useful that is.
> An approach that uses a separate set of constant-time intrinsics would be a lot more robust, as we’d be starting from a clean slate in terms of optimizations.

Hmm... If we cannot reuse existing optimizations, it looks like we are rewriting a new compiler. It has achieved by using "value barriers".

> Do we need other mitigations for functions containing “constant-time” operations, to deal with issues with speculative execution? I think I remember reading about that somewhere…
> It’s probably worth mentioning that next to constant-time guarantees, there is also another concern around compiler-introduced copies of secrets (e.g. secrets spilled on the stack and not zeroed out subsequently).

Good point. I also mentioned that conditional loads may be a problem. But I would like to focus on avoiding branches on "secret" conditions first.

> I agree that a function-level attribute is not a good way to model this. Both because it is very coarse-grained (even in cryptographic code, not all values are secrets) and because all optimizations are going to be wrong by default.

IMO introducing "secrets" to LLVM is too heavy. It is the business of static/dynamic(taint) analysis tools.
The current proposal propagates the attribute after function inlining, so it may introduce unnecessary constraints on some innocent callers. That is a trade-off between maintainability and performance.


I agree with @efriedma-quic. A new LLVM IR type (and a new type in C) provides a better isolation between new logic and existing optimizations. It also avoid mixing ct primitives and normal arithmetics.

I used to propose an attribute-based solution in https://discourse.llvm.org/t/rfc-constant-time-execution-guarantees-in-llvm/86700. It is intended to reuse existing codebase and maintain a good performance. However, after a deep investigation on OpenSSL and BoringSSL, I found that the patterns are significantly different from other applications. Therefore, adding a set of primitives and writing specific optimizations for crypto applications seem feasible.

As you pointed out in the limitations, `__builtin_ct_select` doesn't meet all the needs. I'd like to introduce a list of intrinsics like `llvm.ct.add/sub/mul/and/or/xor/shl/lshr/ashr/fshl/fshr/...`. In addition, we may need two explicit builtins to allow conversions between normal integer types and `secret` types.

Beside the comments above, I still have some concerns about the builtin-based solution:

1. Can these intrinsic calls be moved? In some platforms (e.g., X86 and AArch64), data-independent execution latency is controlled by some CSRs. If the user modifies the CSR before calling ct primitives, the execution order between the CSR modification and the ct arithmetic cannot be changed. Perhaps we can learn something from the FP env modeling in LLVM. 
2. How do we model the side-effect of ct primitives (e.g., the total latency of ct primitive execution are equal in all possible paths)? I'd like to convert the constraints into SMT expressions. Then we can verify the IR transformation in Alive2.
3. The only difference between this approach and the value barrier trick is auto-vectorization. How much effort is involved to add support for secret types in LV and SLP? How do we declare a vector secret type in C? We may use `typedef int secret_int4 __attribute__((ext_vector_type(4))) __attribute__(secret);`.
4. Can we get in touch with developers in OpenSSL/RustCrypto community? Some insights from the downstream users are valuable. There are similar RFCs in LLVM discourse, but crypto library developers rarely get involved into the discussion. 
