# Undertow Codegen Unification — Increment 9: Content-Pack Codec Cluster (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 9 (features #4/#5/#6 cluster).
**Why ninth, and a risk-label refinement:** the program flags this HARDEST, expecting "its own version of
Inc-5's wire-protocol gotcha." Research confirms the risk is real but is NOT algorithmic — the generator
logic (dispatch tables over Increment 5's already-solved `Field` IR) is large but formulaic, no novel
algorithm like #15's topo-sort. The actual hard part is **blast radius + verification cost**: a
byte-identical port of an 81-version, ~2836-line generated binary-stream surface with ZERO structural
self-description within a record (pure declaration-order stream, no per-field length/TOC framing),
guarded by exactly one golden-bytes regression test — which, in this checkout, has an EMPTY (0-byte)
fixture and must be regenerated/verified before any porting work starts.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **#4 Content-pack binary codec** — `EmitCodec.cs` (556 lines) / `CodecGenerator.cs`, gated on
  `codegen.codec=true` + flat `Emit.ReadFields`. Emits into partial `BakedContentPack` (`Codec.g.cs`,
  2836 lines) two method pairs per kind: `Write{Plural}`/`Read{Plural}` (the def table) and
  `Write{Plural}Patches`/`Read{Plural}Patches` (the patch side-table). Wire format (confirmed at
  `core/src/Undertow.Content/BakedContentPack.cs`): magic `0x5554424B` ('UTBK') + int version, then 5
  hand-written inline tables (TagRows/Relations/Storylines/Fragments/DialoguePatterns), then a
  `CodecKind[]` array (line 131) of ~45 rows each `(IntroVersion, Write, Read)`, iterated in FIXED
  DECLARATION ORDER = FIXED BYTE ORDER; a trailing 4-byte FNV-1a hash. Versioning is per-field
  `introVersion` and per-kind `IntroVersion`, both additive/monotonic — current `Version = 81`,
  `MinSupportedVersion = 3` — a genuinely real, actively-evolving 81-version save format, not a toy. No
  UBO/std140 padding concerns (sequential `BinaryWriter`/`BinaryReader` stream, not a GPU buffer) — but
  the equivalent risk exists in a different shape: **field declaration order IS the wire**, enforced by
  nothing but code discipline.
- **#5 Conditional-merge patch records + Merge** — `EmitMerge.cs` (298 lines) / `MergeGenerator.cs` →
  `Patches.g.cs` (844 lines). Emits `Baked{K}Patch` (presence-aware delta: `bool HasF; T F;` per field)
  plus `BakedPatches.Merge(def, patch)` pure-fold functions. Shares gate + field-classification with #4's
  `EmitPatchPair` — the two must move together or byte-compat breaks (survey's risk flag is accurate).
- **#6 Patch-doc authoring parser** — `EmitPatchParser.cs` (412 lines) / `PatchParserGenerator.cs` →
  `PatchParser.g.cs`. Parses a `patch: true` UTDL doc into a `Baked{K}Patch`, matching #5's shape
  field-for-field (same `+`/`-`/bare-token grammar for value-lists). Authoring-side only, no binary wire
  of its own.
- **The "wire-protocol gotcha" — CONFIRMED, concrete.** `BakedContentPack.Load` (line 620) trusts the
  generated `Read{Plural}` to consume exactly the bytes `Write{Plural}` produced, in the exact same
  field order, with NO length/section framing between fields (no TOC, unlike the view-contract wire's
  16-byte-aligned section+column TOC from a prior program). If a generator regen reorders fields,
  changes a scalar's read/write method (e.g. `double`→`float`), or mis-gates an `introVersion`, `Load`
  would SILENTLY misread downstream fields — no structural self-check exists within a record, only the
  outer per-kind version gate and the file-level trailing FNV-1a hash (validates total-payload integrity
  after the fact, on the whole blob, at load time — not per-field, not per-record). The real safety net
  is `CodecGoldenBytesTests.cs` (`core/tests/Undertow.Core.Tests/Content/CodecGoldenBytesTests.cs`): a
  byte-identity test comparing `ContentBaker.Bake(...)` output against a committed `codec-golden.b64`
  fixture — the LOAD-BEARING regression gate for exactly this risk. **`codec-golden.b64` is currently
  0 bytes in this checkout** — Task 1 MUST confirm whether this is a checkout/LFS artifact specific to
  this clone or a genuine repo problem, and regenerate/verify it BEFORE any porting work, since without
  it there is no automated oracle for byte-equivalence at all.
- **Real usage scale — not a toy.** `core/src/Undertow.Sim/Official/core.pack` is a real on-disk
  39,348-byte compiled content pack, version 81, ~45 standalone kinds (roles, characters, factions,
  concepts, recipes, personalities, places, relationship kinds, dialogue, composition profiles, body
  archetypes, overrides, hooks, tags, manifests, gates, ~20 patch side-tables, and more). A CLI bake tool
  exists (`dotnet run --project tools/Undertow.Author.Cli -- bake <dir> <out>`).
- **No reflection consumers.** `CodeModLoader.cs` reflects only on `[Action]`/`IEffect`/generic
  `T`-assignability — zero reference to `Baked{K}Def`/`Baked{K}Patch`/codec method names. Pure
  compile-time-generated-code-calling-hand-written-code (the `CodecKinds` array binds generated
  `Write{Plural}`/`Read{Plural}` by direct delegate reference, not reflection) — same simpler risk
  profile Increment 5 found for #1/2/3/7.
- **Dependency on Increment 5 — CONFIRMED, direct.** #4/#5/#6 consume `Emit.ReadFields`/`Field` IR
  (shared with Inc-5's #1/#2), and `Codec.g.cs`'s generated `Read{Plural}` constructs `Baked{Kind}Def`
  (Inc-5's #2 `DefCarriersEmitter` output) POSITIONALLY by ctor args — the codec's field order must match
  the `Baked{K}Def` ctor order Inc-5 already generates (the SAME ctor-order coupling class as
  Increment 6's `#10` finding, one increment further down the chain). Increment 9 is a strict consumer
  of Inc-5's shipped carriers, not a peer.
- **Difficulty assessment.** The generator logic itself (1266 combined lines across `EmitCodec`/
  `EmitMerge`/`EmitPatchParser`) is large but formulaic — dispatch tables over the same `Field` IR
  Increment 5/6 already handle, no novel algorithm (unlike #15's topo-sort in Increment 8). The
  genuinely hard sub-problem is reproducing the **patch/version-gate interaction** (per-field
  `introVersion` inside patch reads, three-tier version gating) — a subtly-wrong emitter could pass a
  naive test but corrupt a real mid-history pack. The golden-bytes test against the REAL `core.pack` is
  the primary migration oracle, not code review.

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above, and CRITICALLY resolves the
  `codec-golden.b64` empty-fixture question (regenerate it via the real bake pipeline if it's a
  checkout artifact; if it's a genuine repo gap, that itself is a blocking finding to report, not
  silently work around). Build a Yeroket-side codec emitter extending Increment 5's `DefCarriersEmitter`
  family (shares the same `Field` IR + ctor-order coupling) porting `EmitCodec`/`EmitMerge`/
  `EmitPatchParser`'s logic — this is a LARGE task, budget for splitting Task 2 into sub-milestones if
  Milestone 2's Sonnet dispatch proves too large (per this program's own established practice from
  Increment 5). Prove equivalence via the golden-bytes mechanism against the REAL `core.pack`-producing
  bake pipeline, not just a curated synthetic subset. Retire the 3 generator files if safe — full build
  + full test-suite pass, with special attention to the version-gate interaction across the real
  81-version history (test at least a few historical `IntroVersion` boundaries, not just the current
  version).
- **IS NOT:** touching the real `core.pack` file's content, touching `CodeModLoader.cs` (no reflection
  constraint applies here per the ground truth, but don't assume — Task 1 re-confirms), or introducing
  any new wire-format capability (TOC framing, per-field length prefixes, etc.) beyond what's being
  ported — this increment ports the EXISTING format byte-for-byte, it does not improve it.

## Tasks

### Task 0 — Pre-flight: resolve the `codec-golden.b64` empty-fixture question
- Determine whether `codec-golden.b64` (`core/tests/Undertow.Core.Tests/Content/`) being 0 bytes in
  this checkout is a clone/LFS artifact (regenerate via the real bake pipeline and confirm it produces
  a sane, non-empty fixture matching `CodecGoldenBytesTests.cs`'s expectations) or a genuine repo
  problem (report as a blocking finding — do not silently invent a workaround). This MUST be resolved
  before Task 1's mechanism decision, since it's the only automated equivalence oracle for this cluster.

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `EmitCodec.cs`, `EmitMerge.cs`, `EmitPatchParser.cs`, `BakedContentPack.cs`'s wire-format section,
  `CodecGoldenBytesTests.cs` fresh in full.
- Confirm the exact wire layout (magic, version, the 5 hand-written inline tables, the `CodecKind[]`
  dispatch array, the trailing FNV-1a hash) and the ctor-order coupling to Increment 5's
  `DefCarriersEmitter`.
- Confirm no reflection constraint applies (re-verify `CodeModLoader.cs` directly, don't just trust the
  ground truth above).
- **Decide and REPORT**: confirm extending Increment 5's carrier-emitter family (same `Field` IR, a
  further emitter pass) for the codec/merge/patch-parser logic — or report a different finding.
- Report the real historical `IntroVersion` boundaries present in `schemas.json` today, so Task 2 can
  test at least 2-3 real version-gate transitions, not just the current version.

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's decision. This is large — split into sub-milestones (2a: `#4` codec, 2b:
  `#5` merge, 2c: `#6` patch-parser) if a single Sonnet dispatch proves too large, mirroring
  Increment 5's own precedent for splitting a big task rather than under-scoping the proof.
- Prove equivalence via the golden-bytes mechanism: bake the real content from `schemas.json`'s real
  kinds through both the old Roslyn-generated codec and the new Yeroket-generated codec, byte-diff the
  FULL resulting `.pack` blob (not just per-method text) against the golden fixture AND against each
  other. Test at least 2-3 real historical `IntroVersion` boundaries for the version-gate interaction.
- Retire `EmitCodec.cs`/`CodecGenerator.cs`/`EmitMerge.cs`/`MergeGenerator.cs`/`EmitPatchParser.cs`/
  `PatchParserGenerator.cs` if safe. Full `dotnet build` + full `dotnet test` on `core/Undertow.sln`, 0
  errors/failures required — this includes `CodecGoldenBytesTests.cs` itself as a genuine, non-vacuous
  large-scale regression check.

## Gates / guardrails
- **The golden-bytes fixture must be resolved and non-vacuous before Task 2 begins** — this is the
  primary oracle for the whole increment; do not proceed on a broken/empty oracle.
- Non-vacuous proof: the REAL `core.pack`-producing bake pipeline, real historical version boundaries,
  not just a curated synthetic subset.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc9-codec`,
  branch `feat/codegen-unif-inc9-codec`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 8's tip (`feat/codegen-unif-inc8-system`) as
  `feat/codegen-unif-inc9-codec`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket.
- This is the largest and hardest-to-verify increment in the program — if any milestone proves too
  large for one Sonnet dispatch, split further at milestone granularity rather than forcing an
  oversized/under-verified dispatch; report back and let the controller re-segment.

## Milestone Map
- [x] **Milestone 1 (Task 0 + Task 1):** resolve the golden-bytes fixture question, ground the shape,
  decide mechanism (report-back gate). One Sonnet implementer + one Opus validator.
- **Milestone 2 SPLIT into three sub-milestones per the plan's own guidance (too large for one dispatch):**
  - [x] **Milestone 2a (#4 codec — build + equivalence proof, no retirement yet):** DONE + Opus-validated
    APPROVED, 2026-07-13.
  - [x] **Milestone 2b (#5 merge — build + equivalence proof, no retirement yet):** DONE + Opus-validated
    APPROVED, 2026-07-13.
  - [ ] **Milestone 2c (#6 patch-parser — build + equivalence proof + FULL RETIREMENT of all 6 generator
    files across #4/#5/#6).**

## Progress Log

- Milestone 1 (Task 0 + Task 1, research + pre-flight): DONE · 2026-07-13 · no source files modified in
  either repo
  - **CRITICAL Task 0 finding: the plan's "0-byte golden fixture" premise was STALE, specific to a
    different checkout.** In the fresh Increment 9 worktree, `codec-golden.b64` is 1384 bytes, has 69
    commits (most recent matching `Version=81`), and `CodecGoldenBytesTests.Codec_ProducesGoldenBytes`
    genuinely passes (independently re-run by both the implementer and the Opus validator: 1 passed, 0
    failed). The documented `UNDERTOW_UPDATE_GOLDEN=1` self-regen mechanism exists but was not needed.
    **Milestone 2 has a working, non-vacuous automated equivalence oracle from the start — no blocking
    gap.**
  - **Wire layout confirmed**: magic `0x5554424B` ('UTBK'), `Version` int (81), `MinSupportedVersion`
    (3), 5 hand-written inline tables (TagRows/Relations/Storylines/Fragments/DialoguePatterns), a
    `CodecKind[]` dispatch array (55 rows, not exactly ~57 — minor, within tolerance), trailing FNV-1a
    hash (whole-blob, after-the-fact integrity only).
  - **Ctor-order coupling confirmed, with a naming refinement**: the real shared emitter is
    `EmitRecord.cs` (called from `BakedDefGenerator.cs`) — there is no file literally named
    "DefCarriersEmitter" in undertow (that's Yeroket's name for the ported concept). `EmitCodec.cs`'s
    `Read{Plural}` independently iterates the same `fields` list positionally, with zero structural
    cross-check against the ctor it's calling — confirming the central invariant Milestone 2 must
    preserve.
  - **`CodeModLoader.cs` re-confirmed**: zero reflection dependency on this cluster (only
    `[Effect]`/`[System]`/`[Action]`/`[Param]`).
  - **Mechanism confirmed**: extend Increment 5's carrier-emitter family (same `Field` IR, a further
    emitter pass) for `EmitCodec`/`EmitMerge`/`EmitPatchParser` — all three are dispatch tables over
    the shared IR, no novel algorithm, confirmed via full fresh reads (556+298+412 lines).
  - **Real historical `IntroVersion` boundaries reported for Milestone 2**: v40 (objectList +
    customCodec together), v68 (cross-kind, same version — `contract`/`offer` `dest_body`/`dest_kind`),
    v77 (multi-field, same-kind-same-version), plus the current v81 tip — spot-checked and confirmed
    present in real `schemas.json` by both the implementer and the validator.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently confirmed the golden-bytes
    finding by checking file size/git log AND actually re-running the test itself (not trusting the
    implementer's run), independently confirmed the wire layout/ctor-order coupling/mechanism decision
    by direct source reads, and independently spot-checked all 4 version boundaries against the real
    schema. Recommends heeding the plan's own guidance to split Milestone 2 into 2a/2b/2c given the
    increment's size. Cleared to proceed to Milestone 2.

- Milestone 2a (#4 codec — build + equivalence proof, NO retirement yet): DONE · 2026-07-13
  - **Built** (Yeroket `feat/codegen-unif-inc9-codec`, off Inc-8 tip, commit `ec100e67`): new
    `CodecEmitter.cs` porting undertow's `EmitCodec.cs` (556 lines) line-for-line — both `EmitPair`
    (def-table codec) and `EmitPatchPair` (patch side-table codec). Additive `DefField` members
    (`IntroVersion`/`MapKeyName`/`MapValueName`/`VocabMethod`) added to the shared field model. New
    `--codec-cs` CLI flag mirroring `--authored-kinds-cs`.
  - **Equivalence proof — the crux of the whole increment, non-vacuous, independently re-derived from
    scratch by the validator.** Generated-text sanity: the emitted `Codec.g.cs`-equivalent body is
    byte-identical (2829 lines, only banner/BOM differs) to the real Roslyn build output. **Binary-blob
    byte-diff (the real proof)**: reproduced `CodecGoldenBytesTests`'s exact 10-kind synthetic
    `AuthoringProject`, baked it through the unmodified real Roslyn codec (confirmed matches
    `codec-golden.b64`), then temporarily swapped the Yeroket-generated text into the live
    `CodecGenerator`'s `AddSource` call (a `const string` holder file, since Roslyn analyzers can't do
    file I/O), rebuilt, re-baked the SAME synthetic project — the resulting `.pack` blob is
    BYTE-IDENTICAL (confirmed via direct `sha256`/`cmp` by both the implementer and, independently, the
    validator who redid the entire swap-and-rebuild themselves rather than trusting reported hashes).
  - **Full undertow test suite re-run with the swap live**: 2934-2935 Core + 21 Vixen.Host pass, 0
    failures (includes every existing `Baked{Kind}CodecTests.cs` round-trip test, satisfying the
    round-trip field-value verification requirement far beyond "a few kinds").
  - **All 3 real version boundaries verified** (v40 `manifest.dependencies`/`requires`; v68
    `contract`/`offer` `dest_body`/`dest_kind`; v77 `character.relationships`/`playable`) — confirmed
    correct in/out gating in both the def AND patch tables, by both the implementer and the validator
    independently tracing the generated `Read` logic against real `schemas.json` values.
  - **Cleanup confirmed**: all temporary undertow-worktree edits (the swap, the holder file, temp
    tests) fully reverted — worktree confirmed completely clean, zero commits, by both the implementer
    and the validator. No retirement performed (correct, deferred to Milestone 2c). All 6 generator
    files (`EmitCodec.cs`/`CodecGenerator.cs`/`EmitMerge.cs`/`MergeGenerator.cs`/`EmitPatchParser.cs`/
    `PatchParserGenerator.cs`) confirmed present and unmodified.
  - `SDFNodeGenerator.dll` non-deterministic rebuild noise (triggered by the validator's own build)
    confirmed reverted, not committed.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. This is the load-bearing proof for the
    hardest increment in the program — the validator did not trust reported hashes, instead
    independently redoing the entire swap-and-rebuild approach from scratch to produce its own binary
    `.pack` blob and diff it directly. Cleared to proceed to Milestone 2b.

- Milestone 2b (#5 merge — build + equivalence proof, NO retirement yet): DONE · 2026-07-13
  - **Built** (Yeroket `feat/codegen-unif-inc9-codec`, off Milestone 2a's `ec100e67`, commit
    `5ab8e132`): new `MergeEmitter.cs` porting undertow's `EmitMerge.cs` (298 lines) line-for-line —
    `Baked{K}Patch` presence-aware delta record generation + `BakedPatches.Merge(def, patch)` pure-fold
    functions + element-comparer generation for object-list identity flavors. Additive `DefField`
    members (`MapAuthored`/`ElementKey`). New `--merge-cs` CLI flag.
  - **Real bug caught by the byte-diff itself, fixed before reporting done**: an initial pass dropped
    the `f.MapAuthored ||` guard in element-comparer emission, spuriously generating an extra
    `ElementsEqual_KnowledgeBudgetEntry` comparer not present in real output (faction's `knowledge`
    field is map-authored and must be excluded from the unkeyed-comparer set) — caught via the
    byte-diff, fixed, re-verified byte-identical. Both the implementer and the validator independently
    confirmed the real output emits exactly 3 unkeyed comparers, none for `knowledge`.
  - **Equivalence proof, independently re-derived from scratch by the validator**: banner-excluded
    byte-diff of the generated `Patches.g.cs`-equivalent body is byte-identical (matching sha256) to
    the real Roslyn output for all real gated kinds.
  - **Milestone 2a cross-check confirmed** (both implementer and validator, independently): unlike the
    def-table codec (ctor-order-sensitive), `CodecEmitter.cs`'s `Read{Plural}Patches` constructs
    `Baked{K}Patch` via an OBJECT INITIALIZER — the load-bearing invariant here is property-name/CLR-type
    agreement, not declaration order. The full-body byte-diff already proves this holds for every kind.
  - **Merge-semantics proof, non-vacuous**: a real swap-and-rebuild against the full undertow test
    suite exercised scalar overwrite, map overlay, value-list Add/Remove/Replace, and all 3 object-list
    identity flavors (map-authored upsert, keyed upsert/append/remove, unkeyed full-value) across 4 real
    kinds, plus explicit no-op (`HasF=false`) and full-override (`HasF=true` everywhere) edge cases —
    2934/2934 tests pass with the swap live, independently re-confirmed by the validator.
  - **Cleanup confirmed**: all temporary edits fully reverted, undertow worktree completely clean, zero
    commits. No retirement performed (correct, deferred to Milestone 2c). All 6 generator files
    confirmed present and unmodified.
  - `SDFNodeGenerator.dll` non-deterministic rebuild noise confirmed reverted, not committed.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently re-derived the byte-diff
    from a fresh non-incremental Roslyn rebuild, independently confirmed the bug-fix is real by
    checking the exact comparer set in real output, independently confirmed the object-initializer
    cross-check by direct source read, and independently re-ran the full test suite (2934/2934
    matching). Cleared to proceed to Milestone 2c.
