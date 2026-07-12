# Undertow Codegen Unification — Increment 3: `[Param]` Declarations (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 3 (feature #14 in the survey).
**Why third:** smallest attribute-driven surface (8 sites per the survey), simplest shape (data
declaration, not logic transplant) — proves the ATTRIBUTE-DRIVEN (not schema-JSON-driven) input model can
also merge/migrate cleanly, before tackling logic-transplant features (`[Action]`/`[Effect]`).

## Ground truth (read fresh 2026-07-13 — verify file:line if moved)

- **`ParamAttribute`** (`core/src/Undertow.Sim/ParamAttribute.cs`, 18 lines) — `[AttributeUsage(Field,
  AllowMultiple=false, Inherited=false)]`, ctor `(string id)`, properties `Base` (double), `Scope`
  (`ParamScope` enum), `SeedOnApply` (bool). Declares a tunable knob on an `AttrKey` field — "the field is
  the dense AttrKey handle the reads use" (the attribute is metadata; the FIELD carries the runtime handle).
- **`EmitRegisterParams.cs`** (`core/src/Undertow.Authoring.Codegen/EmitRegisterParams.cs`, 44 lines) —
  `Emit(IReadOnlyList<ParamDecl> declsIn, out diagnostics)`: id-sorts (deterministic), validates no
  duplicate ids (diagnostic, not a silent skip), emits ONE method: `static void
  DeclareGeneratedParams(ParamRegistry reg)` inside `partial class UndertowSim` (`Undertow.Sim` namespace),
  with one `reg.Declare("id", baseExpr, scopeExpr, seedOnApply: bool)` call per decl.
- **`ParamDecl`** (same file) — the Roslyn-Extract-phase-flattened decl: `Id`, `BaseExpr`/`ScopeExpr`
  (already-resolved C# EXPRESSION STRINGS, not raw values — the enum/double literal resolution happens in
  a separate Extract phase this plan's Task 1 must locate and read), `SeedOnApply`.
- **`ParamRegistrationGenerator.cs`** — presumably the `ForAttributeWithMetadataName`-based
  `IIncrementalGenerator` that discovers `[Param]`-decorated FIELDS, builds `ParamDecl`s, and calls
  `EmitRegisterParams.Emit`. Task 1 must read this in full — the survey only summarizes it.
- **Key structural difference from `[KernelCallable]`**: `[KernelCallable]` targets METHODS (transplants a
  method BODY). `[Param]` targets FIELDS and emits a single registration call per field into an EXISTING
  partial class — closer in shape to registering a VALUE than transplanting LOGIC. Task 1 must determine
  whether this is close enough to be a to a straightforward new small attribute (mirroring Increment 1/2's
  "attribute carries what's needed" pattern) or whether `[KernelCallable]`'s existing discovery pipeline
  can be reused/extended for field-level (not just method-level) attributes.

## Scope boundary

- **IS:** Task 1 determines the mechanism (new small attribute, most likely, given the field-vs-method
  target mismatch with `[KernelCallable]` — but confirm, don't assume). Build + prove equivalence for the
  real 8 (or however many Task 1 actually finds) `[Param]` sites in `Undertow.Sim` against
  `EmitRegisterParams.cs`'s current output (same id-sort, same duplicate-id diagnostic, same
  `DeclareGeneratedParams` method body). Retire if safe (no dependents expected, but Task 1 must confirm —
  this feature's output IS consumed by `UndertowSim`'s runtime `ParamRegistry`, unlike Increment 2's
  purely-structural output, so check whether that consumption creates any coupling this plan doesn't yet
  know about).
- **IS NOT:** touching the 8 real `[Param]`-decorated fields' actual game-tuning values.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `ParamAttribute.cs`, `EmitRegisterParams.cs`, `ParamRegistrationGenerator.cs` fresh in full.
- Find and read the Extract phase that resolves `BaseExpr`/`ScopeExpr` from the attribute's typed
  `Base`/`Scope` properties into C# expression strings (likely uses the survey's cited `EnumExprHelper`/
  `EnumExpr.cs` for the `ParamScope` enum) — confirm this shared helper's real signature/behavior.
- Find and read all real `[Param(...)]` call sites in `Undertow.Sim` (the survey says 8 — confirm the
  actual count and list them).
- **Decide and REPORT**: new small attribute (most likely, given the target-type mismatch with
  `[KernelCallable]`), or genuine reuse of an existing mechanism. Justify against real code.
