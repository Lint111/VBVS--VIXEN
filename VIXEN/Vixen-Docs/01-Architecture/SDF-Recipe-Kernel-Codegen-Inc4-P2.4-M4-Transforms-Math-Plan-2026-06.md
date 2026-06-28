# P2.4 M4 — domain transforms + value/float3 math + VM-control + DistScale — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox steps. **Cross-repo per milestone:** Yeroket `feat/kernel-codegen-p2` (`/home/liory/Github/Yeroket-Fantasy`) for the kernels; VIXEN `feat/sdf-recipe-codegen-p2` (`/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0/VIXEN`) for eval/emit + stack/control/DistScale machinery. Final P2.4 catalogue batch after M3 (leaves+CSG+modifiers done).

**Goal:** Wire the remaining domain transforms, the value-math + float3-math lanes, VM-control opcodes, and DistScale into VIXEN's recipe evaluators — completing the generic SDF opcode catalogue. Proven by per-op CPU parity against INDEPENDENT references (not circular oracles), SPIR-V compile, and live lavapipe renders.

**Architecture (KEY — read carefully):** Following the program's single-source thesis (and M2's MirrorX precedent, which made a position-transform a `[SdfCoreKernel]`), the **pure math is generated from canonical C# via `[SdfCoreKernel]` kernels**; only the **stack/control/DistScale machinery lives hand-written in VIXEN's `evalRecipe` + `EmitProceduralComputeShader`**. We do NOT hand-transcribe op math into VIXEN (that would duplicate canonical and reopen the M3b circular-oracle bug class at scale).
- **Domain transforms** → `[SdfCoreKernel]` **float3-returning** kernels `SdfCore_X(float3 p, …) → float3` (the mutated position). VIXEN manages `posStack`/`distScaleStack` around the call (the MirrorX pattern from M2).
- **Value-math** → `[SdfCoreKernel]` **scalar** kernels `SdfCore_MathX(float x, …) → float`. VIXEN manages the value stack (unary peek-modify / binary pop-modify / ternary).
- **Float3-math** → `[SdfCoreKernel]` kernels returning float/float3 over scalar args. VIXEN manages the 3-float groups.
- **VM-control** (Output/PushParam/PositionChannel/PushFloat3/ComposeFloat3/Passthrough/DecomposeFloat3) → NO kernel (pure stack/control ops); hand-dispatched in VIXEN eval/emit + marked `[SdfCoreOp]` in canonical so they enter the generated `SdfOpCodes.g.h` enum (like RestorePos).
- **DistScale** → VIXEN state only (a stack; see Global Constraints).

**Tech Stack:** C# (Roslyn source-gen, `~/.dotnet/dotnet`), C++23, GoogleTest, CMake (`vixen-wsl`), glslang/Vulkan (lavapipe), glm.

## Global Constraints

