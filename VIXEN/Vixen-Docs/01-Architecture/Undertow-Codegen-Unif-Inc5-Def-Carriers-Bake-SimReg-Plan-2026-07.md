# Undertow Codegen Unification — Increment 5: Authored/Baked Def Carriers, Parse/Bake Table, Sim Registration (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 5 (features #1/#2/#3/#7 cluster).
**Why fifth, and why one increment not four:** these are the real dependents Increment 1 deferred
`EmitRegistrySlots.cs` retirement on. All four features share ONE JSON source (`schemas.json`), one
`Emit.ReadFields` IR, and the same "iterate `[RegistrySlots]`'s generated slot list, emit a method/lambda
body per kind" generation pattern Increment 1 already validated — splitting into separate increments
would duplicate JSON-parsing/gate-plumbing work for no real risk-isolation benefit. Tackled as one
increment with an internal two-part sequence (Part A = carriers, Part B = bake table + sim reg, since B
consumes A's generated types).

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **#1 Authored def carriers** — `EmitDefs.cs` (32 lines, shared core) + `EmitRecord.cs` (60 lines,
  shared record-shape emitter) + `AuthoredDefGenerator.cs` (25 lines). For each `schemas.json` kind gated
  `codegen.data=true` with a flat scalar field list (`Emit.ReadFields` non-null), emits `public sealed
  class Authored{Kind}Def` — immutable POCO, all-args ctor, `{ get; }`-only properties — into
  `AuthoredDefs.g.cs` → `Undertow.Authoring`. Field typing handles scalars, enums, vocab-keys, maps,
  object-lists, value-lists, custom-codec escape hatches (`EmitRecord.cs:27-58`).
- **#2 Baked def carriers** — the SAME `EmitDefs.All`/`EmitRecord.Emit` core, invoked with
  `typePrefix="Baked"`, gated on `codegen.codec=true` instead of `data`, into `BakedDefs.g.cs` →
  `Undertow.Content` (`BakedDefGenerator.cs`). Structurally identical mechanism, different gate/
  namespace/target assembly — genuinely ONE generator core parameterized by `typePrefix`, not two
  independent ones. `BakedRegistries.cs:8-40` documents ~15+ per-kind shapes (tag_row, relation,
  character, faction, timeline, relationship_kind, dialogue_opportunity, concept, knowledge_node,
  material, composition_profile, body_archetype, place, personality, hook, rule, effect_set, …).
- **#3 Parse/bake table** — `EmitAuthoredKinds.cs` (113 lines, the most intricate file in the cluster) /
  `AuthoredKindsGenerator.cs`. Emits `AuthoredKinds.All` = `AuthoredKind[]` into `Undertow.Authoring`, one
  entry per kind opted into membership (`data:true` OR a name resolving to a non-Entity `SymbolKind`).
  Each entry bundles `(Type, SymbolKind, LoadBody delegate, Bake delegate)`. The `Bake` lambda is itself
  codegen-generated STRING-TEMPLATED code (`EmitAuthoredKinds.Bake()`, lines 63-110) that field-copies
  Authored→Baked, including nested per-element conversion for non-shared object-list fields — codegen
  emitting a closure body over a dynamic field list, the most novel generation shape in this cluster.
- **#7 Sim registration** — `EmitRegisterKinds.cs` (61 lines) / `RegisterKindsGenerator.cs`. Emits
  `RegisterKinds.All` = `RegisterKind[]` into `Undertow.Sim`, one entry per kind gated `simRegister=true`
  (a THIRD independent gate, distinct from `data`/`codec`). Each entry is a `(sim, baked) => { foreach (x
  in baked.{Plural}) sim.Content.Register(x.Id, ..., out _); }` closure, with an optional
  `RuntimeCarrier` conversion (`Emit.cs:332`) when the sim wants a different runtime type than the baked
  POCO. **Confirmed distinct from Increment 1's `[RegistrySlots]`**: #7 registers baked instances already
  sitting in the slots into `sim.Content`'s runtime registry; `[RegistrySlots]` only creates the `List<T>`
  properties that hold them. Adjacent, cooperating layers, not the same feature re-surfacing.
