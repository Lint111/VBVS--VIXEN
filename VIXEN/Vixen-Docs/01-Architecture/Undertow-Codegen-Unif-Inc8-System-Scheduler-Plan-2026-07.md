# Undertow Codegen Unification — Increment 8: `[System]` Schedule Solver (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 8 (feature #15 in the survey).
**Why eighth, and a risk-label correction:** the program doc flagged this HARD — "novel algorithm, no
Yeroket analog." Research shows the algorithm itself (Kahn's topological sort, phase-bucketed, stable
tiebreak) is textbook and mechanically portable — NOT a novel algorithm. The real difficulty is that
Yeroket has no `Phase`/`TickPhase`-bucketed scheduling CAPABILITY at all (a net-new concept, not a hard
port), plus a genuine pre-existing design gap in `CodeModLoader.cs`'s mod-load path that this increment
must decide how to handle (see below). Risk label corrected to "novel capability, trivial algorithm."

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **`SystemAttribute`** (`core/src/Undertow.Sim/Systems/SystemModel.cs:16-28`) —
  `[AttributeUsage(AttributeTargets.Class, AllowMultiple=false, Inherited=false)]`. Ctor `(string id)`;
  named args: `Phase` (`TickPhase` enum: Decide/Integrate/Settle/Knowledge/Observe), `Order` (int,
  tiebreak), `Fidelity` (FineOnly/Both/CoarseOnly), `Before`/`After` (`string[]` of other system ids,
  SAME-PHASE only). Behavior facets are separate marker interfaces (`ISystem`, `ISystemSetup`,
  `ISystemCleanup`, `ISystemSave`, `ICoarseSystem`) — the attribute carries only scheduling metadata,
  mirroring `[Action]`'s split (Inc-4).
- **Generator** — `SystemRegistrationGenerator.cs` (Roslyn `ForAttributeWithMetadataName`) extracts
  `SystemDecl`s (`EmitRegisterSystems.cs:12-23`), validates `UTSYS002` (must implement `ISystem` unless
  `CoarseOnly`) and `UTSYS003` (`Both`/`CoarseOnly` must implement `ICoarseSystem`), calls
  `SystemScheduleSolver.Solve`, and emits `RegisterSystems.g.cs` (partial `UndertowSim.
  RegisterGeneratedSystems()`).
- **The algorithm (`SystemScheduleSolver.cs`) — CONFIRMED textbook, not novel.** Stable Kahn's-algorithm
  topological sort, bucketed by phase:
  - Groups decls into a `SortedDictionary<phaseRank, List<SystemDecl>>` via a hardcoded `PhaseRank` map
    (duplicated from `TickPhase` since the analyzer can't reference `Undertow.Sim` — a KEEP-IN-SYNC
    comment flags drift risk, must be ported alongside).
  - Cross-phase `Before`/`After` edges are only VALIDATED (`UTSYS006`: pointing outside phase order is
    an error), never scheduled across phases — the real solve is intra-phase only.
  - Within a phase: builds a successor/indegree graph from `Before`/`After` edges, runs Kahn's with the
    ready-set sorted by `(Order, Id)` each pop for determinism — explicitly commented "O(n² log n) is
    irrelevant, phases hold <20 systems."
  - Cycles (`UTSYS004`) leave nodes with indeg>0; appended degenerately so emission proceeds but the
    diagnostic fails the build. Dangling refs (`UTSYS005`) checked separately up front.
  - The solved rank is baked back as each system's `Order` — `Before`/`After` are compile-time-only
    sugar; the runtime just re-sorts by baked `(Phase, Order)`.
  - Already unit-tested in isolation (`Undertow.Core.Tests/Systems/SystemScheduleSolverTests.cs`) — pure
    C#, zero Roslyn dependency, directly portable/testable outside the compiler pipeline.