- Confirm retirement safety: does `UndertowSim`'s `ParamRegistry`/runtime consumption of
  `DeclareGeneratedParams` create any coupling beyond "call this method once at startup" that this
  increment needs to account for?

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's decision.
- Prove equivalence: same id-sort order, same duplicate-id diagnostic behavior (test with a real
  duplicate-id scenario, not just the happy path), same generated `DeclareGeneratedParams` method body
  for the real 8 (or actual count) sites.
- Retire `EmitRegisterParams.cs`/`ParamRegistrationGenerator.cs` if Task 1 confirmed safety, mirroring
  Increment 2's full-retirement pattern (build + full test-suite pass required, not just a unit test).

## Gates / guardrails
- Non-vacuous proof: real multi-site data, exercise the duplicate-id diagnostic path explicitly (a
  deliberately-malformed test scenario), not just the clean case.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — do not touch the main checkout or any other
  worktree. Do NOT push. Commit as work completes.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required before calling retirement done (same bar Increment 2 met).

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [x] **Milestone 2 (Task 2):** build + equivalence proof + retire. Implementer DONE 2026-07-13;
  pending Opus validator sign-off.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in either repo
  - **Corrected ground truth: 37 real `[Param(...)]` sites, not the survey's estimated 8** — spans 8
    files (`PopulationAttrs.cs`(2), `KnowledgeAttrs.cs`(4), `ResearchAttrs.cs`(2), `EconomyAttrs.cs`(2),
    `DiplomacyAttrs.cs`(13), `TrajectoryAttrs.cs`(6), `PlayerAttrs.cs`(3), `LoyaltyAttrs.cs`(5)) —
    controller independently verified the count via grep. All 37 use `Scope = ParamScope.Faction`;
    `SeedOnApply=true` appears on exactly 2 (`EconomyAttrs`'s `extract:efficiency`/`refine:efficiency`) —
    genuine non-vacuous coverage for Milestone 2's proof (a duplicate-id AND a SeedOnApply split both
    present in real data).
  - **Finding: a genuinely new, small attribute is needed — `[KernelCallable]`'s existing pipeline does
    NOT generalize, and neither `[RegistrySlots]` (Inc-1) nor `[SharedMapElements]` (Inc-2) fit either.**
    `KernelCallableAttribute` is confirmed strictly `[AttributeUsage(AttributeTargets.Method, ...)]`
    (controller independently verified) — a hard `AttributeUsage` wall against `[Param]`'s field-level
    target, and its downstream visitor-based body-transpilation pipeline has nothing to reuse for
    "read 3 typed properties, format 2 as expression strings, emit one registration call." `[RegistrySlots]`/
    `[SharedMapElements]` are both schema-JSON-DRIVEN (attribute carries an external file pointer);
    `[Param]`'s attribute INSTANCES themselves are the complete per-item data — even Inc-1/2's narrower
    "attribute carries a pointer" precedent doesn't transfer, since there's no external file here at all.
  - **New mechanism sketch**: a new Yeroket-side `[Param]`-equivalent attribute (`Id` ctor arg +
    `Base`/`Scope`/`SeedOnApply` named properties, mirroring the real `ParamAttribute`), a
    `ForAttributeWithMetadataName`-based CLI discovery path targeting FIELDS (new — no existing Yeroket
    discovery targets fields for logic/registration purposes), and a small emitter porting
    `EmitRegisterParams.Emit`'s logic (id-sort via `OrderBy(Id, StringComparer.Ordinal)`, dup-id
    diagnostic via `HashSet.Add` BEFORE sorting — note: adds a diagnostic, does NOT dedupe/skip the
    list itself, Milestone 2 must preserve this exact behavior) line-for-line. **Also ports
    `EnumExprHelper.EnumExpr`** (the survey's #17 trivial utility) — this is the first increment that
    needs it, per the parent program doc's own "fold into whichever increment needs it first" note.
  - **Retirement safety: SAFE, single simple call site.** `UndertowSim.cs:461`:
    `DeclareGeneratedParams(Params);` — called once, unconditionally, in the sim ctor's setup sequence.
    Controller independently confirmed via grep: only the generator/emitter, this one call site, and a
    pre-existing unit test (`EmitRegisterParamsTests.cs`, which tests the OLD mechanism being retired, not
    a downstream dependent) reference `DeclareGeneratedParams` anywhere in the codebase. One stale-comment
    correction noted (not a scope signal): the call site's own comment says "KnowledgeAttrs + EconomyAttrs"
    but the real field set spans 8 files — the comment predates later `[Param]` additions.
  - No blockers.
  - **Opus validator (independent re-verification):** confirmed all 37 sites/counts, the `Scope`/
    `SeedOnApply` distribution, `KernelCallableAttribute`'s genuine method-only `AttributeUsage` (did
    NOT conflate with the unrelated field-targeted `SdfCoreOpAttribute` sibling in the same file), the
    `[RegistrySlots]`/`[SharedMapElements]` rejection framing (both carry an external pointer;
    `[Param]`'s attribute instances ARE the data), `EnumExprHelper.EnumExpr`'s exact behavior AND its
    genuine reuse across `[Effect]`/`[System]`'s generators (confirming porting it once here is the
    right, reusable call), the single real `DeclareGeneratedParams` call site, and the dup-id diagnostic
    behavior EXACTLY (`HashSet.Add` check → diagnostic added, list NOT deduped, `declsIn` sorted whole —
    an existing test `Emit_RejectsDuplicateId` locks this in; Milestone 2 must preserve it precisely).
    **New finding, not a blocker but real constraint for Milestone 2**: `CodeModLoader.cs:164`
    (`sim.Params.Declare(a.Id, a.Base, a.Scope, a.SeedOnApply)`) is a LIVE RUNTIME-REFLECTION consumer
    that reads `ParamAttribute` DIRECTLY (via `FieldsWith<ParamAttribute>`) for code-mod support — it
    does NOT go through `DeclareGeneratedParams` at all, so retiring the codegen emitter doesn't touch
    it, but the new Yeroket-side `[Param]`-equivalent attribute MUST preserve the exact
    Id/Base/Scope/SeedOnApply property shape the reflection reads, or `CodeModLoader` breaks. **APPROVED
    — Milestone 2 may proceed, carrying this constraint forward.**