- **Dependency chain on Increment 1 — CONFIRMED, concretely, real load-bearing consumption:**
  - `EmitAuthoredKinds.Bake()` generates `foreach (var x in a.{slot}) ... b.{slot}.Add(new
    Baked{P}Def(...))` — `a.{slot}`/`b.{slot}` are literally `[RegistrySlots]`-generated
    `List<Authored{K}Def>`/`List<Baked{K}Def>` properties.
  - `ContentBaker.cs:37` — real call site: `foreach (var k in AuthoredKinds.All) k.Bake(authored.Registries, reg);`
  - `EmitRegisterKinds.All()` generates `foreach (var x in b.{plural}) sim.Content.Register(...)` — again
    directly over a `[RegistrySlots]` slot.
  - `ContentLoader.cs:66` — real call site: `foreach (var k in RegisterKinds.All) k.Register(sim, baked);`
  - `ContentLoader.cs:57` — `foreach (var id in baked.OverridableIds())` — directly consumes Increment
    1's generated `OverridableIds()` method, confirming #7 is a real, live dependent exactly as
    Increment 1's plan doc anticipated.
- **No CodeModLoader-style runtime-reflection safety constraint applies to this cluster.**
  `core/src/Undertow.Sim/CodeModLoader.cs` reflects only on `[Action]`, `IEffect`, and generic
  `T`-assignability (lines ~401/424/436) — zero reference to `Authored{K}Def`/`Baked{K}Def`/
  `AuthoredKind`/`RegisterKind` shapes. This is pure compile-time codegen-to-codegen consumption
  (generated table → hand-written call site → other generated types), not reflection over a
  hand-authored attribute contract — a genuinely different (and simpler) risk profile than Inc-3/Inc-4.
- **Scale**: `schemas.json` is 378 lines; 38 kinds opt into `codegen` (recheck exact count in Task 1).
  4 generator .cs files + 2 shared emitter files (`EmitDefs.cs`, `EmitRecord.cs`) = 6 files total for this
  cluster (excluding shared IR like `Emit.cs`/`Json.cs`/`SchemaKeys.cs` already touched by Increment 1).
  Three INDEPENDENT gates in play: `data` (#1), `codec` (#2), `simRegister` (#7); #3 gates on `data OR`
  a named-symbol-kind check.

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above (exact kind count, exact gate combinations
  present in real data). Part A (Task 2): port `EmitDefs`/`EmitRecord`'s core (parameterized by
  typePrefix/gate) onto Yeroket — likely as an extension of Increment 1's `[RegistrySlots]` emitter
  family (shares the same schema-JSON-pointer input model) rather than a wholly new mechanism; emits
  both `Authored{K}Def`/`Baked{K}Def` POCOs. Part B (Task 3): port `EmitAuthoredKinds`/
  `EmitRegisterKinds`'s method-table generation (the harder Bake-lambda templating + the simpler
  Register-lambda), consuming Part A's generated types + Increment 1's slot/`OverridableIds()` shape.
  Prove equivalence for real schema data (all 38-ish kinds, all real gate combinations). Retire all 6
  cluster files (`EmitDefs.cs`, `EmitRecord.cs`, `AuthoredDefGenerator.cs`, `BakedDefGenerator.cs`,
  `EmitAuthoredKinds.cs`/`AuthoredKindsGenerator.cs`, `EmitRegisterKinds.cs`/`RegisterKindsGenerator.cs`)
  if safe — full build + full test-suite pass. **Also retire Increment 1's deferred
  `EmitRegistrySlots.cs`/its generator now**, since this increment is exactly the dependent migration
  Increment 1 was waiting on — confirm in Task 1 that after Parts A+B migrate, nothing else still calls
  the old `EmitRegistrySlots` output.
- **IS NOT:** touching `ContentBaker.cs`/`ContentLoader.cs`'s own call-site logic (only their generated
  callees change), touching the real game-content schema data itself, or designing this cluster's
  mechanism to also cover future increments (#4/#5/#6 content-pack codec cluster, #10/#11
  factories/builders) beyond genuine natural reuse — don't over-generalize preemptively.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `EmitDefs.cs`, `EmitRecord.cs`, `AuthoredDefGenerator.cs`, `BakedDefGenerator.cs`,
  `EmitAuthoredKinds.cs`, `AuthoredKindsGenerator.cs`, `EmitRegisterKinds.cs`,
  `RegisterKindsGenerator.cs` fresh in full.
- Recount real kinds opted into each of the 3 independent gates (`data`, `codec`, `simRegister`) plus #3's
  `data OR named-symbol-kind` predicate — confirm the ~38-kind estimate and list the gate-combination
  matrix (which kinds are in which of the 4 features).
- Confirm Increment 1's `[RegistrySlots]` is a real, sufficient foundation for Parts A+B to build on (its
  slot properties + `OverridableIds()` + `MergeGeneratedFrom` are what #1/#2/#3/#7 actually consume) —
  or report if a gap exists.
