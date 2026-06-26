# P1 — Real AST-Visitor Codegen (productionization) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox (`- [ ]`) steps. **Yeroket-only milestone, pure .NET (no Unity, no VIXEN changes).** Stacks on P0.

**Goal:** Replace P0's text/regex SDF-kernel emitter with **real Roslyn AST visitors** — HLSL via the existing `HLSLVisitor`, C++ via a NEW parallel AST visitor — proven on a multi-statement kernel (`SmoothUnion`), with the existing `Sphere`/`Union` kernels emitting byte-identically (regression-guarded).

**Architecture (Path B, user-chosen):** HLSL `[KernelCallable]` emit routes through the existing real `HLSLVisitor.EmitFunction` (delete the text `BuildHlslHeader` path). C++ gets a new `CppAstVisitor` mirroring `HLSLVisitor`'s 35-case traversal with C++/glm mappings (skips the SDF-graph `_kernel` parameter binding — callables are pure math). The load-bearing `HLSLVisitor` (drives all Yeroket SDF-graph HLSL) is **not** refactored.

**Tech Stack:** C# / Roslyn (netstandard2.0, Roslyn 4.3.0), NUnit (net8.0), `~/.dotnet/dotnet`.

## Global Constraints

- **Behaviour-preserving for existing kernels.** `Sphere`/`Union` C++ AND HLSL emit must stay **byte-identical** to P0 (the vendored VIXEN artifacts must not need re-vendoring). Regression-guarded by the existing `CppEmitterTests`.
- **No band-aids.** Both backends end on real AST traversal; the P0 regex/`.Replace()` path is **deleted**, not left dormant.
- **Don't touch `HLSLVisitor`'s behaviour** (Path B). Reuse it for HLSL; do not refactor it.
- **Linux dotnet only:** `~/.dotnet/dotnet`. Generator is a compiled DLL — rebuild + **commit the DLL** after `SourceGenerator~` edits.
- **Repo:** `/home/liory/Github/Yeroket-Fantasy`, branch `feat/kernel-codegen-p1` (off `feat/kernel-cpp-emitter`). Commit there. No VIXEN changes; no worktree.
- **Out of scope (deferred):** per-consumer manifest, opcode layering, conformance-vector export (not blocking P2 — add when needed); full opcode catalog (P2); Burst/GPU runtime parity (Unity-gated — a separate controller/user check, not in this pipeline).

## File Structure

**Create:**
- `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppAstVisitor.cs` — real C++ AST visitor (parallel to `HLSLVisitor`).

**Modify:**
- `…/Transpiler/CppEmitter.cs` — `BuildCppHeader` uses `CppAstVisitor`; `BuildHlslHeader` uses the existing `HLSLVisitor.EmitFunction`. Delete the text-extraction body path.
- `…/Transpiler/CppVisitor.cs`, `…/Transpiler/CppMappingTables.cs` — keep the mapping tables; **delete** the `TranslateBody`/regex methods (now dead). Extend `CppMappingTables` with the expansions the prove-it kernel needs (`math.saturate`→`glm::clamp(x,0,1)`, `math.lerp`→`glm::mix`).
- `…/Tests/CppEmitterTests.cs` — add the `SmoothUnion` multi-statement golden tests (C++ + HLSL); keep the `Sphere`/`Union` tests as the regression guard.

---

## Milestone Map

> Persisted for the context-manager pipeline (2026-06-26). One milestone; runs autonomously. Yeroket-only.

- [ ] **M1 `[YEROKET]` — Real AST visitors (Tasks 1–5).** Repo `/home/liory/Github/Yeroket-Fantasy`, branch `feat/kernel-codegen-p1`. Implementer: **Sonnet**. Gate: `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj` green — `Sphere`/`Union` C++/HLSL byte-identical to P0 + new `SmoothUnion` multi-statement golden passes (C++ & HLSL) + 0 new failures; rebuilt DLL committed.

Validator: **Opus** (fix-loop cap 3), tamper-check the multi-statement golden. Controller (me): Opus, thin.

## Progress Log

_(controller appends one line per completed milestone)_

---

## Task 1: Add the multi-statement prove-it kernel + failing goldens

