# P0 — SDF Recipe Codegen Walking Skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This plan spans **two repos** — tasks tag `[YEROKET]` or `[VIXEN]`. The generator side `[YEROKET]` is **pure .NET (no Unity)**; the VIXEN side is C++23/gtest.

**Goal:** Prove the single-source → multi-backend codegen thesis end-to-end on two opcodes (`Sphere` + `Union`): one C# source → generated **C++** *and* **HLSL** → VIXEN evaluates the C++ correctly (parity-gated) and ingests the HLSL to valid SPIR-V.

**Architecture:** Annotate `Sphere`/`Union` as single-source `[KernelCallable]` functions; add a **C++ emitter** to the existing Roslyn generator (which already emits HLSL via pluggable backends); commit the generated artifacts; VIXEN consumes the generated C++ in a tiny stack-VM evaluator (parity vs analytic golden vectors) and compiles the generated HLSL through a new `EShSourceHlsl` path in `ShaderCompiler`.

**Tech Stack:** C# / Roslyn source generator (netstandard2.0, Roslyn 4.3.0), NUnit tests (net8.0), `~/.dotnet/dotnet`; C++23, glslang, GoogleTest, CMake (`vixen-wsl`), glm.

## Global Constraints

- **VIXEN owns its format; engine-generic opcodes only** — no game-specific opcodes (plant/turtle/MC) enter VIXEN. (Design D4, [[vixen-owns-content-format-not-consumer]])
- **Byte-compat with the C# format** — `SdfOpCode` values and the `SdfInstruction` layout MUST match `Yeroket-Fantasy/Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs` (Design D6). For P0, hand-mirror the subset with explicit, verified values; full-enum generation is P1.
- **Canonical source = C#; generated artifacts are committed.** VIXEN's normal build stays .NET-free. (Design D8)
- **Linux dotnet only** for the generator: `~/.dotnet/dotnet`. Windows dotnet via `/mnt/c` → MSB3030 (kernel-framework friction). The generator runs as a **compiled DLL** — editing `SourceGenerator~/*.cs` has no effect until the DLL is rebuilt; **commit the rebuilt DLL** with the source change.
- **Repos (execution workspaces):** `[YEROKET]` = `/home/liory/Github/Yeroket-Fantasy` on branch `feat/kernel-cpp-emitter` (canonical checkout, NOT a worktree). `[VIXEN]` = worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0/VIXEN` on branch `feat/sdf-recipe-codegen-p0`.
- **P0 deliberately defers** (to P2): GPU eval *runtime* parity (compute dispatch + readback) and the sphere-traced PNG render. P0 proves the generated HLSL **compiles** in VIXEN; CPU parity proves the codegen *semantics* (HLSL and C++ are emitted from the same source by parallel visitors).

---

## Milestone Map

> Persisted by post-brainstorm-context-manager (2026-06-26). On resume, reuse this grouping verbatim — do NOT re-segment. Milestones run sequentially; M2 consumes M1's artifacts.

- [x] **M1 `[YEROKET]` — Generator C++ backend (Tasks 1–2 + emit artifacts to disk).** Repo `/home/liory/Github/Yeroket-Fantasy`, branch `feat/kernel-cpp-emitter`. Implementer: **Sonnet**. Gate: `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` green (incl. new `CppEmitterTests`); generated `SdfCoreKernels.g.hpp` + `SdfCoreKernels.hlsl` written to disk via **pure dotnet, no Unity**.
- [ ] **M2 `[VIXEN]` — Consumer + parity + HLSL ingest (Tasks 3–5).** Worktree `…/sdf-recipe-codegen-p0`, branch `feat/sdf-recipe-codegen-p0`. Implementer: **Sonnet**. Gate: `test_recipe_eval_parity` + `test_hlsl_ingestion` green; existing GLSL compile no-regression.

Validators: **Opus** per milestone (fix-loop cap 3). Final review: **Opus** over the full two-repo diff.

## Progress Log

- **M1 `[YEROKET]` (Tasks 1–2): DONE** · commits `b8939cc0`..`4fb67c91` · Opus validator APPROVED (HLSL gap caught + fixed on re-validate) · 2026-06-26 — C++ **and** HLSL single-sourced from `SdfCoreKernels.cs` via a new pluggable backend (callable-through-HLSL route); artifacts `SdfCoreKernels.g.hpp` + `.g.hlsl` committed on Yeroket `feat/kernel-cpp-emitter`; suite 83/4 (4 pre-existing, 0 new). **P1 note:** the regex emitter must become a real AST visitor before the catalog expands (both backends share it).

---

## File Structure

**`[YEROKET]` create:**
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppMappingTables.cs` — C#→C++ name/type maps (glm/std).
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppVisitor.cs` — C# AST → C++ string (parallel to `HLSLVisitor.cs`).
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppEmitter.cs` — orchestrates `CppVisitor`, writes the `.g.cpp`/`.g.hpp` text (parallel to the HLSL emitter).
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs` — golden/string-assert tests (mirror `HlslVisitorRewriteTests.cs`).
- `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs` — the single-source `[KernelCallable]` `Sphere`/`Union` (thin wrappers over `SDFPrimitives`/`SDFOperations`).

**`[YEROKET]` modify:**
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/SDFNodeSourceGenerator.cs` — add `EmitCppEmitter(...)` call in `GenerateAll()` (after the HLSL emitter, ~line 438).