- Confirm no CodeModLoader-style (or other) runtime-reflection constraint applies to any of the 4
  features' generated shapes.
- Confirm Increment 1's `EmitRegistrySlots.cs` retirement is finally safe to execute in this increment
  (no other caller beyond what Parts A+B will migrate).
- **Decide and REPORT**: confirm the A-then-B two-part sequencing (carriers, then bake-table+sim-reg)
  and the specific Yeroket-side mechanism for each part — most likely extending `[RegistrySlots]`'s
  existing emitter family for Part A, and a new small emitter(s) for Part B's method-table generation
  (mirroring Increment 1's own `MergeGeneratedFrom`/`OverridableIds()` method-body-generation shape).
  Justify against real code, not just this plan's summary.

### Task 2 — Part A: Authored/Baked def carriers (build + equivalence proof)
- Implement the carrier emitter (parameterized by typePrefix `Authored`/`Baked` and gate `data`/`codec`),
  porting `EmitDefs.All`/`EmitRecord.Emit`'s field-typing logic (scalars, enums, vocab-keys, maps,
  object-lists, value-lists, custom-codec escape hatches) line-for-line.
- Prove equivalence: same generated `Authored{K}Def`/`Baked{K}Def` shapes for all real gated kinds.

### Task 3 — Part B: Parse/bake table + sim registration (build + equivalence proof + retire)
- Implement the bake-table emitter (`AuthoredKinds.All`, including the templated `Bake` lambda body
  generation for nested object-list field conversion) and the sim-registration emitter (`RegisterKinds.All`,
  including the optional `RuntimeCarrier` conversion path), porting `EmitAuthoredKinds`/
  `EmitRegisterKinds`'s logic line-for-line.
- Prove equivalence: same generated `AuthoredKinds.All`/`RegisterKinds.All` tables for all real gated
  kinds, exercising at least one real kind with a nested object-list field (personality/hook-style) and
  one real kind using the `RuntimeCarrier` conversion path.
