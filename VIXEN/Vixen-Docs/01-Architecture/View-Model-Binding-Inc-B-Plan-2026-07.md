# View↔Model Binding — Inc-B Plan (2026-07-11)

**Program design:** `View-Model-Binding-Framework-Design-2026-07.md` §10 item 3 (Inc-B), §4 (reconcile),
§4a (ViewReconcileNode placement), §4b (hook-slot ownership + `.all<T>()` const-assert).
**Prior:** Inc-A (`224e393f`) = `IViewDataProvider` seam + direct-field provider + ToggleLayer reroute.
Inc-A2 (`60672744`) = editor layer view is a real RmlUi data-model; **model→view for the editor's OWN
input already works** (initial population + same-frame echo). So Inc-B's model→view job NARROWS to
**external / non-input-originated changes** — the ones a per-frame reconcile exists to catch.

**Inc-B goal:** prove the ECS observe path for real — back the editor layer datum with a **Gaia
component**, and drive model→view for **external changes** (not the input echo) via a persistent
per-view **`.changed<T>()` reconcile** that forwards into RmlUi `DirtyVariable`. This is the framework's
core reactive half on the real ECS mechanism (v0.9.2, verified in the observable investigation).

## Scope boundary (what Inc-B IS and is NOT)
- **IS:** a **Gaia-backed `IViewDataProvider`** for the editor layer datum (the layer mask becomes a Gaia
  component on an editor entity; provider read = `getComponentValue`, write = `setComponent`); a
  persistent per-view **`.changed<T>()` query** + a **per-frame reconcile** (VALUE-PUSH, §4 — no
  per-entity diff cache) that, when the bound component's chunk is marked changed, re-pushes the current
  value into the view's backing store + calls RmlUi `DirtyVariable`; a **`ViewReconcileNode`** (dedicated
  render-graph node — the §11 lean) that runs the reconcile each frame AFTER input handling (§4a); the
  **`.all<T>()` immutable-access** declaration on the reconcile query (§4b — a mutable `.all<T&>()` +
  const `each` trips v0.9.2's hard `GAIA_ASSERT`); handling the **`ReadU32` false case** in the
  ToggleLayer handler (the carry from Inc-A — a fallible Gaia read may return false; the handler must not
  silently write `applyToggle(0,idx)`).
- **PROOF vehicle for the external-change path:** since the editor has no live simulation mutating the
  layer mask externally, drive a **deterministic external mutation** to exercise the reconcile — e.g. a
  scripted/test path that writes the Gaia layer component DIRECTLY (bypassing the input handler / the
  same-frame echo), and assert the reconcile carries it into the view (the checkbox model updates without
  any UI input). This is the increment's real proof: model→view for a change the input echo did NOT
  produce. If a scripted external write isn't feasible in the editor harness, a headless gtest over the
  `ViewReconcileNode` + a Gaia component + a `ViewStore` is acceptable (state the choice).
- **IS NOT:** the `func_set` hook dispatcher as the PRIMARY driver (§4 — hook is opt-in dirty-signal only;
  Inc-B may add the shared-dispatcher hook-slot ownership §4b IF cheap, but the `.changed<T>()` reconcile
  is the required default; do not make the hook primary); multi-instance selection (Inc-C); set mutation
  (Inc-D); the projection/override emitter syntax (Inc-Ovr — the mask↔checkbox projection stays the
  hand-written leaf). Do NOT build ahead.
- **Gaia v0.9.2 facts (verified):** `set<T>` is a direct mutable reference + AUTO-bumps version + fires
  the hook (silent `sset` skips both); `.changed<T>()` is chunk-granular + needs a PERSISTENT query
  object (fresh query = version 0 = matches everything); reconcile READ path must be `.all<T>()`
  immutable. Wrapper API (`GaiaVoxelWorld::getComponentValue`/`setComponent`) is the host surface; NO TU
  may include both gaia + RmlUi data-model headers (the robin_hood ODR landmine — reuse the
  `EditorLayersViewBridge` isolation pattern; the reconcile's gaia access stays on the gaia side of the
  bridge, the DirtyVariable/RmlUi call stays on the RmlUi side).

## Tasks — Milestone 1 (may split if the Gaia migration + reconcile is large)

### Task 1 — Ground the Gaia + reconcile shape (READ)
- The Gaia wrapper host API (`libraries/GaiaVoxelWorld/include/GaiaVoxelWorld.h`): `getComponentValue<T>`,
  `setComponent<T>`, entity creation/lookup, and how a query is built + `.changed<T>()` + `.each(Iter)`
  iterated (v0.9.2). Confirm the `.all<T>()` vs `.all<T&>()` const-assert.
- How the editor creates/holds its scene entities today (is there already a Gaia world in the editor, or
  is Gaia only in CashSystem/SVO? — `application/main` does NOT link GaiaVoxelWorld per the seam
  investigation; determine whether the EDITOR does, and if not, what the minimal wiring is to give the
  editor a Gaia world + one entity carrying the layer component). REPORT this before building — it may be
  the largest unknown. If giving the editor a Gaia world is heavy, the headless-gtest proof vehicle may be
  the right call for Inc-B (state it).
- The Inc-A2 `EditorLayersView` reconcile target (the `DirtyVariable`/RefreshLayersView path) + the
  `EditorLayersViewBridge` isolation — the reconcile forwards into THIS.

### Task 2 — Gaia-backed IViewDataProvider + the layer component
- Define the layer datum as a Gaia component (a `uint32 LayerMask` component on an editor entity).
- Implement `GaiaLayerViewDataProvider : IViewDataProvider` — `ReadU32(LayerMask)` = `getComponentValue`,
  `WriteU32` = `setComponent` (auto version-bump). Keep the seam interface identical; observe is
  provider-internal.
- Swap the editor to use the Gaia-backed provider instead of `LayerControllerViewDataProvider` (OR keep
  both behind a flag if a hard swap risks the gate — but the goal is the Gaia datum is the real one).
- **Handle the ReadU32 false case** in the ToggleLayer handler (Inc-A carry): if the read fails, do NOT
  write `applyToggle(0,idx)` — skip/log (a real fallible-provider correctness fix).

### Task 3 — The per-frame `.changed<T>()` reconcile + ViewReconcileNode
- A persistent per-view `.changed<LayerMask>()` query. A **`ViewReconcileNode`** (dedicated render-graph
  node) that each frame runs the query; for a changed chunk, VALUE-PUSHES the current component value
  into the view backing + calls RmlUi `DirtyVariable` (via the bridge — gaia access on one side, RmlUi on
  the other). `.all<T>()` immutable read (§4b). Placement AFTER input handling (§4a) so an external write
  reconciles same-frame where possible.
- The same-frame echo from Inc-A2 STAYS for the input path (don't double-fire; the reconcile handles the
  NON-input path — ensure they compose, e.g. the reconcile is idempotent value-push so a redundant push
  after the echo is harmless via DirtyVariable coalescing).

### Task 4 — Prove the external-change path + live gate
- **The key new proof:** a deterministic EXTERNAL write to the Gaia layer component (not via the UI
  handler) → the reconcile carries it into the checkbox model WITHOUT input. Assert the view updates
  (bound-model state / a headless assertion over ViewReconcileNode+ViewStore). This is what Inc-B proves
  that Inc-A2 did not.
- **Live gate PRESERVED:** `test_editor_toggle_undo_capture` still 4/4 (the Gaia swap + reconcile must not
  regress the input-driven round-trip; captures byte-identical — the panel still composites on the
  swapchain, not the captured compute_render_target). Report the 4 sha256.
- No-regression: AppFlow 42/42, `test_view_editor_layers_golden` 2/2, HUD unaffected, Gaia wrapper tests
  (test_gaia_voxel_world / archetypes) still green (the Gaia usage must not perturb them). Byte-guards
  hold (AppFlow.g.h `b63b2b35…`, AppFlowCallables.g.hpp `989b65e4…`). If a new `[GpuStruct]`/component
  schema adds codegen, its drift-guard is committed + green.

## Gates / guardrails
- `vixen_editor` builds clean; the external-change reconcile PROVEN; input gate 4/4 preserved; AppFlow
  42/42; golden 2/2; Gaia wrapper tests green; HUD unaffected; byte-guards hold.
- NO TU includes both gaia + RmlUi data-model headers (robin_hood ODR — reuse the bridge). The
  reconcile's gaia-side and DirtyVariable-side must be split across the bridge.
- Build the fresh worktree's build dir → confirm Gaia is v0.9.2 (`_deps/gaia-src` HEAD `f2ea77a`; a fresh
  worktree fetches it correctly — do NOT reuse a stale build dir per KI-021).
