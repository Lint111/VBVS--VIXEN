# P2.4 M2 — VM-emitter position-stack extension + representative opcodes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox steps. **Cross-repo, two stacked branches:** Yeroket `feat/kernel-codegen-p2` (`/home/liory/Github/Yeroket-Fantasy`) for M2a; VIXEN `feat/sdf-recipe-codegen-p2` (`/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0`) for M2b. Design of record: [[SDF-Recipe-Kernel-Codegen-Inc4-P2.4-M2-VM-Emitter-Design-2026-06]].

**Goal:** Extend VIXEN's recipe evaluators from leaf+binary to the full VM model by adding a **position stack**, proven on three representative opcodes spanning every new lane — `Box` (leaf), `SmoothUnion` (binary with a `k` param), `MirrorX`+`RestorePos` (domain transform) — end-to-end (Yeroket single-source → regen → VIXEN CPU eval + straight-line HLSL → live lavapipe render). After M2, M3/M4 add the rest of the catalogue as pure switch-case + generated-kernel work.

**Architecture:** M2a authors the 3 kernels as `[KernelCallable]`+`[SdfCoreKernel]` statics in Yeroket's `SdfCoreKernels.cs` (single source), rebuilds the source-gen DLL, regenerates `SdfCoreKernels.g.hpp`/`.g.hlsl` (now containing the new functions) via the confirmed dotnet path, and re-vendors them into VIXEN. M2b adds a position stack (`pos` + a save stack) to both VIXEN evaluators — `evalRecipe` (CPU) and `EmitProceduralComputeShader` (emit-time) — mirroring the C# VM, plus the three new `SdfOpCode` cases; leaves sample the current `pos`, domain transforms push/mutate it, `RestorePos` pops.

**Tech Stack:** C# (Roslyn source-gen, `~/.dotnet/dotnet`), C++23, GoogleTest, CMake (`vixen-wsl`), glslang/Vulkan (lavapipe), glm.

## Global Constraints

- **Mirror the C# VM exactly** (it is the parity oracle). Math from the verified investigation:
  - `Box(p, b)`: `float3 q = abs(p) - b; return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);`
  - `SmoothUnion(a, b, k)`: `float h = saturate(0.5 + 0.5*(b - a)/k); return lerp(b, a, h) - k*h*(1 - h);`
  - `MirrorX(p)`: `return float3(abs(p.x), p.y, p.z);`