- Retire all 6 cluster files + (finally) Increment 1's deferred `EmitRegistrySlots.cs`/its generator, if
  Task 1 confirmed safety. Full `dotnet build` + full `dotnet test` on `core/Undertow.sln`, 0
  errors/failures required. Confirm `ContentBaker.cs`/`ContentLoader.cs` call sites are byte-identical
  pre/post (only their generated callees' assemblies change, not the hand-written call-site files).

## Gates / guardrails
- Non-vacuous proof: real multi-kind data covering every gate combination present in real
  `schemas.json`, not just a curated happy-path subset.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc5-defcarriers`,
  branch `feat/codegen-unif-inc5-defcarriers`. Do not touch the main checkout or any other worktree. Do
  NOT push. Commit as work completes.
- Yeroket-side work branches off Increment 4's tip (`feat/codegen-unif-inc4-action`) as
  `feat/codegen-unif-inc5-defcarriers`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required before calling retirement done. `ContentBaker.cs`/`ContentLoader.cs` byte-
  identical pre/post retirement diff (their call sites, not their compiled callee assembly).
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket — never commit it
  unless a real source change to that generator was made; discard via
  `git checkout -- Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll`.
- This is the largest increment so far (3 tasks, not 2) — if Task 2 or Task 3 proves too large for one
  Sonnet dispatch, split further at milestone granularity (e.g. Milestone 2a/2b) rather than forcing one
  oversized dispatch; report back and let the controller re-segment rather than silently under-scoping
  the proof.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [x] **Milestone 2 (Task 2):** Part A — authored/baked def carriers, build + equivalence proof. One
  Sonnet implementer + one Opus validator.
- [x] **Milestone 3 (Task 3):** Part B — parse/bake table + sim registration, build + equivalence proof +
  retire (all 6 cluster files + Increment 1's deferred `EmitRegistrySlots.cs`). DONE + Opus-validated
  APPROVED, 2026-07-13. **INCREMENT 5 COMPLETE.**

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in either repo
  - **Corrected per-feature kind counts (not a flat ~38)**: 38 total kinds in `schemas.json`, but
    `data=true`→31 (#1), `codec=true`→36 (#2), `simRegister=true`→28 (#7), AuthoredKinds membership
    (`data:true OR named-symbol`)→32 (#3). Gate-combination matrix: (T,T,T)=28 kinds (the common case);
    (T,T,F)=3 kinds (`manifest`, `place`, `tag` — in #1/#2/#3 but excluded from #7); (F,T,F)=5 kinds
    (`dialogue_pattern`, `fragment`, `relation`, `storyline`, `tag_row` — codec-only); `role` is the sole
    real (F,F,F) peer — `planet` is excluded upstream by the `ReadFields!=null` flat-scalar co-gate (its
    `orbit` field is a non-generatable `block` kind), not a genuine (F,F,F) bucket member. Opus validator
    independently re-derived the entire matrix via a Python script against the real `schemas.json` and
    confirmed every count exactly, flagging only this planet/role bucket-labeling nuance (not a numeric
    error) — Milestones 2/3's equivalence proofs must account for the `ReadFields!=null` co-gate
    explicitly to keep "all real gate combinations" coverage honest.
  - **`[RegistrySlots]` (Increment 1) confirmed sufficient**: slots, Patches, `MergeGeneratedFrom`,
    `OverridableIds()` all present and real. Real consumers confirmed: `ContentBaker.cs:37`,
    `ContentLoader.cs:57,66`, `AuthoringModel.cs:192`, plus a second `OverridableIds()` call site at
    `UndertowSim.cs:159` (beyond this plan's original ground truth). No gap found.
  - **`CodeModLoader.cs` confirmed to have zero reflection dependency** on this cluster's generated
    shapes (`Authored{K}Def`/`Baked{K}Def`/`AuthoredKind`/`RegisterKind`) — pure compile-time
    codegen-to-codegen consumption, no runtime-reflection safety constraint (unlike Inc-3/Inc-4).
  - **`EmitRegistrySlots.cs` retirement: SAFE, with one concrete addendum.** Only 2 production call
    sites (`RegistrySlotGenerator.cs:23,26`). ADDITIONALLY found:
    `tests/Undertow.Core.Tests/Authoring/RegisterKindsCodegenTests.cs:49` directly unit-tests
    `EmitRegistrySlots.All` itself (not just its generated output) — this test must be migrated/retired
    in Milestone 3 alongside the source file, or retirement breaks the build. Folded into Milestone 3's
    scope.
  - **Mechanism confirmed**: A-then-B sequencing is a hard ordering dependency (Part B's generated
    `Bake()`/`Register()` lambdas reference Part A's `Authored{K}Def`/`Baked{K}Def` types by name — not
    just a risk-isolation choice). Part A extends the existing `[RegistrySlots]` emitter family (same
    attribute/model, a second emitter pass, no new attribute). Part B needs genuinely new small emitters
    (`AuthoredKindsEmitter`, `RegisterKindsEmitter`) — confirmed by the Opus validator surveying all 27
    existing Yeroket emitters, none of which emits an array-of-closures method table.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently re-derived the full gate
    matrix from real `schemas.json` via a Python script (not trusted from the report), verified all
    cited file:line claims directly, confirmed the Yeroket-side `[RegistrySlots]` mechanism's real shape
    on the Inc-4 tip, and confirmed Part B's "genuinely new emitter" claim by surveying all 27 existing
    Yeroket emitters. Cleared to proceed to Milestone 2.

- Milestone 2 (Task 2, Part A — Authored/Baked def carriers): DONE · 2026-07-13
  - **Built** (Yeroket `feat/codegen-unif-inc5-defcarriers`, off Inc-4 tip, commit `2e61ba3c`): new
    `DefCarriersEmitter.cs` — a second emitter pass over the existing `RegistrySlotsModel` (no new
    attribute), porting `EmitDefs.All`/`EmitRecord.Emit`'s field-typing logic (scalar/enum/vocab-key/map/
    object-list/value-list/custom-codec) line-for-line, including the real `ReadFields!=null` flat-scalar
    co-gate found in Milestone 1. New `--def-carriers-cs <ClassName>` CLI flag, invoked once per
    Authored/Baked target.
  - **Equivalence proof — non-vacuous, PASSED, independently re-derived by the validator from scratch**:
    real `dotnet build` of undertow regenerated the actual Roslyn `AuthoredDefs.g.cs` (31 kinds)/
    `BakedDefs.g.cs` (36 kinds); new mechanism run against the same real `schemas.json` produced 31/36;
    banner-stripped diff IDENTICAL for both sets. `planet` confirmed absent from both (co-gate working).
    All 7 field-typing paths confirmed present + correctly typed in the byte-identical real output
    (scalar, real-enum `EdgeKind`, vocab-key `EffectKey`, map/shared-element, object-list
    `EffectSetEntry`, value-list, custom-codec `ConstraintNode?` on `manifest`).
  - **No retirement this milestone** (correct, deferred to Milestone 3) — `EmitDefs.cs`/`EmitRecord.cs`/
    `AuthoredDefGenerator.cs`/`BakedDefGenerator.cs` confirmed still present, unmodified, in the undertow
    worktree; the worktree itself confirmed completely clean (zero diff vs `master`).
  - `SDFNodeGenerator.dll` non-deterministic rebuild noise discarded, confirmed not in the commit.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Fully re-derived the equivalence proof
    from scratch (own build harness, real Roslyn rebuild, real `--def-carriers-cs` run, own diff) rather
    than trusting the implementer's description; also independently spot-checked the
    `ReadFields`/`EmitRecord.Emit` port against undertow's real source. Cleared to proceed to Milestone 3.

- Milestone 3 (Task 3, Part B — bake table + sim registration + FULL RETIREMENT): DONE · 2026-07-13
  - **Built** (Yeroket `feat/codegen-unif-inc5-defcarriers`, on top of Milestone 2's `2e61ba3c`, commit
    `03e813fa`): `AuthoredKindsEmitter.cs` + `RegisterKindsEmitter.cs` (line-for-line ports of
    `EmitAuthoredKinds.All`/`EmitRegisterKinds.All`, fixed-namespace CLI generation — no
    `[RegistrySlots]`-attributed class to resolve, unlike Part A). New `--authored-kinds-cs`/
    `--register-kinds-cs` CLI flags.
  - **Equivalence proof — non-vacuous, PASSED, fully closed the loop.** Real fresh `dotnet build` at the
    pre-retirement commit captured the actual Roslyn output for all 6 cluster `.g.cs` files
    (`AuthoredKinds`, `RegisterKinds`, `RegistrySlots`×2, reusing Milestone 2's already-proven
    `AuthoredDefs`/`BakedDefs`) — banner-excluded diff IDENTICAL for all: 32 (AuthoredKinds), 28
    (RegisterKinds), 31+36 (RegistrySlots Authoring+Content). Validator additionally built and ran the
    NEW Yeroket emitters directly against the real schema and confirmed their output is fully identical
    (banner included) to the checked-in files — closing the loop: new-emitter == checked-in ==
    old-generator. Nested object-list Bake-lambda coverage confirmed (`hook`/`personality`/`effect_set`/
    `rule`/`concept`, `building` doubly-nested). RuntimeCarrier conversion path confirmed
    (`character`/`faction`/`timeline`, 3 of 28 real kinds using it).
  - **Retired, all 10 files confirmed gone** (undertow worktree, commit `60106236`): `EmitDefs.cs`,
    `EmitRecord.cs`, `AuthoredDefGenerator.cs`, `BakedDefGenerator.cs`, `EmitAuthoredKinds.cs`,
    `AuthoredKindsGenerator.cs`, `EmitRegisterKinds.cs`, `RegisterKindsGenerator.cs`, and — finally —
    Increment 1's long-deferred **`EmitRegistrySlots.cs`/`RegistrySlotGenerator.cs`**, this increment
    being exactly the dependent migration Increment 1 was waiting on. 6 generated `.g.cs` files checked
    in as ordinary compiled source.
  - **Test surgery (highest-risk claim, independently re-verified method-by-method)**: 3 test files fully
    deleted (tested only retired APIs, zero surviving coverage need) + 13 files surgically trimmed
    (removed only the `EmitDefs.All`/retired-generator-calling method, left surviving `EmitCodec`/
    `CodecGen_*` tests untouched) — exactly 22 `[Fact]`/`[Theory]` methods removed net, recounted
    independently by the validator via `git show` before/after diffing, not trusted from the report.
  - **Call-site byte-identity confirmed**: `ContentBaker.cs`, `ContentLoader.cs`, `AuthoringModel.cs`,
    `UndertowSim.cs` all zero-diff pre/post retirement.
  - **Full build + test, post-retirement, independently re-run from fresh by the validator**: 0
    errors/warnings, **2933 pass, 0 failures** (2912 `Undertow.Core.Tests` + 21
    `Undertow.Vixen.Host.Tests`).
  - `SDFNodeGenerator.dll` confirmed not committed with unrelated noise.
  - No blockers. Increment 5 (all 3 milestones, the largest increment in the program) COMPLETE in both
    repos, not yet merged to either main/master, not pushed (per gate).
  - **Opus validator (independent re-verification):** APPROVED. This was the riskiest milestone in the
    program to date (10 files retired, 3 test files deleted, 13 surgically trimmed) — held up on every
    axis under full independent re-derivation, including closing the full equivalence loop from scratch.