- Milestone 2 (Task 2, build + equivalence proof + retire): DONE · 2026-07-13
  - **Attribute-ownership decision: DISCOVER undertow's real, pre-existing `Undertow.Sim.ParamAttribute`
    by syntax name, do NOT introduce a new Yeroket-owned attribute.** This is the opposite of Inc-1/
    Inc-2 ([RegistrySlots]/[SharedMapElements], both NEW Yeroket attributes undertow's code migrated
    to use) — here the attribute already exists in undertow and IS the source of truth, and
    `CodeModLoader.cs:164`'s runtime reflection on it (`FieldsWith<ParamAttribute>` →
    `sim.Params.Declare(a.Id, a.Base, a.Scope, a.SeedOnApply)`) means its shape cannot change or be
    replaced. Verified this against `--callable-cpp`'s existing precedent (confirmed via a dedicated
    Explore-agent read of `CodegenTool~/Program.cs`/`CompilationLoader.cs`): that mechanism ALREADY
    discovers an attribute by syntax-node name match (`a.Name.ToString() is "KernelCallable" or
    "KernelCallableAttribute"`, no `AttributeClass` symbol binding, no assembly reference to the
    attribute's defining assembly) rather than requiring the attribute's own assembly loadable into the
    CLI's Roslyn `Compilation` — the exact shape `[Param]` needs. `CompilationLoader.LoadParamFields`
    mirrors `LoadKernelCallables`'s whole-Compilation-return shape (attribute lands on FIELDS, so the
    CLI branch scans `VariableDeclaratorSyntax` nodes directly, matching `IFieldSymbol.GetAttributes()`
    by `AttributeClass.Name is "Param" or "ParamAttribute"` — one step past syntax-only matching since
    fields (unlike --callable-cpp's methods) need the resolved `IFieldSymbol` to read the attribute's
    typed constructor/named arguments cleanly).
  - **New mechanism** (Yeroket `feat/codegen-unif-inc3-param`, branched off Inc-2's
    `feat/codegen-unif-inc2-mapelem`, commit `91a297d6`): `ParamRegistrationEmitter.cs` +
    `EnumExprHelper.cs` (`SourceGenerator~/Transpiler/`, both ported line-for-line from undertow's
    `EmitRegisterParams.Emit`/`EnumExprHelper.EnumExpr` — identical id-sort via
    `OrderBy(Id, StringComparer.Ordinal)`, identical dup-id diagnostic via `HashSet.Add` BEFORE sorting
    that adds a diagnostic but does NOT dedupe), `CompilationLoader.LoadParamFields` (whole-Compilation
    return), `--param-cs` CLI flag in `CodegenTool~/Program.cs` (mirrors `--callable-cpp`'s
    `--schema`/`--out-cs`/`[--check]` shape; discovers fields by attribute name, extracts
    Id/Base/Scope/SeedOnApply the same way `ParamRegistrationGenerator.Extract` does). 3 new NUnit
    tests (`ParamRegistrationCliTests.cs`) plus the full existing 52-test `CodegenTool.Tests` suite —
    all 52 pass (49 pre-existing + 3 new).
  - **Equivalence proof, all 37 real sites + dup-id diagnostic exercised**:
    1. Ran `--param-cs` against undertow's REAL `Undertow.Sim` source tree (not a curated mirror —
       the attribute lives in real production files spread across 8 files/175 `.cs` files scanned):
       output is 37 `reg.Declare` calls, id-sorted, `seedOnApply: true` on exactly
       `extract:efficiency`/`refine:efficiency` (both in `EconomyAttrs.cs`), matching Milestone 1's
       finding exactly.
    2. Diffed the new mechanism's output body against the REAL Roslyn generator's actual emitted file
       (`core/src/Undertow.Sim/obj/.../ParamRegistrationGenerator/DeclareParams.g.cs`, from a fresh
       `dotnet build`): **byte-identical body** (banner line differs by design, same as Inc-2's
       precedent).
    3. Dup-id diagnostic exercised with a deliberately-malformed 3-field scenario (`dup:id` declared
       twice with different Base/SeedOnApply, plus one clean `aaa:solo`): CLI run reports
       `error: [Param] duplicate id 'dup:id'.`, exits 1, does NOT write the output file (mirrors the
       real generator's `UTPARAM001` compile-error diagnostic). A direct `Emit()`-level probe confirms
       the non-dedupe behavior precisely: BOTH `dup:id` entries (Base=1/seedOnApply=false and
       Base=2/seedOnApply=true) remain in the id-sorted output alongside the diagnostic — exactly
       `Emit_RejectsDuplicateId`'s locked-in contract, now covered by
       `Emit_DuplicateId_KeepsBothEntriesSorted_DiagnosticNotDedupe`.
  - **Retirement executed** (undertow `feat/codegen-unif-inc3-param`, commit `966c37b0`, branched off
    `master` in a fresh isolated worktree): `EmitRegisterParams.cs`, `ParamRegistrationGenerator.cs`,
    and the old `EmitRegisterParamsTests.cs` (which tested the retired mechanism, not a downstream
    dependent) deleted; `DeclareParams.g.cs` (the new mechanism's real output against
    `Undertow.Sim`) checked in as an ordinary compiled source file — same "no existing CLI
    build-invocation wiring in undertow" pragmatic choice Inc-2 made. Updated `UndertowSim.cs:461`'s
    stale comment (predated later `[Param]` additions beyond `KnowledgeAttrs`/`EconomyAttrs`) to
    reflect the new provenance. **Note**: this worktree branched off `master`, which does NOT yet
    include Inc-1/Inc-2's own retirements (`feat/codegen-unif-inc1-slots`/`feat/codegen-unif-inc2-mapelem`
    were never merged to master) — `EmitRegistrySlots.cs`/`EmitSharedMapElement.cs`/etc. are still
    present in this worktree; out of scope for Inc-3, unrelated to this retirement's safety.
  - **Consumer verification**: full `dotnet build` of `core/Undertow.sln` — 0 errors, 0 warnings. Full
    `dotnet test` — **2953 tests pass, 0 failures** (2932 `Undertow.Core.Tests` + 21
    `Undertow.Vixen.Host.Tests`; 2955 Inc-2 baseline minus the 2 retired `EmitRegisterParamsTests.cs`
    tests = 2953, consistent). Confirmed zero remaining functional references to the deleted
    `EmitRegisterParams`/`ParamRegistrationGenerator` symbols (2 residual hits are both harmless
    doc-comment cross-references in unrelated files — `EmitRegisterSystems.cs`'s summary comment and
    `EmitRegisterSystemsTests.cs`'s doc comment — no code coupling).
  - No plan-doc assumption disproven.
  - Not yet independently re-verified by an Opus validator as of this write-up — pending dispatch.
