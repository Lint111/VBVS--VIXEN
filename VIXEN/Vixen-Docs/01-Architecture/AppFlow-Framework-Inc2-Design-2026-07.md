# AppFlow Framework — Increment 2 Design (LayerController + snapshot-fallback + headless render-gate)

**Date:** 2026-07-05
**Status (reverified 2026-09-01):** COMPLETE — shipped with the LayerController, snapshot-fallback
undo path, and GPU render gate (`c20d507f`; close-out `1a072c1c`).
**Program:** AppFlow app-flow/state/action framework — see `AppFlow-Framework-Design-2026-07.md`
**Builds on:** Inc-1 walking skeleton (SHIPPED in the current engine history, close-out `c5150e56`) — `VIXEN/libraries/AppFlow/` (generated mirror, FlowStateMachine, ActionStack, BindingStore, AppFlowLoader, AppFlowRuntime, DispatchBySelector spine).

---

## 1. Goal

Turn the Inc-1 offline skeleton into a **working, GPU-gated layer-toggle-with-undo**: a `ToggleLayer`
FlowAction dispatched through `AppFlowRuntime` mutates layer-enabled state, the existing
re-flatten→re-bake→`SetRecipePool` path renders it, and `Undo()` restores the previous render exactly
— proven by a headless render-gate on lavapipe. Also builds the **general snapshot-fallback undo
engine** the Inc-1 design promised (scheduled for this increment).

**Key reframing (corrected from the roadmap):** editor "layers" are NOT RenderGraph nodes. A layer
toggle re-flattens the `VoxelDocument` with a per-layer enabled mask
(`EditorDocumentModel::enabledOverride_`) → re-bakes → `SetRecipePool` (see
`EditorApplication::ApplyDocumentToScene`). Per-node enable/skip doesn't even exist as a live
mechanism (Architecture-Review-Game-Renderer-2026-06: `ShouldExecuteThisFrame` is checked only by the
dead `Execute(VkCommandBuffer)` path, `NodeState` has no `Disabled`). So Inc-2's LayerController owns
the **enabled mask** and wraps the **existing re-flatten path**, making toggles reversible — it does
NOT build node-enable infrastructure.

