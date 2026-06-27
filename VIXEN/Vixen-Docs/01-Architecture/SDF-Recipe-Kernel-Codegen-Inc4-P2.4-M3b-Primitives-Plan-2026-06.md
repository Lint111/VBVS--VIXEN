# P2.4 M3b — leaf-primitive catalogue (20 primitives, 3 batches) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox steps. **Cross-repo per milestone:** each batch wraps kernels in Yeroket `feat/kernel-codegen-p2` (`/home/liory/Github/Yeroket-Fantasy`), regen+vendor, then wires VIXEN `feat/sdf-recipe-codegen-p2` (`/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0/VIXEN`). Continues M3a (CSG+modifiers); same WRAP+MARK pattern, now for LEAF primitives.

**Goal:** Add the 20 remaining leaf SDF primitives to VIXEN's recipe evaluators (CPU `evalRecipe` + HLSL `EmitProceduralComputeShader`) by marking their `SDFPrimitives.cs` math as `[SdfCoreKernel]` kernels (auto-grown into the generated enum) and wiring the eval/emit leaf cases — proven by per-primitive CPU analytic parity, SPIR-V compile, and a live lavapipe render per batch (PNG-read).

**Architecture:** Same single-source pipeline as M3a: Yeroket `SdfCore_*` kernel (math from `SDFPrimitives.cs`) → generated `.g.hpp`/`.g.hlsl`/`SdfOpCodes.g.h` → vendored into VIXEN → called by the leaf eval/emit cases. Leaves are **nullary-pop / push-1 / position-stack-untouched** (the Box leaf is the exact template). The 20 enum values already exist in the canonical `SDFOpCode` (`SDFInstruction.cs`); marking the kernels regenerates VIXEN's enum subset to include them — **no enum edits**.

**Tech Stack:** C# (Roslyn source-gen, `~/.dotnet/dotnet`), C++23, GoogleTest, CMake (`vixen-wsl`), glslang/Vulkan (lavapipe), glm.

## Global Constraints

- **Leaf eval/emit template** (replicate the Box leaf exactly):
  - Eval (`SdfRecipeEval.h`): `case SdfOpCode::X: { <unpack data[] into params>; stack[sp++] = SdfCore_X(<sample>, <params>); } break;` — read `pos`, never write it; `assert(sp < 64)` before the push.
  - Emit (`SdfRecipeCodegen.h`): `case SdfOpCode::X: { std::string t="t"+std::to_string(n++); body += "  float "+t+" = SdfCore_X("+curPos+", <literals>);\n"; stk.push_back(t); } break;` — plus the `paramMask==0` assert (already at loop top) and non-empty-at-return assert.