**`[YEROKET]` generated artifacts (committed):**
- `…/Generated/SdfCoreKernels.g.hpp` + `.g.cpp` (C++) and the existing HLSL emit for the two kernels — copied into VIXEN below.

**`[VIXEN]` create:**
- `VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h` — byte-compat C++ mirror of `SDFInstruction`/`SDFOpCode` (P0 subset).
- `VIXEN/libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp` — vendored copy of the generated C++ kernels.
- `VIXEN/libraries/SVO/include/Recipe/SdfRecipeEval.h` — tiny stack-VM that dispatches `SdfInstruction[]` to the generated kernels.
- `VIXEN/libraries/SVO/tests/test_recipe_eval_parity.cpp` — CPU parity vs analytic golden vectors.
- `VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl` — vendored copy of the generated HLSL kernels (for the compile test).
- `VIXEN/libraries/ShaderManagement/tests/test_hlsl_ingestion.cpp` — HLSL→SPIR-V compile gate.

**`[VIXEN]` modify:**
- `VIXEN/libraries/ShaderManagement/include/ShaderCompiler.h` — add `SourceLanguage sourceLanguage` to `CompilationOptions`.
- `VIXEN/libraries/ShaderManagement/src/ShaderCompiler.cpp` — honor it at `setEnvInput` (line 117).
- `VIXEN/libraries/SVO/tests/CMakeLists.txt`, `VIXEN/libraries/ShaderManagement/tests/CMakeLists.txt` — register the two new tests.

---

## Task 1 [YEROKET]: Single-source `Sphere`/`Union` + failing C++-emit test

**Files:**
- Create: `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs`
- Create: `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs`

**Interfaces:**
- Produces: two `[KernelCallable]` functions — `float SdfCore_Sphere(float3 p, float3 center, float radius)` and `float SdfCore_Union(float a, float b)` — that later tasks generate C++/HLSL for and VIXEN dispatches.

- [ ] **Step 1: Write the single-source kernels.** These bodies are the ONE source of truth; the generator transpiles them. Bodies use only `math.*` (self-contained — the only kind the transpiler guarantees, per the HLSLVisitor context-boundary caveat).

```csharp
// Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs
using Unity.Mathematics;
using Yeroket.Util.KernelFramework;

namespace Yeroket.Sdf.Kernels
{
    // Engine-generic SDF core ops, single-sourced for all backends.
    public static class SdfCoreKernels
    {
        [KernelCallable]
        public static float SdfCore_Sphere(float3 p, float3 center, float radius)
            => math.length(p - center) - radius;

        [KernelCallable]
        public static float SdfCore_Union(float a, float b)
            => math.min(a, b);
    }
}
```

- [ ] **Step 2: Confirm the existing HLSL emit covers them.** Build the current source-gen DLL and run the existing emit test for `[KernelCallable]` to capture the HLSL these produce (this is the reference the C++ output mirrors):