- **Parity oracles MUST be INDEPENDENT** (the M3b/Pyramid lesson — non-negotiable): each op's parity oracle re-derives the result from an EXTERNAL reference (glm trig/quat, published IQ/standard formulas, geometric first-principles) — NEVER a transcription of the kernel body. For transforms, **shape-check numerically** (e.g. a known point maps to a known transformed point), not just a silhouette. A parity oracle that mirrors the kernel's source cannot catch a shared transcription bug.
- **DistScale = a STACK, applied per-transform at RestorePos** (NOT a single reset-to-1 register — that is the documented buggy-Burst structure). Add `float distScaleStack[64]` paralleling `posStack` via the SAME `psp`. EVERY domain transform pushes its scale (`1.0f` for non-scaling transforms; `data[7]`=`Data2.w` for Transform): `posStack[psp]=pos; distScaleStack[psp]=<scale>; psp++; pos=SdfCore_X(pos,…)`. `RestorePos` applies + pops: `psp--; pos=posStack[psp]; stack[sp-1]*=distScaleStack[psp];`. This composes correctly for nested transforms (inner RestorePos applies the inner scale first), matching the C# **HLSL compiler** `childResult * distScaleLiteral` (`SDFHLSLCompiler.cs:935-938`) — NOT the Burst VM (which never applies it = a known C# bug). **M4a must update the EXISTING MirrorX case (push 1.0f) + RestorePos case** when introducing the distScaleStack. Emit side: a parallel emit-time scale stack; at RestorePos, if `|scale-1|>1e-4` emit `float tN = <stk.back()> * <scaleLiteral>;` and replace TOS, then pop `curPos`.
- **Transform's quaternion rotate** has no `math.mul(quat,float3)` mapping on either side → author `SdfCore_Transform`'s body with the explicit cross-product quaternion-rotate formula (`t = 2*cross(q.xyz, v); v' = v + q.w*t + cross(q.xyz, t)`) — uses only +,*,cross (all mapped), transpiles straight-line. Oracle = glm::quat (independent).
- **Kernel bodies = copy/transpile from canonical C#** at the cited location (the math is the reference, but verify against an INDEPENDENT formula per the parity rule). Carry forward M3 gotchas: glm has NO swizzles → use `new float2(p.x,p.z)` not `p.xz`; any `math.*` in a kernel body must map in BOTH `MappingTables.cs` (HLSL) AND `CppMappingTables.cs` (C++) — if a kernel needs `fmod`/`round`/`smoothstep`/`frac`/`step`/`exp`/`log`/`log2`, ADD the missing `CppMappingTables.cs` entry first (glm::mod/round/smoothstep/fract/step/exp/log/log2; ⚠ HLSL `frac`→glm `fract` name differs).
- **Enum is generated**: kernels via `[SdfCoreKernel]`, control ops via `[SdfCoreOp]`. Confirm canonical `SDFOpCode` values (transforms 37-46, value-math 56-82, float3 98-109, control 94-100). Do NOT hand-edit VIXEN's enum.
- **DEFER (P4, dynamic params):** ReadParam(96), ReadParamFloat3(111). **EXCLUDE:** loops/branches (123-128), blackboard (112-118), array (119-122), noise (47-55, 83-88, 110), 2D prims (89-92), plant/MC (129-150).
- **Float3ScalarMul(107)/Float3Dot(108)/Float3Normalize(109) canonical bug:** Yeroket's `CompileToBurst` returns `SDFOpCode.Output` for these (the real opcode is never dispatched in C#-compiled recipes). The `EvalBurst` math is correct. **Wire them CORRECTLY in VIXEN** (kernel + eval/emit). Flag the C# `CompileToBurst` bug in the report; fixing it canonical-side (so C#-compiled recipes also work, like the Pyramid fix) is a small follow-up to raise with the user — not required for M4 to ship VIXEN-correct ops.
- **Render ICD-only:** `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`; the PNG read is a guard but for transforms/math also do the numeric independent parity.
- **Yeroket:** `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` only (else 0 tests run); rebuild+commit DLL; never Unity. Both branches UNMERGED. Spawn workers with `mode: bypassPermissions`. No-regression: dotnet 4 pre-existing fails; ALL M2+M3 parity (M3b 33/33 etc.) + every prior render (MirrorCsg 25,332 / Subtract 26,604 / Torus 20,922 / Cone 17,060 / Pyramid 18,802) stay green. Trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` + `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.

## File Structure (each milestone)
- Yeroket `…/Runtime/Kernels/SdfCoreKernels.cs` (+ canonical math sources cited per-op) + regen `…/GPU/Generated/{SdfCoreKernels.g.hpp,.g.hlsl,SdfOpCodes.g.h}` + DLL; for control ops, `[SdfCoreOp]` on the canonical `SDFOpCode` members; CppMappingTables.cs entries as needed.
- VIXEN vendor: `libraries/SVO/include/Recipe/generated/{SdfCoreKernels.g.hpp,SdfOpCodes.g.h}` + `libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl`.
- VIXEN `libraries/SVO/include/Recipe/SdfRecipeEval.h` + `SdfRecipeCodegen.h` (cases + the distScaleStack machinery).
- VIXEN tests: `test_recipe_eval_parity.cpp` (independent oracles) + `test_recipe_codegen.cpp` (SPIR-V) + `libraries/RenderGraph/tests/Nodes/test_procedural_recipe_render.cpp` (one live render/milestone).

## Milestone Map