- **Real usage: 34 confirmed `[System(...)]` sites** across `core/src/Undertow.Sim/Systems/**` (Economy,
  Knowledge, Diplomacy, Market, FactionAi, Narrative, Bodies, Places, Stock, Builtins). Dependency
  complexity is MODEST: most systems use only `Order` within a phase (no edges at all); only ~10 use
  `Before`/`After`, and every observed case is a LINEAR CHAIN, not a branching DAG (e.g. Knowledge phase:
  `core:coarse-propagate → core:coarse-leak → core:coarse-decay`). No cycles, no diamond dependencies,
  no multi-predecessor nodes found in the current codebase — recheck this in Task 1, don't assume it
  stays true, but today's real data is simple.
- **CodeModLoader.cs safety — CONFIRMED same reflection pattern, PLUS a real pre-existing design gap.**
  Reflects on `[System]`/`ISystem` (lines ~107-118, 362-365), instantiating `ISystem` and wiring
  `Setup`/`Cleanup`/`Save`/`Load` facets — same "discover, don't replace" constraint as
  `[Param]`/`[Action]`/`[Effect]`. **Critical gap**: the mod-load path registers with the type's RAW
  DECLARED `a.Order` — it does NOT call `SystemScheduleSolver` and does NOT process `Before`/`After` AT
  ALL for mod-loaded systems. Declarative ordering only applies at compile time to first-party systems
  today; mods get best-effort raw-Order-only scheduling. **This increment must decide**: replicate this
  asymmetry as-is (safest, matches current behavior exactly) or flag it as a known limitation to fix
  later (out of this increment's scope either way — do NOT silently "fix" it as part of a routine port,
  that would be a behavior change beyond equivalence).

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above (exact site count, dependency-graph shape,
  re-verify no cycles/diamonds exist today). Build a new `--system-cs` CLI discovery path (mirroring
  Inc-4/Inc-7's `CompilationLoader` pattern) + a ported `SystemScheduleSolver` (verbatim algorithm port,
  including the `PhaseRank` KEEP-IN-SYNC map) + an emitter porting `EmitRegisterSystems`'s logic
  line-for-line, including all diagnostics (`UTSYS002`-`UTSYS006`). Prove equivalence for all real sites,
  including at least one real `Before`/`After` chain and explicit cycle/dangling-ref diagnostic tests
  (synthetic, since none occur in real data). Retire `SystemRegistrationGenerator.cs`/
  `EmitRegisterSystems.cs`/`SystemScheduleSolver.cs` if safe. Explicitly document (not silently replicate
  or silently fix) the `CodeModLoader.cs` Order/Before-After asymmetry as a known, unchanged behavior.
- **IS NOT:** touching `CodeModLoader.cs`'s own logic (including NOT "fixing" its Before/After gap for
  mod-loaded systems — that is a separate, explicit decision outside this increment's scope), touching
  the 34 real System classes' behavior, or introducing cross-phase scheduling (the real algorithm doesn't
  do this either — don't add capability beyond what's being ported).

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `SystemModel.cs`, `SystemRegistrationGenerator.cs`, `EmitRegisterSystems.cs`,
  `SystemScheduleSolver.cs`, `SystemScheduleSolverTests.cs`, and `CodeModLoader.cs`'s System-handling
  section fresh in full.
- Recount real `[System(...)]` sites, confirm the dependency-graph shape claim (linear chains only, no
  cycles/diamonds) against the CURRENT real data — flag if this has changed.
- Confirm the `CodeModLoader.cs` Order/Before-After asymmetry precisely — exact behavior for mod-loaded
  systems today.
- **Decide and REPORT**: confirm the mechanism (extend the `CompilationLoader`/dispatch-table-emitter
  pattern from Inc-4/7, PLUS a verbatim `SystemScheduleSolver` port as a small standalone algorithm
  module) — or report a different finding.
- Confirm retirement safety: `CodeModLoader.cs`'s System-handling block is self-contained and doesn't
  call the generator/emitter/solver being retired.

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's decision: `--system-cs` CLI branch, `CompilationLoader.LoadSystemClasses`,
  a ported `SystemScheduleSolver` (verbatim, including `PhaseRank`), and an emitter porting
  `EmitRegisterSystems`'s logic line-for-line — all 5 diagnostics (`UTSYS002`-`UTSYS006`).