Run: `cd /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/SourceGenerator~ && ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter KernelCallable`
Expected: existing KernelCallable HLSL tests PASS (confirms `SdfCore_Sphere`/`SdfCore_Union` will emit HLSL `length(p-center)-radius` / `min(a,b)`). If no `[KernelCallable]` HLSL test exists, add one mirroring `HlslVisitorRewriteTests` asserting the HLSL body, and confirm it passes — this pins the HLSL reference.

- [ ] **Step 3: Write the failing C++-emit test.** Mirror `Tests/HlslVisitorRewriteTests.cs` harness (`RunGenerator`/`RunAndFindGenerated`), asserting on the **C++** emit that does not exist yet.

```csharp
// Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs
using NUnit.Framework;
// (reuse RunGenerator/RunAndFindGenerated + FrameworkStubs from HlslVisitorRewriteTests; copy them here or factor a shared helper)

[TestFixture]
public class CppEmitterTests
{
    const string Source = /* FrameworkStubs + */ @"
using Unity.Mathematics; using Yeroket.Util.KernelFramework;
namespace T { public static class K {
  [KernelCallable] public static float SdfCore_Sphere(float3 p, float3 center, float radius) => math.length(p - center) - radius;
  [KernelCallable] public static float SdfCore_Union(float a, float b) => math.min(a, b);
}}";

    [Test]
    public void EmitsCppForSphereAndUnion()
    {
        string cpp = RunAndFindGenerated(Source, "SdfCore_Sphere");   // find the .g.hpp/.g.cpp tree
        Assert.IsNotNull(cpp, "no C++ emit produced");
        StringAssert.Contains("float SdfCore_Sphere(glm::vec3 p, glm::vec3 center, float radius)", cpp);
        StringAssert.Contains("glm::length(p - center) - radius", cpp);
        StringAssert.Contains("float SdfCore_Union(float a, float b)", cpp);
        StringAssert.Contains("glm::min(a, b)", cpp);
    }
}
```

- [ ] **Step 4: Run the test to verify it FAILS.**

Run: `cd …/SourceGenerator~ && ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter CppEmitterTests`
Expected: FAIL — `RunAndFindGenerated` returns null (no C++ backend yet).

- [ ] **Step 5: Commit (test + kernels only).**

```bash
cd /home/liory/Github/Yeroket-Fantasy
git add Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs \
        Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs
git commit -m "test(kernel-codegen): single-source SdfCore Sphere/Union + failing C++-emit test (P0 T1)"
```

---

## Task 2 [YEROKET]: Implement the C++ emitter (make T1 pass)

**Files:**
- Create: `…/SourceGenerator~/Transpiler/CppMappingTables.cs`
- Create: `…/SourceGenerator~/Transpiler/CppVisitor.cs`
- Create: `…/SourceGenerator~/Transpiler/CppEmitter.cs`
- Modify: `…/SourceGenerator~/SDFNodeSourceGenerator.cs` (`GenerateAll()`)

**Interfaces:**
- Consumes: `KernelInfo` (the same model `HLSLVisitor` consumes).
- Produces: a generated tree containing `SdfCore_Sphere`/`SdfCore_Union` as C++/glm free functions.

- [ ] **Step 1: Write `CppMappingTables.cs`** (data only — fully specified here):

```csharp
// Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppMappingTables.cs
using System.Collections.Generic;
namespace Generator.Transpiler
{
    static class CppMappingTables
    {
        // C# call → C++ call. Extend per opcode-catalog growth (P2).
        public static readonly Dictionary<string, string> CSharpToCpp = new()
        {
            { "math.length", "glm::length" }, { "math.distance", "glm::distance" },
            { "math.dot", "glm::dot" },       { "math.cross", "glm::cross" },
            { "math.normalize", "glm::normalize" },
            { "math.min", "glm::min" },       { "math.max", "glm::max" },
            { "math.abs", "glm::abs" },       { "math.clamp", "glm::clamp" },
            { "math.lerp", "glm::mix" },      { "math.sqrt", "glm::sqrt" },
            { "math.sin", "glm::sin" },       { "math.cos", "glm::cos" },
            { "math.pow", "glm::pow" },       { "math.select", "/*select*/" }, // expanded inline like HLSL
        };
        public static readonly Dictionary<string, string> TypeMap = new()
        {
            { "float",  "float" },  { "float2", "glm::vec2" },
            { "float3", "glm::vec3" }, { "float4", "glm::vec4" },
            { "int", "int32_t" }, { "bool", "bool" },
        };
    }
}
```