- **Data layout (baked literals, `ParamMask`=0):** Box halfExtents = `data[0..2]` (Data0.xyz); SmoothUnion `k` = `data[2]` (Data0.z); MirrorX takes no params. (Matches the C# `Data` layout.)
- **`SdfOpCode` values are a cross-repo binary contract** — read the EXACT byte values from `com.utility.graph-framework/Runtime/VM/SDFInstruction.cs` (expected: `Box=1`, `SmoothUnion=25`, `MirrorX=41`, plus `RestorePos` — confirm its value from the enum). VIXEN's `SdfOpCode` mirror values MUST equal them.
- **No `ParamMask`/`DistScale` this milestone.** Recipes use baked literals (`ParamMask`=0); `MirrorX` is rigid so `DistScale` stays 1 (not added until M4's `Transform`). `InputMask` handling stays as today (binary pops 2).
- **Regen is dotnet-only** (P2.4 spec, resolved): `cd Packages/com.yeroket.utility.kernel-framework/SourceGenerator~ && ~/.dotnet/dotnet build -c Release` then `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj`. Commit the rebuilt `RoslynAnalyzers/SDFNodeGenerator.dll`. Do NOT run Unity / Unity MCP / `run_tests`.
- **Non-regression:** the existing Sphere/Union recipe paths (`test_recipe_eval_parity`, `test_recipe_codegen`, `test_recipe_bake`, the P2.1/P2.2/P2.3 render gates) stay green. The layering filter (M1) is unchanged.
- **Branches KEPT, not merged.** Commit M2a on Yeroket `feat/kernel-codegen-p2`, M2b on VIXEN `feat/sdf-recipe-codegen-p2`.

## File Structure

**M2a (Yeroket `/home/liory/Github/Yeroket-Fantasy`):**
- Modify `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs` — add `[KernelCallable]`+`[SdfCoreKernel]` `Box`, `SmoothUnion`, `MirrorX` (math above; mirror the existing Sphere/Union shape).
- Modify `Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll` — rebuilt (committed).
- Regenerate (in place) `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfCoreKernels.g.hpp` + `.g.hlsl` — now contain the 3 new functions.
- Modify the golden test infra (`SourceGenerator~/Tests/CppEmitterTests.cs`) — see Task 1 Step 4 (regen-write mechanism + keep the byte-identity guard valid for the new functions).

**M2b (VIXEN worktree `…/sdf-recipe-codegen-p0/VIXEN`):**
- Re-vendor: `libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp` + `libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl` (copied from Yeroket — byte-identical to the regenerated ones).
- Modify `libraries/SVO/include/Recipe/SdfInstruction.h` — add `Box`/`SmoothUnion`/`MirrorX`/`RestorePos` to `SdfOpCode`.
- Modify `libraries/SVO/include/Recipe/SdfRecipeEval.h` — position stack + 4 new cases.
- Modify `libraries/SVO/include/Recipe/SdfRecipeCodegen.h` — emit-time position stack + 4 new cases.
- Create/extend tests: `libraries/SVO/tests/test_recipe_eval_parity.cpp` (+ MirrorX/Box/SmoothUnion analytic parity), `test_recipe_codegen.cpp` (+ compile gate for the new shape), and a new live render gate (mirror the P2.2 procedural-render harness if present, else a lavapipe dispatch of the emitted shader).

---

## Milestone Map

> Two milestones, SEQUENTIAL (M2b consumes M2a's vendored kernels). Cross-repo.

- [x] **M2a `[YEROKET]` — author 3 kernels + regen + vendor (Task 1).** Gate: DLL rebuilt; `~/.dotnet/dotnet test` green (golden tests now guard Box/SmoothUnion/MirrorX; the 4 pre-existing RefKind/ChainDispatch fails unchanged); regenerated `.g.hpp`/`.g.hlsl` contain `SdfCore_Box`/`SdfCore_SmoothUnion`/`SdfCore_MirrorX`; copied into VIXEN. Implementer **Sonnet**, validator **Opus**.
- [x] **M2b `[VIXEN]` — position-stack VM extension + cases + gates (Tasks 2–3).** Gate: CPU analytic parity for each new opcode; `EmitProceduralComputeShader` output compiles to SPIR-V; **live lavapipe render** of `MirrorX(SmoothUnion(Box, Sphere))` shows the mirrored CSG body; Sphere/Union non-regression green. Implementer **Sonnet**, validator **Opus** (reads the render PNG).

Validators **Opus** per milestone. Controller Opus, thin.

## Progress Log

- **M2a (Task 1, Yeroket + vendor): DONE** · Yeroket `a20bd16c` · VIXEN `3d80a133` · Opus validator **APPROVED** (round 2). Fix-loop resolved a generator C++-emitter float-literal blocker: `CppAstVisitor` now emits `0f`→`0.0f`/`1f`→`1.0f`/`0.5f`→`0.5f` + `saturate`→`glm::clamp(x, 0.0f, 1.0f)` (was bare ints → glm template-deduction failures). Regen reproducible (88/4, same 4 pre-existing); golden guards corrected artifact; generated C++ compiles clean (g++ -std=c++23 + glm); Sphere/Union untouched. · 2026-06-27
- **M2b (Tasks 2-3, VIXEN consume): DONE** · VIXEN `c2d2d21` · Opus validator **APPROVED** (tamper conclusive: removing the `MirrorX` pos-mutation fails parity at the negative-x points → `git checkout` restore → green; PNG = bilaterally-symmetric mirror-CSG, box centre + 2 mirror-folded sphere lobes, 25,332px). Position stack (`pos`+`posStack`) added to `evalRecipe` + emit-time (`curPos`+`posSaveStk`) to `EmitProceduralComputeShader`; `SdfOpCode` Box=1/SmoothUnion=25/MirrorX=41/RestorePos=97 (match C# implicit ordinals); CPU analytic parity + SPIR-V compile + live lavapipe render all green; P2.2 + P2.1/P2.3 stored gates non-regressed. Trailing fix: `#include <cassert>` self-contained-header. · 2026-06-27

---

## Task 1 [M2a]: Author Box / SmoothUnion / MirrorX, regen, vendor

**Repo:** Yeroket `/home/liory/Github/Yeroket-Fantasy`, branch `feat/kernel-codegen-p2` (already checked out; verify).

**Interfaces produced:** generated `SdfCore_Box(float3 p, float3 b)`, `SdfCore_SmoothUnion(float a, float b, float k)`, `SdfCore_MirrorX(float3 p)` in both `SdfCoreKernels.g.hpp` (glm) and `.g.hlsl` (native), vendored into VIXEN.

- [ ] **Step 1: Read the existing pattern.** Open `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs`; note exactly how `Sphere`/`Union` are written (`[KernelCallable]`+`[SdfCoreKernel]`, signatures, `math.*` usage, namespace). New kernels mirror this shape.
- [ ] **Step 2: Add the three kernels** (math from Global Constraints; use `Unity.Mathematics` / `math.*` per the existing style — `math.abs`, `math.max`, `math.min`, `math.length`, `math.lerp`, `math.saturate`):
```csharp
[KernelCallable, SdfCoreKernel]
public static float Box(float3 p, float3 b) {
    float3 q = math.abs(p) - b;
    return math.length(math.max(q, 0f)) + math.min(math.max(q.x, math.max(q.y, q.z)), 0f);
}

[KernelCallable, SdfCoreKernel]
public static float SmoothUnion(float a, float b, float k) {
    float h = math.saturate(0.5f + 0.5f * (b - a) / k);
    return math.lerp(b, a, h) - k * h * (1f - h);
}

[KernelCallable, SdfCoreKernel]
public static float3 MirrorX(float3 p) {
    return new float3(math.abs(p.x), p.y, p.z);
}
```
  (Match attribute spelling + return-type style to Sphere/Union exactly. If `[SdfCoreKernel]` lives in a namespace requiring a `using`, copy it from Sphere.)
- [ ] **Step 3: Rebuild the DLL.** `cd Packages/com.yeroket.utility.kernel-framework/SourceGenerator~ && ~/.dotnet/dotnet build -c Release`. Confirm 0 errors + the `RoslynAnalyzers/SDFNodeGenerator.dll` redeploy.
- [ ] **Step 4: Establish the regen-write + update the golden guard.** The golden byte-identity tests in `Tests/CppEmitterTests.cs` currently compile an inline `RealSourceMirror` constant (NOT the real `SdfCoreKernels.cs`) and compare against the committed artifacts. **Preferred (root-cause):** refactor the golden test to read the real `SdfCoreKernels.cs` as the generator input (single source of truth), and add an `if (Environment.GetEnvironmentVariable("UPDATE_GOLDENS")=="1") File.WriteAllText(<artifactPath>, <provenanceHeader> + generated);` write-mode in the assert helper (preserve the existing leading `//`-provenance block — read it from the current file, re-prepend). **Fallback (if the refactor balloons):** keep `RealSourceMirror` but add `Box/SmoothUnion/MirrorX` to it (in sync with Step 2) + the same `UPDATE_GOLDENS` write-mode. Either way the golden test must end up *guarding the new functions* against the committed artifact.
- [ ] **Step 5: Regenerate the artifacts.** Run the write-mode (`UPDATE_GOLDENS=1 ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter <the golden test>`), then **inspect** `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfCoreKernels.g.hpp` + `.g.hlsl` — they must now contain `SdfCore_Box`, `SdfCore_SmoothUnion`, `SdfCore_MirrorX` (correct glm/HLSL bodies) alongside Sphere/Union, with the provenance header intact.
- [ ] **Step 6: Full test green.** `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` (no UPDATE_GOLDENS) → the golden tests now PASS against the regenerated artifacts; total failures still exactly the 4 pre-existing (RefKind ×3 + ChainDispatch ×1), zero new.
- [ ] **Step 7: Force-add the artifacts** (`Generated/` is gitignored): `git add -f Packages/com.utility.sdf/Runtime/GPU/Generated/SdfCoreKernels.g.hpp Packages/com.utility.sdf/Runtime/GPU/Generated/SdfCoreKernels.g.hlsl` + the kernels + the test + the DLL.
- [ ] **Step 8: Commit (Yeroket)** `feat(kernel-codegen): Box + SmoothUnion + MirrorX core kernels + dotnet regen-write mode (P2.4 M2a)` with the `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` + `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c` trailers.
- [ ] **Step 9: Vendor into VIXEN.** Copy the two regenerated files to `…/sdf-recipe-codegen-p0/VIXEN/libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp` and `…/VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl` (byte-identical bodies; keep VIXEN's own provenance/path header convention if it differs). Leave them staged for M2b's commit (or commit on the VIXEN branch as `chore(recipe): re-vendor SdfCoreKernels with Box/SmoothUnion/MirrorX (P2.4 M2a)`).

## Task 2 [M2b]: Position stack + opcode cases in both VIXEN evaluators

**Repo:** VIXEN worktree, branch `feat/sdf-recipe-codegen-p2`.

**Interfaces consumed:** vendored `Yeroket::Sdf::Generated::SdfCore_{Box,SmoothUnion,MirrorX}` (hpp) + the HLSL `SdfCore_*` (from Task 1 Step 9).

- [ ] **Step 1: `SdfOpCode` values.** In `SdfInstruction.h`, extend the enum with the EXACT C# byte values (confirm from `SDFInstruction.cs`): `Box = 1, SmoothUnion = 25, MirrorX = 41, RestorePos = <read it>` (keep `Sphere=0, Union=24`).
- [ ] **Step 2: Position stack in `evalRecipe`** (`SdfRecipeEval.h`). Add a current position + save stack mirroring the C# VM; leaves sample `pos`:
```cpp
inline float evalRecipe(const SdfInstruction* prog, uint32_t count, glm::vec3 p) {
    float stack[64]; int sp = 0;
    glm::vec3 pos = p;                       // current sample point (C# VM: ctx.Pos)
    glm::vec3 posStack[64]; int psp = 0;     // domain save stack (C# VM: ctx.PosStack)
    using namespace Yeroket::Sdf::Generated;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                glm::vec3 c(in.data[0], in.data[1], in.data[2]); float r = in.data[3];
                stack[sp++] = SdfCore_Sphere(pos, c, r);          // was: p → now pos
            } break;
            case SdfOpCode::Box: {
                glm::vec3 b(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_Box(pos, b);
            } break;
            case SdfOpCode::Union: {
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = SdfCore_Union(a, b);
            } break;
            case SdfOpCode::SmoothUnion: {
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = SdfCore_SmoothUnion(a, b, in.data[2]);   // k = Data0.z
            } break;
            case SdfOpCode::MirrorX: {
                posStack[psp++] = pos; pos = SdfCore_MirrorX(pos);
            } break;
            case SdfOpCode::RestorePos: {
                pos = posStack[--psp];
            } break;
        }
    }
    return stack[sp - 1];
}
```
- [ ] **Step 3: Emit-time position stack in `EmitProceduralComputeShader`** (`SdfRecipeCodegen.h`). Track a current pos-expression name + a save stack of names; leaves use the current name; domain ops emit a `float3` temp + push; RestorePos pops:
```cpp
// before the loop:
std::string curPos = "p";
std::vector<std::string> posSaveStk;
// in the loop, alongside the existing valStk (stk):
case SdfOpCode::Box: {
    std::string t = "t" + std::to_string(n++);
    body += "  float " + t + " = SdfCore_Box(" + curPos + ", float3("
          + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
    stk.push_back(t);
} break;
case SdfOpCode::SmoothUnion: {
    std::string b = stk.back(); stk.pop_back();
    std::string a = stk.back(); stk.pop_back();
    std::string t = "t" + std::to_string(n++);
    body += "  float " + t + " = SdfCore_SmoothUnion(" + a + ", " + b + ", " + f(in.data[2]) + ");\n";
    stk.push_back(t);
} break;
case SdfOpCode::MirrorX: {
    std::string pN = "pp" + std::to_string(n++);
    body += "  float3 " + pN + " = SdfCore_MirrorX(" + curPos + ");\n";
    posSaveStk.push_back(curPos); curPos = pN;
} break;
case SdfOpCode::RestorePos: {
    curPos = posSaveStk.back(); posSaveStk.pop_back();
} break;
```
  Also change the existing `Sphere` emit to use `curPos` instead of the literal `p`. (Add `#include <vector>`/`<string>` if not already present.)
- [ ] **Step 4: Guards.** Assert on pop underflow + a non-empty value stack at `return` in both evaluators (the C# compiler validates stack balance; VIXEN should fail loudly, not UB).
- [ ] **Step 5: Build** `cmake --preset vixen-wsl && cmake --build ../build-wsl --target test_recipe_eval_parity test_recipe_codegen test_recipe_bake` → compiles clean.

## Task 3 [M2b]: Gates — CPU parity, SPIR-V compile, live render

- [ ] **Step 1: CPU analytic parity** — extend `test_recipe_eval_parity.cpp`. Add a case that builds the recipe `[MirrorX, Box(b), Sphere(c,r), SmoothUnion(k), RestorePos]` and asserts `evalRecipe(p)` equals the analytic oracle at several points:
```cpp
auto mirrorX = [](glm::vec3 q){ return glm::vec3(std::abs(q.x), q.y, q.z); };
auto box = [](glm::vec3 q, glm::vec3 b){ glm::vec3 d = glm::abs(q)-b;
    return glm::length(glm::max(d, glm::vec3(0))) + std::min(std::max(d.x,std::max(d.y,d.z)),0.0f); };
auto sph = [](glm::vec3 q, glm::vec3 c, float r){ return glm::length(q-c)-r; };
auto su  = [](float a,float b,float k){ float h=glm::clamp(0.5f+0.5f*(b-a)/k,0.0f,1.0f);
    return glm::mix(b,a,h)-k*h*(1.0f-h); };
// oracle(p) = su( box(mirrorX(p), b), sph(mirrorX(p), c, r), k )
```
  Build the `SdfInstruction[]` with the matching opcodes/data + assert `EXPECT_NEAR(evalRecipe(prog,5,p), oracle(p), 1e-4)` at ≥4 points (incl. one with negative x to exercise the mirror). Run → PASS. Also keep the existing Sphere∪Sphere parity case green.
- [ ] **Step 2: SPIR-V compile gate** — extend `test_recipe_codegen.cpp`: emit the same `[MirrorX, Box, Sphere, SmoothUnion, RestorePos]` recipe, assert the emitted HLSL contains `SdfCore_MirrorX(p)` + `SdfCore_Box(pp` + `SdfCore_SmoothUnion(` and compiles to valid SPIR-V via the existing `ShaderCompiler` path. Run → PASS.
- [ ] **Step 3: Live lavapipe render gate** — add a render test (reuse the P2.2 procedural-render harness if one exists in the SVO/RenderGraph tests; otherwise stand up a minimal lavapipe compute dispatch of the emitted shader, mirroring the P2.2 M2 gate). Render `MirrorX(SmoothUnion(Box, Sphere))` where Box+Sphere are offset in +x so the mirror produces a visibly symmetric body. Assert the body renders (non-trivial pixel count) + write `/tmp/glsl_sdf_m2_mirror.png`. **Controller/validator reads the PNG** to confirm a symmetric two-sided CSG shape (the mirror + smooth-union are visible) — the authoritative proof the unrolled HLSL position stack matches the CPU eval.
  Run on lavapipe: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VK_LAYER_PATH=<provisioned 1.4.350.1 SDK>/x86_64/share/vulkan/explicit_layer.d ./build-wsl/.../<test> --gtest_filter=*<name>*`.
- [ ] **Step 4: No-regression.** `ctest --test-dir ../build-wsl -R "test_recipe_eval_parity|test_recipe_codegen|test_recipe_bake" --output-on-failure` green; the P2.1/P2.3 Stored-SDF render gates (`*RenderStoredSdf*`, `*RenderRecipeBakedBody*`, `*RematerializeEditLoop*`) still green on lavapipe.
- [ ] **Step 5: Commit (VIXEN)** `feat(recipe): position-stack VM extension + Box/SmoothUnion/MirrorX cases + live mirror-CSG gate (P2.4 M2b)` with the Co-Authored-By + Claude-Session trailers.

## Self-Review

**Coverage:** position stack (the structural gap) → Task 2 Steps 2–3 (both evaluators), proven by the MirrorX+RestorePos render + parity. New-leaf pattern → Box; k-param binary pattern → SmoothUnion. Cross-repo single-source→regen→vendor → Task 1. The three lanes (leaf / k-binary / domain-transform) that M3/M4 will fill en masse are each proven once here. ✓
**Placeholders:** the kernel math, data layouts, eval/emit cases, and the parity oracle are concrete; the enum byte values + the exact render-harness reuse are "read from the authoritative source / reuse the P2.2 gate" (the implementer locates them — they exist). The regen-write mechanism gives a preferred root-cause path + a fallback. No bare "TODO". ✓
**Type consistency:** `SdfCore_Box(float3,float3)`, `SdfCore_SmoothUnion(float,float,float)`, `SdfCore_MirrorX(float3)`; `pos`/`posStack`/`psp` (eval) ↔ `curPos`/`posSaveStk` (emit); `data[2]`=k, `data[0..2]`=halfExtents — consistent across M2a/M2b/tests. ✓
**Risk:** (1) the regen-write is the first exercise of the dotnet artifact-write — Task 1 Steps 4–6 pin it with the golden guard. (2) The position-stack must leave the Sphere/Union path behaviour-identical (Sphere now reads `pos`, which == `p` when no transform is active) — the existing parity/render gates catch any drift.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones, sequential). M2a Sonnet+Opus (dotnet regen; validator confirms the new functions in the regenerated artifacts + golden guard + tests green). M2b Sonnet+Opus (validator reads `/tmp/glsl_sdf_m2_mirror.png` + tamper-checks: e.g. removing the `RestorePos` handling or pointing a leaf at `p` instead of `pos` must break the mirror gate).
