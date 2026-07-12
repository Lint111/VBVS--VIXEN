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
- [ ] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire (if safe). One Sonnet implementer + one
  Opus validator.

## Progress Log

*(none yet — plan authored, not yet dispatched)*