**Reach (decided):** headless render-gate through AppFlow only. The full windowed `EditorApplication`
rewire (retiring `DrainClickedElementId`/`ParseLayerToggleId`/`glfwGetKey(S)`/`ConsumeDirty` in favor
of AppFlow's BindingStore+ActionStack) is a **later increment (Inc-2b)**, not this one.

---

## 2. Architecture — three additions on the Inc-1 runtime

### 2.1 `LayerController` (new AppFlow unit)

Source of truth for layer-enabled state, held as the codegen'd `LayerState { uint32_t enabledMask }`
(≤32 layers — see §5 constraint). Where `EditorDocumentModel::enabledOverride_` migrates to.

```cpp
class LayerController {
public:
    void      SetLayerCount(uint32_t n);        // n <= 32; inits all-enabled
    uint32_t  LayerCount() const;
    bool      IsEnabled(uint32_t i) const;      // false if i >= count
    bool      Toggle(uint32_t i);               // flips bit i; false + no-op if i >= count
    uint32_t  Mask() const;                     // the raw enabledMask
    void      SetMask(uint32_t m);              // restore path (masked to count)
    Generated::LayerState Snapshot() const;     // { enabledMask }
    void      Restore(const Generated::LayerState& s);
};
```

### 2.2 Snapshot-fallback engine (extends `ActionStack`)

The Inc-1 `ActionStack` recorded a forward+inverse `ApplyFn` per entry. Inc-2 adds a **snapshot path**
for actions whose decl has `hasInvert == false`:

- On `Dispatch`, if `decl.hasInvert == false`: `memcpy` the action's declared footprint
  (`footprintBytes` bytes, read from `AppFlowActionDecl`) from a caller-provided footprint pointer
  into a per-entry snapshot buffer BEFORE running `apply(true)`.
- On `Undo` of a snapshot entry: `memcpy` the saved bytes back into the footprint pointer (instead of
  running `apply(false)`), then invoke a caller `onRestore` hook so the consumer re-derives from the
  restored state (re-flatten).

**Generic over any footprint by byte size** — no per-type code, no knowledge of `LayerState`. The
entry carries `{footprintPtr, footprintBytes, snapshotBytes}`. This realizes the design's "generic
snapshot/diff a footprint" claim. ToggleLayer keeps its fast self-inverse (`hasInvert == true`); a
second, snapshot-only action exercises the fallback.

New `ActionStack` surface (additive; Inc-1 inverse API unchanged):
```cpp
// Snapshot-mode dispatch: footprint bytes are saved before apply; undo restores them + calls onRestore.
DispatchResult DispatchWithSnapshot(FlowActionId id, void* footprint, uint32_t footprintBytes,
                                    ApplyFn apply, std::function<void()> onRestore);
```
(An action's decl `hasInvert` selects which the runtime uses; both live behind `AppFlowRuntime`.)

### 2.3 The re-flatten seam

`EditorDocumentModel::Flatten` (and `FlattenToRecipeEntry`) read the enabled mask from
`LayerController` instead of the owned `enabledOverride_` vector.

**Decided default (to remove ambiguity):** `Flatten`/`FlattenToRecipeEntry` take the enabled state as
an explicit parameter — a `uint32_t enabledMask` (or a `const LayerController&`) — derived from
`LayerController::Mask()` at the call site. `EditorDocumentModel::enabledOverride_` and its
`ToggleLayer`/`IsEnabled`/`ConsumeDirty` members are **removed** (that responsibility moves wholesale
to `LayerController`); the model becomes purely the document-load + flatten + save unit with no
mutable layer state of its own. `FlattenVoxelDocument`'s existing `const std::vector<uint8_t>*
enabledOverride` parameter is fed from the mask (bit i → byte i) at the call site, so the flattener
itself is unchanged. LayerController is unambiguously the single source of truth.

`AppFlowRuntime` gains an owned `LayerController` + accessor, and the toggle dispatch reuses
`DispatchAction` with the `layerIndex:Int` typed param from Inc-1 (the param signature already exists
in `AppFlow.g.h`).

---

## 3. Data flow — the toggle→render→undo loop (what the live gate exercises, headless/GPU)

```
Setup: LayerController.SetLayerCount(3); mask = 0b111 (all enabled)
       EditorDocumentModel reads its mask from LayerController
       AppFlowRuntime owns the LayerController + ActionStack

TOGGLE layer 2 ("cut"):
  AppFlowRuntime.DispatchAction(ToggleLayer, {layerIndex:2}, applyFn)
    → ActionStack.Dispatch: decl.hasInvert==true → record forward+inverse (self-inverse)
    → applyFn(true): LayerController.Toggle(2)            [0b111 → 0b011]
    → publish AppFlowChangedEvent{ActionApplied}
  Re-flatten: EditorDocumentModel.Flatten reads LayerController.Mask() (0b011)
    → FlattenVoxelDocument(view, mask) → re-bake → SetRecipePool
  Render → PNG_toggled  (cut disabled: bore column now solid box-top)

UNDO:
  AppFlowRuntime.Undo()
    → self-inverse: applyFn(false): LayerController.Toggle(2)   [0b011 → 0b111]
       (snapshot-only action instead: memcpy saved LayerState back + onRestore)
    → publish AppFlowChangedEvent{ActionUndone}
  Re-flatten reads Mask() (0b111) → re-bake → SetRecipePool
  Render → PNG_undone

ASSERT: PNG_toggled ≠ PNG_initial at the bore column (toggle rendered);
        PNG_undone == PNG_initial byte-for-byte (undo restored exactly).
```

`applyFn` is the generic-meets-specific seam: AppFlow's ActionStack calls an editor-supplied lambda
that mutates `LayerController` and triggers re-flatten. The engine never knows what a "layer" is — it
runs forward/inverse or snapshot/restore over the declared footprint.

---

## 4. Testing

### 4.1 GPU live gate (headless, lavapipe) — the authoritative proof
`test_appflow_editor_toggle_render` (extends the existing `test_editor_document_render.cpp` pixel-diff
pattern — bore-column non-silhouette-blind diff, PNG output): builds `AppFlowRuntime` +
`LayerController(3)`, binds `EditorDocumentModel` to read its mask, renders `PNG_initial`; dispatches
`ToggleLayer{2}` through AppFlow → re-flatten → `PNG_toggled`; `Undo()` → re-flatten → `PNG_undone`.
Asserts `PNG_toggled` differs at the bore column and `PNG_undone == PNG_initial` byte-for-byte.

### 4.2 Offline unit tests (no GPU — the bulk)
1. `LayerController` — SetLayerCount/IsEnabled/Toggle/Mask/SetMask/Snapshot/Restore; bitmask
   correctness; ≤32-layer bound; out-of-range no-op.
2. **Snapshot engine** — a `hasInvert==false` action snapshots its footprint before apply, restores
   exactly on undo; footprint read generically by `footprintBytes` (a second synthetic footprint size
   proves genericity, not LayerState-hardcoding).
3. **Parity test (design headline)** — inverse-undo and snapshot-undo of an equivalent change both
   return byte-identical `LayerState`.
4. **Grouping + snapshot** — a group mixing a self-inverse and a snapshot action undoes as one unit,
   both mechanisms firing in reverse order.
5. `AppFlowRuntime` — toggle dispatch mutates `LayerController` + publishes; undo reverts + publishes.

Gate discipline mirrors Inc-1: offline units via direct gtest binaries (KI-014 ctest gap persists);
the one GPU test on lavapipe headless is the authoritative proof (live-run-is-authoritative rule).

---

## 5. Error handling + constraints

**Error handling** (unchanged model — typed results, no throw across a boundary):
- `LayerController::Toggle(i)`/`IsEnabled(i)` out-of-range → no-op / false (mirrors today's
  `if (layerIndex >= LayerCount()) return`).
- Snapshot engine: a zero/oversized `footprintBytes` is a **codegen/load-time** error (an action can't
  register a footprint the engine can't size), surfaced via `LoadResult`, not a runtime surprise.
- Re-flatten failures propagate through the existing `Flatten`→`err` string path and are logged,
  exactly as `ApplyDocumentToScene` does today.
- Undo/redo on an empty stack → `NothingToUndo`/`NothingToRedo` (Inc-1).

**Constraints (stated explicitly):**
- **≤32 layers** — the `LayerState { uint32_t enabledMask }` bitmask. The golden document has 3.
  Variable-length layer lists are a later concern; documented, not silently assumed.
- **`EditorDocumentModel` gains a dependency on `LayerController`** for the mask read (a small change
  to its `Flatten`/`FlattenToRecipeEntry` signature). This is the intended consolidation — layer state
  moves into AppFlow.
- **Snapshot footprint lifetime (added post-Inc-2, final-review note).** `DispatchWithSnapshot` stores a
  raw `void* footprint` in the Entry and `memcpy`s bytes back to it on undo; §5 guards the footprint
  *size* (load-time) but NOT its *lifetime*. In Inc-2 this is harmless — there are ZERO snapshot-mode
  production callers (the live ToggleLayer path is inverse-mode, `footprint==nullptr`); only the tests
  use snapshot mode, with locals that outlive `Undo()`. **The FIRST real snapshot-mode action (Inc-2b/
  Inc-3) MUST pass a footprint that outlives every Undo of that entry** — i.e. a stable owned buffer, not
  a stack local or an element of a reallocating container. Consider having the runtime own the snapshot
  target (or store the snapshot by value only) when snapshot-mode gains a live consumer.

---

## 6. Scope — Inc-2 vs. deferred

**Inc-2 (this design):** `LayerController` + generic snapshot-fallback engine (both undo paths +
parity) + the re-flatten seam + the headless GPU toggle/undo render-gate.

**Deferred:**
- **Inc-2b — windowed `EditorApplication` rewire:** retire the hand-wired
  `DrainClickedElementId`/`ParseLayerToggleId`/`glfwGetKey(S)`/`ConsumeDirty` in `EditorApplication::Update`
  in favor of AppFlow's BindingStore + ActionStack, verified by running the windowed editor.
- Variable-length (>32) layer lists.
- `graph.Run()` render-loop consolidation (design §7d) — separate increment.
- ModuleController, PanelLayout, undertow migration, callback/native actions (later increments).

---

## 7. Files (anticipated — the plan finalizes)

**New:** `VIXEN/libraries/AppFlow/include/LayerController.h` + `src/LayerController.cpp`;
`tests/test_layer_controller.cpp`, `tests/test_snapshot_undo.cpp` (snapshot + parity + grouping);
`VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp` (GPU gate) — or a
location matching where `test_editor_document_render.cpp` lives.
**Modified:** `ActionStack.h/.cpp` (snapshot path), `AppFlowRuntime.h/.cpp` (owned LayerController +
toggle path), `EditorDocumentModel.h` (read mask from LayerController), the two AppFlow
`CMakeLists.txt` (new test targets).
**Unchanged:** the generated `AppFlow.g.h` (`LayerState`/`AppFlowActionDecl.footprintBytes`/param
signature already emitted in Inc-1 — Inc-2 consumes them, no regen).