- **`data[]` packing rule** (from `CompileToBurstEmitter.AssignSlots`): params in declaration order; a `float3` takes a full slot (and bumps to a fresh slot if mid-slot); scalars pack 4-per-slot. `data[0..3]`=Data0.xyzw, `data[4..7]`=Data1.xyzw, `data[8..11]`=Data2.xyzw. The per-primitive mapping is given in each batch table — use it verbatim.
- **Position offset (`data[4..6]`):** several primitives bake a position into Data1.xyz and the C# evaluates `f(pos − position, …)`. For those (flagged **pos-off=YES** in the tables), the eval/emit **sample point** is `pos − vec3(data[4],data[5],data[6])`, NOT `pos`. For pos-off=NO primitives, sample `pos` directly (like Box). This mirrors C# exactly — it is required for parity with C#-baked recipes. The `SdfCore_*` kernel itself stays position-agnostic (the raw `SDFPrimitives` function); the offset lives in the VIXEN eval/emit case.
- **Kernel bodies = copy VERBATIM from `SDFPrimitives.cs`** at the cited line (the math IS the parity oracle), EXCEPT the 3 flagged rewrites (Ellipsoid/Pyramid/RoundCone) which must be authored as a **mathematically-equivalent straight-line form** (no `if`-return, no loops) — see each note. For rewrites, the parity oracle uses the ORIGINAL `SDFPrimitives.cs` formula and asserts equivalence at non-degenerate sample points.
- **Float-literal guards are already in place** (M3a): CppAstVisitor (sci-notation) + HLSLVisitor (bare-int `1/6`); the regen will emit valid C++ and HLSL. Still: after each regen, spot-check the generated `.g.hlsl` for any bare-int division (`/ N`) and the `.g.hpp` for non-float literals.
- **Render gate = PNG-read is the only real guard** (M3a lesson): a pixel-count `ASSERT_GT` cannot distinguish shapes; the validator MUST read the PNG and confirm the primitive's shape. Pick poses where the shape is unmistakable from the camera.
- **Yeroket:** `~/.dotnet/dotnet` only; rebuild + commit the DLL; never Unity; run tests via `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` (without the csproj target it silently runs 0 tests, exit 0). Both branches KEPT/unmerged.
- **Render ICD-only:** `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`.
- **No-regression:** dotnet at the 4 pre-existing fails; ALL prior recipe parity (M2 + M3a 13/13) + the M2 MirrorCsg (25,332px) + M3a Subtract render stay green.
- **Excluded (do NOT wire):** `BezierCurve`(19, nested Newton-Raphson loops — not unrollable), `ProfileExtrude`(13, reads a runtime profile array). **Flagged, deferred:** `CylinderRounded` (`SDFPrimitives.cs:252`) has NO `SDFOpCode` member — cannot wire without an append-only enum addition; leave out of M3b, note for a future decision.
- Commit trailers on every commit (both repos):
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` + `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.

## File Structure (same set each batch)
- Yeroket `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs` — add the batch's `SdfCore_*` kernels; regen `…/GPU/Generated/{SdfCoreKernels.g.hpp,.g.hlsl,SdfOpCodes.g.h}` + rebuilt DLL.
- VIXEN vendor: `libraries/SVO/include/Recipe/generated/{SdfCoreKernels.g.hpp,SdfOpCodes.g.h}` + `libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl`.
- VIXEN `libraries/SVO/include/Recipe/SdfRecipeEval.h` + `SdfRecipeCodegen.h` — the batch's leaf cases.
- VIXEN tests: `libraries/SVO/tests/test_recipe_eval_parity.cpp` (per-primitive parity) + `test_recipe_codegen.cpp` (SPIR-V) + `libraries/RenderGraph/tests/Nodes/test_procedural_recipe_render.cpp` (one live render/batch).

## Milestone Map

> 3 cross-repo milestones, SEQUENTIAL (each accretes kernels onto the prior regen). Escalating complexity. Implementer **Sonnet**, validator **Opus** (reads the batch render PNG) per milestone.

- [x] **M3b-1 — 5 no-position leaves (Capsule, Cylinder, Torus, BoxRounded, Plane) (Task 1).** Pure Box-pattern, Data0-only. Gate: 5 kernels wrapped+vendored (C++ compiles, HLSL no bare-int div); 5 eval/emit cases; per-primitive parity (≥4 pts each); SPIR-V; live **Torus** render PNG-confirmed (a ring/donut); no-regression.
- [x] **M3b-2 — 9 position-offset ops (HollowCylinder, TaperedCylinder, Cone, Link, Ellipsoid, Panel, Plank, RoundedBox, CappedTorus) (Task 2).** Introduces the `data[4..6]` position-offset lane + the Ellipsoid rewrite; Panel/Plank/RoundedBox share one `SdfCore_BoxRounded` kernel (already wrapped in M3b-1 as opcode 2 — reuse it, 3 new eval cases reading position). Gate: kernels+vendored; 9 eval/emit cases (correct pos-offset); parity (Ellipsoid equivalence at non-degenerate pts); SPIR-V; live **Cone** render PNG-confirmed (a cone); no-regression.
- [ ] **M3b-3 — 6 prisms + cone-family (TriangularPrism, HexPrism, Pyramid, Segment, FakeRoundCone, RoundCone) (Task 3).** Pyramid + RoundCone rewrites; Segment is endpoint-parametric (no position field — pointA/pointB ARE geometry). Gate: kernels+vendored; 6 eval/emit cases; parity (Pyramid/RoundCone equivalence); SPIR-V; live **Pyramid** render PNG-confirmed (a pyramid); no-regression.