> 4 cross-repo milestones, SEQUENTIAL, escalating. Implementer **Sonnet** (`mode: bypassPermissions`), validator **Opus** (`mode: bypassPermissions`, reads renders + runs INDEPENDENT numeric checks) per milestone.

- [x] **M4a — mirror/simple transforms + the DistScale-stack scaffold (Task 1).** MirrorY(42), MirrorZ(43), Elongate(38), Revolution(46) as float3 kernels; introduce `distScaleStack` (all push 1.0f) + extend MirrorX & RestorePos. No trig, no scale yet. Gate: kernels+vendored; eval/emit cases + distScaleStack scaffold; independent parity; SPIR-V; live **Revolution** render (a revolved profile) PNG-confirmed; no-regression (incl. MirrorX still correct after the RestorePos change).
- [x] **M4b — warp transforms + Transform + DistScale application (Task 2).** Twist(39), Bend(40), RepeatInfinite(44), RepeatLimited(45) trig/fmod/round kernels + Transform(37) quat-rotate kernel + the DistScale APPLICATION (Transform pushes data2.w; RestorePos applies). Add CppMappingTables entries for fmod/round if the kernels use them. Gate: kernels+vendored; Transform quat-rotate parity vs glm::quat; DistScale nested-correctness parity (a scaled Transform changes distances by the scale); live **Twist** render PNG-confirmed (visible helical warp); no-regression.
- [ ] **M4c — value-math lane (Task 3).** ~25 scalar `SdfCore_MathX` kernels (Sin/Cos/Smoothstep/Remap/Clamp/Abs/Frac/Pow/Sqrt/Negate/Step/Sign/Saturate/Exp/Log/Log2; Add/Sub/Mul/Div/Min/Max; Lerp; Select; + PositionChannel/Displacement/DistanceTo leaf/peek ops) + VIXEN value-stack dispatch (unary/binary/ternary). Add CppMappingTables entries (smoothstep/frac/step/exp/log/log2). Gate: kernels+vendored; independent parity per op; SPIR-V; live **Displacement** render (sphere + sin surface bumps) PNG-confirmed; no-regression.
- [ ] **M4d — float3-math + VM-control (Task 4).** Float3 kernels (Add/Sub/MulCW/Min/Max 102-106; ScalarMul/Dot/Normalize 107-109 wired CORRECTLY) + VM-control (Output 94, PushParam 95, PushFloat3 98, ComposeFloat3 99 no-op, Passthrough 100, DecomposeFloat3 101, PositionChannel 73 if not in M4c) as `[SdfCoreOp]`/hand-dispatch. Gate: kernels+vendored; parity; SPIR-V; a live render exercising a float3-math path; no-regression; flag the Float3 CompileToBurst canonical bug. **M4 COMPLETE.**

## Progress Log

- Milestone M4a (Task 1): DONE · Yeroket `f9615435` + VIXEN `d42a2c77` (+teeth fix `e93c37d3`) · Opus validator APPROVED after 1 fix-loop · 2026-06-28
  - Kernels: SdfCore_MirrorY(42)/MirrorZ(43)/Elongate(38)/Revolution(46) as float3 [SdfCoreKernel]; enum 37→41; dotnet 91 pass / 4 pre-existing fail.
  - DistScale STACK scaffold landed: `distScaleStack[64]` parallel to `posStack` (same `psp`); MirrorX + all M4a transforms push 1.0f; RestorePos pops+applies `stack[sp-1]*=distScaleStack[psp]`; emit mirrors via `distScaleSaveStk`. (Application of a non-1 scale arrives in M4b/Transform.)
  - Parity 38/38 (independent oracles), SPIR-V 7/7, live RenderRevolution=21,900px (validator read PNG = clean revolved torus).
  - FIX-LOOP (durable lesson): validator's TAMPER test caught MirrorY/MirrorZ parity tests as VACUOUS — an origin-centered child sphere makes `length(p)` invariant under a single-axis sign flip, so an identity-broken mirror kernel PASSED. Fixed test-only by moving the child off-axis (center (0,0.5,0)/(0,0,0.5)) + matching independent oracle; re-tamper confirmed both now FAIL on a broken kernel. The Pyramid lesson recurs: a parity oracle needs geometry where the op is observable, or it has no teeth.
  - No-regression exact: MirrorCsg 25,332 / Subtract 26,604 / Torus 20,922 / Cone 17,060 / Pyramid 18,802.
