# View↔Model Binding — Inc-D Plan (2026-07-12)

**Program design:** `View-Model-Binding-Framework-Design-2026-07.md` §6 (set mutation + undo-over-a-set,
critic item 8), §10 item 5 (Inc-D).
**Prior:** Inc-A (`224e393f`) = `IViewDataProvider` seam. Inc-A2 (`60672744`) = editor layer view is a real
RmlUi data-model. Inc-B (`5a7e55ce`) = Gaia-backed provider + `.changed<T>()` reconcile for a single bound
entity. Inc-C (`8269546f`) = `IViewSelectionProvider` seam — a committed `Selected` tag component yields a
stable, ordered set of entities; `SelectionResolvingViewDataProvider` resolves "the Nth selected instance"
to a concrete entity and delegates to Inc-B's provider. **Inc-D is the first increment that MUTATES all N
entities of a selection in one action, with undo over the whole set.**

## Goal

Prove **set mutation + undo-over-a-set**: one declared action, dispatched once, writes to every entity
`IViewSelectionProvider::ids()` yields at dispatch time — "hire 10 selected characters" — with an undo
contract that is **provably correct against the design doc's own resolved hole** (§6, critic item 8):
undo must NOT re-run the live selection query at undo time (the `Selected` set, or entity liveness, may
have drifted since dispatch). Undo restores from a **materialized `(entity, prior-value)` snapshot**
captured at dispatch, not from re-querying "the selection." Dead entities encountered at undo time are
**skipped + logged**, never asserted/crashed on.

The design doc is explicit that this increment "carries real correctness weight — budget it as such (may
need sub-milestones), do not treat undo as a one-liner." This plan follows that instruction: undo-over-a-
set is its own milestone with its own adversarial proof, not a corollary of the forward-mutation milestone.

## Scope boundary (what Inc-D IS and is NOT)

- **IS:** a set-mutation action — given a resolved selection (`IViewSelectionProvider::ids()`), apply one
  write per entity (reuse Inc-B's `GaiaLayerViewDataProvider`/Inc-C's resolving provider — do not
  duplicate read/write logic); a **materialized snapshot** taken at dispatch time: the concrete list of
  `(entity, prior-value)` pairs for exactly the entities the action is about to touch (captured BEFORE the
  forward write, sized to N, not a single footprint — this does not fit `ActionStack::DispatchWithSnapshot`'s
  existing single-footprint-blob shape as-is; Task 1 decides whether to extend `ActionStack` with an
  N-entry snapshot variant or compose N snapshot entries under one `BeginGroup`/`EndGroup` bracket, and
  documents the choice before building); an inverse that iterates the **captured list**, never the live
  query, restoring each entity's prior value; **dead-entity skip+log** at undo (an entity destroyed between
  dispatch and undo is skipped, not restored, not asserted on, and the skip is logged/observable so a test
  can assert it happened); redo that **re-materializes from the forward snapshot** (the post-write values
  captured at dispatch), also never re-querying the live selection.
- **PROOF vehicle:** reuse Inc-C's 3-entity fixture pattern (headless gtest, `LayerMask` component,
  `Selected` tag) — commit a subset (e.g. 2 of 3) to `Selected`, dispatch the set-mutation action, assert
  all and only the committed subset's values changed (the excluded entity's value is untouched — proves
  the action didn't silently touch the whole world); assert undo restores exactly the pre-dispatch values
  for the subset and leaves the excluded entity untouched; assert redo re-applies. **The dead-entity case
  needs its own explicit test**: dispatch over a selection, then destroy one of the touched entities
  (Gaia `del`) BEFORE calling `Undo()`, assert `Undo()` does not crash/assert, restores the surviving
  entities correctly, and the destroyed entity's skip is observable (log line, or a return/count the test
  can check — Task 1 decides the exact observability mechanism and documents it).
- **IS NOT:** a new query-engine ("constraints node" vs. raw Gaia query, §11 — stays open, unchanged from
  Inc-C); UI-driven action dispatch (a real "Hire" button wired into RmlUi — the proof vehicle is a direct
  C++ dispatch call, mirroring every prior increment's "deterministic external write/dispatch" pattern,
  not a UI interaction); transient selection/highlight (unchanged from Inc-C — still out of scope);
  Inc-Ovr's projection/override syntax (Inc-D's mutation is a plain identity write per entity, same shape
  as Inc-B's single-entity write — no projection transform is needed to prove the set/undo contract);
  Inc-E's authoring tooling.
- **Reuse, don't reinvent:** `IViewSelectionProvider` (Inc-C, unchanged) yields the set.
  `GaiaLayerViewDataProvider`/`SelectionResolvingViewDataProvider` (Inc-B/C, unchanged) do the actual
  per-entity read/write. `ActionStack` (Inc-1, `libraries/AppFlow/include/ActionStack.h`) is the undo
  substrate — Task 1 must read it closely: its `Entry`/`Group` shape is currently ONE footprint per entry
  (`void* footprint; uint32_t footprintBytes; std::vector<uint8_t> snapshot`), sized for a single scalar,
  not a list of N `(entity, prior-value)` pairs. Inc-D's core design decision is how the N-sized snapshot
  fits this substrate (see Task 1).

