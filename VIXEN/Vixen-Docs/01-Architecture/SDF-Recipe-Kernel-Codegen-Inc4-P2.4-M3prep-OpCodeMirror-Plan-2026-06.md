# P2.4 M3-prereq #1 — Codegen-generated OpCode-ID mirror — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox steps. **Cross-repo, two stacked branches:** Yeroket `feat/kernel-codegen-p2` (`/home/liory/Github/Yeroket-Fantasy`) for MA; VIXEN `feat/sdf-recipe-codegen-p2` (`/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0`) for MB. Closes the #1 follow-up from the M2 final review.

**Goal:** Make VIXEN's C++ `SdfOpCode` a **codegen-generated subset mirror** of the canonical C# `SDFOpCode`, with explicit append-only values — so the cross-repo opcode byte-contract can never silently desync, and each consumer's opcode set is *declared* (via `[SdfCoreKernel]` kernels + a new `[SdfCoreOp]` marker for control opcodes) rather than hand-maintained.

**Architecture:** C# `SDFOpCode` becomes the single source of truth with explicit `= N` values (pinned to today's ordinals; append-only henceforth). A new generator pass (`EmitSdfOpCodeEnum`, via `CompilationProvider`) reads each `SDFOpCode` member's name→value and emits `SdfOpCodes.g.h` — `enum class SdfOpCode : uint8_t` (VIXEN's `Recipe` namespace) containing exactly VIXEN's **declared subset**: the `[SdfCoreKernel]` kernel opcodes ∪ the `[SdfCoreOp]`-marked control opcodes, pinned to the canonical values. VIXEN replaces its hand-maintained enum with an `#include`. A diagnostic fires if a marked opcode has no `SDFOpCode` member; a golden test guards the generated header.

**Tech Stack:** C# (Roslyn source-gen, `~/.dotnet/dotnet`), C++23, GoogleTest, CMake (`vixen-wsl`), glm.

## Global Constraints

- **Keep the 132-byte `SdfInstruction` layout + `opCode : uint8`** (≤256 opcodes; 151 today). No widening.
- **Explicit C# values MUST equal today's implicit ordinals** — the investigation confirmed `SDFOpCode` is contiguous `0..150` with NO gaps, so pinning `Sphere = 0 … DepthBlend = 150` is a **value-preserving no-op**. Append-only after this.
- **Self-checking correctness gate:** VIXEN's current declared set = the 5 `[SdfCoreKernel]` kernels (Sphere, Box, Union, SmoothUnion, MirrorX) + `RestorePos` (control). The generated `SdfOpCodes.g.h` for that set MUST reproduce VIXEN's existing enum **exactly**: `Sphere=0, Box=1, Union=24, SmoothUnion=25, MirrorX=41, RestorePos=97`. If it doesn't, the design or the name-mapping is wrong.
- **Subset, not full mirror:** emit only the declared set, NOT all 151 members (honors the per-consumer-set decision + the P2.4 generic/excluded curation). Kernel opcodes come free from the existing `[SdfCoreKernel]` marks; only **control** opcodes (no kernel body) need the new `[SdfCoreOp]` marker — today that's just `RestorePos`.
- **Confirm-from-source (not placeholders):** the implementer reads the EXACT values from the authoritative source — `SDFOpCode`'s fully-qualified metadata name + namespace from `SDFInstruction.cs`; VIXEN's enum namespace from `SdfInstruction.h`; the kernel-method→`SDFOpCode`-member name convention from the existing Evaluator emit in `SDFNodeSourceGenerator.cs` (~line 1647, `case SDFOpCode.Sphere:`).
- **Yeroket conventions:** Linux `~/.dotnet/dotnet` only; rebuild + commit `RoslynAnalyzers/SDFNodeGenerator.dll` after any `SourceGenerator~` edit; never run Unity / Unity MCP / run_tests. `Generated/` is gitignored → `git add -f` artifacts.
- **Both branches KEPT, not merged.** Commit trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` + `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.
- **No-regression:** the M2 dotnet suite stays 88/4 (only the 4 pre-existing fails); VIXEN's recipe + render gates stay green.

## File Structure