- [ ] **Step 2: Write `CppVisitor.cs` by adapting `HLSLVisitor.cs`.** This is an *adapt-to-test* step (the source mirror is ~300 lines — do not paste blind): copy `Transpiler/HLSLVisitor.cs` → `CppVisitor.cs`, then (a) route type lookups through `CppMappingTables.TypeMap` instead of the HLSL type map, (b) route call lookups through `CppMappingTables.CSharpToCpp`, (c) keep statement/expression structure identical, (d) emit a free function `<retType> <Name>(<params>) { return <expr>; }` for `[KernelCallable]` bodies. Only the **expression/return + call/type mapping** paths are exercised by Sphere/Union — leave the rest structurally mirrored. Acceptance is T1's golden test, not line-count.

- [ ] **Step 3: Write `CppEmitter.cs` by adapting the HLSL emitter.** Mirror the HLSL emitter's collection of `[KernelCallable]` kernels; for each, call `new CppVisitor(kernel).EmitFunction("")`; concatenate into one `.g.hpp` tree with a preamble:

```cpp
#pragma once
#include <glm/glm.hpp>
namespace Yeroket::Sdf::Generated {
// <emitted functions>
}
```
Add the tree via `ctx.AddSource("SdfCoreKernels.g.hpp.cs", ...)` following the HLSL emitter's `AddSource` pattern (string-wrapped, as the HLSL emitter does).

- [ ] **Step 4: Hook into `GenerateAll()`** in `SDFNodeSourceGenerator.cs` (after the HLSL emit call, ~line 438), matching the existing try/catch shape:

```csharp
try { EmitCppEmitter(ctx, data.Nodes, validKernels); }
catch (System.Exception ex) { /* mirror sibling emitters' diagnostic */ }
```
…with `EmitCppEmitter` delegating to `CppEmitter`.

- [ ] **Step 5: Rebuild the source-gen DLL** (required — Unity/Roslyn loads the binary, not source):

Run: `cd …/SourceGenerator~ && ~/.dotnet/dotnet build -c Release`
Expected: build succeeds; `CopyToUnityAnalyzers` post-build deploys the DLL.

- [ ] **Step 6: Run T1's test to verify it PASSES.**

Run: `cd …/SourceGenerator~ && ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter CppEmitterTests`
Expected: PASS.

- [ ] **Step 7: Full generator suite green (no regression to HLSL/Burst emit).**

Run: `cd …/SourceGenerator~ && ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj`
Expected: all PASS.

- [ ] **Step 8: Commit (source + rebuilt DLL).**

```bash
cd /home/liory/Github/Yeroket-Fantasy
git add Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/Cpp*.cs \
        Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/SDFNodeSourceGenerator.cs \
        Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/   # the rebuilt DLL
git commit -m "feat(kernel-codegen): C++ emit backend (Sphere/Union slice) (P0 T2)"
```

---

## Task 3 [YEROKET→VIXEN]: Generate + vendor the artifacts

**Files:** generated `.g.hpp` (C++) + the kernels' `.hlsl`; copied into VIXEN.

