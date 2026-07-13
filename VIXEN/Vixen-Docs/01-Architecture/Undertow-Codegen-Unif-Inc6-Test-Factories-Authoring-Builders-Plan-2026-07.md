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
- [x] **Milestone 2 (Task 2):** #10 test factories — build + equivalence proof + retire. One Sonnet
  implementer + one Opus validator.
- [x] **Milestone 3 (Task 3):** #11 authoring builders — build + equivalence proof + retire. DONE +
  Opus-validated APPROVED, 2026-07-13. **INCREMENT 6 COMPLETE.**

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

- Milestone 2 (Task 2, #10 test factories — build + equivalence proof + retire): DONE · 2026-07-13
  - Yeroket `TestFactoryEmitter.cs` (`Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/
    Transpiler/`) ports `EmitTestFactory.cs` line-for-line, extending the `RegistrySlotsModel`-based
    carrier-emitter family (reuses `DefCarriersEmitter`'s `DefField`/`ReadFields`/`ReadElementFields`, no
    new field model) — takes `schemas.json` directly (fixed-target shape, mirroring
    `AuthoredKindsEmitter`, since there is no `[RegistrySlots]`-attributed class for this emitter to
    resolve). `--test-factories-cs` wired into `CodegenTool~/Program.cs` mirroring `--authored-kinds-cs`.
    Yeroket branch `feat/codegen-unif-inc6-factories` (off `feat/codegen-unif-inc5-defcarriers`), commit
    `e6c78eef`.
  - **Equivalence proof:** real `dotnet build` in the undertow worktree captured the actual Roslyn
    `Make.g.cs` (159 lines, 30 kinds). Ran the new Yeroket CLI mechanism against the same real
    `schemas.json`; byte-diffed bodies (banner excluded — the two families use intentionally different
    banner text, as already established for the other ported emitters) are **IDENTICAL (0 diff lines)**
    across all 30 real gated kinds.
  - **Field-value positional-correctness proof** (the critical extra check beyond the byte-diff):
    added `MakeFactoryPositionalOrderTests.cs` (`core/tests/Undertow.Core.Tests/TestKit/`) constructing
    `Place` (id/name/kind/body — two same-typed neighbor string fields), `RelationshipKind`
    (id/edge/tag/mutual/rate — two same-typed neighbor string fields + bool/float neighbors), and
    `Resource` (id/name/category/density/conservation — exercises the real-enum path,
    `Undertow.Substrate.Conservation`) with distinct non-default values in every argument position;
    asserted each value lands on its correctly-named property. All 3 tests passed against the
    then-still-live Roslyn-generated `Make.g.cs` before retirement (proving the test harness itself is
    valid), and continued to pass after retirement against the checked-in Yeroket-generated `Make.g.cs`.
  - **Retirement:** deleted `TestFactoryGenerator.cs`/`EmitTestFactory.cs`. Also had to delete
    `TestFactoryCodegenTests.cs` (a unit-test file directly exercising `EmitTestFactory.All` with
    synthetic schemas — not itself a `Make.*` call site, but it could not compile once the emitter was
    deleted; not called out explicitly in the plan but a necessary consequence of the retirement).
    Checked in the Yeroket-generated `Make.g.cs` as an ordinary compiled file under
    `core/tests/Undertow.Content.TestKit/`. Full `dotnet build core/Undertow.sln`: 0 errors. Full
    `dotnet test core/Undertow.sln`: **2949 passed, 0 failed, 0 skipped** (2928 in
    `Undertow.Core.Tests` — includes the ~494 real `Make.*` factory call sites as a large-scale
    regression check — + 21 in `Undertow.Vixen.Host.Tests`). No ordering drift surfaced. Undertow
    worktree `.claude/worktrees/codegen-unif-inc6-factories`, branch `feat/codegen-unif-inc6-factories`,
    commit `4773daf7`.
  - No blockers. Not pushed (per gate).
  - **Opus validator (independent re-verification):** APPROVED. Rebuilt the pre-retirement parent
    commit from scratch to regenerate the real Roslyn `Make.g.cs` and confirmed byte-identical bodies
    independently; independently read `MakeFactoryPositionalOrderTests.cs`'s actual assertions and
    confirmed each uses genuinely discriminating (non-default, distinct-per-adjacent-same-type) values
    that would catch a transposition bug, not values that would pass vacuously; independently re-ran the
    full build+test suite from a clean state (2949/2949, matching); confirmed #11's files untouched by
    this milestone's diff. Cleared to proceed to Milestone 3.

- Milestone 3 (Task 3, #11 authoring builders — build + equivalence proof + retire): DONE · 2026-07-13
  - Yeroket `AuthoringBuilderEmitter.cs` (`Packages/com.yeroket.utility.kernel-framework/
    SourceGenerator~/Transpiler/`) is a new standalone emitter porting `Emit.Builders`/
    `BuilderGenerator.cs` line-for-line — hardcoded `TargetKinds = {"material","role",
    "relationship_kind","place"}` (verbatim, NOT schema-driven), staged fluent wizard
    (`Define.{K}()` → chained required-field marker interfaces → `IReady.Build()`), and the
    UTDL-assembly-then-reparse `Build()` body (`AuthoringProject.LoadContent`, never constructs
    `Authored{K}Def`/`Baked{K}Def` directly). Also emits `CodegenMarker.g.cs` verbatim. Uses its own
    minimal `Field` shape (not `DefCarriersEmitter`'s `DefField`) since `TargetKinds` is flat-scalar-only
    by design — but had to port `Emit.ReadFields`'s enum-collapses-to-scalar behavior (an `enum` field's
    CLR shape = its `backing` scalar): `place.kind` and `role.preference` are both `enum`-kind (not
    `scalar`-kind) in the real schemas.json, and without this collapse both kinds were silently dropped
    during equivalence testing (only `relationship_kind` emitted) — caught and fixed before the proof
    passed. `--authoring-builders-cs` (+ optional `--out-marker-cs`) wired into `CodegenTool~/Program.cs`
    mirroring `--test-factories-cs`. Yeroket branch `feat/codegen-unif-inc6-factories`, commit `53c6ccbc`.
  - **Ground-truth correction vs Milestone 1's count:** `material` is no longer present in the real
    `schemas.json` (removed upstream of this increment) — `TargetKinds` still lists it, and both the
    original Roslyn `Emit.Builders` and the ported emitter silently skip a `TryGetValue` miss. Real
    generated set today = **3 kinds** (`role`, `relationship_kind`, `place`), not 4; `TargetKinds` itself
    is still the 4-name array Milestone 1 confirmed — this is a data-drift fact about schemas.json, not a
    mechanism error.
  - **Equivalence proof:** real `dotnet build` in the undertow worktree captured the actual Roslyn
    `Builders.g.cs` (from `obj/.../generated/...BuilderGenerator/`) + `CodegenMarker.g.cs`. Ran the new
    Yeroket CLI mechanism against the same real `schemas.json`; byte-diffed bodies (banner excluded, same
    convention as prior ported emitters) are **IDENTICAL (0 diff lines)** for all 3 real kinds. The one
    other observed byte difference — Roslyn's `AddSource` emits a UTF-8 BOM on `CodegenMarker.g.cs` that
    plain `File.WriteAllText` does not — is a pre-existing, harmless artifact (confirmed by stripping the
    BOM before diffing: 0 diff lines) and does not affect compiled behavior. CLI `--check` mode against
    the checked-in output returned exit 0 (green).
  - **Real-call-site proof:** the 5 real call sites in `GeneratedFlatBuilderTests.cs`
    (`Role_GeneratedBuilder_RoundTrips`, `RelationshipKind_GeneratedBuilder_RoundTrips`,
    `Place_GeneratedBuilder_RoundTrips`, `Place_RequiredOnly_BodyDefaultsToDeepSpace`,
    `RelationshipKind_RequiredOnly_OmitsOptionals`) all pass unchanged against the checked-in
    Yeroket-generated `Builders.g.cs`, proving the staged-wizard + UTDL-reparse round-trip is correct
    end-to-end (not just text-shape equivalent).
  - **Retirement:** deleted `BuilderGenerator.cs` and only the `Emit.Builders`-specific region of
    `Emit.cs` (`TargetKinds`, `Builders`, `EmitBuilder`, `EmitSupport`, `Render`, `ParamType`,
    `BackingType`) — confirmed via grep that `Emit.cs`'s shared field-model helpers (`ReadFields`,
    `Pascal`, `Camel`, `OptsIntoData`/`OptsIntoCodec`, `Clr`, etc.) are still used by 11 other live
    generator files and left untouched. Checked in the Yeroket-generated `Builders.g.cs`/
    `CodegenMarker.g.cs` as ordinary compiled files under `core/src/Undertow.Authoring/` (SDK-style
    project auto-globs `*.cs`; no `.csproj` edit needed, and the project's existing
    `Undertow.Authoring.Codegen` analyzer reference stays — 11 other live `[Generator]` classes still
    target `Undertow.Authoring`, matching Milestone 2's precedent of leaving now-partially-unused
    analyzer wiring in place rather than touching it). Confirmed hand-written staged builders for
    `planet`/`moon`/`composition_profile` (`Define.cs`) have **zero diff** — completely untouched, as
    required (out of `TargetKinds` scope by design). Full `dotnet build core/Undertow.sln`: 0 errors, 0
    warnings. Full `dotnet test`: **2949 passed, 0 failed, 0 skipped** (2928 in `Undertow.Core.Tests` +
    21 in `Undertow.Vixen.Host.Tests`) — identical pass count to Milestone 2's baseline, confirming no
    regression. Undertow worktree `.claude/worktrees/codegen-unif-inc6-factories`, branch
    `feat/codegen-unif-inc6-factories`, commit `f46db9ed`.
  - No blockers. Not pushed (per gate).
  - **Opus validator (independent re-verification):** APPROVED. Independently confirmed the `material`
    data-drift finding by grepping the real `schemas.json` itself, independently confirmed the
    enum-collapse bug/fix by reading `Emit.cs`'s real logic and the raw schema entries for
    `place.kind`/`role.preference`, rebuilt the pre-retirement commit from scratch to re-derive the
    byte-identical equivalence proof, confirmed the `Emit.cs` surgical removal by diffing and spot-
    checking 2 other still-live generator files, confirmed `Define.cs` zero-diff, and independently
    re-ran the full test suite from a clean state (2949/2949, matching, no regression). **Increment 6
    (#10 test factories + #11 authoring builders) COMPLETE.**