- Milestone M4b (Task 2): DONE · Yeroket `251127bb` + VIXEN `60f68413` (+cleanup `f40e1a8d`) · Opus validator APPROVED · 2026-06-28
  - Kernels: Twist(39)/Bend(40)/RepeatInfinite(44)/RepeatLimited(45)/Transform(37). CppMappingTables got math.fmod→glm::mod + math.round→glm::round. Transform quat-rotate = explicit cross-product form (no math.mul(quat,float3) needed).
  - **DistScale APPLICATION proven**: Transform pushes `data[11]` (= canonical Data2.w = distanceScale) onto distScaleStack; RestorePos applies `stack[sp-1]*=distScaleStack[psp]`. DURABLE GOTCHA: the plan's estimated indices (data[7]/data[3..6]) were WRONG — canonical SDFTransformNode packs Data0=data[0..3](trans), Data1=data[4..7](invRot quat xyzw), Data2=data[8..11](invScale.xyz + distScale.w). Implementer correctly trusted canonical over the plan; validator confirmed eval AND emit read the same slots (a parity test alone can't catch a wrong-but-consistent index — only a canonical cross-check can).
  - PROCESS WIN: implementer self-tamper-proofed all 7 (5 transforms→`return p`; distScale push→1.0; stack→register collapse) BEFORE reporting → no toothless test reached the validator (the M4a lesson, front-loaded). Validator independently re-tampered all 7 + confirmed register-collapse fails ONLY the nested test (proves real stack composition) + Transform oracle is independent glm::quat (≠ kernel's cross-product).
  - Parity 45/45, codegen/SPIR-V 8/8, render 8/8; live RenderTwist=19,161px (validator PNG read = helical twisted column). No-regression all exact.
  - Cleanup `f40e1a8d`: re-vendored SdfCoreKernels.g.hpp VERBATIM (M4b kernels had been hand-reformatted → broke the byte-identical-regen invariant; now Yeroket↔VIXEN diff empty) + stale data[7]→data[11] comments. NOTE: real Yeroket generated-file path is `Packages/com.utility.sdf/Runtime/GPU/Generated/` (the SDF package), generator/SourceGenerator~ is under `com.yeroket.utility.kernel-framework`. ENV CAVEAT: full `build-wsl` can't link 2 non-recipe RenderGraph targets (missing libxcb-dev / xcb.h) — pre-existing, unrelated to recipe work.

---

## Task 1 [M4a]: mirror/simple transforms + DistScale-stack scaffold

**Transform kernels** (float3-returning `[KernelCallable,SdfCoreKernel]`; copy math from the cited C# location; oracle independent):

| Opcode | Val | C# location | Math (pos mutation) | data[] | DistScale |
|---|---|---|---|---|---|
| MirrorY | 42 | SDFModifierNode.cs:~700 | `p.y = abs(p.y)` | none | push 1.0f |
| MirrorZ | 43 | SDFModifierNode.cs:~710 | `p.z = abs(p.z)` | none | push 1.0f |
| Elongate | 38 | SDFModifierNode.cs:~635 | `p - clamp(p, -h, h)` | data[0..2]=h | push 1.0f |
| Revolution | 46 | RevolutionNode.cs:57-62 | `c=center(data[4..6]); pp=p-c; q=(length(vec2(pp.x,pp.z))-data[0], pp.y); return vec3(q.x,q.y,0)+c` | data[0]=offset, data[4..6]=center | push 1.0f |

- [ ] **Step 1 (Yeroket kernels):** add `SdfCore_MirrorY/MirrorZ/Elongate/Revolution` float3 kernels (mirror the existing `SdfCore_MirrorX` shape; Revolution uses `new float2(pp.x,pp.z)` per the glm-swizzle rule). `// mirrors <C# source>`. Rebuild DLL; `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj`; full test green (4 pre-existing); g++-compile the regenerated `.g.hpp`; grep `.g.hlsl` no bare-int division. Commit Yeroket + vendor.
- [ ] **Step 2 (VIXEN distScaleStack scaffold):** in `SdfRecipeEval.h` add `float distScaleStack[64];` paralleling `posStack`. Update the EXISTING `MirrorX` case to `posStack[psp]=pos; distScaleStack[psp]=1.0f; psp++; pos=SdfCore_MirrorX(pos);`. Update `RestorePos` to `psp--; pos=posStack[psp]; stack[sp-1]*=distScaleStack[psp];`. Mirror the same in `SdfRecipeCodegen.h` (an emit-time `std::vector<float> distScaleSaveStk`; at RestorePos, if `|scale-1|>1e-4` emit `float tN = <top>*<lit>;` replace top, then pop curPos). Add the 4 new transform cases (push pos + distScaleStack 1.0f + call the kernel; emit mirrors). Build the 3 recipe targets clean.
- [ ] **Step 3 (independent parity):** in `test_recipe_eval_parity.cpp`, per transform a recipe `[<transform>, Sphere, RestorePos]` (transform a sphere) → oracle = `sphere(transform_ref(p))` where `transform_ref` is computed INDEPENDENTLY (e.g. MirrorY oracle mirrors p.y by hand; Revolution oracle via glm). ≥4 points incl. ones where the transform changes the result. Verify `evalRecipe == oracle` to 1e-5. Also a no-op DistScale sanity (all-1.0 path equals the pre-M4a result — MirrorX non-regression).
- [ ] **Step 4 (SPIR-V + render):** extend `test_recipe_codegen.cpp` (a transform recipe compiles, the `posStack`/scale emit appears). Add live `RenderRevolution` (revolve a small circle offset from the axis → a torus-like revolved solid) → `/tmp/glsl_sdf_m4_revolution.png` ICD-only; assert non-trivial px; READ the PNG (a revolved/ring solid).
- [ ] **Step 5 (no-regression + commit):** all recipe parity/codegen/bake green; the M2 `RenderMirrorCsgRecipe` STILL 25,332px (proves the RestorePos+distScaleStack change didn't break MirrorX); M3 renders unchanged. Commit VIXEN.

## Task 2 [M4b]: warp transforms + Transform + DistScale application

| Opcode | Val | C# location | Math | data[] | DistScale |
|---|---|---|---|---|---|
| Twist | 39 | SDFModifierNode.cs:~660 | `k=data[0]; c=cos(k*p.y); s=sin(k*p.y); return vec3(c*p.x - s*p.z, p.y, s*p.x + c*p.z)` | data[0]=k | push 1.0f |
| Bend | 40 | SDFModifierNode.cs:~680 | `k=data[0]; c=cos(k*p.x); s=sin(k*p.x); return vec3(c*p.x - s*p.y, s*p.x + c*p.y, p.z)` | data[0]=k | push 1.0f |
| RepeatInfinite | 44 | SDFModifierNode.cs:~720 | `sp=data[0..2]; return fmod(abs(p)+sp*0.5, sp) - sp*0.5` (component-wise) | data[0..2]=spacing | push 1.0f |
| RepeatLimited | 45 | SDFModifierNode.cs:~730 | `s=data[0]; lim=data[1..3]; return p - s*clamp(round(p/s), -lim, lim)` | data[0]=spacing, data[1..3]=limit | push 1.0f |
| Transform | 37 | SDFTransformNode.cs:231-303 | `return quatRotate(invRot, p - trans) * invScale` (quatRotate = cross-product form) | data[0..2]=translation, data[3..6]=invRot quat(xyzw), data[8..10]=invScale, data[7]=Data2.w=distScale | **push data[7]** |

- [ ] **Step 1 (CppMappingTables):** if Twist/Bend use `math.sin`/`cos` (mapped) fine; RepeatInfinite needs `math.fmod`→`glm::mod`, RepeatLimited needs `math.round`→`glm::round` — ADD these to `CppMappingTables.cs` (component-wise glm for vec3) BEFORE the kernels. Rebuild DLL.
- [ ] **Step 2 (Yeroket kernels):** add `SdfCore_Twist/Bend/RepeatInfinite/RepeatLimited` (verbatim per table, swizzle-safe) + `SdfCore_Transform` with the explicit cross-product quat-rotate (`t=2*cross(q.xyz,v); v'=v+q.w*t+cross(q.xyz,t)` applied to `p-trans`, then `*invScale`). Regen; full dotnet test green; g++-compile; HLSL no bare-int div. Commit Yeroket + vendor.
- [ ] **Step 3 (VIXEN eval/emit):** add the 5 cases. Twist/Bend/Repeat* push distScaleStack 1.0f; **Transform pushes `distScaleStack[psp]=in.data[7]`**. Decode Transform's quat from `data[3..6]`, trans `data[0..2]`, invScale `data[8..10]`. Emit mirrors (Transform's data2.w literal into the emit-time scale stack). Build clean.
- [ ] **Step 4 (independent parity — the crux):** Twist/Bend oracle via glm sin/cos by hand; Repeat* via glm mod/round; **Transform oracle via `glm::quat` rotation** (independent of the cross-product kernel — this catches a quat-formula transcription bug). **DistScale nested test:** a recipe `[Transform(scale=2), Sphere(r=1), RestorePos]` must yield `2 * sphere_at(transformed_p)` (distances scaled) — and a NESTED `[Transform(s1), Transform(s2), Sphere, RestorePos, RestorePos]` yields `s1*s2*…` (proves the stack composes, not a single register). ≥4 points each.
- [ ] **Step 5 (SPIR-V + render):** SPIR-V test for a Twist+Transform recipe. Live `RenderTwist` (twist a tall box → visible helical warp) → `/tmp/glsl_sdf_m4_twist.png` ICD-only; READ the PNG (a clearly twisted/helical solid, not a plain box).
- [ ] **Step 6 (no-regression + commit):** all parity/codegen/bake green; prior renders unchanged. Commit VIXEN.

## Task 3 [M4c]: value-math lane

**Scalar kernels** (`SdfCore_MathX(float x, …) → float`; unary unless noted; copy from the explore's value-math table / ScalarMathNode.cs; INDEPENDENT oracle per op):
- Unary: MathSin(56) `sin(x*d0+d1)*d2`, MathCos(57), MathSmoothstep(58), MathRemap(59), MathClamp(66), MathAbs(67), MathFrac(68), MathPow(69) `pow(abs(x),d0)*sign(x)`, MathSqrt(70) `sqrt(abs(x))`, MathNegate(72), MathStep(75), MathSign(76), MathSaturate(77), MathExp(78), MathLog(79) `log(max(x,1e-30))`, MathLog2(80).
- Binary (pop b=top, a=deeper): MathAdd(60), MathSub(61) `a-b`, MathMul(62), MathDiv(63) `b!=0?a/b:0`, MathMin(64), MathMax(65).
- Ternary: MathLerp(71) `lerp(a,b,t)` (t=top,b,a=deeper), Select(81) `cond>d0?a:b` (b=top,a,cond=deeper).
- Leaf/peek (sample pos / modify TOS): PositionChannel(73) push `pos.{x|y|z}` or `length(vec2(pos.x,pos.z))` per data[0]; Displacement(74) pop disp + `stack[sp-1] += disp*d0` (net −1); DistanceTo(82) push `length(pos - data[0..2])`.

- [ ] **Step 1 (CppMappingTables):** add `smoothstep/frac(→fract)/step/exp/log/log2` to `CppMappingTables.cs` (the kernels that use them). Rebuild DLL.
- [ ] **Step 2 (Yeroket kernels):** add the ~25 `SdfCore_MathX` kernels (verbatim per table; abs/clamp/etc. mapped). Regen; dotnet green; g++-compile; HLSL no bare-int div. Commit + vendor.
- [ ] **Step 3 (VIXEN eval/emit):** dispatch — unary `stack[sp-1]=SdfCore_MathX(stack[sp-1],…)`; binary `float b=stack[--sp]; stack[sp-1]=SdfCore_MathX(stack[sp-1],b)` (a=stack[sp-1]); ternary similar; PositionChannel/DistanceTo push (sample `pos`); Displacement pop+modify. Emit mirrors with the value-name stack. Build clean.
- [ ] **Step 4 (independent parity):** per op, oracle via glm/std (`std::sin`, etc.) re-derived independently; binary/ternary with asymmetric operands to catch order bugs. ≥4 pts.
- [ ] **Step 5 (SPIR-V + render):** SPIR-V test mixing several math ops. Live `RenderDisplacement` (`Displacement(Sphere, MathSin(PositionChannel.y))` → a sphere with sinusoidal surface bumps) → `/tmp/glsl_sdf_m4_displace.png` ICD-only; READ the PNG (a bumpy/wavy sphere, not a smooth one).
- [ ] **Step 6 (no-regression + commit):** green; prior renders unchanged. Commit VIXEN.

## Task 4 [M4d]: float3-math + VM-control

**Float3 kernels** (scalar args, return float/float3; stack = 3 consecutive scalars, x=deeper z=top): Float3Add(102)/Float3Sub(103)/Float3MulComponentWise(104)/Float3Min(105)/Float3Max(106) (−3 net), Float3ScalarMul(107) (−1 net), Float3Dot(108) (−5 net, returns scalar), Float3Normalize(109) (0 net). **Wire 107/108/109 CORRECTLY** (canonical CompileToBurst placeholder-bug noted).
**VM-control** (`[SdfCoreOp]`, hand-dispatch, no kernel): Output(94) no-op (result=`stack[sp-1]`), PushParam(95) push `data[0]`, PushFloat3(98) push 3 from data[0..2], ComposeFloat3(99) no-op, Passthrough(100) no-op, DecomposeFloat3(101) pop3 push component[data0], PositionChannel(73) if not done in M4c.

- [ ] **Step 1 (Yeroket):** add the float3 `SdfCore_Float3*` kernels; mark the VM-control opcodes `[SdfCoreOp]`. Regen; dotnet green; g++-compile; HLSL no bare-int div. Commit + vendor.
- [ ] **Step 2 (VIXEN eval/emit):** float3-math cases (pop/push the 3-float groups); VM-control cases (no-ops / pushes / index). Build clean.
- [ ] **Step 3 (parity + SPIR-V):** float3-math parity (independent oracle; asymmetric operands; Normalize against glm::normalize); a recipe using PushFloat3→ComposeFloat3→Float3Add. SPIR-V test. (Float3 ops are mostly internal plumbing — a render that drives an SDF param through a float3 path if natural; else parity+SPIR-V suffice with a note.)
- [ ] **Step 4 (no-regression + commit + flag):** all green; prior renders unchanged. Commit VIXEN. Report the Float3ScalarMul/Dot/Normalize C# `CompileToBurst`→Output canonical bug for a user decision (fix canonical like Pyramid, or leave VIXEN-only-correct).

## Self-Review

**Coverage:** transforms 37-46 (M4a+M4b), value-math 56-82 (M4c), float3 102-109 + control 94-101 (M4d), DistScale (M4a scaffold + M4b apply). Deferred ReadParam*; excluded loops/blackboard/array/noise/2D/plant/MC — all listed. ✓
**Placeholders:** every op has opcode value + C# source + math + data mapping; kernels copy canonical, oracles independent. ✓
**Architecture consistency:** math→kernels (single-source, M2/M3 pattern), machinery→VIXEN; distScale STACK (not register); quat via cross-product. ✓
**Risk:** (1) circular oracle → INDEPENDENT references mandated + numeric transform shape-checks (Pyramid lesson). (2) DistScale nesting → a stack + a nested-Transform parity test. (3) quat transcription → glm::quat oracle. (4) CPU/GPU intrinsic asymmetry → add CppMappingTables entries before regen + g++ gate. (5) the RestorePos change could break the done MirrorX → explicit MirrorCsg 25,332 non-regression in M4a.

## Execution Handoff

Run via post-brainstorm-context-manager (4 sequential cross-repo milestones; Sonnet impl + Opus validator, both `mode: bypassPermissions`). Each validator: re-run dotnet+g+++recipe tests; verify kernels vs canonical + the INDEPENDENT-oracle property; **for transforms run an independent numeric shape-check + read the render PNG**; for M4b verify DistScale nesting + the glm::quat Transform parity + tamper (zero a scale → distance parity fails); confirm no-regression incl. MirrorCsg 25,332. Persist progress per milestone. After M4: **M5** (broad live CSG-composition render gate) → P2.4 catalogue COMPLETE.