Validators **Opus** per milestone. Controller Opus, thin.

## Progress Log

- M3b-1 (Task 1): DONE · Yeroket `d324334e` + VIXEN `c3b9d424` · Opus validator APPROVED (read Torus PNG = donut-with-hole; tamper-confirmed slots) · dotnet 91/95, parity 18/18, SPIR-V 4/4, RenderTorus 20,922px, M2 MirrorCsg 25,332 + M3a Subtract 26,604 non-regressed · 2026-06-27. 5 no-position leaves (Capsule/Cylinder/Torus/BoxRounded/Plane); generated enum now 22 members. **DURABLE GOTCHA (forward to M3b-2/3):** `glm::vec3` has NO swizzles → any kernel body using `p.xz`/`.xy`/etc. must be authored as `new float2(p.x, p.z)` (C# Unity.Mathematics float3 HAS swizzles, glm doesn't); the codegen then emits `glm::length(glm::vec2(...))` — faithful + compiles. Cone/HexPrism/prisms in later batches will need this. **PROCESS FRICTION:** the first validator inherited harness PLAN MODE (read-only) → couldn't run any dynamic gate; honored the live-run-gate rule and refused to APPROVE on static alone. Fix: spawn validators (and to be safe, implementers) with `mode: bypassPermissions` so the autonomous background workers can build/test/render/tamper.
- M3b-2 (Task 2): DONE · Yeroket `7204de6d` + VIXEN `beca4274` · Opus validator APPROVED (Cone PNG = cone w/ apex+base; tamper-confirmed the position-offset is load-bearing) · dotnet 91/95, parity 27/27, SPIR-V 5/5, RenderCone 17,060px, M2/M3a/M3b-1 renders non-regressed · 2026-06-27. 9 position-offset (`data[4..6]`) leaves; Ellipsoid branchless rewrite (`k0*(k0-1)/max(k1,0.0001)`, oracle = original branched formula sampled off-center); Panel/Plank/RoundedBox (10/11/12) reuse `SdfCore_BoxRounded` via `[SdfCoreOp]` (shared-kernel opcodes, enum-included without a new function); generated enum now 31 members. **DURABLE GOTCHA:** any `math.*` used in a kernel body needs an entry in BOTH the HLSL `MappingTables` AND `CppMappingTables.cs` — `math.sign` was mapped for HLSL but MISSING for C++ → invalid generated C++ (a CPU/GPU mapping ASYMMETRY); fixed by adding `math.sign→glm::sign`. M3b-3 implementers must check each new intrinsic maps on BOTH sides before regen.

---

## Task 1 [M3b-1]: 5 no-position leaves

**Primitives** (sample point = `pos`; all pos-off=NO; copy bodies verbatim from `SDFPrimitives.cs`):

| OpCode | Val | SDFPrim line | Signature | data[] mapping |
|---|---|---|---|---|
| Capsule | 3 | :96 `CapsuleVertical` | `(float3 p, float height, float radius)` | data[0]=halfHeight, data[1]=radius |
| Cylinder | 4 | :210 | `(float3 p, float height, float radius)` | data[0]=halfHeight, data[1]=radius |
| Torus | 6 | :293 | `(float3 p, float majorRadius, float minorRadius)` | data[0]=majorR, data[1]=minorR |
| BoxRounded | 2 | :194 | `(float3 p, float3 halfExtents, float roundRadius)` | data[0..2]=halfExtents, data[3]=rounding |
| Plane | 5 | :267 | `(float3 p, float3 normal, float distance)` | data[0..2]=normal, data[3]=distance |

- [ ] **Step 1 (Yeroket kernels):** In `SdfCoreKernels.cs` add 5 `[KernelCallable, SdfCoreKernel] public static float SdfCore_<Name>(...)` wrappers, bodies copied VERBATIM from `SDFPrimitives.cs` at the cited lines (`// mirrors SDFPrimitives.<Name> (line N)`). Names must be `SdfCore_Capsule/Cylinder/Torus/BoxRounded/Plane` (map to the `SDFOpCode` members). Signatures exactly as the table.
- [ ] **Step 2 (regen+gate):** rebuild DLL; `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` → goldens regrow (5 funcs added to `.g.hpp`/`.g.hlsl`; `SdfOpCodes.g.h` grows to include Capsule=3,Cylinder=4,Plane=5,Torus=6,BoxRounded=2). Full `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` → green except the 4 pre-existing. g++ -std=c++23 compile of a TU including the regenerated `.g.hpp` + glm, odr-using the 5 new funcs → clean. grep the regenerated `.g.hlsl` for bare-int division → none.
- [ ] **Step 3 (vendor + commit Yeroket):** commit `feat(kernel-codegen): 5 leaf primitives — Capsule/Cylinder/Torus/BoxRounded/Plane (P2.4 M3b-1)`; vendor the 3 artifacts into VIXEN.
- [ ] **Step 4 (VIXEN eval/emit):** add 5 leaf cases each to `SdfRecipeEval.h` + `SdfRecipeCodegen.h` per the Box template. Examples:
```cpp
// eval
case SdfOpCode::Torus: { stack[sp++] = SdfCore_Torus(pos, in.data[0], in.data[1]); } break;
case SdfOpCode::BoxRounded: { glm::vec3 he(in.data[0],in.data[1],in.data[2]); stack[sp++] = SdfCore_BoxRounded(pos, he, in.data[3]); } break;
case SdfOpCode::Plane: { glm::vec3 n(in.data[0],in.data[1],in.data[2]); stack[sp++] = SdfCore_Plane(pos, n, in.data[3]); } break;
// emit (Torus)
case SdfOpCode::Torus: { std::string t="t"+std::to_string(n++); body+="  float "+t+" = SdfCore_Torus("+curPos+", "+f(in.data[0])+", "+f(in.data[1])+");\n"; stk.push_back(t); } break;
```
- [ ] **Step 5 (build):** `cmake --build ../build-wsl --target test_recipe_eval_parity test_recipe_codegen test_recipe_bake -j` → clean.
- [ ] **Step 6 (parity):** in `test_recipe_eval_parity.cpp`, a case per primitive: `EXPECT_NEAR(evalRecipe([{op, data…}], p), oracle(p), 1e-5f)` over ≥4 points. Oracle = the `SDFPrimitives.cs` formula re-derived in C++ (NOT a call to `SdfCore_*`). Pick params giving a non-degenerate shape (e.g. Torus major=0.6/minor=0.2).
- [ ] **Step 7 (SPIR-V):** extend `test_recipe_codegen.cpp` — a recipe using ≥2 of the new leaves compiles to SPIR-V; assert the `SdfCore_*` calls appear.
- [ ] **Step 8 (live render):** add `RenderTorus` to `test_procedural_recipe_render.cpp`: recipe `[Torus]` (major 0.6, minor 0.2) → `/tmp/glsl_sdf_m3b_torus.png`, ICD-only, assert non-trivial body px. **Validator reads it — must show a ring/donut.**
- [ ] **Step 9 (no-regression + commit VIXEN):** all recipe parity/codegen/bake green; M2 MirrorCsg + M3a Subtract renders unchanged. Commit `feat(recipe): 5 leaf primitives + Torus live gate (P2.4 M3b-1)`.

## Task 2 [M3b-2]: 9 position-offset ops

**Sample point = `pos − vec3(data[4],data[5],data[6])`** (pos-off=YES for all in this batch). Copy bodies verbatim EXCEPT Ellipsoid (rewrite).

| OpCode | Val | SDFPrim line | Signature (position-agnostic kernel) | data[0..3] | pos |
|---|---|---|---|---|---|
| HollowCylinder | 8 | :210+Onion | `(float3 p, float halfLen, float outerR, float wall)` = `Onion(Cylinder(p,halfLen,outerR), wall)` | x=halfLen,y=outerR,z=wall | data[4..6] |
| TaperedCylinder | 9 | :428 `ConeCapped` | `(float3 p, float height, float r1, float r2)` (ternary `q.y<0?r1:r2` — HLSL-native, no rewrite) | x=halfH,y=baseR,z=topR | data[4..6] |
| Cone | 15 | :353 | `(float3 p, float2 angle, float height)` (uses `math.sign`→`sign`, mapped) | x=sinAngle,y=cosAngle,z=height | data[4..6] |
| Link | 23 | :605 | `(float3 p, float halfLength, float majorRadius, float minorRadius)` | x=halfLen,y=majorR,z=minorR | data[4..6] |
| Ellipsoid | 7 | :62 | `(float3 p, float3 radii)` — **REWRITE** | data[0..2]=radii | data[4..6] |
| Panel | 10 | :194 | reuse `SdfCore_BoxRounded(p, he, rounding)` | data[0..2]=halfExtents,data[3]=rounding | data[4..6] |
| Plank | 11 | :194 | reuse `SdfCore_BoxRounded` | data[0..2]=he,data[3]=rounding | data[4..6] |
| RoundedBox | 12 | :194 | reuse `SdfCore_BoxRounded` | data[0..2]=he,data[3]=rounding | data[4..6] |
| CappedTorus | 14 | :336 `TorusCapped` | `(float3 p, float2 sc, float majorR, float minorR)` (ternary — HLSL-native) | x=sinA,y=cosA,z=majorR,w=minorR | data[4..6] |

> **Ellipsoid rewrite:** `SDFPrimitives.cs:62` has `if (k1 < 0.0001f) return <…>;` guarding a divide. Author the kernel as the standard GPU-safe form `k0*(k0-1.0f) / max(k1, 0.0001f)` (or the source's exact equivalent without the branch). Parity oracle = the ORIGINAL formula; sample points must avoid the exact center (k1≈0) so the oracle and the eps-form agree to 1e-5.
> **Panel/Plank/RoundedBox:** NO new kernel — they all call the `SdfCore_BoxRounded` wrapped in M3b-1. They are 3 distinct OPCODES (different values) that differ from BoxRounded(2) ONLY in reading the position from data[4..6]. Add 3 eval + 3 emit cases that offset, all calling `SdfCore_BoxRounded`.

- [ ] **Step 1 (Yeroket kernels):** add `SdfCore_HollowCylinder/TaperedCylinder/Cone/Link/Ellipsoid/CappedTorus` (6 new kernels; Panel/Plank/RoundedBox reuse BoxRounded). Verbatim from source except Ellipsoid (rewrite per note + a `// rewritten branchless from SDFPrimitives.Ellipsoid:62` comment). Confirm `math.sign` is the only new intrinsic (mapped).
- [ ] **Step 2 (regen+gate):** rebuild DLL; `UPDATE_GOLDENS=1` regen; full dotnet test green (4 pre-existing); g++ compile clean; grep `.g.hlsl` for bare-int division → none (Ellipsoid's `max(k1,0.0001f)` must be float).
- [ ] **Step 3 (vendor+commit Yeroket):** `feat(kernel-codegen): 6 positioned/cone leaf kernels + branchless Ellipsoid (P2.4 M3b-2)`; vendor.
- [ ] **Step 4 (VIXEN eval/emit):** 9 cases each. The position-offset pattern:
```cpp
// eval — positioned leaf
case SdfOpCode::Cone: { glm::vec3 q = pos - glm::vec3(in.data[4],in.data[5],in.data[6]);
    stack[sp++] = SdfCore_Cone(q, glm::vec2(in.data[0],in.data[1]), in.data[2]); } break;
case SdfOpCode::Panel: { glm::vec3 q = pos - glm::vec3(in.data[4],in.data[5],in.data[6]);
    glm::vec3 he(in.data[0],in.data[1],in.data[2]); stack[sp++] = SdfCore_BoxRounded(q, he, in.data[3]); } break;
// emit — positioned leaf (bake the offset into the position expr)
case SdfOpCode::Cone: { std::string t="t"+std::to_string(n++);
    std::string q = curPos+" - float3("+f(in.data[4])+", "+f(in.data[5])+", "+f(in.data[6])+")";
    body+="  float "+t+" = SdfCore_Cone("+q+", float2("+f(in.data[0])+", "+f(in.data[1])+"), "+f(in.data[2])+");\n";
    stk.push_back(t); } break;
```
- [ ] **Step 5 (build + parity + SPIR-V):** build the 3 test targets clean. Parity per primitive (≥4 pts; for positioned ops, set data[4..6] to a non-zero position and verify the oracle offsets the same way — include a point where a wrong/missing offset would fail). Ellipsoid: oracle = original formula, non-center points. SPIR-V test covering ≥2 positioned ops + confirming the `curPos - float3(...)` offset appears.
- [ ] **Step 6 (live render):** add `RenderCone` to `test_procedural_recipe_render.cpp`: recipe `[Cone]` (angle for a ~30° cone, height ~1.0; data[4..6]=0 or a small offset) → `/tmp/glsl_sdf_m3b_cone.png`, ICD-only. **Validator reads it — must show a cone.**
- [ ] **Step 7 (no-regression + commit VIXEN):** all recipe tests green; prior renders unchanged. Commit `feat(recipe): 9 positioned leaf primitives + Cone live gate (P2.4 M3b-2)`.

## Task 3 [M3b-3]: 6 prisms + cone-family

| OpCode | Val | SDFPrim line | Signature | data[] mapping | pos-off | rewrite |
|---|---|---|---|---|---|---|
| TriangularPrism | 20 | :530 | `(float3 p, float2 h)` | x=halfWidth,y=halfHeight | data[4..6] | no (abs+max) |
| HexPrism | 22 | :580 | `(float3 p, float2 h)` (uses `sign`, mapped) | x=circumR,y=halfH | data[4..6] | no |
| Pyramid | 21 | :547 | `(float3 p, float height)` | x=height | data[4..6] | **YES** |
| Segment | 18 | :82 `Capsule` | `(float3 p, float3 a, float3 b, float radius)` | data[0..2]=pointA,data[3]=radius,data[4..6]=pointB | **NO pos field** | no |
| FakeRoundCone | 17 | :468 | `(float3 p, float r1, float r2, float height)` | x=r1,y=r2,z=height | data[4..6] | no (saturate+lerp) |
| RoundCone | 16 | :447 `ConeRounded` | `(float3 p, float r1, float r2, float height)` | x=r1,y=r2,z=height | data[4..6] | **YES** |

> **Segment is special:** NO position field — pointA=data[0..2], radius=data[3], pointB=data[4..6]; sample `pos` directly (the endpoints carry the geometry). Eval: `SdfCore_Segment(pos, glm::vec3(data[0..2]), glm::vec3(data[4..6]), data[3])`.
> **Pyramid rewrite:** `SDFPrimitives.cs:547` has `if (q.z > q.x) q = q.zyx-style swap;`. Author as a float3 ternary: `q = (q.z > q.x) ? q.<swizzle> : q;` (valid HLSL/glm). Equivalent, branchless.
> **RoundCone rewrite:** `SDFPrimitives.cs:447` `ConeRounded` has two `if (k<0) return …; if (k>a*h) return …;` early-returns. Author as a `math.select`/ternary chain over the 3 regions so all branches evaluate and the correct one is selected (guard any `sqrt` against negative args inside the non-taken branch with `max(x,0)`). Parity oracle = the ORIGINAL piecewise formula; verify equivalence by sampling points in EACH of the 3 regions.

- [ ] **Step 1 (Yeroket kernels):** add `SdfCore_TriangularPrism/HexPrism/Pyramid/Segment/FakeRoundCone/RoundCone`. Verbatim except Pyramid + RoundCone (rewrites, with equivalence comments). Confirm no new unmapped intrinsics.
- [ ] **Step 2 (regen+gate):** rebuild DLL; `UPDATE_GOLDENS=1` regen; full dotnet test green; g++ compile clean; grep `.g.hlsl` for bare-int division → none.
- [ ] **Step 3 (vendor+commit Yeroket):** `feat(kernel-codegen): 6 prism/cone leaf kernels + branchless Pyramid/RoundCone (P2.4 M3b-3)`; vendor.
- [ ] **Step 4 (VIXEN eval/emit):** 6 cases each. Positioned (TriangularPrism/HexPrism/Pyramid/FakeRoundCone/RoundCone) offset by data[4..6]; Segment samples `pos` with pointA/pointB from data[0..2]/data[4..6]. Mirror the M3b-2 positioned pattern + the Segment example above.
- [ ] **Step 5 (build + parity + SPIR-V):** build clean. Parity per primitive (≥4 pts). RoundCone: sample a point in EACH of the 3 regions (proves the select-chain matches the piecewise original). Pyramid: include a point on each side of the `q.z>q.x` split. Segment: asymmetric endpoints. SPIR-V test covering ≥2 of these.
- [ ] **Step 6 (live render):** add `RenderPyramid` to `test_procedural_recipe_render.cpp`: recipe `[Pyramid]` (height ~1.0) → `/tmp/glsl_sdf_m3b_pyramid.png`, ICD-only. **Validator reads it — must show a pyramid** (proves the rewrite renders correctly on GPU).
- [ ] **Step 7 (no-regression + commit VIXEN):** all recipe tests green; prior renders unchanged. Commit `feat(recipe): 6 prism/cone leaf primitives + Pyramid live gate (P2.4 M3b-3)`.

## Self-Review

**Coverage:** all 20 wirable primitives across 3 batches (5+9+6); each → kernel (Yeroket) + eval/emit (VIXEN) + parity + a per-batch render. Excluded BezierCurve/ProfileExtrude + flagged CylinderRounded recorded. ✓
**Placeholders:** every primitive has opcode value + source line + signature + data[] mapping + pos-off + rewrite flag; bodies cite the authoritative `SDFPrimitives.cs` line (no-author principle) — concrete, not guesses. ✓
**Type consistency:** leaf cases `SdfCore_X(<sample>, <params>)`, push-1, pos untouched; position-offset = `pos − data[4..6]` (eval) / `curPos - float3(...)` (emit) consistently; reused `SdfCore_BoxRounded` for Panel/Plank/RoundedBox. ✓
**Risk:** (1) wrong data[] slot → parity catches it (set non-trivial params + a point sensitive to the slot). (2) missing/incorrect position-offset → parity with non-zero data[4..6] + a position-sensitive point catches it. (3) rewrite non-equivalence (Ellipsoid/Pyramid/RoundCone) → oracle = ORIGINAL formula, sampled per-region/away-from-singularity. (4) HLSL int-division regression → grep `.g.hlsl` each regen (guard already in place from M3a). (5) degenerate render → PNG-read by the validator (pixel-count can't guard shape).

## Execution Handoff

Run via post-brainstorm-context-manager (3 sequential cross-repo milestones). Each: Sonnet implementer (Yeroket wrap+regen+vendor → VIXEN cases+parity+SPIR-V+render → commit both) + Opus validator (re-run dotnet+g++; verify kernel bodies vs SDFPrimitives.cs incl. the rewrites' equivalence; verify data[] slots + position-offset via parity; **read the batch render PNG**; tamper one slot/offset → parity fails; confirm no-regression). Persist progress per milestone. After M3b: M4 (domain transforms + value/float3 math + non-uniform DistScale + `UPDATE_GOLDENS` regen-write), M5 (broad live CSG-composition render gate).