**MA (Yeroket `/home/liory/Github/Yeroket-Fantasy`):**
- `Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs` — pin explicit `= N` on every `SDFOpCode` member (= current ordinals); add `[SdfCoreOp]` to `RestorePos`.
- `Packages/com.yeroket.utility.kernel-framework/.../KernelCallableAttribute.cs` (or wherever `SdfCoreKernelAttribute` lives) — add `SdfCoreOpAttribute` (`[AttributeUsage(AttributeTargets.Field)]`).
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/SDFNodeSourceGenerator.cs` — add the `EmitSdfOpCodeEnum` pass + a "marked opcode has no `SDFOpCode` member" / "SDFOpCode not visible" diagnostic.
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/.../CppEmitter.cs` — an enum-text builder helper (mirror the existing header builders) if it keeps `SDFNodeSourceGenerator.cs` clean.
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/CppEmitterTests.cs` — a golden test for `SdfOpCodes.g.h`.
- `Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll` — rebuilt (committed).
- `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfOpCodes.g.h` — the new generated artifact (force-added).

**MB (VIXEN worktree):**
- `VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h` — vendored copy.
- `VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h` — replace the hand-written `enum class SdfOpCode { … }` with `#include "generated/SdfOpCodes.g.h"`.

## Milestone Map

> Two milestones, SEQUENTIAL (MB consumes MA's generated enum). Cross-repo.

- [ ] **MA `[YEROKET]` — pin values + `[SdfCoreOp]` marker + `EmitSdfOpCodeEnum` + diagnostic + golden + regen (Tasks 1–4).** Gate: DLL rebuilt; `~/.dotnet/dotnet test` 88/4 (+ the new enum golden passes); generated `SdfOpCodes.g.h` contains EXACTLY `{Sphere=0,Box=1,Union=24,SmoothUnion=25,MirrorX=41,RestorePos=97}` in VIXEN's `Recipe` namespace; pinned C# values unchanged from ordinals. Implementer **Sonnet**, validator **Opus**.
- [ ] **MB `[VIXEN]` — vendor + swap the enum to the generated include + build + tests (Task 5).** Gate: `SdfInstruction.h` includes the generated enum; `git diff` shows the generated enum is semantically identical to the prior hand-written one (same 6 names+values); build clean; recipe parity/codegen/bake + the lavapipe render gates all green (ICD-only). Implementer **Sonnet**, validator **Opus**.

Validators **Opus** per milestone. Controller Opus, thin.

## Progress Log

- _(pending execution)_

---

## Task 1 [MA]: Pin explicit `SDFOpCode` values + add `[SdfCoreOp]`

**Repo:** Yeroket, branch `feat/kernel-codegen-p2` (verify).

- [ ] **Step 1:** Read `Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs`. Confirm the enum is contiguous `0..150` with no gaps and no existing `= N`. Note the enum's namespace + fully-qualified name (for the generator's `GetTypeByMetadataName`).
- [ ] **Step 2:** Pin explicit values on EVERY member equal to its current 0-based ordinal: `Sphere = 0,` `Box = 1,` … `DepthBlend = 150,`. This is a value-preserving no-op (compile + any existing serialization unaffected). Keep the section comments. Add a header comment: `// EXPLICIT, APPEND-ONLY. Insertion before an existing member would silently break the C++ mirror + serialized recipes. New opcodes get the next free value at the end of their section's reserved range or appended.`
- [ ] **Step 3:** Locate the `SdfCoreKernelAttribute` definition (added in P2.4 M1). In the same file, add:
```csharp
/// Marks an SDFOpCode member as part of the generated core opcode set for non-kernel
/// (VM-control) opcodes that have no [SdfCoreKernel] body (e.g. RestorePos). Kernel
/// opcodes are included via their [SdfCoreKernel] method; this covers the rest.
[AttributeUsage(AttributeTargets.Field)]
public sealed class SdfCoreOpAttribute : System.Attribute { }
```
- [ ] **Step 4:** Annotate `RestorePos` with `[SdfCoreOp]` (it is VIXEN's only control opcode today). Do NOT mark Output/PushParam/etc. yet (YAGNI — M3/M4 mark them as VIXEN implements them).
- [ ] **Step 5:** `~/.dotnet/dotnet build -c Release` the graph-framework (or the test project) compiles — confirm pinning + the attribute are valid C#.

## Task 2 [MA]: `EmitSdfOpCodeEnum` generator pass + diagnostic

**File:** `SourceGenerator~/SDFNodeSourceGenerator.cs` (+ optional `CppEmitter.cs` helper).

- [ ] **Step 1:** Read the existing `Initialize` provider wiring + `EmitCppEmitter` (~line 1893) + the Evaluator emit (~1647) to learn (a) how `[SdfCoreKernel]` kernels are collected/filtered, (b) the kernel-method→`SDFOpCode`-member name convention (strip the `SdfCore_` prefix, or the `[VMKernel(OpCode=…)]` string — use whichever the existing `case SDFOpCode.X:` emit uses), (c) how `WrapAsCSharpSource` + `ctx.AddSource` materialize an artifact.
- [ ] **Step 2:** Add an `EmitSdfOpCodeEnum` pass wired through `context.CompilationProvider` (combined with the core-kernel list so it has both the compilation and the `[SdfCoreKernel]` opcode names). Gate it on `compilation.AssemblyName == "com.utility.sdf"` (the assembly that references graph-framework) so it runs once. Logic:
```
var sym = compilation.GetTypeByMetadataName("<SDFOpCode FQN from Task1>");
if (sym is null) { ctx.ReportDiagnostic(<SDFKxxx: SDFOpCode not visible in this compilation>); return; }
// value map for ALL members
var valueOf = sym.GetMembers().OfType<IFieldSymbol>()
                 .Where(f => f.HasConstantValue)
                 .ToDictionary(f => f.Name, f => System.Convert.ToInt32(f.ConstantValue));
var subset = new SortedDictionary<int,string>();   // value -> name, sorted ascending
foreach (var opName in coreKernelOpcodeNames) {     // from [SdfCoreKernel] kernels
    if (!valueOf.TryGetValue(opName, out var v))
        ctx.ReportDiagnostic(<SDFKxxx: kernel opcode 'opName' has no SDFOpCode member>);
    else subset[v] = opName;
}
foreach (var f in sym.GetMembers().OfType<IFieldSymbol>())   // [SdfCoreOp]-marked control members
    if (f.GetAttributes().Any(a => a.AttributeClass?.Name is "SdfCoreOpAttribute" or "SdfCoreOp"))
        subset[System.Convert.ToInt32(f.ConstantValue)] = f.Name;
```
- [ ] **Step 3:** Build the header text (a `CppEmitter` helper is fine), matching VIXEN's namespace (confirm from `SdfInstruction.h` — expected `Vixen::SVO::Recipe`) and the kernels' provenance-comment style:
```cpp
#pragma once
#include <cstdint>
// <provenance: generated from SDFOpCode — do not edit by hand>
namespace Vixen::SVO::Recipe {
enum class SdfOpCode : uint8_t {
    Sphere      = 0,
    Box         = 1,
    Union       = 24,
    SmoothUnion = 25,
    MirrorX     = 41,
    RestorePos  = 97,
};
}  // namespace Vixen::SVO::Recipe
```
  (emit members ascending by value, aligned). `ctx.AddSource("SdfOpCodes.g.h.cs", CppEmitter.WrapAsCSharpSource("SdfOpCodeHeader", text));`
- [ ] **Step 4:** Rebuild the DLL: `cd …/SourceGenerator~ && ~/.dotnet/dotnet build -c Release` → 0 errors + `RoslynAnalyzers/SDFNodeGenerator.dll` redeploy.

## Task 3 [MA]: Golden test + materialize the artifact

- [ ] **Step 1:** In `Tests/CppEmitterTests.cs`, add a golden test for `SdfOpCodes.g.h` mirroring the existing `RegeneratedCpp_FromReferencedMathType_MatchesCommittedArtifact` pattern: compile a compilation that references the REAL `SDFInstruction.cs` (as a metadata reference / via `RunAndFindGeneratedAgainstMathRef`) plus the `[SdfCoreKernel]` kernels, extract the `SdfOpCodeHeader` constant, and byte-compare against the committed `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfOpCodes.g.h`. Support the same `UPDATE_GOLDENS=1` write-mode (preserve the provenance header).
- [ ] **Step 2:** Add an assertion (in this test or a sibling) that the generated enum text contains EXACTLY the 6 expected lines `Sphere = 0` / `Box = 1` / `Union = 24` / `SmoothUnion = 25` / `MirrorX = 41` / `RestorePos = 97` and no others — the self-checking gate.
- [ ] **Step 3:** Materialize the artifact: `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter <the enum golden>`. Inspect `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfOpCodes.g.h` — must be the 6-member enum above in `Vixen::SVO::Recipe`.
- [ ] **Step 4:** Full suite (no write-mode): `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` → 88 pass + the new enum golden = 89 pass / 4 fail (the same 4 pre-existing). Report exact counts.

## Task 4 [MA]: Commit + vendor

- [ ] **Step 1:** `git -C /home/liory/Github/Yeroket-Fantasy add` the modified `.cs` files + the test + `git add -f Packages/com.utility.sdf/Runtime/GPU/Generated/SdfOpCodes.g.h` + the rebuilt DLL.
- [ ] **Step 2:** Commit: `feat(kernel-codegen): generated SdfOpCode mirror (pinned SDFOpCode + [SdfCoreOp] + EmitSdfOpCodeEnum) (P2.4 M3-prereq #1)` + trailers.
- [ ] **Step 3:** Vendor: copy `Packages/com.utility.sdf/Runtime/GPU/Generated/SdfOpCodes.g.h` → `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0/VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h`. (Commit on the VIXEN branch in MB Task 5, or as a chore here — implementer's choice; keep VIXEN's provenance-header convention.)

## Task 5 [MB]: Swap VIXEN's enum to the generated include

**Repo:** VIXEN worktree, branch `feat/sdf-recipe-codegen-p2`.

- [ ] **Step 1:** Confirm the vendored `VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h` exists + matches MA's output (6 members, `Vixen::SVO::Recipe`).
- [ ] **Step 2:** In `VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h`, REPLACE the hand-written `enum class SdfOpCode : uint8_t { … };` block with `#include "generated/SdfOpCodes.g.h"`. Confirm the include path resolves (same `Recipe/` include root the kernels use) + the namespace matches what the 8 consumers expect. Keep the rest of `SdfInstruction.h` (the 132-byte `SdfInstruction` struct + `static_assert`) untouched.
- [ ] **Step 3:** `git -C … diff VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h` — confirm the only change is the enum→include swap, and the generated enum is semantically identical to the deleted hand-written one (same 6 names, same values).
- [ ] **Step 4:** Build: from `…/sdf-recipe-codegen-p0/VIXEN`, `cmake --build ../build-wsl --target test_recipe_eval_parity test_recipe_codegen test_recipe_bake -j` → clean (all 8 `SdfOpCode` consumers compile unchanged).
- [ ] **Step 5:** Run the recipe gates: `test_recipe_eval_parity` (2/2), `test_recipe_codegen` (2/2), `test_recipe_bake` (1/1) → green. Then build + run the lavapipe render gate ICD-only (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json test_procedural_recipe_render --gtest_filter=*RenderMirrorCsgRecipe*`) → still 25,332-ish body px (the enum swap must not change behaviour).
- [ ] **Step 6:** Commit: `git -C … add` the vendored header + `SdfInstruction.h`, then `refactor(recipe): SdfOpCode is now a codegen-generated mirror of C# SDFOpCode (P2.4 M3-prereq #1)` + trailers.

## Self-Review

**Coverage:** desync root cause → generated mirror (Tasks 2–3, 5) makes VIXEN's enum un-hand-maintained. Time-stability → explicit pinning (Task 1). Per-consumer set → subset emit via `[SdfCoreKernel]` ∪ `[SdfCoreOp]` (Tasks 1–2). Safety → diagnostic (Task 2) + golden (Task 3). Correctness proof → the generated 6-member enum must equal VIXEN's existing one (Global Constraints + Task 3 Step 2 + Task 5 Step 3). ✓
**Placeholders:** the emit logic, the expected 6 values, the pinning, the marker, and the swap are concrete; the few "confirm from source" items (SDFOpCode FQN/namespace, VIXEN Recipe namespace, kernel→member name convention) are authoritative-source lookups, not guesses. ✓
**Type consistency:** `SdfOpCode : uint8_t` (C++) ↔ `SDFOpCode : byte` (C#); `[SdfCoreOp]` Field-usage; `SdfOpCodes.g.h` in `Recipe/generated/`. The struct/layout is untouched. ✓
**Risk:** (1) generator can't see `SDFOpCode` if run over an assembly that doesn't reference graph-framework → the diagnostic + the assembly-name gate handle it. (2) name-mapping mismatch (kernel `SdfCore_Sphere` → `Sphere`) → the self-checking 6-value gate catches it immediately. (3) namespace mismatch breaks the swap → Task 5 Step 2/4 (build) catches it.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones, sequential). MA Sonnet+Opus (validator: re-run dotnet 89/4, confirm the generated enum = the 6 canonical values, tamper — e.g. unmark RestorePos → it drops from the enum → VIXEN would fail to compile; pinning is value-preserving). MB Sonnet+Opus (validator: confirm the enum→include swap is semantically identical, build clean, render ICD-only unchanged at ~25,332px). Then update memory + spec doc (M3 prereq #1 DONE) and continue to M3 proper.