- Prove equivalence: same generated `RegisterGeneratedSystems()` body (baked `Order` values matching
  the solver's real output) for all 34 real sites, byte-diffed against the real Roslyn generator's
  actual build output. Exercise: at least one real `Before`/`After` chain, plus SYNTHETIC cycle
  (`UTSYS004`) and dangling-ref (`UTSYS005`) and cross-phase-edge (`UTSYS006`) diagnostic scenarios
  since none occur in real data.
- Retire `SystemRegistrationGenerator.cs`/`EmitRegisterSystems.cs`/`SystemScheduleSolver.cs` (+ its test
  file, migrated/ported alongside if it tests the solver directly rather than the generator) if safe.
  Full `dotnet build` + full `dotnet test` on `core/Undertow.sln`, 0 errors/failures required. Re-verify
  `CodeModLoader.cs` byte-identical pre/post, INCLUDING its Order/Before-After asymmetry unchanged (not
  silently fixed).

## Gates / guardrails
- Non-vacuous proof: real multi-site data plus synthetic diagnostic-path coverage for the 3 diagnostics
  that don't occur naturally in current data.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc8-system`,
  branch `feat/codegen-unif-inc8-system`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 7's tip (`feat/codegen-unif-inc7-effect`) as
  `feat/codegen-unif-inc8-system`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required. `CodeModLoader.cs` byte-identical pre/post retirement diff.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket.
- **Do not silently "fix" the CodeModLoader.cs Order/Before-After asymmetry for mod-loaded systems** —
  this is out of scope; if it seems worth fixing, report it as a follow-up, don't change behavior as a
  side effect of the port.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire. One Sonnet implementer + one Opus
  validator.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in either repo
  - **Corrected site count: 37, not 34** — `Diplomacy/CoarseHistoryResolverSystems.cs` holds 4 system
    classes in one file. A 38th `[System(` hit is test-fixture scaffolding
    (`Undertow.Tests.CodeModFixture/Fixture.cs:36`), correctly excluded from the real count.
  - **Dependency-graph shape confirmed, with a key refinement**: 3 independent linear chains
    (Knowledge/Integrate/Observe phases), no cycles, no diamonds — but the 11 real edges are **100%
    `After`, ZERO `Before`** anywhere in real data (not "~10 mixed" as the plan estimated). Milestone 2
    must add a SYNTHETIC `Before`-only test since real data never exercises that codepath.
  - **`CodeModLoader.cs` asymmetry confirmed exactly**: `RegisterAssemblySeams`'s SYSTEMS block (lines
    106-120) registers mod-loaded systems with raw `a.Order` straight into `SystemDescriptor`, never
    calling `SystemScheduleSolver`, never reading `a.Before`/`a.After` at all. Reported only, per scope —
    not flagged as something to fix in this increment.
  - **Mechanism confirmed**: extend `CompilationLoader` (Yeroket, `feat/codegen-unif-inc7-effect` tip
    `c4691435`, which already has `LoadActionClasses`/`LoadEffectClasses` precedent) with a new
    `LoadSystemClasses`, PLUS a verbatim standalone port of `SystemScheduleSolver.cs`/
    `EmitRegisterSystems.cs` — both confirmed Roslyn-independent (zero `Microsoft.CodeAnalysis` imports)
    — only `SystemRegistrationGenerator.cs`'s Roslyn-attribute-extraction shell gets replaced.
  - **Retirement safety confirmed**: `CodeModLoader.cs` has zero references to the generator/emitter/
    solver being retired.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently re-grepped the site count
    and edge direction/count itself, confirmed the `CodeModLoader.cs` asymmetry by direct line read,
    confirmed both `SystemScheduleSolver.cs`/`EmitRegisterSystems.cs`'s Roslyn-independence and the
    Yeroket branch tip's real precedent state. Cleared to proceed to Milestone 2.