**Files:** Modify `Tests/CppEmitterTests.cs`.

**Interfaces:** Produces a `SmoothUnion(float d1,float d2,float k)` `[KernelCallable]` in the test's inline source (NOT the production catalog — that's P2) and golden assertions for its C++ + HLSL emit.

- [ ] **Step 1:** Add `SmoothUnion` to the inline kernel source used by the tests (real body from `com.utility.sdf/Runtime/CPU/Core/SDFOperations.cs`):
```csharp
[KernelCallable] public static float SmoothUnion(float d1, float d2, float k) {
    if (k <= 0f) return math.min(d1, d2);
    float h = math.saturate(0.5f + 0.5f * (d2 - d1) / k);
    return math.lerp(d2, d1, h) - k * h * (1f - h);
}
```
- [ ] **Step 2:** Add the failing golden tests (assert on key substrings — robust to whitespace):
```csharp
[Test] public void EmitsCppForSmoothUnion_MultiStatement() {
    string cpp = RunAndFindGenerated(SourceWithSmoothUnion, "SdfCore_SmoothUnion"); // or the CppHeader tree
    Assert.IsNotNull(cpp);
    StringAssert.Contains("float SdfCore_SmoothUnion(float d1, float d2, float k)", cpp);
    StringAssert.Contains("return glm::min(d1, d2)", cpp);        // if-return statement
    StringAssert.Contains("float h = glm::clamp(", cpp);          // saturate→clamp(x,0,1) local var
    StringAssert.Contains("glm::mix(d2, d1, h)", cpp);            // lerp→mix
    StringAssert.IsFalse ??? // (see Step 3 note)
}
[Test] public void EmitsHlslForSmoothUnion_MultiStatement() {
    string hlsl = RunAndFindGenerated(SourceWithSmoothUnion, "HlslHeader");
    Assert.IsNotNull(hlsl);
    StringAssert.Contains("float SdfCore_SmoothUnion(float d1, float d2, float k)", hlsl);
    StringAssert.Contains("return min(d1, d2)", hlsl);
    StringAssert.Contains("saturate(", hlsl);
    StringAssert.Contains("lerp(d2, d1, h)", hlsl);
    Assert.IsFalse(hlsl.Contains("glm::"));                       // no C++ leak into HLSL
}
```
  (Use `Assert.IsFalse(cpp.Contains("saturate("))` in the C++ test to prove the saturate→clamp expansion actually happened. Drop the malformed line above.)
- [ ] **Step 3:** Run → **FAIL** (the regex emitter mishandles the multi-statement body / lacks the saturate expansion):
  Run: `cd Packages/com.yeroket.utility.kernel-framework/SourceGenerator~ && ~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj --filter SmoothUnion`
  Expected: FAIL.
- [ ] **Step 4: Commit** `test(kernel-codegen): multi-statement SmoothUnion golden (P1 T1, red)`.

## Task 2: Route HLSL callables through the existing HLSLVisitor

**Files:** Modify `Transpiler/CppEmitter.cs`.

- [ ] **Step 1:** In `BuildHlslHeader`, for each `[KernelCallable]` `IMethodSymbol`, construct an `HLSLVisitor` for it and call `EmitFunction("")` to produce the HLSL function (replacing the text `CppVisitor.EmitFunctionHlsl`/`TranslateBody` path). If `HLSLVisitor`'s ctor needs a `KernelInfo`/`_kernel`, build the minimal one a pure callable needs (no field mappings) or add a callable-friendly entry point — keep `HLSLVisitor`'s existing behaviour unchanged.
- [ ] **Step 2:** Build the DLL: `~/.dotnet/dotnet build -c Release`. Run the HLSL goldens (`EmitsHlslForSphereAndUnion` + `EmitsHlslForSmoothUnion`):
  Expected: `Sphere`/`Union` HLSL **byte-identical** to P0; `SmoothUnion` HLSL passes.
- [ ] **Step 3: Commit** `feat(kernel-codegen): HLSL callables via real HLSLVisitor (P1 T2)`.

## Task 3: New C++ AST visitor

**Files:** Create `Transpiler/CppAstVisitor.cs`.