## Tasks

### Task 1 — Ground the shape (READ + REPORT before building)

- Read `ActionStack.h`/`ActionStack.cpp` in full (both `Dispatch` and `DispatchWithSnapshot`, `Entry`,
  `Group`, `Undo()`/`Redo()`) to understand exactly how a group of entries undoes/redoes today.
- Read `SelectionResolvingViewDataProvider.h`, `GaiaViewSelectionProvider.h`, `DirectListViewSelectionProvider.h`
  (Inc-C) and `GaiaLayerViewDataProvider.h` (Inc-B) as the machinery Inc-D composes, unchanged.
- **Decide and REPORT the snapshot-shape question** — two candidate approaches, pick one (or propose a
  better one) and justify:
  1. **Extend `ActionStack` with an N-entry snapshot primitive** (e.g. a
     `DispatchSetWithSnapshot(FlowActionId, std::vector<std::pair<EntityHandle, ValueBlob>> snapshot, ApplyFn apply, ...)`
     or similar) — the set-mutation is ONE `Entry` whose snapshot payload is a list, not a single blob.
  2. **Compose N `DispatchWithSnapshot` calls under one `BeginGroup()`/`EndGroup()` bracket** — reuse the
     EXISTING single-footprint primitive N times (one per selected entity), let the existing group
     undo/redo (which already iterates a group's entries in reverse) provide the "undo the whole set as
     one gesture" property for free.
  - Lean toward option 2 if `ActionStack`'s group mechanism already gives "N entries undo/redo together
    atomically" — it may mean Inc-D needs ZERO changes to `ActionStack` itself, only a caller that loops
    `DispatchWithSnapshot` inside a group. Verify this against the actual `Undo()`/`Redo()` group-iteration
    code before committing to option 1's larger surface. **Prefer the option that touches less existing
    machinery, per the program's own "reuse, don't reinvent" discipline** — but report the concrete reason
    (not just "it's simpler"), since dead-entity skip+log behavior must still work correctly under whichever
    is chosen (verify `DispatchWithSnapshot`'s existing restore path doesn't assert/crash on a dead entity's
    footprint pointer — if the footprint is a raw `void*` into a live entity's component storage, a
    destroyed entity's memory may already be invalid/reused by the time `Undo()` runs; this is the crux of
    the dead-entity hazard and must be resolved with real Gaia semantics, not assumed).
- **Decide and REPORT the dead-entity observability mechanism** — how a test asserts "the destroyed
  entity's restore was skipped, not silently attempted": a log line (VIXEN's `NODE_LOG`/Logger per
  `rules/logging.md`), a return value (e.g. `Undo()` reports a skipped-count), or a counter the test can
  read. Pick the smallest addition that makes the skip provable.
- Confirm Gaia v0.9.2's actual behavior for "does the entity still exist" (a liveness check the restore
  path calls before writing back) — likely `world.valid(entity)` or equivalent; verify the exact API name
  against the vendored Gaia source (`_deps/gaia-src`) or Inc-B/C's existing usage, don't assume a name.

### Task 2 — Set-mutation dispatch

- Implement the set-mutation action: given a resolved `IViewSelectionProvider::ids()` set, apply one
  identity write per entity via the existing provider chain, wrapped per Task 1's chosen `ActionStack`
  shape so the whole set undoes/redoes as one unit.
