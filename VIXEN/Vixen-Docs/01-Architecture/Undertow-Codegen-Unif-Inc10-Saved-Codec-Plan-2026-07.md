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
- [ ] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire. One Sonnet implementer + one Opus
  validator.

## Progress Log

(none yet)