**Interfaces:** Produces `CppAstVisitor` with `EmitFunction(IMethodSymbol, indent) -> string` — emits a `float Name(glm::vec3 …)` C++ function from the method's AST.

- [ ] **Step 1:** Implement `CppAstVisitor` by mirroring `HLSLVisitor`'s traversal **structure** (the recursive `ToHLSL(ExpressionSyntax)` + `EmitHLSLStatement(StatementSyntax)` — ~35 node kinds), changing only the backend output: types via `CppMappingTables.MapType` (glm), calls via `CppMappingTables.CSharpToCpp` (+ the `saturate`→`glm::clamp(x,0,1)` / `lerp`→`glm::mix` expansions), C++ literal/float-suffix rules. **Skip** the `_kernel`/SDF-graph parameter-binding path (callables are pure-math; params map by name). This is an *adapt-to-pass-the-goldens* task — the T1 goldens (C++) are the acceptance, not a line count. Add a `// ponytail: parallels HLSLVisitor's traversal; collapse to a shared base if a 3rd backend appears` comment.
- [ ] **Step 2:** (no commit yet — wired in T4.)

## Task 4: Wire CppEmitter to the AST visitor + regression

**Files:** Modify `Transpiler/CppEmitter.cs`, `Transpiler/CppVisitor.cs`, `Transpiler/CppMappingTables.cs`.

- [ ] **Step 1:** `BuildCppHeader` uses `CppAstVisitor.EmitFunction` per callable (replacing `CppVisitor.EmitFunction`/`TranslateBody`). **Delete** the now-dead `TranslateBody`/regex methods from `CppVisitor.cs` (no dormant band-aid). Keep `CppMappingTables`.
- [ ] **Step 2:** Build the DLL. Run the C++ goldens (`EmitsCppForSphereAndUnion` + `EmitsCppForSmoothUnion`):
  Expected: `Sphere`/`Union` C++ **byte-identical** to P0 (compare against the committed P0 `.g.hpp` excerpt: `glm::length(p - center) - radius`, `glm::min(a, b)`); `SmoothUnion` C++ passes.
- [ ] **Step 3: Commit** `feat(kernel-codegen): real C++ AST visitor; delete regex path (P1 T3–T4)`.

## Task 5: Full suite green + DLL + verify artifacts unchanged

**Files:** none new.

- [ ] **Step 1:** Full suite: `~/.dotnet/dotnet test Tests/SDFNodeGenerator.Tests.csproj`.
  Expected: same 4 pre-existing failures only (`Linear_EmitsIndirectSettersAndBranchedDispatch`, `InBlackboard_…`, `MixedInRef_…`, `RefBlackboard_…`), **0 new**; all C++/HLSL goldens green.
- [ ] **Step 2:** Re-generate the `SdfCoreKernels.g.{hpp,hlsl}` via the existing extraction harness; `diff` against the committed P0 artifacts at `Packages/com.utility.sdf/Runtime/GPU/Generated/` — **must be byte-identical** (proves VIXEN needs no re-vendoring). If they differ, the AST visitor diverged from P0 output — fix before commit.
- [ ] **Step 3:** Ensure the rebuilt source-gen DLL is committed. **Commit** `chore(kernel-codegen): rebuild DLL; AST-visitor suite green, artifacts unchanged (P1 T5)`.

## Self-Review

**Coverage:** AST C++ visitor → T3/T4; HLSL via HLSLVisitor → T2; prove-it multi-statement → T1; regex deletion → T4; no-regression (existing kernels byte-identical) → T2/T4/T5 + artifact diff. ✓
**Placeholders:** T3 is adapt-to-goldens (mirrors HLSLVisitor, ~280 lines) — the T1 goldens are complete + are the acceptance; the malformed assert line in T1 is corrected in its Step-3 note. No "TODO". ✓
**Type consistency:** `CppAstVisitor.EmitFunction`, `BuildCppHeader`/`BuildHlslHeader`, `CppMappingTables` — consistent across tasks. ✓

## Execution Handoff

Run via post-brainstorm-context-manager (single milestone). Implementer Sonnet, validator Opus (tamper-check the SmoothUnion golden + the byte-identical artifact diff).