- [ ] **Step 1: Locate the generated C++ + HLSL** for `SdfCoreKernels` (under the generator's output dir / `obj` generated trees, or the Unity-side `Generated/` folder the writers emit to). Confirm the C++ has `SdfCore_Sphere`/`SdfCore_Union` (glm) and the HLSL has the `length`/`min` bodies.

- [ ] **Step 2: Vendor into VIXEN** (committed artifacts, Design D8):

```bash
cp <generated>/SdfCoreKernels.g.hpp \
   /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/stored-sdf/VIXEN/libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp
cp <generated>/SdfCoreKernels.hlsl \
   /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/stored-sdf/VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl
```

- [ ] **Step 3: Add a provenance header** comment to each vendored file: `// GENERATED from SdfCoreKernels.cs by the kernel-framework C++/HLSL emitter — do not edit; regenerate (P1 automates).`

- [ ] **Step 4: Commit (VIXEN).**

```bash
cd /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/stored-sdf
git add VIXEN/libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp \
        VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl
git commit -m "chore(recipe): vendor generated SdfCore Sphere/Union C++/HLSL (P0 T3)"
```

---

## Task 4 [VIXEN]: Format mirror + CPU eval parity

**Files:**
- Create: `VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h`
- Create: `VIXEN/libraries/SVO/include/Recipe/SdfRecipeEval.h`
- Create: `VIXEN/libraries/SVO/tests/test_recipe_eval_parity.cpp`
- Modify: `VIXEN/libraries/SVO/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Yeroket::Sdf::Generated::SdfCore_Sphere/SdfCore_Union` (from the vendored `.g.hpp`).
- Produces: `Vixen::SVO::Recipe::evalRecipe(const SdfInstruction*, uint32_t count, glm::vec3 p) -> float`.

- [ ] **Step 1: Write the byte-compat format mirror.** ⚠ Alignment gotcha: the C# struct is `byte,byte,byte,byte` + 8×`float4` = **132 bytes packed**. Using `glm::vec4` (16-byte aligned) after 4 bytes would pad to 144 — so store the payload as `float data[32]` (4-byte aligned) to hit 132.

```cpp
// VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h
#pragma once
#include <cstdint>
namespace Vixen::SVO::Recipe {
// Byte-compat mirror of Yeroket SDFOpCode. P0 subset; values MUST match the C# enum
// order in com.utility.graph-framework/Runtime/VM/SDFInstruction.cs.
// Sphere is entry 0; Union is the first binary op after the 24 primitive entries (0..23) => 24.
enum class SdfOpCode : uint8_t { Sphere = 0, Union = 24 };

// 132-byte blittable mirror of SDFInstruction (see alignment note above).
struct SdfInstruction {
    uint8_t opCode;
    uint8_t inputMask;
    uint8_t paramMask;
    uint8_t _pad1;
    float   data[32];   // 8 logical float4 lanes; data[0..3] = Data0, etc.
};
static_assert(sizeof(SdfInstruction) == 132, "must match C# SDFInstruction (132 B)");
}
```

- [ ] **Step 2: Write the stack-VM evaluator** that dispatches to the generated kernels.

```cpp
// VIXEN/libraries/SVO/include/Recipe/SdfRecipeEval.h
#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/SdfCoreKernels.g.hpp"   // Yeroket::Sdf::Generated::SdfCore_*
#include <glm/glm.hpp>
#include <cstdint>
namespace Vixen::SVO::Recipe {
inline float evalRecipe(const SdfInstruction* prog, uint32_t count, glm::vec3 p) {
    float stack[64]; int sp = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                glm::vec3 c(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = center
                float r = in.data[3];                              // Data0.w   = radius
                stack[sp++] = Yeroket::Sdf::Generated::SdfCore_Sphere(p, c, r);
            } break;
            case SdfOpCode::Union: {
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = Yeroket::Sdf::Generated::SdfCore_Union(a, b);
            } break;
        }
    }
    return stack[sp - 1];
}
}
```

- [ ] **Step 3: Write the failing parity test** (analytic golden — independent of the C# evaluator for P0; the math is `min(|p-c0|-r0, |p-c1|-r1)`).

```cpp
// VIXEN/libraries/SVO/tests/test_recipe_eval_parity.cpp
#include <gtest/gtest.h>
#include "Recipe/SdfRecipeEval.h"
#include <glm/glm.hpp>
using namespace Vixen::SVO::Recipe;

static SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0]=c.x; in.data[1]=c.y; in.data[2]=c.z; in.data[3]=r; return in;
}
static SdfInstruction unionOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Union; return in; }

TEST(RecipeEvalParity, SphereUnionMatchesAnalytic) {
    const glm::vec3 c0(-1,0,0), c1(1,0,0); const float r0=1.0f, r1=1.0f;
    SdfInstruction prog[] = { sphere(c0,r0), sphere(c1,r1), unionOp() };
    auto golden = [&](glm::vec3 p){ return std::min(glm::length(p-c0)-r0, glm::length(p-c1)-r1); };
    for (glm::vec3 p : { glm::vec3(-1,0,0), glm::vec3(1,0,0), glm::vec3(0,0,0), glm::vec3(0,2,0), glm::vec3(3,1,-1) })
        EXPECT_NEAR(evalRecipe(prog, 3, p), golden(p), 1e-5f) << "at " << p.x << "," << p.y << "," << p.z;
}
```

- [ ] **Step 4: Register the test** in `VIXEN/libraries/SVO/tests/CMakeLists.txt` (follow the existing `test_*` registration pattern, e.g. how `test_sdf_bake` is added).

- [ ] **Step 5: Build → verify FAIL first** (before T3's header exists / if run early), then **PASS** after T3.

Run: `cmake --preset vixen-wsl && cmake --build build-wsl --target test_recipe_eval_parity && ./build-wsl/libraries/SVO/tests/test_recipe_eval_parity --gtest_brief=1`
Expected: PASS (all 5 sample points within 1e-5).

- [ ] **Step 6: Commit (VIXEN).**

```bash
cd /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/stored-sdf
git add VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h \
        VIXEN/libraries/SVO/include/Recipe/SdfRecipeEval.h \
        VIXEN/libraries/SVO/tests/test_recipe_eval_parity.cpp \
        VIXEN/libraries/SVO/tests/CMakeLists.txt
git commit -m "feat(recipe): byte-compat format mirror + CPU stack-VM eval, parity-gated (P0 T4)"
```

---

## Task 5 [VIXEN]: HLSL ingestion → SPIR-V compile gate

**Files:**
- Modify: `VIXEN/libraries/ShaderManagement/include/ShaderCompiler.h`, `…/src/ShaderCompiler.cpp`
- Create: `VIXEN/libraries/ShaderManagement/tests/test_hlsl_ingestion.cpp`
- Modify: `VIXEN/libraries/ShaderManagement/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `CompilationOptions::sourceLanguage` (`SourceLanguage::GLSL` default | `HLSL`); `Compile(...)` honors it.

- [ ] **Step 1: Add the option** in `ShaderCompiler.h` `CompilationOptions` (after line 22):

```cpp
enum class SourceLanguage { GLSL, HLSL };
SourceLanguage sourceLanguage = SourceLanguage::GLSL;   // HLSL ingests via glslang's HLSL frontend
```

- [ ] **Step 2: Honor it** in `ShaderCompiler.cpp` `CompileInternal`, replacing the hardcoded `EShSourceGlsl` (line 117) and adding the HLSL frontend toggles:

```cpp
const glslang::EShSource srcLang =
    (options.sourceLanguage == SourceLanguage::HLSL) ? glslang::EShSourceHlsl
                                                     : glslang::EShSourceGlsl;
shader.setEnvInput(srcLang, shaderStage, glslang::EShClientVulkan, vulkanVersion);
if (options.sourceLanguage == SourceLanguage::HLSL) {
    shader.setEnvTargetHlslFunctionality1();
    shader.setHlslIoMapping(true);   // honor [[vk::binding]] / register→binding
}
```

- [ ] **Step 3: Write the failing HLSL compile test.** A self-contained compute shader that `#include`s the generated kernels and calls both (proves the generated HLSL compiles under glslang's HLSL frontend → valid SPIR-V). Pass the vendored HLSL path via a CMake compile-def (mirror `GLSL_RAYMARCH_SPV`), or inline the two function bodies as a string for hermeticity:

```cpp
// VIXEN/libraries/ShaderManagement/tests/test_hlsl_ingestion.cpp
#include <gtest/gtest.h>
#include "ShaderCompiler.h"
using namespace ShaderManagement;

TEST(HlslIngestion, GeneratedKernelsCompileToSpirv) {
    // Generated bodies (from SdfCoreKernels.g.hlsl); inlined for a hermetic gate.
    const char* hlsl = R"(
        float SdfCore_Sphere(float3 p, float3 center, float radius) { return length(p - center) - radius; }
        float SdfCore_Union(float a, float b) { return min(a, b); }
        RWStructuredBuffer<float> outBuf : register(u0);
        [numthreads(1,1,1)]
        void main(uint3 id : SV_DispatchThreadID) {
            float d = SdfCore_Union(SdfCore_Sphere(float3(0,0,0), float3(-1,0,0), 1.0),
                                    SdfCore_Sphere(float3(0,0,0), float3( 1,0,0), 1.0));
            outBuf[0] = d;
        })";
    ShaderCompiler c;
    CompilationOptions opt; opt.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opt.validateSpirv = true;
    auto out = c.Compile(ShaderStage::Compute, hlsl, "main", opt);
    ASSERT_TRUE(out.success) << out.GetFullLog();
    EXPECT_FALSE(out.spirv.empty());
}
```

- [ ] **Step 4: Register the test** in `VIXEN/libraries/ShaderManagement/tests/CMakeLists.txt` (follow the existing pattern; link `ShaderManagement` + glslang).

- [ ] **Step 5: Build → run → verify PASS.**

Run: `cmake --build build-wsl --target test_hlsl_ingestion && ./build-wsl/libraries/ShaderManagement/tests/test_hlsl_ingestion --gtest_brief=1`
Expected: PASS (valid SPIR-V produced). If glslang reports unmapped HLSL bindings, confirm `setHlslIoMapping(true)` + `register(u0)` are present.

- [ ] **Step 6: No-regression — existing GLSL compile still works.** Run any existing ShaderManagement test (GLSL path) to confirm the default branch is unchanged.

Run: `ctest --test-dir build-wsl -R ShaderManagement --output-on-failure` (or the existing GLSL compile test target)
Expected: PASS.

- [ ] **Step 7: Commit (VIXEN).**

```bash
cd /mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/stored-sdf
git add VIXEN/libraries/ShaderManagement/include/ShaderCompiler.h \
        VIXEN/libraries/ShaderManagement/src/ShaderCompiler.cpp \
        VIXEN/libraries/ShaderManagement/tests/test_hlsl_ingestion.cpp \
        VIXEN/libraries/ShaderManagement/tests/CMakeLists.txt
git commit -m "feat(shader): HLSL→SPIR-V ingestion via glslang HLSL frontend + compile gate (P0 T5)"
```

---

## P0 Done = walking skeleton proven

One thread end-to-end: `SdfCoreKernels.cs` (single source) → generated **C++** (`SdfCore_Sphere`/`Union`, parity-gated in VIXEN, T4) **and HLSL** (compiles to valid SPIR-V in VIXEN, T5). The generator gained a pluggable C++ backend (T2) with a pure-`dotnet` golden test (T1). Everything after is incremental: **P1** adds the manifest + opcode layering + conformance export + routes the full catalog through the transpiler; **P2** adds the GPU eval runtime parity (dispatch+readback) + the sphere-traced render path; **P3** is Materialization.

## Self-Review

- **Spec coverage (vs design P0):** single-source kernel → T1; C++ emit → T2; generated+vendored artifacts → T3; VIXEN C++ eval + parity → T4; VIXEN HLSL ingestion → T5. The design's "render on lavapipe" is **explicitly rescoped to P2** (stated in Global Constraints + P0-Done) because compile-gating the HLSL (T5) + CPU parity (T4) de-risk the codegen thesis without dragging in the Vulkan compute-dispatch/camera harness. ✓
- **Placeholder scan:** T2 Steps 2–3 are *adapt-to-test* (not verbatim) by necessity — they mirror a ~300-line existing file with the golden test (T1, complete) as the exact acceptance; the data table (CppMappingTables) and all VIXEN code are verbatim. No "TODO"/"handle edge cases". ✓
- **Type consistency:** `SdfInstruction`/`SdfOpCode` (T4) used consistently; `evalRecipe` signature matches its caller; `SdfCore_Sphere(glm::vec3,glm::vec3,float)` / `SdfCore_Union(float,float)` consistent across T1 (C# source), T2 (asserted C++), T4 (consumed). `CompilationOptions::SourceLanguage` consistent T5 Steps 1/3. ✓
- **Open risks flagged in-plan:** Union opcode value (verify =24 against the C# enum, T4 S1 comment); generated-artifact location (T3 S1 is a locate step); glslang HLSL binding mapping (T5 S5 troubleshooting).

## Execution Handoff

**Plan complete and saved to `VIXEN/Vixen-Docs/01-Architecture/SDF-Recipe-Kernel-Codegen-Inc4-P0-Plan-2026-06.md`.** Two execution options:

1. **Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks. Note: tasks 1–3 run in the `[YEROKET]` repo (pure `dotnet`), tasks 4–5 in `[VIXEN]`.
2. **Inline Execution** — execute in this session with checkpoints.