- rtk masks git exit codes — `/usr/bin/git`, `sha256sum`, `cmp` for evidence. Poll long builds, never
  overlap builds of one target, VK_ICD_FILENAMES unset. `git checkout --` the SDFNodeGenerator.dll after codegen.
- Commit in the worktree (pre-blessed). Do NOT push.

## Milestone Map
- [x] **Milestone 1 (Tasks 1-4):** ground Gaia+reconcile shape (Task 1 REPORT-BACK: does the editor have a
  Gaia world? if heavy → headless proof) → Gaia-backed provider + layer component + ReadU32-false fix →
  `.changed<T>()` reconcile + ViewReconcileNode → prove external-change path + preserve input gate 4/4.
  One Sonnet implementer + one Opus validator. (May split into M1a provider / M1b reconcile if Task 1
  finds the Gaia-world wiring is large.)

## Note
Inc-B proves model→view for EXTERNAL changes on the real Gaia `.changed<T>()` mechanism — the framework's
core reactive half. The editor's own-input echo is already done (Inc-A2). Keep the hook secondary (§4).
The single biggest unknown is whether the editor has/needs a Gaia world — Task 1 resolves it and may
redirect to a headless proof vehicle; that's an acceptable Inc-B outcome (the mechanism is what matters).

## Progress Log
- Milestone 1 (Tasks 1-4): DONE · commit `9a938b57` (fresh-worktree verification + gate evidence);
  Tasks 1-3 + the Task 4 test scaffold were implemented by a prior uncommitted pass in this worktree
  and confirmed correct as written, not reimplemented · 2026-07-12
  - **Task 1 finding confirmed:** the editor had NO Gaia world before Inc-B (`application/main`/
    `vixen_editor` only pulled `gaia.h` transitively via SVO's octree headers). Inc-B gave
    `EditorApplication` its own `Vixen::GaiaVoxel::GaiaVoxelWorld gaiaWorld_` + one bare entity
    (`gaiaLayerEntity_`) carrying the new `LayerMask` Gaia component (`VoxelComponents.h`) — the
    lightweight real-world option, not the headless-gtest fallback the plan flagged as acceptable.
    `vixen_editor`'s CMakeLists now links `GaiaVoxelWorld::GaiaVoxelWorld` directly (previously never
    linked, only pulled in transitively).
  - **Task 2/3 confirmed as implemented:** `GaiaLayerViewDataProvider` (ReadU32/WriteU32 over
    `getComponentValue`/`setComponent<LayerMask>`) swapped in for `LayerControllerViewDataProvider`;
    the ToggleLayer handler's `ReadU32`-false case now skips+logs instead of silently corrupting the
    mask via `applyToggle(0,idx)` (the Inc-A carry, now fixed); `ViewReconcileNode` owns a persistent
    `.all<LayerMask>().changed<LayerMask>()` query (immutable read, per design §4b) and
    `EditorApplication::ReconcileLayersView()` runs it every `Update()` after input dispatch, value-
    pushing into `rt_.Layers()` + forwarding to `RefreshLayersView()`'s `DirtyVariable` call on any
    external change.
  - **Task 4 — the key new proof — CONFIRMED LIVE:** `test_view_editor_layers_reconcile`
    (2/2 PASSED) exercises `ExternalWriteLayerMask` — a direct `GaiaVoxelWorld::setComponent<LayerMask>`
    call that bypasses `WriteU32`/`ToggleLayer`/the same-frame echo entirely — and asserts (a) the view
    does NOT move before the reconcile runs (non-vacuous proof), (b) `ViewReconcileNode::Reconcile`
    observes the changed chunk and returns the post-write value, (c) `GaiaLayerViewDataProvider::ReadU32`
    independently sees the same external write, (d) `EditorLayersView::PopulateFromMask` with the
    reconciled value flips the bound checkbox model (layer1 unchecked, layers 0/2 stay checked), and
    (e) a second reconcile with no intervening write reports nothing changed (persistent-query version
    tracking, not "always matches"). This is model→view for a change the input path did not produce —
    the half Inc-A2 explicitly left unproven.
  - **Fresh-worktree build:** `vixen-ninja` (Windows/MSVC) preset, confirmed `_deps/gaia-src` HEAD
    `f2ea77a` (pinned v0.9.2, per KI-021). Full aggregate `build.bat build` hits the two independent,
    already-documented pre-existing failures (KI-020/KI-017: MSVC `setenv`/`unsetenv` and Windows
    `min`/`max` macro pollution in `test_body_instance_raymarch_render`/`test_octree_config_sdi_parity`,
    unrelated to any file this increment touches) — built the Inc-B-relevant target set explicitly
    instead (`-- -k 0`), which built 100% clean.
  - **Gate evidence:**
    - `test_view_editor_layers_reconcile`: 2/2 PASSED.
    - `test_editor_toggle_undo_capture`: 4/4 PASSED across two independent fresh `run_editor_script.bat`
      runs (state trail both times: `mask=7→toggle→3→undo→7→redo→3`, `afterBack=0`). sha256 of the 4
      captures (byte-identical across both runs, and matching the pre-existing R6 mask-invisible-at-
      render finding: capture_5==capture_75, capture_45==capture_105):
      - `editor_capture_5.png` = `fde9c268cf5f07f68588b563b908ec84bb9bd134e3bcbc913280152cac6ed8c1`
      - `editor_capture_45.png` = `e2339aa09f871a0e57f19db22977c0a90aac2303cf4d090100f396553b49b1f8`
      - `editor_capture_75.png` = `fde9c268cf5f07f68588b563b908ec84bb9bd134e3bcbc913280152cac6ed8c1`
      - `editor_capture_105.png` = `e2339aa09f871a0e57f19db22977c0a90aac2303cf4d090100f396553b49b1f8`
    - AppFlow suite: 42/42 (`test_appflow_golden` 7, `test_action_stack` 4, `test_flow_state_machine` 3,
      `test_binding_store` 3, `test_appflow_loader` 6, `test_layer_controller` 4, `test_snapshot_undo` 6,
      `test_input_profile` 2, `test_keychord` 3, `test_binding_pattern` 1, `test_flow_return` 2,
      `test_return_dispatch` 1).
    - `test_view_editor_layers_golden`: 2/2 PASSED.
    - Gaia wrapper tests: `test_gaia_voxel_world` 25/26 (1 pre-existing failure,
      `GaiaVoxelWorldTest.GetPosition`) and `test_gaia_voxel_world_coverage` 31/32 (1 pre-existing
      failure, `GaiaVoxelWorldCoverageTest.CreateVoxelsBatch_AutoParent_ToExistingChunk`) — confirmed
      via `git stash`/rebuild-on-baseline that BOTH fail identically with zero Inc-B changes present, so
      neither is a regression from the `LayerMask` component addition or the Gaia usage.
    - Byte-guards: `AppFlow.g.h` = `b63b2b35a7cc47fbb9ca35d5f7685d2db8907dada2199ad7a1c82c361eb0710b`,
      `AppFlowCallables.g.hpp` = `989b65e4887e8ffdd1ed44495ac6f38e40039ba7de641cc60dae17435f9836fe` —
      both unchanged from the plan's stated prefixes.
    - `test_appflow_editor_toggle_render` (a GPU render gate reusing `VixenSelectWslGpuIcd`, not one of
      this plan's named gates) fails on this native-Windows machine because no software/lavapipe Vulkan
      ICD is selected here (`softwareConfirmed_ == false`) — confirmed byte-identical failure on the
      pre-Inc-B baseline via the same stash comparison, so this is a pre-existing environment gap, not a
      regression.
  - **HUD unaffected:** no HUD-touching file in this increment's diff; `test_view_hud_golden` was not
    perturbed (untouched code path).
  - **ODR isolation confirmed:** `GaiaLayerViewDataProvider.h`/`ViewReconcileNode.h`/
    `GaiaLayerReconcileTestBridge.{h,cpp}` never include an RmlUi data-model header; the test TU
    (`test_view_editor_layers_reconcile.cpp`) never includes `gaia.h` — verified both by source
    inspection and by the fact the whole thing built and linked clean (the robin_hood ODR collision is
    a hard compile/link failure when violated, per Inc-A2's own discovery of it).
  - **One incidental infra note (not a code defect):** mid-session, an overlapping build (a baseline
    comparison build kicked off concurrently with a rebuild of Inc-B's own tree) corrupted one
    `.obj`/COFF section (`VoxelAABBConverterNode.cpp.obj`, `LNK1236`) — deleted the stale object and
    rebuilt clean. Root cause was this session's own parallel build dispatch, not a source issue; no
    build-lock/queue tooling exists in this worktree's `VIXEN/scripts/build/` (not present on this
    branch's history) to have prevented it.