- The action is a plain identity write (reuse `LayerMask` or Inc-C's existing test component) — no new
  projection/transform logic; this increment proves the set/undo mechanics, not a new binding kind.

### Task 3 — Undo/redo over the captured snapshot (not the live query)

- Implement the inverse: `Undo()` (via whichever `ActionStack` path Task 1 chose) restores each captured
  entity's prior value from the snapshot taken at dispatch — NEVER by re-calling
  `IViewSelectionProvider::ids()` at undo time. Same for `Redo()` — re-applies from the dispatch-time
  forward values, not a fresh selection read.
- Implement dead-entity skip+log per Task 1's chosen observability mechanism: if a snapshot entry's entity
  is no longer live when `Undo()`/`Redo()` runs, skip that entry (do not write, do not assert/crash),
  record the skip observably, and continue restoring the remaining live entries in the same group.

### Task 4 — Prove set-mutation + undo/redo + dead-entity skip, and preserve all Inc-A/A2/B/C gates

- **Set-mutation proof:** 3-entity fixture, subset of 2 committed to `Selected` with distinct prior
  `LayerMask` values; dispatch the set action; assert exactly the 2 selected entities' values changed to
  the new value and the 3rd (unselected) entity's value is untouched.
- **Undo proof:** after the above dispatch, call `Undo()`; assert both selected entities' values are
  restored to their EXACT pre-dispatch values (not just "changed back," but byte/value-identical to what
  was captured); assert the unselected entity is still untouched.
- **Redo proof:** call `Redo()`; assert both selected entities are back to the post-dispatch (new) values.
- **Dead-entity proof (separate test, this is the increment's hardest correctness bar):** dispatch the set
  action over a selection of 2+ entities; destroy ONE of the touched entities (Gaia `del`, or the
  equivalent per Task 1's confirmed API) between dispatch and `Undo()`; call `Undo()`; assert: (a) no
  crash/assert, (b) the surviving entity IS correctly restored to its prior value, (c) the destroyed
  entity's skip is observable per Task 1's chosen mechanism, (d) a subsequent `Redo()` on the same group
  does not attempt to write the dead entity either (symmetric skip on the forward-snapshot side).
- **No-regression, ALL of these must still hold** (mirror Inc-C's Task 4 bar exactly): `test_view_editor_layers_reconcile`
  2/2, `test_editor_toggle_undo_capture` 4/4 byte-identical (compare sha256 against Inc-C's recorded
  values above), AppFlow suite same count as Inc-C's recorded 42/42 (or report any change explicitly),
  `test_view_editor_layers_golden` 2/2, `test_view_selection_provider` 3/3 (Inc-C's own new gate) unchanged,
  Gaia wrapper tests — confirm the SAME pre-existing failure set Inc-C documented
  (`test_gaia_voxel_world` 25/26, `test_gaia_voxel_world_coverage` 31/32; note the 3 other
  `GaiaVoxelWorld/tests` binaries Inc-C flagged as already-failing/crashing — confirm still isolated to
  those, not spread by anything Inc-D touches), byte-guards unchanged (`AppFlow.g.h`/`AppFlowCallables.g.hpp`
  sha256 from Inc-C's recorded values).

## Gates / guardrails

- The set-mutation + undo + redo proof is non-vacuous: distinct per-entity values (a same-value bug would
  hide behind "everything looks equal"), an explicitly UNSELECTED sibling entity proves the action didn't
  touch the whole world, and exact byte/value equality (not just "different from before") on restore.
- The dead-entity proof is mandatory, not optional — this is the specific correctness hole the design doc
  flagged (§6, critic item 8); a plan that ships set-mutation without proving the dead-entity path has not
  closed that hole.
- Undo/redo must be verified to NOT call `IViewSelectionProvider::ids()` (or any live query) internally —
  code-review this explicitly in the validator pass, not just behaviorally (a live-requery could pass the
  happy-path test above and still be wrong if the selection changes between dispatch and undo in a way the
  test doesn't cover — the fix is structural: undo/redo must only ever touch the captured snapshot).
- No TU includes both gaia.h and RmlUi data-model headers (the robin_hood ODR landmine — reuse Inc-B/C's
  bridge-file split pattern for any new bridge this increment needs).
- All Inc-A/A2/B/C gates named in Task 4 still hold, exactly (no new failures, no silently-changed
  pre-existing failure counts without explicit comment).
- Build the fresh worktree's own build dir; confirm Gaia is still pinned to the same version Inc-C built
  against (`_deps/gaia-src` HEAD — check the pin hasn't silently drifted).
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence. Poll long builds actively
  (~20s foreground loop per `vixen-build-policy`), never overlap builds of one target. Only pass a REAL
  CMake target name to `-BuildTarget`/`-Target`.
- Commit in the worktree (pre-blessed). Do NOT push.

## Milestone Map

- [x] **Milestone 1 (Tasks 1-2):** ground the `ActionStack` snapshot-shape decision (Task 1 REPORT-BACK,
  no building until this is confirmed) → implement set-mutation dispatch (Task 2). One Sonnet implementer
  + one Opus validator.
- [x] **Milestone 2 (Tasks 3-4):** undo/redo over the captured snapshot with dead-entity skip+log (Task 3)
  → full proof suite + all regression gates (Task 4). One Sonnet implementer + one Opus validator.
  (Split from Milestone 1 because undo-over-a-set is explicitly the increment's real correctness weight
  per the design doc — it gets its own validator pass rather than being folded into the forward-mutation
  milestone's review.)
- [x] **Milestone 3 (Task 5, NEW — user-flagged 2026-07-12): lossy vs. non-lossy undo policy — CLOSED,
  no gap found.** See "Task 5" below. Investigated whether an entity that is still LIVE but had its
  bound component removed between dispatch and undo was being silently dropped, identically to a truly
  dead entity. Empirical proof found the premise does not hold against the shipped code: this case was
  ALREADY handled correctly by M1/M2's existing unconditional write, with zero policy flag needed. No
  code change to the undo/redo mechanism was made; a permanent regression test locks the finding in.
  One Sonnet implementer, no Opus validator dispatch needed (nothing built to validate; team-lead
  independently re-confirmed the same code-read before closing).

## Progress Log

- Milestone 1 (Tasks 1-2): DONE · commit `12439196` · Opus validator APPROVED · 2026-07-12
  - **Task 1 decision:** compose N `ActionStack::Dispatch()` calls under one `BeginGroup()`/`EndGroup()`
    bracket (option 2) — zero changes to `ActionStack`. Verified against the real `Undo()`/`Redo()` group-
    iteration code (`ActionStack.cpp`): a group already reverts/reapplies its entries atomically (reverse
    order for undo), so "the whole set undoes as one unit" falls out for free. `DispatchWithSnapshot`
    (option 1) was rejected: its raw `void*` footprint is `memcpy`'d with no liveness check, unsafe if
    pointed at Gaia component storage. `GaiaLayerViewDataProvider`'s read/write path never hands out such
    a pointer anyway (`GaiaVoxelWorld::getComponentValue`/`setComponent` take entities by value and
    internally guard on `world.valid(id)`). Dead-entity observability: a `SetMutationResult{selectedCount,
    writtenCount}` return value (smallest addition; no `ActionStack` shape change). Gaia liveness API
    confirmed as `world.valid(entity)` (`gaia::ecs::World::valid`, already used in
    `GaiaArchetypes/ArchetypeBuilder.cpp`/`RelationshipObserver.cpp`); deletion API `world.del(entity)`.
  - **Task 2:** `SetMutationDispatch.h` (`application/editor/include/`) — `DispatchSetMutation()` reads
    the selection via `IViewSelectionProvider::ids()`, and for each live entity with a `LayerMask`
    component, dispatches one `ActionStack::Dispatch(FlowActionId::ToggleLayer, apply)` inside a
    `BeginGroup`/`EndGroup` bracket, reusing `GaiaLayerViewDataProvider` unchanged for the actual write.
    Each per-entity `apply(bool)` lambda captures `(entity, priorValue, newValue)` BY VALUE (never a
    pointer) and re-checks `world.valid(entity)` itself.
  - **Test:** `test_set_mutation_dispatch.cpp` (2/2 PASS) — non-vacuous: distinct per-entity values,
    an explicitly UNSELECTED sibling entity asserted untouched, and a no-component skip path exercised
    without crash/assert. Deliberately does NOT touch undo/redo restore logic or the dead-entity-at-undo
    proof (Milestone 2's scope).
  - **Opus validator (independent re-verification, not just re-reading the report):** read
    `ActionStack.cpp`/`.h`, `GaiaVoxelWorld.h`, `SetMutationDispatch.h`, the test, and the selection-
    provider headers directly; confirmed `Undo()`/`Redo()`'s real iteration order and atomicity,
    `DispatchWithSnapshot`'s real unchecked-`memcpy` hazard, the by-value/no-dangle capture, and that
    `Dispatch()` invokes `apply(true)` exactly once. Rebuilt + ran all three test binaries fresh:
    `test_set_mutation_dispatch` 2/2, `test_view_editor_layers_reconcile` 2/2, `test_view_selection_provider`
    3/3 — all pass, zero regressions. Tree clean, exactly 3 files changed (+254). **APPROVED, no defects,
    no files modified.** Non-blocking note carried to Milestone 2: rebuild ALL named regression gates
    fresh alongside the undo/redo/dead-entity suite (M1's validator reused pre-commit-adjacent artifacts
    for the two regression binaries, valid here since the commit doesn't touch their code paths, but M2
    should not rely on that assumption).

- Milestone 2 (Tasks 3-4): DONE · 2026-07-12
  - **Task 3 mechanism:** read `ActionStack.cpp`'s real `Undo()`/`Redo()` directly (not assumed) —
    `Undo()` pops one `Group`, iterates its `Entry`s in reverse, calls `entry.apply(false)` for
    inverse-mode entries (no `footprint`); `Redo()` pops from the redo stack and calls `entry.apply(true)`
    forward. Neither function touches anything but the `Group`'s own captured closures — no reference to
    `IViewSelectionProvider`, no call to `ids()`, ever. Milestone 1's per-entity `apply(bool)` lambda
    already re-checked `world.valid(entity)` on every invocation (forward AND inverse) before writing, so
    the crash-safety half was already solved; Task 3's addition is purely **observability**: a
    `SetMutationSkipCounters{forwardSkips, undoSkips}` struct, shared via `std::shared_ptr` between every
    per-entity lambda in one dispatch's group (captured by value alongside `entity`/`priorValue`/
    `newValue`), incremented on a dead-entity skip inside the lambda itself. `DispatchSetMutation()`
    returns it embedded in `SetMutationResult::skipCounters`, readable by the caller AFTER calling
    `stack.Undo()`/`stack.Redo()`. Zero changes to `ActionStack`'s public API or `Entry`/`DispatchResult`
    shape — `ActionStack` is shared, generic, reversible-action substrate (its own `test_action_stack.cpp`
    exercises it with a plain `int` flip-lambda unrelated to Gaia/selection) and must not grow a
    dispatch-mutation-specific return type.
  - **Live-query-independence, verified two ways:** (1) code inspection — `DispatchSetMutation()` calls
    `selection.ids(ids)` exactly ONCE, at the top, before building the group; every per-entity `apply`
    lambda captures the resolved `entity` BY VALUE and holds no reference to `selection`/`ids` at all, so
    it is structurally impossible for `Undo()`/`Redo()` (which only ever invoke these captured closures)
    to re-run the live query — confirmed by re-reading `ActionStack::Undo()`/`Redo()` in `ActionStack.cpp`
    directly, not assumed. (2) a dedicated test
    (`UndoNeverReRunsTheLiveSelectionQueryAfterSelectionDrifts`) that mutates the `Selected` tag set itself
    between dispatch and `Undo()` (drops `Selected` from an originally-selected entity, adds it to a
    previously-unselected one) and asserts `Undo()` still restores the ORIGINAL dispatch-time entities,
    ignoring the drifted selection entirely.
  - **Files touched:** `application/editor/include/SetMutationDispatch.h` (added
    `SetMutationSkipCounters`, wired `skipCounters` into `SetMutationResult` and the per-entity lambda;
    doc comments extended, no rewrite of Milestone 1's logic) and
    `libraries/RenderGraph/tests/test_set_mutation_dispatch.cpp` (extended with 3 new tests, existing 2
    untouched) — Milestone 1's file layout choice (extend the existing test file, not a new one) reused.
  - **Task 4 proof suite — all 5/5 tests PASS** (`test_set_mutation_dispatch.exe`):
    1. `WritesExactlyTheSelectedSubsetNotTheWholeWorld` + `DirectListProviderWithoutLayerMaskComponentIsSkippedNotAsserted`
       (Milestone 1's own 2 tests, unchanged, still pass — set-mutation proof already fully covered, not
       duplicated).
    2. `UndoRestoresExactPreDispatchValuesAndRedoReapplies` — undo restores BOTH selected entities to
       their EXACT pre-dispatch values (byte/value-identical, not just "different from new"), unselected
       sibling untouched across both undo and redo; redo re-applies the post-dispatch values; zero skips
       recorded (no dead entities in this scenario).
    3. `DeadEntityBetweenDispatchAndUndoIsSkippedNotCrashedAndSurvivorRestoresCorrectly` — the
       increment's hardest correctness bar: dispatch over 2 selected entities, destroy one (`world.del()`,
       confirmed real Gaia API, same as Milestone 1's Task 1 finding) AFTER dispatch but BEFORE `Undo()`;
       asserts (a) no crash/assert on `Undo()`, (b) the surviving entity restores correctly, (c) the skip
       is observable (`skipCounters->undoSkips == 1`), (d) a subsequent `Redo()` on the same group
       symmetrically skips the dead entity too (`skipCounters->forwardSkips == 1`), doesn't crash, and
       doesn't resurrect the destroyed entity.
    4. `UndoNeverReRunsTheLiveSelectionQueryAfterSelectionDrifts` — direct proof of the "never re-run the
       live query" requirement (design §6, critic item 8): selection drifted between dispatch and undo;
       `Undo()` still restores based on the captured dispatch-time snapshot, not the drifted selection.
  - **Regression gates — ALL rebuilt/run FRESH in this worktree** (not reused from any prior artifact,
    per Milestone 1's validator note):
    - `test_view_editor_layers_reconcile`: 2/2 PASSED.
    - `test_view_selection_provider`: 3/3 PASSED (Inc-C's own gate, unaffected).
    - `test_view_editor_layers_golden`: 2/2 PASSED.
    - `test_editor_toggle_undo_capture`: 4/4 PASSED (after re-running `temp/run_editor_script.bat` fresh
      to prime the windowed-capture log/PNGs, staging `vixen_editor.exe`+`assets` into `VIXEN/binaries/`
      since this worktree's CMakeLists has no `vixen_editor.exe`-mirroring rule — copied straight from
      `build/ninja/binaries/`, not committed, `binaries/`/`temp/` are gitignored). sha256 of the 4
      captures, confirmed BYTE-IDENTICAL to Inc-C's recorded values:
      - `editor_capture_5.png`   = `fde9c268cf5f07f68588b563b908ec84bb9bd134e3bcbc913280152cac6ed8c1`
      - `editor_capture_45.png`  = `e2339aa09f871a0e57f19db22977c0a90aac2303cf4d090100f396553b49b1f8`
      - `editor_capture_75.png`  = `fde9c268cf5f07f68588b563b908ec84bb9bd134e3bcbc913280152cac6ed8c1`
      - `editor_capture_105.png` = `e2339aa09f871a0e57f19db22977c0a90aac2303cf4d090100f396553b49b1f8`
    - AppFlow suite: 42/42, same per-binary breakdown as Inc-B/C's recorded count (`test_appflow_golden`
      7, `test_action_stack` 4, `test_flow_state_machine` 3, `test_binding_store` 3, `test_appflow_loader`
      6, `test_layer_controller` 4, `test_snapshot_undo` 6, `test_input_profile` 2, `test_keychord` 3,
      `test_binding_pattern` 1, `test_flow_return` 2, `test_return_dispatch` 1) — count UNCHANGED.
    - Gaia wrapper tests: SAME pre-existing failures as documented — `test_gaia_voxel_world` 25/26
      (`GaiaVoxelWorldTest.GetPosition` fails) and `test_gaia_voxel_world_coverage` 31/32
      (`GaiaVoxelWorldCoverageTest.CreateVoxelsBatch_AutoParent_ToExistingChunk` fails); the 3 other
      `GaiaVoxelWorld/tests` binaries (`test_voxeldata_integration` 13/14,
      `test_voxel_injection_queue` assert-fails, `test_voxel_injector` crashes) confirmed STILL isolated
      to those same pre-existing conditions, not spread by anything Inc-D touches.
    - Byte-guards: `AppFlow.g.h` sha256 = `b63b2b35a7cc47fbb9ca35d5f7685d2db8907dada2199ad7a1c82c361eb0710b`,
      `AppFlowCallables.g.hpp` sha256 = `989b65e4887e8ffdd1ed44495ac6f38e40039ba7de641cc60dae17435f9836fe`
      — BOTH byte-identical to Inc-C's recorded values, unchanged.
  - **Build-environment gotcha found (worth flagging for future increments in this worktree layout):**
    this git worktree (`.claude/worktrees/view-binding-inc-d`) has its OWN `build.bat`/`build/ninja` at
    its own root, entirely separate from the outer repo root's `build.bat`/`build/ninja` — running
    `build.bat` from the wrong cwd silently builds a DIFFERENT, unrelated source tree (one with no
    knowledge of this branch's new files at all) and reports "success" having built nothing relevant; it
    also explains a reproducible-but-unexplained "ninja: error: unknown target" on a `--target`-scoped
    build immediately after "Re-checking globbed directories" when invoked against the correct tree mid a
    concurrent-build race window — always `cd` into the WORKTREE's own root (not the outer repo root)
    before invoking its `build.bat`, and prefer the untargeted `build.bat build` (full graph) over a
    `--target`-scoped one if a scoped build ever reports an unknown-target error against a target that
    demonstrably exists in `build.ninja`.
  - **Opus validator (independent structural + behavioral re-verification):** read `ActionStack.cpp`'s
    real `Undo()`/`Redo()` and `SetMutationDispatch.h`'s real lambda-capture list directly; confirmed
    `selection.ids(ids)` is called exactly once, no lambda captures `selection`/`ids`, structurally
    ruling out a live-requery. Verified `SetMutationSkipCounters`'s shared_ptr ownership/lifetime and
    forward/inverse increment correctness against `ActionStack::Dispatch()`'s real calling convention.
    Stress-tested the dead-entity test for a Gaia-ID-recycling false-positive (ruled out — no entity is
    added between `del()` and `Undo()`/`Redo()` in that test). Rebuilt the full worktree fresh (confirmed
    `source:` path in the build log), ran all named binaries directly: `test_set_mutation_dispatch` 5/5,
    `test_view_editor_layers_reconcile` 2/2, `test_view_selection_provider` 3/3,
    `test_view_editor_layers_golden` 2/2, AppFlow suite 42/42 (exact per-binary breakdown match), Gaia
    wrapper tests same pre-existing failures. Computed byte-guard sha256 independently — byte-identical
    to Inc-C's recorded baseline. `test_editor_toggle_undo_capture` treated as trusted-from-report (live
    GPU capture, explicitly flagged as an acceptable exception) but paper-checked the claimed PNG sha256
    values against Inc-C's own recorded baseline — match. Tree clean. **APPROVED, no defects, no files
    modified.**

- Milestone 3 (Task 5): CLOSED, no gap found · 2026-07-12
  - **Investigation, before writing any policy code:** read `SetMutationDispatch.h`'s `apply(bool)`
    lambda, `GaiaLayerViewDataProvider::WriteU32`, and `GaiaVoxelWorld::setComponent<T>`
    (`GaiaVoxelWorld.h:609-616`) directly. Found the lambda only gates on `world.valid(entity)` before
    calling `WriteU32`, never on `hasComponent<T>` — and `setComponent<T>` itself calls Gaia's
    `add<T>(id, value)`, which is unconditionally add-OR-overwrite. This meant the plan's stated
    Task 5 gap (entity-alive-but-component-removed silently dropped, identically to a dead entity)
    could not be true as described in the shipped M1/M2 code.
  - **Empirically verified before trusting the code-read:** wrote a throwaway diagnostic test —
    dispatch over 2 selected entities, remove (not destroy) one entity's `LayerMask` component between
    dispatch and undo (confirmed via `valid()==true` + `getComponentValue()==nullopt`), call `Undo()`,
    print the restored state. Result: component WAS re-added, value == the exact captured pre-dispatch
    value (`0x22`), `skipCounters->undoSkips == 0`. Then deleted the diagnostic test (`git status`/
    `git diff --stat` confirmed zero net diff before proceeding).
  - **Reported BLOCKED to the controller** rather than building the originally-proposed
    `enum class UndoLossPolicy` against a disproven premise — the controller independently re-confirmed
    the same `GaiaVoxelWorld.h:609-616` read and the add-or-overwrite semantics, and directed closing
    Task 5 as "no gap found" (option (a) of the 3 offered) rather than reframing it around the
    dispatch-time absent-component case (a real but different, capture-time issue — recorded as a
    Follow-up instead, not built).
  - **Deliverable:** one permanent regression test,
    `ComponentRemovedButEntityAliveIsAlreadyRestoredCorrectlyByUndoNoPolicyNeeded`
    (`test_set_mutation_dispatch.cpp`), locking in today's already-correct behavior (not diagnostic-only,
    unlike the throwaway one above) so it cannot silently regress if `setComponent`'s semantics ever
    change. Zero changes to `SetMutationDispatch.h` itself — no policy enum, no new parameter, no dead
    branches. `application/editor/include/SetMutationDispatch.h` is UNCHANGED from Milestone 2.
  - **Test suite: 6/6 PASS** (`test_set_mutation_dispatch.exe`) — Milestone 1/2's original 5 tests
    unchanged and still passing, plus the 1 new Milestone 3 regression test.
  - **Regression gates — rebuilt fresh (full `build.bat build`, confirmed worktree's own `source:`
    path in the build log) and rerun**:
    - `test_view_editor_layers_reconcile`: 2/2 PASSED.
    - `test_view_selection_provider`: 3/3 PASSED.
    - `test_gaia_voxel_world`: 25/26 PASSED — same single pre-existing failure,
      `GaiaVoxelWorldTest.GetPosition`, name-confirmed identical to M1/M2's documented baseline.
    - `test_gaia_voxel_world_coverage`: 31/32 PASSED — same single pre-existing failure,
      `GaiaVoxelWorldCoverageTest.CreateVoxelsBatch_AutoParent_ToExistingChunk`, name-confirmed
      identical to M1/M2's documented baseline.
    - Not rerun (per the controller's scope: fully isolated to `SetMutationDispatch.h`'s own test file,
      and `SetMutationDispatch.h` itself was not modified): the AppFlow suite, `test_view_editor_layers_golden`,
      `test_editor_toggle_undo_capture` (live GPU capture), byte-guards.
  - Tree clean after commit (verified via `/usr/bin/git status`/`diff` directly, not the rtk-wrapped
    git which masks exit codes).
  - **Opus validator (independent re-verification of the "no gap found" conclusion itself, not just
    the report):** confirmed `setComponent<T>` gates ONLY on `valid(id)` (no component-existence
    check) and the `apply(bool)` lambda has no `hasComponent<T>` pre-check — the structural fact that
    makes the conclusion true. Cross-checked against the vendored Gaia source
    (`gaia-src/include/gaia/ecs/world.h`): the relevant `add()` overload for the component-ABSENT case
    (this test's exact scenario) is well-defined; separately noted, as a side observation (not a
    defect in this milestone), that Gaia's own doc comment calls add-when-component-ALREADY-PRESENT
    "UB" — but that's the M1/M2 normal-dispatch/redo path (already validated + live-gated), not the
    M3 scenario, and is flagged here for the record rather than acted on. Stress-tested the new test
    for a spurious-pass (default-construction masking a real gap, stale-cache reads) — ruled out.
    Additionally reasoned through a "stomp" variant (component removed AND re-added with a DIFFERENT
    value between dispatch and undo) and confirmed undo's unconditional overwrite correctly restores
    the captured snapshot regardless — correct behavior, not a missed case. Rebuilt the full worktree
    fresh, ran all named tests directly: `test_set_mutation_dispatch` 6/6, `test_view_editor_layers_reconcile`
    2/2, `test_view_selection_provider` 3/3, Gaia wrapper tests same pre-existing failures. Tree clean.
    **APPROVED, no defects, nothing changed.** **Holistic Inc-D check (all 3 milestones):** confirmed
    coherent against the plan's own Goal statement — set mutation (M1) + undo/redo over a
    live-query-independent captured snapshot with observable dead-entity skips (M2) + the last open
    scoping question closed by disproof rather than dead code (M3) — no dangling contradiction between
    the three. **Inc-D reads as DONE.**

### Task 5 — Lossy vs. non-lossy undo policy (Milestone 3, NEW) — CLOSED, no gap found

**The gap AS ORIGINALLY STATED (user-flagged 2026-07-12, during Milestone 2's post-implementation
review):** Milestone 1/2's dead-entity skip was believed to be keyed ONLY to `world.valid(entity)`,
treating "entity destroyed" and "entity alive but its bound component was separately removed"
identically — both silently skipping the restore, with no delta preserved.

**Investigation result: this premise does not hold against the shipped code.** Read
`SetMutationDispatch.h`'s `apply(bool)` lambda, `GaiaLayerViewDataProvider::WriteU32`, and
`GaiaVoxelWorld::setComponent<T>` (`GaiaVoxelWorld.h:609-616`):
```cpp
template<typename TComponent>
void GaiaVoxelWorld::setComponent(EntityID id, ComponentValueType_t<TComponent> value) {
    if (!getWorld().valid(id)) return;          // the ONLY gate: entity liveness
    getWorld().add<TComponent>(id, TComponent{value});  // Gaia add() = add-OR-overwrite, unconditionally
}
```
The `apply(bool)` lambda in `SetMutationDispatch.h` checks `world.valid(entity)` before calling
`WriteU32` but never checks `hasComponent<T>` — so it was already relying on `add()`'s add-or-overwrite
semantics. Consequently: an entity that is alive but had its bound component removed (e.g.
`world.getWorld().del<LayerMask>(entity)`) between dispatch and undo is already correctly re-added
with the exact captured prior value by `Undo()`'s existing unconditional write — no policy flag
needed, `undoSkips` stays 0. This was proven empirically (a throwaway diagnostic test, run once, then
removed) before any policy code was written, per this plan's own "report before building" discipline —
see the Progress Log entry below for the exact evidence.

The only path that is (and must remain) lossy is the true-dead-entity case
(`!world.valid(entity)`), which the plan already required stay lossy under any policy — so there was
never a second case for a `UndoLossPolicy` enum to distinguish in the undo/redo path as scoped. Building
the originally-proposed `enum class UndoLossPolicy { SkipMissingData, RestoreMissingData }` would have
added a policy flag whose `RestoreMissingData` branch is behaviorally identical to `SkipMissingData` —
dead code with no distinguishing test possible, since there is no case left for it to gate.

**What WAS real, but is out of Task 5's scope** — see the Follow-ups section: the DISPATCH-time
absent-component skip (`DirectListProviderWithoutLayerMaskComponentIsSkippedNotAsserted`, Milestone 1)
means no snapshot entry is created at all for an entity with no component at dispatch time — there is no
prior value ever captured for it, so no undo-time policy could restore anything for that entity either.
That is a capture-time fact, not a policy choice, and is unrelated to the undo/redo-time case this task
investigated.

**Deliverable:** `ComponentRemovedButEntityAliveIsAlreadyRestoredCorrectlyByUndoNoPolicyNeeded`
(`test_set_mutation_dispatch.cpp`) — a permanent regression test (not diagnostic-only) that dispatches
over a selection, removes the bound component (not the entity) from one touched entity between dispatch
and undo while the entity stays alive, calls `Undo()`, and asserts the component is re-added with the
EXACT captured prior value and `skipCounters->undoSkips` stays 0. Locks in today's already-correct
behavior so it cannot silently regress if `setComponent`'s semantics ever change.

## Follow-ups (explicitly out of scope, note for later increments)

- Inc-Ovr's projection/override syntax, applied to a set-mutation action whose per-entity write is NOT a
  plain identity transform (e.g. "raise salary by 10%" instead of "set to X") — Inc-D's action is a plain
  identity write; a projected set-mutation composes Inc-D + Inc-Ovr later.
- UI-driven set-mutation dispatch (a real "Hire selected" button) — Inc-D proves the mechanism via direct
  C++ dispatch, same pattern as every prior increment.
- The "constraints node" vs. raw Gaia query open decision (§11) — still open, unchanged by Inc-D.
- Inc-E's authoring/lint tooling, once enough binding surface exists across Inc-A through Inc-D.
- **DISPATCH-time absent-component case (observed during Milestone 3, not built):** if a selected
  entity has no bound component (`LayerMask`) at DISPATCH time, `DispatchSetMutation()` never captures a
  snapshot entry for it at all (`DirectListProviderWithoutLayerMaskComponentIsSkippedNotAsserted`,
  Milestone 1) — there is no prior value to restore, ever, for that entity, regardless of any future
  undo-time policy. This is a capture-time fact, not a policy choice. A future increment MAY want a
  caller-supplied default value so such an entity still gets a component added on dispatch (turning this
  from "nothing captured" into "some captured value, even if synthesized") — left unbuilt and
  unspecified here; whoever picks this up should decide the default-value contract fresh rather than
  reusing Milestone 3's (closed, unrelated) `UndoLossPolicy` naming.

## Note

Inc-D is the program's last correctness-heavy increment before tooling (Inc-E): it proves that "one
declared action over a selection set" is not just a loop that happens to work on the happy path, but has
an undo/redo contract that survives the selection or entity set changing between dispatch and undo — the
specific hole the design doc's adversarial review (§12, critic item 8) identified and required be closed
here, not deferred again. Per every prior increment's own discipline: stay narrowly scoped to set-mutation
+ undo-over-a-set; do not build toward Inc-Ovr's projection syntax or Inc-E's tooling in this increment.
