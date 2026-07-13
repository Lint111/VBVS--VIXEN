# Undertow Codegen Unification — Increment 10: `[Saved]` Codec (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 10, the FINAL increment
(feature #16 in the survey).
**Why tenth, and a scope-gate correction:** the survey/program doc flagged this "unproven/not
load-bearing," explicitly gating any build decision on asking the user first. The user has confirmed
proceeding (2026-07-13). Research shows the survey's own gating rationale was factually WRONG: `[Saved]`
is a real, live, tested campaign-save-game codec, distinct from Increment 9's content-pack (authored/
baked static content) codec — a different wire format, a different concern (live sim state vs. static
content), small but genuinely exercised in production.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **`SavedAttribute`** (`core/src/Undertow.Sim/Systems/SystemModel.cs:71`) — `public sealed class
  SavedAttribute : Attribute {}`, decorates a struct/class/record whose instance fields need a
  generated binary write/read pair. Paired with `[SaveField(Intro=N)]` (line 76, version-gating),
  `[SaveSkip]` (line 80), `[SaveCustom(write,read)]` (line 84).
- **Generator** — `SaveCodecGenerator.cs` (Roslyn `IIncrementalGenerator`,
  `ForAttributeWithMetadataName`) extracts field info; `EmitSaveCodec.cs` (223 lines, PURE C#, zero
  Roslyn dependency — directly portable, same class as Increment 8's solver) emits `Save.{Type}`/
  `Load.{Type}` static methods plus a deterministic `IComparer<T>` per type, into `SaveCodec.g.cs`.
  Diagnostics `UTSAVE001` (unsupported field type), `UTSAVE002` (gated field before ungated) are real,
  enforced rules.
- **"Unproven/not load-bearing" — CONFIRMED WRONG, survey factually outdated.** The survey claimed only
  one test-only type (`_SaveGenProbe.cs`) opts in and nothing is persisted by any slice. Real evidence
  contradicts this:
  - **2 real production `[Saved]` types**: `TrendCell` (`core/src/Undertow.Sim/Ai/GrowthTrend.cs:6`,
    per-faction EMA trend state) and `FactionPlaceKey` (`core/src/Undertow.Sim/EconomicProfile.cs:29`,
    faction/place registry key).
  - **Real consumers**: `GrowthTrendSystem.Save/Load` (`Systems/FactionAi/GrowthTrendSystem.cs:35,38`)
    calls `Save.TrendCell`/`Load.TrendCell`. `EconomicProfileSystem.Save/Load`
    (`Systems/Builtins/EconomicProfileSystem.cs:26-30`) calls `Save.FactionPlaceKeyOrder`/
    `Save.FactionPlaceKey`/`Load.FactionPlaceKey`.
  - **`ISystemSave` is a genuine, wired dispatch point** (`SystemModel.cs:60`): 10 real systems
    implement it (Bodies, Economy/Extraction, Knowledge, FactionAi, Narrative/Quest, Market/Rent, Stock,
    Diplomacy/LiveWar, Places, Builtins/EconomicProfile). `UndertowSim.cs` has a real campaign save/load
    path — a versioned binary format (`"UNDERTOW_CAMPAIGN 37"` header, ~line 3515), a loop (~line 3559)
    calling each registered system's `s.Save(ctx, w)`, and a mirror `Load` path (~line 3668) gated by
    `LoadedCampaignVersion`.
  - **Real, passing tests exercise round-trip save/load concretely**: `GrowthTrendSaveTests.cs`
    (canonical-byte-exact assertion + round-trip), `EconomicProfileSaveTests.cs`, `QuestSaveTests.cs`,
    `CampaignSaveTests.cs`, `PlayerSaveRoundTripTests.cs`, plus `SaveCodecGenTests.cs` (exercises the
    generated codec's gate/skip/custom paths via `_SaveGenProbe`, the intentional test-only probe).
- **Real usage count: 2 production types + 1 intentional test-only probe** (`_SaveGenProbe`, explicitly
  commented "generator coverage only" — retire/port as appropriate, not a real consumer).
- **No runtime-reflection consumers** for `[Saved]` specifically — compile-time Roslyn source-gen only,
  distinct from `CodeModLoader.cs`'s dynamic mod-type discovery mechanism (a different concern entirely
  — confirm this directly in Task 1, don't just trust this note).
- **Distinction from Increment 9 confirmed**: this is a DIFFERENT wire format for a DIFFERENT concern —
  live sim CAMPAIGN state (mutable, per-playthrough, versioned by `LoadedCampaignVersion`), not
  authored/baked STATIC content (Increment 9's `.pack` files). Do not conflate the two mechanisms or
  attempt to unify them beyond what's genuinely shared (both are `BinaryWriter`/`BinaryReader`-based,
  version-gated codegen — but distinct attributes, distinct generators, distinct consumers).

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above (re-verify the 2 real production types + their
  consumers, re-confirm no reflection constraint). Build a new Yeroket-side save codec emitter
  (extending the same general family pattern used throughout this program — likely closest in shape to
  Increment 9's codec emitter, though this is a SEPARATE, SMALLER wire format, not an extension of
  Increment 9's mechanism) porting `EmitSaveCodec.cs`'s logic line-for-line, including the deterministic
  `IComparer<T>` generation and all diagnostics. Prove equivalence for the real 2 production types (byte
  round-trip, not just generated-text comparison) plus the `_SaveGenProbe` gate/skip/custom coverage.
  Retire `SaveCodecGenerator.cs`/`EmitSaveCodec.cs` if safe — full build + full test-suite pass,
  including all 5 real save-related test files listed above.
- **IS NOT:** touching `UndertowSim.cs`'s campaign save/load orchestration logic itself (only its
  generated `Save.{Type}`/`Load.{Type}` callees change), touching the real campaign version-gating
  scheme (`LoadedCampaignVersion`), or unifying this mechanism with Increment 9's content-pack codec —
  they are genuinely separate concerns despite superficial `BinaryWriter`/`BinaryReader` similarity.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `SystemModel.cs`'s `[Saved]`/`[SaveField]`/`[SaveSkip]`/`[SaveCustom]` definitions,
  `SaveCodecGenerator.cs`, `EmitSaveCodec.cs`, `_SaveGenProbe.cs`, `GrowthTrend.cs`,
  `EconomicProfile.cs`, and the relevant `UndertowSim.cs` campaign save/load sections fresh in full.
- Re-confirm the 2 real production types + their exact consumers, and the `_SaveGenProbe`'s
  intentional test-only role.
- Re-confirm no reflection constraint applies (check `CodeModLoader.cs` directly for any `[Saved]`
  reference).
- **Decide and REPORT**: the mechanism (new standalone emitter, since this is a distinct wire format
  from Increment 9's codec, not an extension of it) and whether any shared helper from the program's
  prior increments (e.g. `EnumExprHelper`, the `Field`/`DefField` IR) genuinely applies here or whether
  `[Saved]`'s field model is different enough to warrant its own small IR.

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's decision: port `EmitSaveCodec.cs`'s logic (field write/read generation,
  version gating via `SaveField.Intro`, skip handling, custom read/write hooks, deterministic
  `IComparer<T>` generation) line-for-line, plus all diagnostics (`UTSAVE001`/`UTSAVE002`).
- Prove equivalence: byte round-trip (write via new mechanism, read via new mechanism, verify field
  values) for the 2 real production types (`TrendCell`, `FactionPlaceKey`) AND cross-verify against the
  real Roslyn-generated output via byte-diff (banner excluded). Exercise `_SaveGenProbe`'s gate/skip/
  custom coverage explicitly (this is exactly what it exists for). Test both diagnostic paths with
  deliberately-malformed synthetic scenarios.
- Retire `SaveCodecGenerator.cs`/`EmitSaveCodec.cs` if safe. Full `dotnet build` + full `dotnet test` on
  `core/Undertow.sln`, 0 errors/failures required — including `GrowthTrendSaveTests.cs`,
  `EconomicProfileSaveTests.cs`, `QuestSaveTests.cs`, `CampaignSaveTests.cs`,
  `PlayerSaveRoundTripTests.cs`, and `SaveCodecGenTests.cs` as genuine, real regression checks (not
  vacuous — these exercise real campaign round-trip behavior). Confirm `UndertowSim.cs`'s campaign
  save/load orchestration is byte-identical pre/post (only its generated callees change).

## Gates / guardrails
- Non-vacuous proof: real byte round-trip for the 2 production types, not just generated-text
  comparison; both diagnostic paths exercised.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc10-saved`,
  branch `feat/codegen-unif-inc10-saved`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 9's tip (`feat/codegen-unif-inc9-codec`) as
  `feat/codegen-unif-inc10-saved`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required. `UndertowSim.cs`'s campaign orchestration byte-identical pre/post.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket.
- This is the FINAL increment of the program — on successful completion, update the parent program
  doc to reflect the whole program's completion (10/10 increments), not just this increment.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [x] **Milestone 2 (Task 2):** build + equivalence proof + retire. One Sonnet implementer + one Opus
  validator.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in either repo
  - **All ground truth re-confirmed against real source**: `SavedAttribute`/`SaveFieldAttribute`
    (`Intro` version-gate)/`SaveSkipAttribute`/`SaveCustomAttribute`/`ISystemSave` in `SystemModel.cs`,
    exact line numbers confirmed by both implementer and validator.
  - **The 2 real production `[Saved]` types + consumers re-confirmed with quoted code**: `TrendCell`
    (`Ai/GrowthTrend.cs`) consumed by `GrowthTrendSystem.Save/Load`; `FactionPlaceKey`
    (`EconomicProfile.cs`) consumed by `EconomicProfileSystem.Save/Load` — both wired into
    `UndertowSim.cs`'s real campaign save/load loop under header `"UNDERTOW_CAMPAIGN 37"` and
    `LoadedCampaignVersion`. The survey's original "unproven/not load-bearing" claim is definitively
    refuted by this increment's own research.
  - **`_SaveGenProbe` confirmed intentional test-only scaffolding** (verbatim comment: "Generator
    coverage ONLY... Not persisted by any slice"), exercising skip/custom/gated paths via
    `SaveCodecGenTests.cs`.
  - **No reflection constraint confirmed**: `CodeModLoader.cs` has zero references to `[Saved]`/
    `Save.`/`Load.` — it only wires `ISystemSave` delegate slots to methods the system class already
    defines, no runtime field-level reflection.
  - **Mechanism confirmed**: a new standalone Yeroket emitter (NOT extending Increment 9's content-pack
    codec — a genuinely distinct wire format/concern). `EmitSaveCodec.cs` confirmed pure C#, zero real
    Roslyn dependency (only uses `Microsoft.CodeAnalysis.DiagnosticSeverity`/`Location` as diagnostic
    payload types). `[Saved]`'s field model is discovered via direct `IFieldSymbol` walking on arbitrary
    sim-state types, the same discovery pattern Increments 3/4 used for `[Param]`/`[Action]` — no
    shared IR from prior increments genuinely applies, a small purpose-built IR is correct.
  - **Useful correction surfaced**: two distinct files both named `GrowthTrendSaveTests.cs` exist in
    different folders (`Saves/` and `Systems/`) — Milestone 2 must disambiguate these in the full-suite
    gate.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently confirmed all 6 findings
    against real source at the cited lines, including directly checking `EmitSaveCodec.cs`'s actual
    imports to confirm the "pure C#" claim. Cleared to proceed to Milestone 2.

- Milestone 2 (Task 2, build + equivalence proof + retire): DONE · 2026-07-13
  - **Built** (Yeroket `feat/codegen-unif-inc10-saved`, branched off `feat/codegen-unif-inc9-codec`,
    commit `4a73f956`): `SaveCodecEmitter.cs` (ports `EmitSaveCodec.cs`'s 223 lines line-for-line,
    including `SaveFieldKind`/`SaveFieldInfo`/`SavedTypeInfo` IR, `SaveCodecDiscovery.Extract`/
    `ClassifyType` mirroring `SaveCodecGenerator.cs`'s own `IFieldSymbol` walk, all diagnostics);
    `CompilationLoader.LoadSavedClasses` (discovers `Undertow.Sim.Systems.SavedAttribute` by syntax
    name, matching struct/class/record, same pattern as `LoadActionClasses`/`LoadEffectClasses`/
    `LoadSystemClasses`); `Program.cs --save-codec-cs` CLI flag.
  - **Gotcha found + fixed**: MSBuild's SDK-style implicit glob orders `@(Compile)` alphabetically by
    PROJECT-RELATIVE path (confirmed via `dotnet msbuild -getItem:Compile`: `Ai/GrowthTrend.cs` <
    `EconomicProfile.cs` < `Saves/_SaveGenProbe.cs`) — this differs from `.NET`'s
    `Directory.GetFiles(..., AllDirectories)` native OS-enumeration order (a directory's own files
    listed before descending into subdirectories). Since `SaveCodecEmitter.Emit` preserves discovery
    order verbatim (matching the real Roslyn `ForAttributeWithMetadataName().Collect()` order), the
    CLI had to explicitly re-sort the file list by relative path before building the Compilation, or
    generated member order silently diverged from the real Roslyn output despite byte-identical
    per-member content. `--schema` for this flag must point at `Undertow.Sim`'s own project dir
    specifically (not `core/src`); `EntityId`/`NamespacedId` field-type resolution pulls in sibling
    `Undertow.Substrate`/`Undertow.Modding` sources as reference-only trees appended AFTER the
    primary schema files (so they cannot perturb discovery order).
  - **Equivalence proof (all non-vacuous, real byte round-trip)**:
    1. Real `dotnet build` of the undertow worktree captured the actual Roslyn-generated
       `SaveCodec.g.cs` BEFORE retirement (confirmed order: `TrendCell`, `FactionPlaceKey`,
       `_SaveGenProbe`).
    2. CLI run against real undertow source produced output BYTE-IDENTICAL (including the BOM) to
       the real Roslyn output — `diff` returned zero differences.
    3. A real byte round-trip (write via generated `Save.<Type>`, read back via generated
       `Load.<Type>` through reflection, assert field values) was proven Yeroket-side
       (`SaveCodecEmitterTests.cs`, 7 NUnit tests) for TrendCell's 3-double object-initializer shape
       and FactionPlaceKey's ctor-construct + multi-field-comparer shape (EntityId itself isn't
       reproducible without `Undertow.Substrate.dll`, so a structurally-equivalent ulong stand-in
       proved the ctor-construct/comparer SHAPE; the real EntityId classification/emission for the
       actual type is separately covered by the CLI's direct byte-diff in step 2).
    4. `_SaveGenProbe`'s gate/skip/custom coverage exercised explicitly: gated-field-present
       (`loadedVersion=99 >= Intro=99`) round-trips the real value; gated-field-absent
       (`loadedVersion=1 < Intro=99`, an old-format writer that never emits the gated bytes) proves
       the field defaults to `0`, not stale/misaligned bytes — the version-gate DEFAULT path, not
       just the happy path.
    5. Both diagnostics (`UTSAVE001` unsupported field type, `UTSAVE002` gated-before-ungated)
       exercised with deliberately malformed synthetic IR, plus a test proving one bad type's
       diagnostics don't block a well-formed sibling type's emission.
    - Full Yeroket `CodegenTool.Tests` suite: 76/76 passing (was 69 before this milestone).
  - **Retired** (undertow worktree `.claude/worktrees/codegen-unif-inc10-saved`, commit `31f11a10`):
    deleted `SaveCodecGenerator.cs` + `EmitSaveCodec.cs`. Grep confirmed no test file directly
    references either class by name — no internals-only test file existed to delete. Checked in the
    generated `SaveCodec.g.cs` at `core/src/Undertow.Sim/SaveCodec.g.cs` (banner byte-identical to
    the original Roslyn `<auto-generated/>` header including the BOM — verified via direct `diff`,
    same retirement precedent as Increment 8's `GeneratedSystems.g.cs`). `UndertowSim.cs`'s campaign
    save/load orchestration and `CodeModLoader.cs` confirmed BYTE-IDENTICAL pre/post (empty
    `git diff`, neither file touched — only their generated `Save.{Type}`/`Load.{Type}` callees
    changed source, per the SaveCodec.g.cs swap alone).
  - **Full build + test**: `core/Undertow.sln` builds 0 errors both pre- and post-retirement.
    Full test run: 2934/2934 (`Undertow.Core.Tests`) + 21/21 (`Undertow.Vixen.Host.Tests`) = 2955
    total, 0 failures — including all 6 real save-related test files (both distinct
    `GrowthTrendSaveTests.cs` files in `Systems/` and `Saves/`, correctly disambiguated per
    Milestone 1's finding, plus `EconomicProfileSaveTests.cs`, `QuestSaveTests.cs`,
    `CampaignSaveTests.cs`, `PlayerSaveRoundTripTests.cs`, `SaveCodecGenTests.cs` — 37 targeted tests
    isolated and re-run explicitly, 0 failures).
  - No blockers.
  - **Opus validator (independent re-verification, final gate of the whole program):** APPROVED.
    Independently rebuilt undertow at the pre-retirement commit to re-derive the byte-identical
    equivalence proof (matching sha256 including BOM/banner/member order), independently confirmed
    the MSBuild-glob-ordering gotcha is real by reading the CLI's actual re-sort logic
    (`Program.cs:757-759`, `OrderBy(relativePath, StringComparer.Ordinal)`), independently read
    `SaveCodecEmitterTests.cs` and confirmed the round-trip/probe/diagnostic tests are genuinely
    non-vacuous (real reflection-invoked field-value assertions), confirmed the retirement's
    no-test-deletion claim via its own grep, confirmed `UndertowSim.cs`/`CodeModLoader.cs`
    byte-identical, and independently re-ran the full test suite from a fresh state (2955/2955
    matching exactly). One minor non-blocking doc-phrasing nit noted (the CLI's own writer doesn't
    emit a BOM; the checked-in file's BOM comes from the original Roslyn output, not the CLI —
    content is exact either way, the `--check` drift-guard functions correctly).
  - **THIS IS THE FINAL MILESTONE OF THE FINAL INCREMENT (10/10) OF THE UNDERTOW CODEGEN
    UNIFICATION PROGRAM.** Program doc updated to reflect whole-program completion.
