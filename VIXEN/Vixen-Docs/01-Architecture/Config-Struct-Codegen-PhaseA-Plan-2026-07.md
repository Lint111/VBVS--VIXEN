# Config-Struct Codegen — Phase A Plan (Yeroket: extract core + callable tool)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox (`- [ ]`) steps.
> **Repo:** this phase runs ENTIRELY in the **Yeroket** repo (`/home/liory/Github/Yeroket-Fantasy`). The plan doc lives in the VIXEN vault for epic coherence; read it from there.

**Goal:** stand up the config-struct codegen **inside Yeroket's real kernel-codegen core** as a callable console tool — re-homing P0's proven std430 scalar model + C++/GLSL emitters, adding the shared `[GpuStruct]` attributes, proven on a trivial `SkeletonConfig`. (This redoes P0 on the real core; compound types + OctreeConfig are Phase B.)

**Architecture:** a shared **attributes lib** (`[GpuStruct]`/`[GpuArray]`/`Float3`/`Mat4`, one source consumed by the Unity asmdef + a netstandard2.0 csproj); the **emitter core** (`GpuStructModel`/`GpuStructCppEmitter`/`GpuStructGlslEmitter`) as public classes in `SourceGenerator~/Transpiler/`; a **console tool** `CodegenTool~/` (net8.0, `ProjectReference` the analyzer + attributes) that builds a Roslyn `Compilation` over a schema dir and emits. Reuses the proven regen pattern (net8.0 + CodeAnalysis + ProjectReference, exactly like `SourceGenerator~/Tests`).

