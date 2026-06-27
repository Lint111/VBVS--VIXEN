# P2.4 M3a — Binary CSG + level-set modifiers + remaining prereqs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox steps. **Cross-repo, two stacked branches:** Yeroket `feat/kernel-codegen-p2` (`/home/liory/Github/Yeroket-Fantasy`) for M3a-Y; VIXEN `feat/sdf-recipe-codegen-p2` (`/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0`) for M3a-V. First batch of M3 (the mechanical catalogue fill M2 enabled); user-sequenced "CSG + modifiers first".

**Goal:** Extend VIXEN's recipe evaluators with the 9 remaining binary CSG opcodes (Subtract, SmoothSubtract, Intersect, SmoothIntersect, Xor, SmoothMax, SmoothUnionCubic, SmoothSubtractCubic, SmoothIntersectCubic) + the 2 single-input modifiers (Round, Onion), by marking their math as `[SdfCoreKernel]` kernels (auto-grown into the generated enum) and adding the eval/emit cases — proven by CPU analytic parity (asymmetric oracles for the non-commutative ops), SPIR-V compile, and a live lavapipe CSG render. Also lands the 3 remaining M2-review prereqs.

**Architecture:** Per the "don't author — extract the existing catalogue" principle: the 11 ops already exist as plain `public static float` math in Yeroket `SDFOperations.cs`. M3a-Y adds `SdfCore_*` kernel wrappers to `SdfCoreKernels.cs` (inline bodies copied verbatim from `SDFOperations.cs`, mirroring the 5 existing kernels) marked `[KernelCallable, SdfCoreKernel]` → regen grows `SdfCoreKernels.g.hpp/.hlsl` (the functions) AND `SdfOpCodes.g.h` (the enum, automatically, since each kernel name maps to its `SDFOpCode` member). M3a-V vendors those, adds the eval + emit cases (9 binary + 2 unary), and the gates.

**Tech Stack:** C# (Roslyn source-gen, `~/.dotnet/dotnet`), C++23, GoogleTest, CMake (`vixen-wsl`), glslang/Vulkan (lavapipe), glm.

## Global Constraints

- **Copy the EXACT Yeroket math** (it is the parity oracle) — verify each kernel body against `SDFOperations.cs` at the cited line. ⚠ **Yeroket's SmoothSubtract/SmoothIntersect are NOT IQ's standard form** — use Yeroket's exactly. Pop convention is `b = stack[--sp]` (top, graph input B), `a = stack[--sp]` (deeper, graph input A); call is `op(a, b[, k])`.
  - `Subtract(a,b)` = `max(a, -b)` — **non-commutative** (A=base, B=cutter; "A minus B").
  - `Intersect(a,b)` = `max(a, b)`.
  - `Xor(a,b)` = `max(min(a,b), -max(a,b))`.
  - `SmoothSubtract(a,b,k)` = `h = saturate(0.5 - 0.5*(b+a)/k); lerp(a, -b, h) + k*h*(1-h)` — **non-commutative**.
  - `SmoothIntersect(a,b,k)` = `h = saturate(0.5 - 0.5*(b-a)/k); lerp(b, a, h) + k*h*(1-h)`.
  - `SmoothMax(a,b,k)` = `h = max(k - abs(a-b), 0)/k; max(a,b) + h*h*h*k*(1/6)`.
  - `SmoothUnionCubic(a,b,k)` = `h = max(k - abs(a-b), 0)/k; min(a,b) - h*h*h*k*(1/6)`.
  - `SmoothSubtractCubic(a,b,k)` = `h = max(k - abs(a+b), 0)/k; max(a,-b) + h*h*h*k*(1/6)` — **non-commutative**.
  - `SmoothIntersectCubic(a,b,k)` = `h = max(k - abs(a-b), 0)/k; max(a,b) + h*h*h*k*(1/6)`.
  - `Round(d,radius)` = `d - radius`.
  - `Onion(d,thickness)` = `abs(d) - thickness`.
