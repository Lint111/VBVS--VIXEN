# View↔Model Binding — Inc-C Plan (2026-07-12)

**Program design:** `View-Model-Binding-Framework-Design-2026-07.md` §6 (instance identity + multi-instance
selection), §9 (seams summary), §10 item 4 (Inc-C).
**Prior:** Inc-A (`224e393f`) = `IViewDataProvider` seam + direct-field provider. Inc-A2 (`60672744`) =
editor layer view is a real RmlUi data-model. Inc-B (`5a7e55ce`, merged) = Gaia-backed provider +
`.changed<T>()` reconcile for a SINGLE bound entity (the editor's one layer-mask entity). Inc-C is the
first increment where a view/action binds to **one of MANY** entities, not a single fixed one.

**Inc-C goal:** prove multi-instance selection — a committed (low-frequency, user-intentional) selection
of Gaia entities, expressed as a runtime tag component, with a seam (`IViewSelectionProvider`) that yields
the selected set so a bound view/action can address "the Nth selected instance" instead of a single
hardcoded entity. This is the prerequisite for Inc-D (set mutation over a selection).

## Naming disambiguation — READ THIS FIRST
There is an **unrelated, already-shipped** "Selection System" in this codebase
(`Selection-System-Design-2026-06.md`, DONE 2026-06-15): `SelectionCoordinatorNode` +
`VoxelSelectionProviderNode` + `UISelectionProviderNode`, which answers "what did the user click on in
the 3D world / HUD" via GPU pixel-ID readback / RmlUi hit-testing. Its identity space is a packed
brick/voxel pick-ID or an RmlUi-element-id hash — **not a Gaia entity** — and it is a render-graph
NODE-based system with (deliberately, per its own design doc's decision #2) **no C++ provider
interface**. An EARLIER draft of that system did once have a class literally named `ISelectionProvider`,
which was explicitly retired in the 2026-06-15 "providers are nodes" refactor (see
`Selection/SelectionCandidate.h`'s header comment: "supersedes the old C++ `Hit`/`ISelectionProvider`").

**Inc-C's new seam is a genuinely different concept that happens to share the word "selection":**
different identity space (Gaia entity, not pick-ID), different trigger model (a durable ECS query set,
not an edge-triggered mouse click), different consumer (View↔Model-Binding set-mutation actions, not
pixel-picking/highlight UX). To avoid the coincidental name collision confusing future readers, **name
the new interface `IViewSelectionProvider`**, NOT bare `ISelectionProvider` (the design doc's §9 uses the
bare name — this plan supersedes that spelling; update §9 to match once Inc-C lands). Do NOT reuse or
extend `SelectionCoordinatorNode`/`VoxelSelectionProviderNode`/`UISelectionProviderNode` — they solve a
different problem in a different architecture (nodes vs. a small C++ interface mirroring
`IViewDataProvider`).

## Scope boundary (what Inc-C IS and is NOT)
- **IS:** a **committed-selection Gaia tag component** (`struct Selected {}` — zero-field, per §6's
  "selection is data" framing: `all<Selected, Bound...>()`); an **`IViewSelectionProvider`** seam
  (`ids()` / `at(index)`, mirroring `IViewDataProvider`'s shape) whose Gaia-backed implementation wraps a
  `query().all<Selected>()`; a **direct-list implementation** too (mirrors `IViewDataProvider`'s
  direct-field provider from Inc-A — a trivial `std::vector<EntityID>`-backed provider, useful for tests
  and for any non-Gaia consumer); and **a view/action bound to "the Nth selected instance"** via the
  seam's opaque `instance` slot (the shipped `ViewNounKey{ViewNounId; uint64_t instance}` — `instance`
  here becomes "index into the selection," resolved through `IViewSelectionProvider::at(index)` to a
  concrete entity, then through the EXISTING Gaia-backed `IViewDataProvider` machinery from Inc-B to
  read/write that entity's bound component).
- **PROOF vehicle:** since the editor currently has exactly ONE Gaia entity (the Inc-B layer-mask
  entity), Inc-C needs at least 2-3 entities to prove indexing/selection is real (not vacuously "the one
  entity is always index 0"). Create a small number of additional bare entities (test-only or a small
  editor-side multi-entity fixture — state your choice) each carrying the SAME kind of bindable component
  (reuse `LayerMask` from Inc-B, or introduce a second trivial component if that's cleaner — your call,
  document it), commit a subset of them to `Selected` (deterministically, not via UI input — a scripted/
  test-side commit is fine, mirroring Inc-B's "deterministic external write" proof pattern), and assert
  `IViewSelectionProvider::ids()`/`at(index)` yields EXACTLY the committed subset, in a stable order,
  and that a view/action bound through it resolves to the RIGHT entity's data (not entity 0 by
  coincidence). This is the increment's real proof: selection-of-N is genuinely wired, not scaffolding.
- **Transient vs. committed (§6, DO NOT conflate):** Inc-C is **committed selection only** — the `Selected`
  tag component, changed infrequently (a click that commits, or here a scripted commit), where an
  archetype move per change is acceptable. Do NOT implement transient hover/highlight in this increment
  (that's explicitly a value-field-or-UI-side-only concern per §6, and is not needed to prove Inc-C's
  contract) — if a "highlight the selected rows in the UI" nicety falls out cheaply, it's a bonus, not a
  requirement; don't build toward it.
- **IS NOT:** set MUTATION — an action that writes to every selected entity (that's Inc-D, next); the
  undo-over-a-set snapshot contract (also Inc-D, §6 critic item 8 — materialized `(entity, prior-value)`
  list, dead-entity skip+log — real correctness weight, deliberately deferred); the "constraints node"
  alternative query-engine discussion (§11 — still open, Inc-C uses a raw Gaia query, not a VIXEN
  constraint node; note the open decision in your report but don't resolve it here); UI-driven selection
  (click-to-select) — the PROOF vehicle is a scripted/deterministic commit, not a UI interaction; wiring
  actual mouse-click selection into `Selected` is a future increment (possibly reusing
  `SelectionCoordinatorNode`'s `SelectionChangedEvent` as a trigger — note this as a follow-up, don't
  build it now).
- **Reuse, don't reinvent:** `IViewDataProvider` (Inc-A) and the Gaia-backed provider + `.changed<T>()`
  reconcile machinery (Inc-B) are UNCHANGED by Inc-C — Inc-C adds a NEW seam
  (`IViewSelectionProvider`) that resolves WHICH entity a given `IViewDataProvider` call should target;
  it does not replace or duplicate Inc-B's read/write/observe machinery. Study
  `GaiaLayerViewDataProvider.h`/`ViewReconcileNode.h` (Inc-B, already merged) as the sibling pattern to
  mirror for the new seam's shape and file layout.

## Tasks — Milestone 1 (may split if the multi-entity fixture + provider + resolution wiring is large)

### Task 1 — Ground the shape (READ + REPORT before building)
- Read `GaiaLayerViewDataProvider.h`, `ViewReconcileNode.h`, `GaiaLayerReconcileTestBridge.h/.cpp` (Inc-B,
  merged `5a7e55ce`) as the sibling pattern.
- Read `Selection-System-Design-2026-06.md` + `Selection/SelectionCandidate.h`'s header comment (the
  retired `ISelectionProvider` note) to confirm the naming-disambiguation reasoning above still holds
  against current code (things may have shifted since the research pass that informed this plan).
- Confirm there is NO existing Gaia tag-component convention in `VoxelComponents.h` (only
  `VOXEL_COMPONENT_SCALAR`/`VOXEL_COMPONENT_VEC3` macros exist as of Inc-B — a zero-field tag needs either
  a new macro variant or a plain hand-written Gaia component outside that registry; REPORT which you
  choose and why before building — `Selected` is arguably not "voxel data" and may not belong in
  `VoxelComponents.h` at all; consider a small new header, e.g. `SelectionComponents.h`, if that's
  cleaner).
- Decide the proof vehicle's entity-creation mechanism (a small editor-side fixture vs. a pure headless
  gtest creating its own `GaiaVoxelWorld` + entities, mirroring `GaiaLayerReconcileTestBridge`'s
  ODR-isolation pattern) — REPORT your choice. A pure headless gtest is likely sufficient and lower-risk
  (Inc-B's own Task 4 proof was headless); only touch `EditorApplication` if a live editor-visible proof
  is genuinely needed.

### Task 2 — `Selected` tag component + `IViewSelectionProvider` seam
- Define `struct Selected {}` (zero-field Gaia tag component) wherever Task 1 decided it belongs.
- Define `IViewSelectionProvider` (new header, mirrors `IViewDataProvider`'s file/interface shape):
  `size_t ids(std::vector<EntityID>& out) const` / `EntityID at(size_t index) const` (exact signature is
  your call — mirror `IViewDataProvider`'s existing style, e.g. bool-return-for-fallibility if `at()` can
  be out-of-range).
- Implement a **Gaia-backed** `IViewSelectionProvider` wrapping `query().all<Selected>()` (stable
  ordering — decide and document how: Gaia's own iteration order, or an explicit sort key if the query
  doesn't guarantee stability across `.changed<T>()` calls — verify this against Gaia v0.9.2's actual
  behavior, don't assume).
- Implement a **direct-list** `IViewSelectionProvider` (a trivial `std::vector<EntityID>`-backed
  implementation) for tests/non-Gaia consumers — mirrors Inc-A's direct-field `IViewDataProvider`.

### Task 3 — Wire "Nth selected instance" resolution
- Extend the seam's `instance` handle usage: given `IViewSelectionProvider::at(index)` → an `EntityID`,
  route that entity into the EXISTING Gaia-backed `IViewDataProvider` (Inc-B's provider, unchanged) so a
  view/action bound "to selection index N" reads/writes the correct entity's component.
- This task is primarily WIRING, not new machinery — Inc-B's provider already takes an entity; Inc-C's
  job is resolving WHICH entity from a selection index before handing it to that provider. Keep this
  thin; do not duplicate Inc-B's read/write logic.

### Task 4 — Prove selection-of-N + preserve all Inc-A/A2/B gates
- **The key new proof:** create 2-3 Gaia entities (per Task 1's chosen fixture mechanism) each carrying a
  bindable component; commit a deterministic SUBSET to `Selected` (e.g. entities 0 and 2 of 3, not all,
  not none — must be a genuine subset to prove filtering, not just "everything is selected"); assert
  `IViewSelectionProvider::ids()` yields exactly that subset; assert `at(0)`/`at(1)` resolve to the RIGHT
  entities in a stable order; assert resolving through to the Inc-B provider reads/writes the CORRECT
  entity's actual component value (not entity 0 by coincidence — vary which entity holds which value so a
  wrong-entity bug would show up as a wrong VALUE, not just a wrong bool).
- **No-regression, ALL of these must still hold** (Inc-C must not perturb Inc-B's shipped behavior):
  `test_view_editor_layers_reconcile` 2/2 (Inc-B's own gate), `test_editor_toggle_undo_capture` 4/4 with
  byte-identical captures (report the 4 sha256), AppFlow 42/42, `test_view_editor_layers_golden` 2/2, Gaia
  wrapper tests (`test_gaia_voxel_world`/archetypes) unchanged from whatever their current baseline is
  (there were 2 pre-existing failures as of Inc-B's merge — confirm the SAME 2, not more, not fewer),
  byte-guards hold (`AppFlow.g.h`/`AppFlowCallables.g.hpp` — Inc-B's merge recorded
  `b63b2b35a7cc47fbb9ca35d5f7685d2db8907dada2199ad7a1c82c361eb0710b` /
  `989b65e4887e8ffdd1ed44495ac6f38e40039ba7de641cc60dae17435f9836fe`; confirm unchanged).

## Gates / guardrails
- The new selection-of-N proof test passes, non-vacuous (varies which entity holds which value, asserts
  the RIGHT entity resolves, not just "some entity").
- All Inc-A/A2/B gates named in Task 4 still hold, exactly (no new failures, no fewer-than-expected
  pre-existing ones silently "fixed" without comment — if a pre-existing Gaia-wrapper failure count
  CHANGES, report it explicitly, don't just note the new number).
- No TU includes both gaia.h and RmlUi data-model headers (the robin_hood ODR landmine — reuse the
  existing bridge-file split pattern, e.g. `GaiaLayerReconcileTestBridge`'s approach, for any new bridge
  this increment needs).
- Build the fresh worktree's own build dir; confirm Gaia is still pinned to the same version Inc-B built
  against (`_deps/gaia-src` HEAD — check the pin hasn't silently drifted).
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence. Poll long builds
  actively (~20s foreground loop per `vixen-build-policy`), never overlap builds of one target. Only pass
  a REAL CMake target name to `-BuildTarget`/`-Target` (a descriptive/free-text value there silently
  breaks the build — a bug found and fixed 2026-07-12, `b517c3f2`, already on main).
- Commit in the worktree (pre-blessed). Do NOT push.

## Milestone Map
- [ ] **Milestone 1 (Tasks 1-4):** ground the tag-component/seam shape (Task 1 REPORT-BACK) → `Selected`
  tag + `IViewSelectionProvider` (Gaia-backed + direct-list) → wire Nth-selected-instance resolution
  through Inc-B's existing provider → prove selection-of-N + preserve all Inc-A/A2/B gates. One Sonnet
  implementer + one Opus validator. (May split into M1a component/seam / M1b resolution+proof if Task 1
  finds the multi-entity fixture is large.)

## Follow-ups (explicitly out of scope, note for later increments)
- Wiring real UI click-to-select into `Selected` (a future increment could consume
  `SelectionCoordinatorNode`'s `SelectionChangedEvent` as the trigger that adds/removes the tag — this
  would be the first real bridge between the two "selection" systems, as a one-way consumer relationship,
  not a merge).
- The "constraints node" vs. raw Gaia query open decision (§11) — Inc-C uses a raw query; revisit if a
  future increment wants selection expressed through a VIXEN constraint/selection render-graph node
  instead.
- Updating `View-Model-Binding-Framework-Design-2026-07.md` §9's bare `ISelectionProvider` spelling to
  `IViewSelectionProvider` once Inc-C lands, plus a cross-link to `Selection-System-Design-2026-06.md`
  disambiguating the two "selection" concepts for future readers.

## Note
Inc-C proves selection-of-N is real (committed tag component → stable ordered set → correct per-entity
resolution), on top of Inc-B's already-proven single-entity Gaia read/write/observe machinery. It
deliberately does NOT touch set mutation or undo (Inc-D) or UI-driven selection (future) — keep it
narrowly scoped to the identity/indexing contract, per the program's own "do not build ahead" discipline
established in every prior increment's plan.
