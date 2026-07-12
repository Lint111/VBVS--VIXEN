# Undertow Codegen Unification — Increment 6: Test Factories + Authoring Builders (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 6 (features #10 + #11).
**Why sixth:** the survey flagged both low-risk; research confirms this holds structurally (no binary
protocol, no novel algorithm — both are thin codegen over the already-solved `Emit.ReadFields` IR), but
their dependency profiles genuinely differ, mirroring the pattern that already surfaced in Inc-4/Inc-5
where a flat "low-risk" survey tag hid a real coupling. Sequenced as two largely-independent milestone
pairs rather than one combined milestone.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **#10 Test factories** — `TestFactoryGenerator.cs` (self-guards to `Undertow.Content.TestKit` assembly
  only) drives `EmitTestFactory.cs` (139 lines), reading `schemas.json` via the shared `Emit.ReadFields`
  IR. Filters kinds where `Emit.OptsIntoData(kindObj) && Emit.OptsIntoCodec(kindObj)` are both true
  (`Emit.cs:318-322`) and skips any kind with a `customCodec` field. Emits `Make.g.cs` → `static partial
  class Make` with one `public static Baked{K}Def {K}(...)` method per opted-in kind, constructing the
  def **positionally in `Emit.ReadFields` order** — documented as "correct by construction" because
  that's the exact ctor order `EmitRecord` (Increment 5 Part A / `DefCarriersEmitter`) emits. Real usage:
  **496 call sites across ~140 test files** under `core/tests/Undertow.Core.Tests/**` (Content/Sim/
  Knowledge/Ai/Constraints suites) plus a dedicated `MakeFactoryTests.cs`. Far larger in scale than the
  survey's one-line note suggests — this is the load-bearing test-data-construction path for most of the
  test suite (test-only, not production/runtime code).
- **#11 Authoring builders** — `BuilderGenerator.cs` self-guards to `Undertow.Authoring` only, calling
  `Emit.Builders(json)` (`Emit.cs`). Hardcoded `TargetKinds = { "material", "role",
  "relationship_kind", "place" }` (`Emit.cs:20`) — NOT schema-driven membership, by explicit design
  comment (excluded kinds encode maps/lists in Text fields, or already have hand-written staged
  builders). Emits `Builders.g.cs`: a staged fluent wizard (`Define.{K}()` → chained required-field
  interfaces → `IReady.Build()`). Critically, `Build()` does NOT construct an
  `Authored{K}Def`/`Baked{K}Def` directly — it assembles a UTDL document string and re-parses it through
  the real hand-written UTDL parser, returning an `AuthoringResult`. Also emits `CodegenMarker.g.cs`
  (trivial wiring canary). Real usage: narrow — 5 call sites, all in one file
  (`core/tests/Undertow.Core.Tests/Authoring/GeneratedFlatBuilderTests.cs`).