- **Param slots:** smooth binary ops `k` = `data[2]`; Round radius / Onion thickness = `data[0]`. Non-smooth binary (Subtract/Intersect/Xor) take no param. `ParamMask=0` (baked) for M3a.
- **Enum is generated** (M3-prereq #1 landed): marking the kernels `[SdfCoreKernel]` auto-adds their `SDFOpCode` members to `SdfOpCodes.g.h`. Do NOT hand-edit VIXEN's enum. Confirm the canonical values: Subtract=26, SmoothSubtract=27, Intersect=28, SmoothIntersect=29, Xor=30, SmoothMax=31, SmoothUnionCubic=32, SmoothSubtractCubic=33, SmoothIntersectCubic=34, Round=35, Onion=36 (verify against the pinned `SDFOpCode` in `SDFInstruction.cs`).
- **New unary lane:** Round/Onion modify the top of stack in place — `stack[sp-1] = SdfCore_X(stack[sp-1], data[0])` (net stack delta 0); emit pops-and-replaces the top name.
- **Render ICD-only** (validation now optional, `c3cbfdb6`): `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`.
- **Yeroket:** `~/.dotnet/dotnet` only; rebuild + commit the DLL; never Unity; run tests via `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` (running `dotnet test` from `SourceGenerator~` WITHOUT the csproj target silently picks the non-test project → 0 tests, exit 0 — a false "pass"). Both branches KEPT/unmerged. Trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` + `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.
- **No-regression:** dotnet suite stays at the 4 pre-existing fails; VIXEN's M2 parity/codegen/bake + the MirrorCsg render gate stay green.

## File Structure

**M3a-Y (Yeroket):**
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/.../CppAstVisitor.cs` — **prereq:** sci-notation float-literal guard.
- `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs` — add the 11 `SdfCore_*` kernel wrappers.
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs` — sci-notation regression test; the kernel + enum goldens grow.
- DLL rebuilt; `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfCoreKernels.g.hpp` + `.g.hlsl` + `SdfOpCodes.g.h` regen.

**M3a-V (VIXEN):**
- Vendor: `libraries/SVO/include/Recipe/generated/{SdfCoreKernels.g.hpp, SdfOpCodes.g.h}` + `libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl`.
- `libraries/SVO/include/Recipe/SdfRecipeEval.h` — 9 binary + 2 unary cases; **prereq** asserts + DistScale breadcrumb.
- `libraries/SVO/include/Recipe/SdfRecipeCodegen.h` — 9 binary + 2 unary emit cases; **prereq** asserts.
- `libraries/SVO/tests/test_recipe_eval_parity.cpp` — parity per op (asymmetric oracles for the 3 non-commutative).
- `libraries/SVO/tests/test_recipe_codegen.cpp` — SPIR-V compile gate for a CSG+modifier recipe.
- A live render gate (extend `test_procedural_recipe_render.cpp`).

## Milestone Map

> Two milestones, SEQUENTIAL (M3a-V consumes M3a-Y's vendored kernels+enum).

- [x] **M3a-Y `[YEROKET]` — sci-notation guard + 11 kernel wrappers + regen + vendor (Task 1).** Gate: DLL rebuilt; sci-notation guard + regression test (a `1e-3f` literal emits valid C++ `1e-3f`, compiles); `~/.dotnet/dotnet test` green (kernel + enum goldens regrown — enum now 17 members incl. the 11 new); generated C++ compiles (g++ -std=c++23 + glm); vendored. Implementer **Sonnet**, validator **Opus**.
- [x] **M3a-V `[VIXEN]` — asserts + DistScale note + 11 cases + gates (Tasks 2–3).** Gate: build clean; CPU analytic parity for all 11 (asymmetric oracles for Subtract/SmoothSubtract/SmoothSubtractCubic); SPIR-V compile; **live lavapipe render** of `Subtract(Box, Sphere)` (a box with a carved spherical cavity) ICD-only; M2 gates non-regressed. Implementer **Sonnet**, validator **Opus** (reads the render PNG).

Validators **Opus** per milestone. Controller Opus, thin.

## Progress Log

- M3a-Y (Task 1): DONE · Yeroket `dc12d029` (feat/kernel-codegen-p2) + VIXEN vendor `9bfb7b14` (feat/sdf-recipe-codegen-p2) · Opus validator APPROVED · dotnet 91/95 (4 pre-existing) · 2026-06-27. **Fix-loop 1:** validator caught a real GPU-only correctness bug — `HLSLVisitor.cs` stripped the `f` suffix without re-adding `.0`, so `1f/6f`→`1/6`→HLSL integer-division `0`, zeroing the cubic-smoothing term in SmoothMax/SmoothUnionCubic/SmoothIntersectCubic/SmoothSubtractCubic (C++ path was correct → silent CPU≠GPU divergence; no existing gate covered it — g++ checks only C++, the SPIR-V gate only checks that HLSL *compiles*, the live render had no cubic op). Root-caused in all 3 HLSLVisitor emit paths + a `1f/6f` regression test; glslang-proven (`0.166666672`). **DURABLE GOTCHA:** the codegen float-literal guard must live in BOTH CppAstVisitor (sci-notation `1e-3f`) AND HLSLVisitor (bare-int `1f`→int-division); HLSL `1/6==0`. _(cosmetic: dc12d029's message still reads only "sci-notation guard"; local WIP branch, fix at squash/merge. Untracked VIXEN build artifacts noted, not from these commits.)_
- M3a-V (Tasks 2–3): DONE · VIXEN `83b5ed25` (feat/sdf-recipe-codegen-p2) · Opus validator APPROVED (read the render PNG) · eval_parity 13/13, codegen 3/3, bake 1/1, M2 MirrorCsg 25,332px non-regressed · 2026-06-27. 9 binary + 2 unary (new TOS-modify lane) eval/emit cases + `sp<64`/`sp>=2`/`paramMask==0` asserts + DistScale breadcrumb; independent analytic oracles (Yeroket non-IQ `lerp` form for SmoothSubtract/SmoothIntersect) with REAL `op(A,B)≠op(B,A)` assertions for the 3 non-commutative ops; tamper-confirmed parity constrains operand order. **Fix-loop 1 (geometry):** the live `Subtract(Box,Sphere)` gate first rendered a SOLID BOX — the plan's origin-sphere (r0.55) was fully ENCLOSED by the box (h0.7) → an interior void occluded by the solid face; a bare Box renders identically. Fixed by moving the sphere onto the +z face `(0,0,0.7)` → visible concave bite (PNG-confirmed). **DURABLE GOTCHA:** a CSG-Subtract render gate needs the cutter to PROTRUDE through a visible face, and a carved-in depression doesn't change the body-pixel count → `ASSERT_GT(px)` can't catch a degenerate solid-box render; the validator's PNG read is the only real guard (live-run gate authoritative).

---

## Task 1 [M3a-Y]: sci-notation guard + 11 kernel wrappers + regen + vendor

**Repo:** Yeroket `feat/kernel-codegen-p2` (verify).

- [ ] **Step 1 (prereq — sci-notation guard):** In `CppAstVisitor.cs` (the float-literal handler added in M2a, ~line 138 that appends `.0` when no `.` is present), guard it: SKIP the `.0` insertion when the literal text contains `e` or `E` (scientific notation). So `1e-3f` → `1e-3f` (valid C++), not `1e-3.0f` (invalid). Add a regression test in `CppEmitterTests.cs`: a kernel/inline source with a `1e-3f` literal → assert the emitted C++ contains `1e-3f` (not `1e-3.0f`) and compiles. Rebuild the DLL after.
- [ ] **Step 2:** Read `SDFOperations.cs` lines 59/67/75/103/116/155/165/175/185/219/228 to get the EXACT bodies. Read `SdfCoreKernels.cs` `SdfCore_SmoothUnion` (lines ~29-33) for the kernel-wrapper pattern (`[KernelCallable, SdfCoreKernel] public static float SdfCore_X(...) { ... }`, `Unity.Mathematics` `math.*`).
- [ ] **Step 3:** Add the 11 kernels to `SdfCoreKernels.cs`, bodies copied VERBATIM from `SDFOperations.cs` (per Global Constraints; the names MUST be `SdfCore_<OpCodeName>` so they map to the `SDFOpCode` member: `SdfCore_Subtract`, `SdfCore_SmoothSubtract`, `SdfCore_Intersect`, `SdfCore_SmoothIntersect`, `SdfCore_Xor`, `SdfCore_SmoothMax`, `SdfCore_SmoothUnionCubic`, `SdfCore_SmoothSubtractCubic`, `SdfCore_SmoothIntersectCubic`, `SdfCore_Round`, `SdfCore_Onion`). Signatures: binary non-smooth `(float a, float b)`; binary smooth `(float a, float b, float k)`; modifiers `(float d, float r)`. Add a `// mirrors SDFOperations.<Name> (SDFOperations.cs:<line>)` comment on each (the math↔kernel single-source cleanup is a noted future refactor, consistent with the existing 5 inlined kernels).
- [ ] **Step 4:** Rebuild the DLL (`~/.dotnet/dotnet build -c Release` in SourceGenerator~). `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test` to regrow the kernel goldens (`SdfCoreKernels.g.hpp/.hlsl` gain the 11 functions) AND the enum golden (`SdfOpCodes.g.h` grows from 6 → 17 members; the 11 new opcodes appear with values 26-36). Inspect all three artifacts.
- [ ] **Step 5:** Full `~/.dotnet/dotnet test` (no write-mode) → green; only the 4 pre-existing fails; the kernel + enum goldens now pass against the regrown artifacts. Report counts.
- [ ] **Step 6 (C++ compile gate):** g++ -std=c++23 -c a TU including the regenerated `SdfCoreKernels.g.hpp` + glm, odr-using all 16 `SdfCore_*` functions → clean (confirms the 11 new bodies + the sci-notation guard produce valid C++).
- [ ] **Step 7:** `git add -f` the 3 regenerated artifacts + `git add` the kernels + CppAstVisitor.cs + the test + the rebuilt DLL; commit `feat(kernel-codegen): 9 binary CSG + Round/Onion kernels + sci-notation literal guard (P2.4 M3a)` + trailers.
- [ ] **Step 8:** Vendor the 3 regenerated artifacts into VIXEN (`libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp`, `…/generated/SdfOpCodes.g.h`, `libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl`; keep VIXEN provenance headers); commit on the VIXEN branch `chore(recipe): vendor CSG+modifier kernels + grown SdfOpCode enum (P2.4 M3a)` + trailers.

## Task 2 [M3a-V]: eval + emit cases (+ prereq asserts + DistScale note)

**Repo:** VIXEN `feat/sdf-recipe-codegen-p2`.

- [ ] **Step 1:** Confirm the vendored `SdfOpCodes.g.h` now has the 11 new members (26-36) + `SdfCoreKernels.g.hpp` has the 11 `SdfCore_*` functions.
- [ ] **Step 2 (eval):** In `SdfRecipeEval.h`, add 9 binary cases (mirror the `Union`/`SmoothUnion` shape):
```cpp
case SdfOpCode::Subtract:  { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_Subtract(a,b); } break;
case SdfOpCode::Intersect: { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_Intersect(a,b); } break;
case SdfOpCode::Xor:       { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_Xor(a,b); } break;
case SdfOpCode::SmoothSubtract:      { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothSubtract(a,b,in.data[2]); } break;
case SdfOpCode::SmoothIntersect:     { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothIntersect(a,b,in.data[2]); } break;
case SdfOpCode::SmoothMax:           { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothMax(a,b,in.data[2]); } break;
case SdfOpCode::SmoothUnionCubic:    { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothUnionCubic(a,b,in.data[2]); } break;
case SdfOpCode::SmoothSubtractCubic: { float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothSubtractCubic(a,b,in.data[2]); } break;
case SdfOpCode::SmoothIntersectCubic:{ float b=stack[--sp]; float a=stack[--sp]; stack[sp++]=SdfCore_SmoothIntersectCubic(a,b,in.data[2]); } break;
```
  and the 2 unary cases (new lane — TOS modify, no net stack change):
```cpp
case SdfOpCode::Round: { stack[sp-1] = SdfCore_Round(stack[sp-1], in.data[0]); } break;  // radius = Data0.x
case SdfOpCode::Onion: { stack[sp-1] = SdfCore_Onion(stack[sp-1], in.data[0]); } break;  // thickness = Data0.x
```
- [ ] **Step 3 (prereq asserts):** add `assert(sp < 64 && "value stack overflow")` before each leaf/binary push, `assert(sp >= 2 ...)` before binary pops / `assert(sp >= 1 ...)` before unary, and `assert(in.paramMask == 0 && "ParamMask!=0 deferred to P4")` at the top of the loop. (mirror the existing `psp` guard.) Add `#include <cassert>` if not present.
- [ ] **Step 4 (DistScale breadcrumb):** add a comment at the `RestorePos` case: `// NOTE: M4's Transform must reintroduce distScale (reset to 1 here + apply on leaf distances) — the one non-switch-case extension; deferred (no rigid M2/M3a op needs it).`
- [ ] **Step 5 (emit):** In `SdfRecipeCodegen.h`, add the 9 binary emit cases (mirror `Union`/`SmoothUnion`: `b=stk.pop; a=stk.pop; emit "float tN = SdfCore_X(a,b[, f(in.data[2])]);"; stk.push(tN)`) and the 2 unary emit cases (pop-and-replace):
```cpp
case SdfOpCode::Round: { std::string a=stk.back(); stk.pop_back(); std::string t="t"+std::to_string(n++);
    body += "  float "+t+" = SdfCore_Round("+a+", "+f(in.data[0])+");\n"; stk.push_back(t); } break;
case SdfOpCode::Onion: { /* same with SdfCore_Onion */ } break;
```
  Add the same pop-underflow / non-empty-at-return asserts in the emitter.
- [ ] **Step 6:** Build: `cmake --build ../build-wsl --target test_recipe_eval_parity test_recipe_codegen test_recipe_bake -j` → clean.

## Task 3 [M3a-V]: gates — parity (asymmetric oracles), SPIR-V, live render

- [ ] **Step 1 (CPU parity):** Extend `test_recipe_eval_parity.cpp` with a case per op (or per logical group), `EXPECT_NEAR(evalRecipe(...), oracle(p), 1e-5f)` over ≥4 points. The oracle = the EXACT Yeroket formula (Global Constraints). For the recipe `[Sphere|Box, Sphere2, <op>]` the oracle is `op(distA(p), distB(p))`. **For the 3 non-commutative ops (Subtract, SmoothSubtract, SmoothSubtractCubic): use ASYMMETRIC primitives (distA ≠ distB) AND verify the oracle is `op(A, B)` not `op(B, A)`** — e.g. assert `Subtract(Box,Sphere)(p) != Subtract(Sphere,Box)(p)` at some p (proves operand order is correct, not accidentally swapped). For Round/Onion: recipe `[Sphere, Round]` → oracle `sphere(p) - radius`; `[Sphere, Onion]` → `abs(sphere(p)) - thickness`.
- [ ] **Step 2 (SPIR-V):** Extend `test_recipe_codegen.cpp` — emit a recipe mixing a smooth CSG + a modifier (e.g. `Onion(SmoothSubtract(Box, Sphere))`), assert the HLSL contains `SdfCore_SmoothSubtract(` + `SdfCore_Onion(` and compiles to SPIR-V.
- [x] **Step 3 (live render):** Extend `test_procedural_recipe_render.cpp` with `RenderSubtractBoxSphere`: recipe `Subtract(Box, Sphere)` (Box halfExtents ~0.7, Sphere **centered ON the +z face at (0,0,0.7) r~0.55 so it PROTRUDES** — ⚠ an origin sphere would be fully ENCLOSED by the box → a purely interior void hidden behind the solid face, a render indistinguishable from a bare Box; this bit M3a-V fix-loop 1) → write `/tmp/glsl_sdf_m3a_subtract.png`; assert non-trivial body px. Run ICD-only. **Validator reads the PNG — the ONLY real guard:** a carved-in depression does NOT change the body-pixel count, so `ASSERT_GT(px)` can't distinguish a degenerate solid box; the image read must confirm the concave spherical bite (proves non-commutative Subtract on the GPU path matches the CPU).
- [ ] **Step 4 (no-regression):** `test_recipe_eval_parity` / `test_recipe_codegen` / `test_recipe_bake` all green; the M2 `RenderMirrorCsgRecipe` ICD-only still 25,332px.
- [ ] **Step 5:** Commit `feat(recipe): 9 binary CSG + Round/Onion eval/emit cases + asserts + live Subtract gate (P2.4 M3a)` + trailers.

## Self-Review

**Coverage:** all 11 ops → kernels (Task 1) + eval/emit (Task 2) + parity (Task 3). New unary lane → Round/Onion (Task 2 Steps 2/5). Non-commutative correctness → asymmetric oracles + the `op(A,B)≠op(B,A)` assertion (Task 3 Step 1). The 3 prereqs → sci-notation guard (T1S1), asserts (T2S3/S5), DistScale note (T2S4). Enum auto-growth → marking flows to `SdfOpCodes.g.h` (T1S4). ✓
**Placeholders:** every kernel formula + case + slot + oracle is concrete; "verify against SDFOperations.cs:line" is an authoritative-source check (the no-author principle), not a guess. ✓
**Type consistency:** `SdfCore_X(float,float[,float])` / `(float d, float r)`; `k=data[2]`, `r/thickness=data[0]`; binary pop2-push1, unary TOS-modify — consistent eval↔emit↔oracle. ✓
**Risk:** (1) SmoothSubtract non-IQ form — Global Constraints pins it + the parity oracle copies the same formula → mismatch caught immediately. (2) operand swap on non-commutative ops — the `op(A,B)≠op(B,A)` assertion catches it. (3) unary-lane stack bookkeeping — the asserts + parity catch underflow/imbalance.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones, sequential). M3a-Y Sonnet+Opus (validator: regrown enum = 17 members 26-36 correct, C++ compiles, sci-notation guard works + tamper, kernel bodies match SDFOperations exactly). M3a-V Sonnet+Opus (validator: parity incl. the non-commutative asymmetry, reads `/tmp/glsl_sdf_m3a_subtract.png` for the carved cavity, tamper — e.g. swap a/b on Subtract → parity fails). Then persist progress + continue to M3b (primitives batch).