**Tech Stack:** .NET 8 (`~/.dotnet/dotnet`), Roslyn (`Microsoft.CodeAnalysis.CSharp` 4.3.0 — match the repo), NUnit (match the repo's test framework), netstandard2.0 (attributes + analyzer).

## Global Constraints
- **Port, don't reinvent:** P0's proven code is on VIXEN `origin/main` (`70165c13`) at `VIXEN/codegen/Vixen.Codegen/{Layout/StructModel.cs, Emit/CppStructEmitter.cs, Emit/GlslStructEmitter.cs, CompilationLoader.cs, Program.cs}` and `VIXEN/codegen/Vixen.Codegen.Attributes/GpuStructAttributes.cs` (+ P1's `Float3`/`Mat4`/`[GpuArray]` at VIXEN branch `feat/config-struct-codegen-p1` commit `0bc54da5`). Port that logic; adapt namespaces to `Yeroket.KernelFramework.Codegen`.
- Match the repo: `Microsoft.CodeAnalysis.CSharp` **4.3.0**, **NUnit** (not xUnit), net8.0 for runnable projects, netstandard2.0 for the analyzer/attributes. Follow `SourceGenerator~/Tests/SDFNodeGenerator.Tests.csproj` verbatim as the project template.
- Emitter output **LF-normalized** (`.Replace("\r\n","\n")` — carry P0's hardening).
- `~/.dotnet/dotnet` only. Commit trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` / `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.
- Work on a Yeroket branch `feat/gpustruct-codegen-phaseA` off Yeroket `main`. Do not push/merge (controller finishes). Do NOT edit committed `.g.*` artifacts by hand.

## File Structure (all under `Packages/com.yeroket.utility.kernel-framework/`)
| File | Responsibility |
|---|---|
| `Runtime/GpuStructAttributes.cs` | `[GpuStruct]`, `[GpuArray]`, `Float3`, `Mat4` (namespace `Yeroket.Util.KernelFramework`). Unity asmdef picks it up automatically. |
| `Attributes~/Yeroket.KernelFramework.Attributes.csproj` | netstandard2.0 lib; `<Compile Include="../Runtime/GpuStructAttributes.cs"/>` → a dotnet-referenceable attributes DLL (the shared "own dll"). |
| `SourceGenerator~/Transpiler/GpuStructModel.cs` | public std430 scalar layout model (port of P0 `StructModel.cs`). |
| `SourceGenerator~/Transpiler/GpuStructCppEmitter.cs` | public C++ emitter (port of P0 `CppStructEmitter.cs`, incl. `_padChannels`-style asserts + LF norm). |
| `SourceGenerator~/Transpiler/GpuStructGlslEmitter.cs` | public GLSL emitter (port of P0 `GlslStructEmitter.cs`). |
| `CodegenTool~/CodegenTool.csproj` | net8.0 Exe; `ProjectReference` `SDFNodeGenerator.csproj` + `Attributes~/…csproj`; `Microsoft.CodeAnalysis.CSharp` 4.3.0. |
| `CodegenTool~/Program.cs` | CLI `--schema <dir> --struct <Name> --out-cpp --out-glsl [--check]` (port of P0 `CompilationLoader.cs` + `Program.cs`; references the attributes DLL so `[GpuStruct]` resolves). |
| `CodegenTool~/Tests/CodegenTool.Tests.csproj` + tests | NUnit golden tests for model + both emitters + the CLI (port of P0's tests, NUnit-ized). |
| `CodegenTool~/sample/SkeletonConfig.cs` | the Phase-A proof schema (`[GpuStruct]` 2 scalars). |

## Milestone Map (post-brainstorm-context-manager)
- [x] **A1 — attributes + emitter core** · Tasks 1–4: shared attributes lib + port std430 model + C++/GLSL emitters into the core, with NUnit golden tests. Gate: `~/.dotnet/dotnet test` (the CodegenTool tests) green for model+emitters.
- [x] **A2 — console tool + proof** · Tasks 5–6: `CodegenTool` CLI + `SkeletonConfig` proof + generate/`--check` golden. Gate: `dotnet test` green + `dotnet run` generates `SkeletonConfig.g.h`/`.glsl` and `--check` is clean.

### Progress Log
- Milestone A1 (Tasks 1–4): DONE · Yeroket worktree `feat/gpustruct-codegen-phaseA` commits `d54c0961` (attrs lib) / `4c2f7029` (std430 scalar model) / `1f878065` (C++ emitter) / `7d108fad` (GLSL emitter) · gate `~/.dotnet/dotnet test CodegenTool~/Tests` = **8/8 green** (controller-run) · Opus review APPROVED (controller-direct): faithful port of P0 into `Yeroket.KernelFramework.Codegen`; netstandard2.0 adaptation `record`→sealed-class (SourceGenerator~ can't use `IsExternalInit`) — sound; shared `[GpuStruct]`/`[GpuArray]`/`Float3`/`Mat4` in `Yeroket.Util.KernelFramework`; isolation held (worktree-only) · 2026-07-02
- Milestone A2 (Tasks 5–6): DONE · Yeroket worktree `feat/gpustruct-codegen-phaseA` commits `99071bab` (CodegenTool CLI generate/`--check`) / `779a73e4` (SkeletonConfig proof + generated C++/GLSL) / `f5d10c26` (rebuild analyzer DLL to include A1 emitters) · gate (controller-run): `dotnet test CodegenTool~/Tests` = **9/9 green** + real-CLI `--check` vs committed artifacts = exit 0 (zero drift) + g++ `-fsyntax-only` on `.g.h` = pass (glslc skipped — no Vulkan SDK) · Opus validator APPROVED (all 7 points): faithful fold of P0 `Program`+`CompilationLoader`; attributes DLL referenced by reflection (`typeof(GpuStructAttribute).Assembly.Location`) not hardcoded; `[GpuStruct]` matched by name + `--struct <Name>` selector (multi-struct-ready); LF-normalized artifacts; csproj deviations sound (`DefaultItemExcludes` for nested tests/sample, `Nullable=enable` P0-carryover); DLL-sync commit = right call per precedent `8660c3a9`. Non-blocking: usage/not-found exit code 2 is P0-faithful + std Unix convention. Isolation held (worktree-only, no push/merge) · 2026-07-02

---

### Task 1: Shared attributes lib
**Files:** Create `Runtime/GpuStructAttributes.cs`, `Attributes~/Yeroket.KernelFramework.Attributes.csproj`.
- [ ] **Step 1:** Port P0's `GpuStructAttributes.cs` + P1's `Float3`/`Mat4`/`[GpuArray]` into `Runtime/GpuStructAttributes.cs`, namespace `Yeroket.Util.KernelFramework`:
```csharp
using System;
namespace Yeroket.Util.KernelFramework
{
    public enum GpuLayout { Std430 }
    [AttributeUsage(AttributeTargets.Struct, AllowMultiple = false)]
    public sealed class GpuStructAttribute : Attribute { public GpuLayout Layout { get; set; } = GpuLayout.Std430; }
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
    public sealed class GpuArrayAttribute : Attribute { public int Length { get; } public GpuArrayAttribute(int length){ Length = length; } }
    public struct Float3 { }   // std430 vec3 (Phase B)
    public struct Mat4 { }     // std430 mat4 (Phase B)
}
```
- [ ] **Step 2:** Create the netstandard2.0 attributes csproj:
```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>netstandard2.0</TargetFramework><Nullable>disable</Nullable><LangVersion>latest</LangVersion>
    <AssemblyName>Yeroket.KernelFramework.Attributes</AssemblyName><RootNamespace>Yeroket.Util.KernelFramework</RootNamespace></PropertyGroup>
  <ItemGroup><Compile Include="../Runtime/GpuStructAttributes.cs" /></ItemGroup>
</Project>
```
- [ ] **Step 3:** `~/.dotnet/dotnet build Packages/com.yeroket.utility.kernel-framework/Attributes~` → succeeds.
- [ ] **Step 4:** Commit — `feat(codegen): shared [GpuStruct] attributes lib in kernel framework (Phase A)`.

---

### Task 2: Port the std430 scalar layout model
**Files:** Create `SourceGenerator~/Transpiler/GpuStructModel.cs`. Test in `CodegenTool~/Tests/` (set up in Task 5's csproj; for now add the model test file + a minimal test csproj if needed, or fold tests into Task 5). **Recommended:** create `CodegenTool~/Tests/CodegenTool.Tests.csproj` now (NUnit, net8.0, ProjectReference the analyzer + attributes) so Tasks 2–4 are TDD.
- [ ] **Step 1:** Create the NUnit test csproj (template = `SourceGenerator~/Tests/SDFNodeGenerator.Tests.csproj`: net8.0, `Microsoft.CodeAnalysis.CSharp` 4.3.0, `Microsoft.NET.Test.Sdk`/NUnit/NUnit3TestAdapter, `ProjectReference` `../../SourceGenerator~/SDFNodeGenerator.csproj` + `../../Attributes~/…csproj`).
- [ ] **Step 2: failing test** — port P0 `StructModelTests`: 2-scalar struct → offsets 0,4, size 8; non-scalar throws. (NUnit `[Test]`/`Assert.That`.)
- [ ] **Step 3:** run → FAIL.
- [ ] **Step 4:** port P0 `StructModel.cs` → `GpuStructModel.cs`, namespace `Yeroket.KernelFramework.Codegen`, public types (`ScalarKind`, `FieldLayout`, `StructModel`, `StructLayout.Build`). Keep the scalar std430 rules verbatim.
- [ ] **Step 5:** run → PASS. Commit — `feat(codegen): port std430 scalar model into kernel core (Phase A)`.

---

### Task 3: Port the C++ emitter
**Files:** Create `SourceGenerator~/Transpiler/GpuStructCppEmitter.cs`. Test in the Task-2 test project.
- [ ] **Step 1: failing test** — port P0 `CppStructEmitterTests` (asserts `uint32_t`/`int32_t`, `static_assert(sizeof/offsetof)`, `#pragma once`, `Vixen::Gpu` namespace — keep the SAME emitted shape as P0 for continuity).
- [ ] **Step 2:** run → FAIL.
- [ ] **Step 3:** port P0 `CppStructEmitter.cs` → `GpuStructCppEmitter.cs` (namespace `Yeroket.KernelFramework.Codegen`, keep LF-normalization + fail-loud default arm).
- [ ] **Step 4:** run → PASS. Commit — `feat(codegen): port C++ struct emitter into kernel core (Phase A)`.

---

### Task 4: Port the GLSL emitter
**Files:** Create `SourceGenerator~/Transpiler/GpuStructGlslEmitter.cs`. Test in the Task-2 test project.
- [ ] **Step 1: failing test** — port P0 `GlslStructEmitterTests` (guard, `uint`/`int`, LF).
- [ ] **Step 2:** run → FAIL.
- [ ] **Step 3:** port P0 `GlslStructEmitter.cs` → `GpuStructGlslEmitter.cs`.
- [ ] **Step 4:** run → PASS. Commit — `feat(codegen): port GLSL struct emitter into kernel core (Phase A)`.

**A1 gate (controller):** `~/.dotnet/dotnet test Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests` — model + both emitters green.

---

### Task 5: Console tool CLI
**Files:** Create `CodegenTool~/CodegenTool.csproj`, `CodegenTool~/Program.cs`. Test extend the Task-2 test project.
- [ ] **Step 1:** csproj — net8.0 `OutputType=Exe`, `Microsoft.CodeAnalysis.CSharp` 4.3.0, `ProjectReference` `../SourceGenerator~/SDFNodeGenerator.csproj` + `../Attributes~/…csproj`.
- [ ] **Step 2: failing CLI test** — port P0 `CliTests`: write a temp schema (`[GpuStruct]` 2 scalars) → `Program.Main(["--schema",dir,"--struct","SkeletonConfig","--out-cpp",…,"--out-glsl",…])` returns 0 + files contain `uint32_t`/`uint`; `--check` → 0; tamper → 1.
- [ ] **Step 3:** run → FAIL.
- [ ] **Step 4:** port P0 `CompilationLoader.cs` + `Program.cs` into `Program.cs`. Adapt: the schema `Compilation` references the **attributes DLL** (`typeof(Yeroket.Util.KernelFramework.GpuStructAttribute).Assembly.Location`); match `[GpuStruct]` by attribute name; add the `--struct <Name>` selector (multi-struct-ready); call `StructLayout.Build` + `GpuStructCppEmitter.Emit`/`GpuStructGlslEmitter.Emit`; write / `--check`.
- [ ] **Step 5:** run → PASS. Commit — `feat(codegen): CodegenTool console CLI (generate/--check) (Phase A)`.

---

### Task 6: SkeletonConfig proof + golden
**Files:** Create `CodegenTool~/sample/SkeletonConfig.cs`, generated `CodegenTool~/sample/generated/SkeletonConfig.g.h` + `.glsl`.
- [ ] **Step 1:** `sample/SkeletonConfig.cs` = `[GpuStruct] public struct SkeletonConfig { public uint version; public int payload; }` (namespace-free or a sample ns).
- [ ] **Step 2:** generate:
```bash
cd Packages/com.yeroket.utility.kernel-framework
~/.dotnet/dotnet run --project CodegenTool~ -c Release -- \
  --schema CodegenTool~/sample --struct SkeletonConfig \
  --out-cpp CodegenTool~/sample/generated/SkeletonConfig.g.h \
  --out-glsl CodegenTool~/sample/generated/SkeletonConfig.glsl
```
- [ ] **Step 3:** verify `--check` (append `--check`) → exit 0; tamper the `.g.h` → non-zero + `STALE:` → restore.
- [ ] **Step 4:** g++/glslc compile-smoke both artifacts (glslc from the Yeroket `.vulkan-sdk` if present, else skip glslc with a logged note). Commit — `feat(codegen): SkeletonConfig proof + generated C++/GLSL via CodegenTool (Phase A)`.

**A2 gate (controller):** `dotnet test` green · `dotnet run` generates + `--check` clean · tamper bites · compile-smokes (glslc gated on SDK presence).

---

## Self-Review
- **Spec coverage (pivot V1/V2/V3):** shared attributes in kernel framework (Task 1) · emitter core in `SourceGenerator~` (Tasks 2–4) · callable console tool (Task 5) · proof (Task 6). ✓ Re-homes P0 (Global Constraints port list). ✓
- **Placeholders:** ports reference exact P0 source paths/commits; test intents + expected offsets (0/4/8) are concrete; CLI exit codes 0/1 specified. Compound types (Float3/Mat4/nested/array) + OctreeConfig are **Phase B** (explicitly out of scope here).
- **Type consistency:** `Yeroket.KernelFramework.Codegen` namespace across model/emitters; attributes in `Yeroket.Util.KernelFramework`; the tool refs both.
- **Deferred:** wiring the POD-struct emitter into the analyzer's `ctx.AddSource` path (the console tool is the P-A deliverable); DRYing `RecipeContainerEmitter` onto `GpuStructModel`; VIXEN-side invocation (Phase B).

## Execution Handoff
Run via post-brainstorm-context-manager (A1 then A2) **in the Yeroket repo**, Sonnet implementer + Opus validator per milestone, controller-run `dotnet test` gates.