- **Dependency on Increment 5 — CONFIRMED for #10, REFUTED (indirect only) for #11.**
  - #10 is a direct, provable dependent of Increment 5 Part A's `Authored{K}Def`/`Baked{K}Def`
    (`DefCarriersEmitter`) — its whole emission strategy ("positional construction in ReadFields order
    is correct by construction") is contractually coupled to that emitter's ctor-parameter ordering. Any
    drift in Increment 5's field ordering breaks #10 silently (wrong values in wrong params) unless
    caught by a test.
  - #11 only depends on the shared `Field`/`ReadFields` IR (used generically by #1-#10) and the real
    UTDL parser — it never references `Authored{K}Def`/`Baked{K}Def` types at all, going through the
    text-authoring round-trip instead. Not a Def-carrier dependent in the same sense #10 is.
- **No reflection consumers found.** `CodeModLoader.cs` reflects only on `[Action]`/`IEffect`/attribute-
  marked mod types (the #12/#13 kernel-generator domain) — nothing reflects on `Make.*` factory method
  shapes or `Define.*`/`Gen{K}Builder` builder shapes; both are called directly/statically only.

## Scope boundary
- **IS:** Milestone-pair 1 (#10): port `EmitTestFactory`'s logic onto Yeroket, likely as a further
  extension of the `[RegistrySlots]`/`DefCarriersEmitter` emitter family (shares the same schema-JSON
  input + `data&&codec` gate predicate) — emit `Make.{K}(...)` factory methods positionally matching
  Increment 5's real ctor order. Prove equivalence for all real gated kinds (positional argument order
  is the critical correctness axis — test this explicitly, not just "compiles"). Retire
  `TestFactoryGenerator.cs`/`EmitTestFactory.cs` if safe — full build + full test-suite pass (496 real
  call sites are the proof's own safety net: any ordering drift fails loudly).
  Milestone-pair 2 (#11): port `Emit.Builders`'s staged-wizard generation onto Yeroket — likely a new
  small emitter (the hardcoded 4-kind membership list + staged-interface-chain generation + UTDL-
  document-assembly-then-reparse `Build()` body is a different generation shape than anything built so
  far). Prove equivalence against the real 4 kinds + the 5 real call sites. Retire `BuilderGenerator.cs`/
  `Emit.Builders` if safe.
- **IS NOT:** touching the real UTDL parser, touching the hand-written staged builders for
  planet/moon/composition_profile (out of `TargetKinds` scope by design), or changing the 496 real test
  call sites' behavior (only their generated callee changes).

## Tasks

### Task 1 — Ground the shape + decide mechanism, BOTH features (READ + REPORT before building)
- Read `TestFactoryGenerator.cs`, `EmitTestFactory.cs`, `BuilderGenerator.cs`, `Emit.Builders`'s section
  of `Emit.cs` fresh in full.
- Recount real gated kinds for #10 (`data && codec`, minus `customCodec`-bearing kinds) and confirm
  `TargetKinds` for #11 (4 kinds, confirm still accurate).
- Confirm the positional-ordering coupling to Increment 5's `DefCarriersEmitter` for #10 — read
  `DefCarriersEmitter.cs`'s real ctor-parameter emission order and confirm `EmitTestFactory`'s argument
  order matches it exactly today.
- **Decide and REPORT** the mechanism for each: #10 as an extension of the existing carrier-emitter
  family (most likely) vs. a standalone emitter; #11 as a new small emitter (most likely, given no
  existing Yeroket mechanism assembles a UTDL-document-string + staged-interface wizard).

### Task 2 — Milestone-pair 1: #10 Test factories (build + equivalence proof + retire)
- Implement per Task 1's decision. Prove equivalence: same generated `Make.{K}(...)` method signatures +
  same positional argument order for all real gated kinds, byte-diffed against the real Roslyn
  generator's actual build output. Explicitly test that argument order is correct (not just "the code
  compiles") — e.g. construct one real def via the new factory and verify field values land in the
  correct properties, not just type-correct positions.
- Retire `TestFactoryGenerator.cs`/`EmitTestFactory.cs` if safe. Full build + full test-suite pass
  required (this suite includes the 496 real call sites — a genuine, large-scale regression check).

### Task 3 — Milestone-pair 2: #11 Authoring builders (build + equivalence proof + retire)
- Implement per Task 1's decision. Prove equivalence: same generated `Define.{K}()` staged-wizard shape
  + same `Build()` UTDL-assembly-and-reparse behavior for the real 4 kinds, exercised against the real 5
  call sites in `GeneratedFlatBuilderTests.cs`.
- Retire `BuilderGenerator.cs`/`Emit.Builders` if safe. Full build + full test-suite pass required.

## Gates / guardrails
- Non-vacuous proof: real multi-kind data; for #10, explicit field-value correctness (not just type/
  compile correctness) given the positional-ordering coupling is the real risk here.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc6-factories`,
  branch `feat/codegen-unif-inc6-factories`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 5's tip (`feat/codegen-unif-inc5-defcarriers`) as
  `feat/codegen-unif-inc6-factories`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required before calling retirement done.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism for BOTH features (report-back gate).
  One Sonnet implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** #10 test factories — build + equivalence proof + retire. One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 3 (Task 3):** #11 authoring builders — build + equivalence proof + retire. One Sonnet
  implementer + one Opus validator.

## Progress Log

- Milestone 1 (Task 1, research-only, both features): DONE · 2026-07-13 · no files modified in either
  repo
  - **#10: 30 net gated kinds** (31 `data&&codec` kinds minus `manifest`, the sole `customCodec` bearer),
    listed by name. **496 real `Make.*` call sites across 156 files** — validator flagged this is a
    loose upper bound (includes some non-factory `Make.*Def(` helpers, e.g. `Make.FactionDef(`); the
    paren-requiring variant is 494. Note for Milestone 2: don't treat "496" as "496 generated-factory
    call sites" 1:1.
  - **#11: `TargetKinds` confirmed unchanged** — `{"material","role","relationship_kind","place"}`
    (`Emit.cs:20`). Exactly 5 real call sites, all in `GeneratedFlatBuilderTests.cs`. Confirmed
    `Define.Planet`/`Define.Moon` are hand-written staged builders (`Define.cs`), NOT part of #11's
    generated scope.
  - **CRITICAL: ctor-order coupling confirmed to hold exactly, traced concretely (not assumed).**
    Yeroket `DefCarriersEmitter.cs:76-78` (Increment 5) builds ctor params in plain, unreordered
    field-array order. Undertow `EmitTestFactory.cs:51` builds the CTOR-CALL args in the same plain
    unreordered order — the only reordering `EmitTestFactory` does is on the factory METHOD's own
    declared parameters (required-first, a C# legality requirement), never on the `Baked{K}Def` ctor
    call itself. Traced `place` and `relationship_kind` end-to-end (implementer + validator
    independently) — identical ctor-arg order on both sides. This is the single riskiest correctness
    axis in the increment and it checks out clean today.
  - **Mechanism confirmed**: #10 extends the existing `RegistrySlotsModel`-based carrier-emitter family
    (a third emitter pass, same model, same `--def-carriers-cs`-style CLI-flag pattern). #11 is a new
    standalone emitter (no existing Yeroket mechanism assembles a UTDL-string + staged-interface wizard
    — confirmed via grep, no prior art).
  - **`CodeModLoader.cs` confirmed** to reflect only on `[Effect]`/`[System]`/`[Action]`/`[Param]`/
    `[ModManifest]` — zero reflection on either feature's generated shapes.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently re-derived the kind count
    via its own Python script, re-traced 2 real kinds' ctor-argument order directly from both emitters'
    source, and confirmed the mechanism decision by grepping for staged-wizard prior art itself. Cleared
    to proceed to Milestone 2.
