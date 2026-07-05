# AppFlow Framework — Increment 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `LayerController` (owns the layer enabled-mask as the source of truth) + a generic snapshot-fallback undo engine on `ActionStack`, and prove the full toggle→re-flatten→render→undo loop on GPU via a headless render-gate routed through `AppFlowRuntime`.

**Architecture:** `LayerController` holds `LayerState { uint32_t enabledMask }` (≤32 layers). `ActionStack` gains a snapshot path: an action whose decl has `hasInvert == false` snapshots its declared footprint (`footprintBytes` bytes) before apply and restores it on undo — generic over any footprint by byte size. `EditorDocumentModel` reads its enabled mask from `LayerController` at the flatten call site (its own `enabledOverride_` is removed). A headless GPU test dispatches `ToggleLayer` through `AppFlowRuntime`, renders, asserts the pixel-diff changed, then `Undo()` and asserts the render reverts byte-for-byte.

**Tech Stack:** C++23, CMake, GoogleTest, VIXEN AppFlow + SVO (`FlattenVoxelDocument`, `RecipeRegistry`, `RecipeBaker`) + RenderGraph (`BodyOctreeSceneNode`) + Vulkan (lavapipe/software headless).

**Design doc:** `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2-Design-2026-07.md`
**Inc-1 (shipped, built on):** `VIXEN/libraries/AppFlow/` — `AppFlow.g.h`, FlowStateMachine, ActionStack, BindingStore, AppFlowLoader, AppFlowRuntime.

## Global Constraints

- C++ standard: **C++23** (`target_compile_features(... cxx_std_23)`).
- **No throw across a boundary** — typed result enums (`DispatchResult`/`LoadResult`); the `VulkanGraphApplication::Prepare` rule.
- **Generated `AppFlow.g.h` is NOT regenerated** — `LayerState`, `AppFlowActionDecl.footprintBytes`, and the `layerIndex:Int` param signature are already emitted (Inc-1). Inc-2 consumes them. Do NOT edit the generated header or the canonical `AppFlowReference.cs`.
- **≤32 layers** — the `LayerState` bitmask. Out-of-range layer ops are no-ops (mirrors `EditorApplication::ToggleLayer`'s `if (i >= LayerCount()) return`).
- **Tests run via direct gtest binaries**, NOT `ctest` (KI-014: `enable_testing()` is ordered after `add_subdirectory(libraries)`, so no library test is ctest-discoverable). Build a target, run its binary.
- **Structural rule (hard):** the `AppFlow` library links ONLY `EventBus` — it has no Vulkan/RenderGraph/SVO dependency and must keep none. Therefore:
  - Offline AppFlow units (LayerController, snapshot engine) → tests in `VIXEN/libraries/AppFlow/tests/`.
  - The GPU render-gate (needs BodyOctreeSceneNode + SVO + Vulkan) → a test in `VIXEN/libraries/RenderGraph/tests/Nodes/` that links AppFlow + RenderGraph + SVO. Do NOT add Vulkan/SVO deps to the AppFlow lib.
- Build in the worktree's own `build/` dir (configured with `-DBUILD_TESTS=ON -DAUTO_LOCATE_VULKAN=ON`, Ninja); never reuse the main checkout's build dir. Do NOT commit `build/` (gitignored).
- Commit per task step. In-tree git ops are pre-blessed in the pipeline worktree.

## Milestone Map (context-manager pipeline — segment on execution)

- **Milestone 1 — LayerController (Task 1):** the enabled-mask source of truth + offline tests.
- **Milestone 2 — Snapshot-fallback engine (Tasks 2–3):** `ActionStack` snapshot path + the inverse-vs-snapshot parity + grouping tests.
- **Milestone 3 — Runtime + re-flatten seam (Tasks 4–5):** `AppFlowRuntime` owns LayerController + toggle/undo dispatch; `EditorDocumentModel` reads the mask from LayerController; offline wiring tests + CMake for new offline test targets.
- **Milestone 4 — GPU render-gate (Task 6):** the headless toggle→re-flatten→render→undo test in `RenderGraph/tests/`; the authoritative live proof.
- **Milestone 5 — Close-out (Task 7):** full-suite verify + docs, folds into Finish.

## Progress Log

- Milestone 1 (Task 1): DONE · commit 23fe1de3 · Opus validator APPROVED · 2026-07-05
  - Bitmask logic hand-traced (all 4 tests pass); n==32 shift-overflow guarded via `(count_>=32u)?0xFFFFFFFFu:((1u<<count_)-1u)` ternary short-circuit. Consumes Generated::LayerState (no regen). Tree clean.
  - **gtest mechanism (pipeline note):** provided by FetchContent — `VIXEN/dependencies/CMakeLists.txt:158-192` (googletest v1.14.0), `add_subdirectory(dependencies)` runs before `add_subdirectory(libraries)` so `GTest::gtest_main` alias exists for AppFlow test link. **It's a NETWORK fetch — no cached `_deps` in this worktree, so the first `cmake -B build -DBUILD_TESTS=ON` (M3/Task 5) needs internet to clone googletest and will take longer.** Offline, only `-fsyntax-only` gates are possible (why M1/M2 can't run gtest until M3).
- Milestone 2 (Tasks 2–3): DONE · commits 96a16aaf (Task 2), 72e04ee9 (Task 3) · Opus validator APPROVED · 2026-07-05
  - `ActionStack::DispatchWithSnapshot` — generic snapshot-fallback undo, keys off `footprintBytes` via memcpy, zero LayerState knowledge. `Entry` gained snapshot fields (all default-init so Inc-1 aggregate init keeps `IsSnapshot()==false` → no regression). `Undo` iterates rbegin→rend (LIFO-correct), branches snapshot(memcpy back + onRestore) vs inverse(apply(false)); `Redo` re-runs apply(true) forward for both.
  - **PLAN-TEST FIX (deviation from the verbatim plan, validator-confirmed correct):** the plan's `MixedGroupUndoesAsOneUnit` had a latent bug — the snapshot's shadow `mask` var wasn't re-synced to `lc.Mask()` after the sibling inverse entry ran, so it snapshotted a stale baseline. Implementer added one line `mask = lc.Mask();` before the snapshot `DispatchWithSnapshot` call (test_snapshot_undo.cpp:80) rather than change `Undo`. Opus independently hand-traced both versions: flipping `Undo` to forward-order would mask the bug by luck AND regress LIFO for order-dependent inverse groups — the test was wrong, not the engine. Undo left unchanged (byte-identical across both commits).
- Milestone 3 (Tasks 4–5): DONE · commits a15a35e2 (Task 4), eb6be60d (Task 5) · Opus validator APPROVED (re-ran gate independently) · 2026-07-05
  - **First runnable gate — GREEN.** AppFlow suite 27/27: test_layer_controller 4/4, test_snapshot_undo 6/6 (incl. Task-4 RuntimeToggleLayerAndUndoFireOnChanged), Inc-1 no-regression 17/17 (action_stack 4, golden 4, fsm 3, binding 3, loader 3). `vixen_editor` builds + LINKS clean (force-recompiled to rule out stale objects).
  - Task 4: `AppFlowRuntime` owns `LayerController` (`Layers()`), `ToggleLayer(index, onChanged)` = self-inverse `stack_.Dispatch` that flips `layers_.Toggle` + fires onChanged on BOTH apply and undo + publishes ActionApplied. Task 5: `EditorDocumentModel::{Flatten,FlattenToRecipeEntry,Save}` take `uint32_t enabledMask` → `vector<uint8_t> ovr(layerCount)` fed to FlattenVoxelDocument's `enabledOverride`; `enabledOverride_`/`ToggleLayer`/`IsEnabled`/`ConsumeDirty` REMOVED (zero dangling refs). EditorApplication owns `LayerController layers_` + `bool dirty_`, routes toggle/save/apply through it.
  - **TWO BEYOND-PLAN CMAKE FIXES (validator-confirmed necessary, not scope creep):** (a) `libraries/AppFlow/CMakeLists.txt` — M1 created LayerController.h/.cpp but NEVER registered them in APPFLOW_SOURCES (the M1 syntax-only gate couldn't catch this; AppFlowRuntime now embeds a LayerController so its symbols must link) → added both. (b) `application/editor/CMakeLists.txt` — `vixen_editor` didn't link AppFlow → added `AppFlow` to target_link_libraries. **DURABLE: a source-file that isn't in its lib's CMake source list passes every offline `-fsyntax-only` gate but fails to link — the CMake registration must land in the SAME milestone that adds the file, or the first real build catches it late.**
  - Data-flow §3 coherent end-to-end (runtime ToggleLayer→mask→flatten proven at unit level); nothing missing for M4's GPU gate.
- Milestone 4 (Task 6): DONE · commit ccaee646 · Opus validator APPROVED (independently re-rendered on GPU + own PNG hashes) · 2026-07-05
  - **AUTHORITATIVE LIVE GATE — GPU-PROVEN.** `test_appflow_editor_toggle_render` PASSED on a REAL GPU (Microsoft D3D12/dzn on an RTX 3060 — this env resolves to real hardware via VixenSelectWslGpuIcd, NOT lavapipe; renders ~25-50s). boreDiffPixels=6400/6400 (full 80×80 bore region changed on toggle, ≫ the >3000 threshold). Validator's OWN md5sums (stale PNGs deleted first): initial `23656fe3…` == undone `23656fe3…` byte-for-byte; toggled `67870990…` differs. Undo proof is an EXACT full-RGBA-buffer memcmp (stronger than PNG equality).
  - Drives the REAL `AppFlowRuntime` (Load → Layers().SetLayerCount(3) → ToggleLayer(2, onChanged) → Undo), no mask bypass; `renderMask` re-reads the current mask each call so the toggle genuinely changes the flattener input. **A broken/no-op undo WOULD fail this gate** (would leave mask=0b011, memcmp fails; the `changed==2` assertion also catches a missing inverse). Render body extracted verbatim from `test_editor_document_render.cpp`'s ablation test (cut-layer index 2, same camera/threshold); template's own ablation re-run green as GPU-path sanity.
  - Structural rule honored: 2 files only (test .cpp +602, test_critical_nodes.cmake +35 linking AppFlow via `if(TARGET AppFlow)`); NO AppFlow-lib source touched. Tree clean.
  - **DURABLE (env):** VixenSelectWslGpuIcd resolves to a REAL GPU (D3D12/dzn RTX 3060) here, not lavapipe — GPU render tests genuinely run headless but take ~25-50s. `cmake --build` auto-backgrounds in this harness; overlapping builds of one link target race and truncate the binary — build one target at a time, block on the process. First VIXEN configure with -DBUILD_TESTS=ON ~500s (network FetchContent), reconfigure ~115s. (Logged to ~/.claude/friction.md.)
- Milestone 5 (Task 7): DONE · commit TBD · 2026-07-05
  - **★ Inc-2 COMPLETE ★** Full offline AppFlow suite from fresh `cmake --build` (single invocation, all 7 targets): **27/27 PASS**, zero Inc-1 regression — `test_layer_controller` 4/4, `test_snapshot_undo` 6/6 (Inc-2 new); `test_appflow_golden` 4/4, `test_flow_state_machine` 3/3, `test_action_stack` 4/4, `test_binding_store` 3/3, `test_appflow_loader` 3/3 (Inc-1, byte-identical pass counts to M3 baseline). GPU render-gate `test_appflow_editor_toggle_render` re-run fresh (rebuilt, no-op — binary already current): **PASSED** on real hardware (D3D12/dzn, RTX 3060 Laptop GPU), boreDiffPixels=6400/6400 matching the M4-recorded value exactly, 29.2s. Inc-2 (LayerController + generic snapshot-fallback undo + runtime re-flatten seam + GPU toggle→undo proof) is done end-to-end.

---

## File Structure (Inc 2)

**New (AppFlow lib):**
- `VIXEN/libraries/AppFlow/include/LayerController.h` + `src/LayerController.cpp` — enabled-mask source of truth.
- `VIXEN/libraries/AppFlow/tests/test_layer_controller.cpp` — LayerController units.
- `VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` — snapshot path + parity + grouping.

**Modified (AppFlow lib):**
- `VIXEN/libraries/AppFlow/include/ActionStack.h` + `src/ActionStack.cpp` — add the snapshot dispatch path.
- `VIXEN/libraries/AppFlow/include/AppFlowRuntime.h` + `src/AppFlowRuntime.cpp` — own a LayerController; add toggle/snapshot dispatch.
- `VIXEN/libraries/AppFlow/tests/CMakeLists.txt` — register the 2 new offline test targets.

**Modified (editor):**
- `VIXEN/application/editor/include/EditorDocumentModel.h` — read enabled mask from a caller-supplied mask/`LayerController`; remove `enabledOverride_` + `ToggleLayer`/`IsEnabled`/`ConsumeDirty`.
- Callers of the removed members: `VIXEN/application/editor/source/EditorApplication.cpp` (`ToggleLayer`, `Update`'s `ConsumeDirty`) — adjust to the new seam (Inc-2 keeps the app compiling; the full windowed rewire is Inc-2b, so the minimal change here is: route through LayerController where trivial, leave the windowed input path otherwise intact and compiling).

**New (RenderGraph tests — GPU gate):**
- `VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp` — headless GPU gate.
- `VIXEN/libraries/RenderGraph/tests/…/CMakeLists.txt` — register it (link AppFlow + RenderGraph + SVO).

---

### Task 1: `LayerController` — enabled-mask source of truth

**Files:**
- Create: `VIXEN/libraries/AppFlow/include/LayerController.h`, `src/LayerController.cpp`
- Create: `VIXEN/libraries/AppFlow/tests/test_layer_controller.cpp`

**Interfaces:**
- Consumes: `Generated::LayerState` (from `generated/AppFlow.g.h` — `struct LayerState { uint32_t enabledMask; }`).
- Produces: `class Vixen::AppFlow::LayerController` with `SetLayerCount(uint32_t)`, `uint32_t LayerCount() const`, `bool IsEnabled(uint32_t) const`, `bool Toggle(uint32_t)`, `uint32_t Mask() const`, `void SetMask(uint32_t)`, `Generated::LayerState Snapshot() const`, `void Restore(const Generated::LayerState&)`.

- [x] **Step 1: Write the failing test.**

```cpp
#include <gtest/gtest.h>
#include "LayerController.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using Vixen::AppFlow::Generated::LayerState;

TEST(LayerController, DefaultsAllEnabled) {
    LayerController lc;
    lc.SetLayerCount(3);
    EXPECT_EQ(lc.LayerCount(), 3u);
    EXPECT_TRUE(lc.IsEnabled(0));
    EXPECT_TRUE(lc.IsEnabled(1));
    EXPECT_TRUE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b111u);
}

TEST(LayerController, ToggleFlipsBit) {
    LayerController lc; lc.SetLayerCount(3);
    EXPECT_TRUE(lc.Toggle(2));
    EXPECT_FALSE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b011u);
    EXPECT_TRUE(lc.Toggle(2));
    EXPECT_TRUE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b111u);
}

TEST(LayerController, OutOfRangeIsNoOp) {
    LayerController lc; lc.SetLayerCount(3);
    EXPECT_FALSE(lc.Toggle(3));      // i >= count → no-op, false
    EXPECT_FALSE(lc.IsEnabled(9));   // out of range → false
    EXPECT_EQ(lc.Mask(), 0b111u);    // unchanged
}

TEST(LayerController, SnapshotRestoreRoundTrips) {
    LayerController lc; lc.SetLayerCount(3);
    lc.Toggle(1);                    // 0b101
    LayerState snap = lc.Snapshot();
    lc.Toggle(0); lc.Toggle(2);      // 0b000
    EXPECT_EQ(lc.Mask(), 0b000u);
    lc.Restore(snap);
    EXPECT_EQ(lc.Mask(), 0b101u);
}
```

- [x] **Step 2: Run to verify it fails.** No CMake target yet (added Task 5). Verify via standalone compile: `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/tests/test_layer_controller.cpp` → Expected: FAIL (`LayerController.h` not found / undefined).

- [x] **Step 3: Write the implementation.** `LayerController.h` declares the class; `.cpp` implements it. Store `uint32_t mask_ = 0`, `uint32_t count_ = 0`. `SetLayerCount(n)`: clamp `n<=32` (assert or clamp — document; the design caps at 32), set `count_=n`, `mask_ = (n>=32) ? 0xFFFFFFFFu : ((1u<<n)-1u)` (all-enabled). `IsEnabled(i)`: `i<count_ && (mask_>>i)&1u`. `Toggle(i)`: if `i>=count_` return false; `mask_ ^= (1u<<i)`; return true. `Mask()`: `mask_`. `SetMask(m)`: `mask_ = m & ((count_>=32)?0xFFFFFFFFu:((1u<<count_)-1u))`. `Snapshot()`: `{mask_}`. `Restore(s)`: `SetMask(s.enabledMask)`.

- [x] **Step 4: Verify compile.** `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/src/LayerController.cpp` → Expected: clean.

- [x] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/LayerController.h VIXEN/libraries/AppFlow/src/LayerController.cpp VIXEN/libraries/AppFlow/tests/test_layer_controller.cpp
git commit -m "feat(appflow): LayerController — layer enabled-mask source of truth (Inc-2)"
```

---

### Task 2: `ActionStack` snapshot-fallback path

**Files:**
- Modify: `VIXEN/libraries/AppFlow/include/ActionStack.h`, `src/ActionStack.cpp`

**Interfaces:**
- Consumes: existing `ActionStack` (Inc-1: `LoadActions`, `BeginGroup`/`EndGroup`, `Dispatch(id, ApplyFn)`, `Undo`/`Redo`, `UndoDepth`/`RedoDepth`; `Entry{id, apply}`, `Group{id, entries}`).
- Produces: new method `DispatchResult DispatchWithSnapshot(FlowActionId id, void* footprint, uint32_t footprintBytes, ApplyFn apply, std::function<void()> onRestore)` and an extended `Entry` that can be inverse-mode (Inc-1) OR snapshot-mode. `Undo`/`Redo` handle both entry modes.

- [x] **Step 1: Write the failing test** (in `tests/test_snapshot_undo.cpp`, new file):

```cpp
#include <gtest/gtest.h>
#include "ActionStack.h"
#include "generated/AppFlow.g.h"
#include <cstring>
using namespace Vixen::AppFlow;
using Vixen::AppFlow::Generated::FlowActionId;
using Vixen::AppFlow::Generated::AppFlowContainerView;

TEST(SnapshotUndo, SnapshotRestoresFootprintOnUndo) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t state = 5;              // a 4-byte footprint
    int restores = 0;
    // snapshot-mode dispatch: save `state` bytes, then apply mutates it; undo memcpy's them back.
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &state, sizeof(state),
                            [&](bool fwd){ if (fwd) state = 99; },
                            [&]{ ++restores; });
    EXPECT_EQ(state, 99u);
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(state, 5u);            // footprint bytes restored
    EXPECT_EQ(restores, 1);          // onRestore fired
}

TEST(SnapshotUndo, SnapshotRedoReappliesForward) {
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t state = 5; 
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &state, sizeof(state),
                            [&](bool fwd){ if (fwd) state = 99; }, []{});
    st.Undo();
    EXPECT_EQ(st.Redo(), DispatchResult::Ok);
    EXPECT_EQ(state, 99u);           // forward apply re-ran
}

TEST(SnapshotUndo, GenericOverFootprintSize) {
    // an 8-byte footprint proves the engine keys off footprintBytes, not a hardcoded type
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint64_t big = 0xAAAAAAAABBBBBBBBull;
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &big, sizeof(big),
                            [&](bool fwd){ if (fwd) big = 0; }, []{});
    EXPECT_EQ(big, 0ull);
    st.Undo();
    EXPECT_EQ(big, 0xAAAAAAAABBBBBBBBull);
}
```

- [x] **Step 2: Run to verify it fails.** `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` → Expected: FAIL (`DispatchWithSnapshot` undefined).

- [x] **Step 3: Write the implementation.** In `ActionStack.h`, extend `Entry` to carry snapshot mode:

```cpp
struct Entry {
    FlowActionId id;
    ApplyFn apply;                         // inverse-mode: apply(false) undoes. snapshot-mode: apply(true) only.
    // snapshot mode (empty when inverse-mode):
    void* footprint = nullptr;             // where to restore bytes
    uint32_t footprintBytes = 0;
    std::vector<uint8_t> snapshot;         // saved bytes (size == footprintBytes)
    std::function<void()> onRestore;       // called after a snapshot restore
    bool IsSnapshot() const { return footprint != nullptr; }
};
```

Declare `DispatchResult DispatchWithSnapshot(FlowActionId id, void* footprint, uint32_t footprintBytes, ApplyFn apply, std::function<void()> onRestore);`. In `.cpp`: it mirrors `Dispatch` (unknown id → RejectedByState; clear redo_; open/append group) but BEFORE `apply(true)` it `snapshot.resize(footprintBytes); std::memcpy(snapshot.data(), footprint, footprintBytes);` and stores `footprint`/`footprintBytes`/`onRestore` on the entry. In `Undo`, for each entry in reverse: if `IsSnapshot()` → `std::memcpy(footprint, snapshot.data(), footprintBytes); if (onRestore) onRestore();` else `apply(false)`. In `Redo`, forward order: `apply(true)` for both modes (snapshot re-runs the forward apply; the redo's own snapshot for a subsequent undo is re-taken — for Inc-2 simplicity, re-snapshot on redo is NOT required since the test only does undo-then-redo once; document that a second undo after redo uses the original snapshot, which is correct because the footprint was restored forward to the same post-apply state). Add `#include <cstring>`.

- [x] **Step 4: Verify compile** both header consumers + the impl: `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/src/ActionStack.cpp` → clean.

- [x] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/ActionStack.h VIXEN/libraries/AppFlow/src/ActionStack.cpp VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp
git commit -m "feat(appflow): ActionStack generic snapshot-fallback undo path (Inc-2)"
```

---

### Task 3: Inverse-vs-snapshot parity + grouping tests

**Files:**
- Modify: `VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` (add the parity + mixed-group tests)

**Interfaces:**
- Consumes: `ActionStack` (Inc-1 `Dispatch` inverse path + Task-2 `DispatchWithSnapshot`), `LayerController` (Task 1).

- [x] **Step 1: Write the failing tests.**

```cpp
#include "LayerController.h"
using Vixen::AppFlow::LayerController;

// The design headline: undo-via-inverse and undo-via-snapshot of an equivalent change
// leave LayerController in byte-identical state.
TEST(SnapshotUndo, InverseAndSnapshotParity) {
    // Path A — inverse: toggle layer 2 via a self-inverse apply.
    LayerController a; a.SetLayerCount(3);
    ActionStack sa;
    sa.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    sa.Dispatch(FlowActionId::ToggleLayer, [&](bool /*fwd*/){ a.Toggle(2); });  // self-inverse: toggle both ways
    sa.Undo();

    // Path B — snapshot: same net change, undone by restoring the footprint.
    LayerController b; b.SetLayerCount(3);
    ActionStack sb;
    sb.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t mask = b.Mask();
    sb.DispatchWithSnapshot(FlowActionId::ToggleLayer, &mask, sizeof(mask),
                            [&](bool fwd){ if (fwd) { b.Toggle(2); mask = b.Mask(); } },
                            [&]{ b.SetMask(mask); });
    sb.Undo();

    EXPECT_EQ(a.Mask(), b.Mask());          // both back to 0b111
    EXPECT_EQ(a.Snapshot().enabledMask, b.Snapshot().enabledMask);
}

// A group mixing a self-inverse and a snapshot action undoes as one unit, both reversed.
TEST(SnapshotUndo, MixedGroupUndoesAsOneUnit) {
    LayerController lc; lc.SetLayerCount(3);
    ActionStack st;
    st.LoadActions(AppFlowContainerView::actions().data(), AppFlowContainerView::actions().size());
    uint32_t mask = lc.Mask();
    st.BeginGroup(1);
    st.Dispatch(FlowActionId::ToggleLayer, [&](bool){ lc.Toggle(0); });                     // inverse
    st.DispatchWithSnapshot(FlowActionId::ToggleLayer, &mask, sizeof(mask),
                            [&](bool fwd){ if (fwd) { lc.Toggle(1); mask = lc.Mask(); } },
                            [&]{ lc.SetMask(mask); });                                        // snapshot
    st.EndGroup();
    EXPECT_EQ(lc.Mask(), 0b100u);           // layers 0 and 1 disabled
    EXPECT_EQ(st.Undo(), DispatchResult::Ok);
    EXPECT_EQ(lc.Mask(), 0b111u);           // ONE undo reverted BOTH
    EXPECT_EQ(st.UndoDepth(), 0u);
}
```

- [x] **Step 2: Run to verify it fails.** `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` → FAIL only if the impl is wrong; if Task 2 is correct these compile — the point is they must PASS once built (Task 5). If the mixed-group reverse-order handling is wrong they'll fail at runtime.

- [x] **Step 3: No new implementation expected** — Tasks 1–2 should already satisfy these. If `MixedGroupUndoesAsOneUnit` reveals a reverse-order bug (snapshot + inverse in one group), fix `ActionStack::Undo` so it walks entries in strict reverse regardless of mode. Document any fix.

- [x] **Step 4: Verify compile.** As Step 2.

- [x] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp VIXEN/libraries/AppFlow/src/ActionStack.cpp
git commit -m "test(appflow): inverse-vs-snapshot parity + mixed-group undo (Inc-2)"
```

---

### Task 4: `AppFlowRuntime` owns LayerController + toggle/snapshot dispatch

**Files:**
- Modify: `VIXEN/libraries/AppFlow/include/AppFlowRuntime.h`, `src/AppFlowRuntime.cpp`

**Interfaces:**
- Consumes: `LayerController` (Task 1), `ActionStack` inverse + `DispatchWithSnapshot` (Tasks 2–3), existing `AppFlowRuntime` (Inc-1).
- Produces: `AppFlowRuntime` gains `LayerController& Layers()`, and `DispatchResult ToggleLayer(uint32_t index, std::function<void()> onChanged)` — dispatches a self-inverse `ToggleLayer` action that flips `Layers().Toggle(index)`, calls `onChanged` (the re-flatten hook) after each apply/undo, and publishes `ActionApplied`. Undo/Redo already publish (Inc-1) — `onChanged` must also fire on undo/redo, so the runtime wires `onChanged` into the ApplyFn (called on both forward and inverse).

- [x] **Step 1: Write the failing test** (`tests/test_snapshot_undo.cpp` or a small addition to a runtime test — put it in `test_snapshot_undo.cpp` to avoid a new file):

```cpp
#include "AppFlowRuntime.h"

TEST(SnapshotUndo, RuntimeToggleLayerAndUndoFireOnChanged) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    ASSERT_EQ(rt.Load(), LoadResult::Ok);
    rt.Layers().SetLayerCount(3);
    int changed = 0;
    EXPECT_EQ(rt.ToggleLayer(2, [&]{ ++changed; }), DispatchResult::Ok);
    EXPECT_FALSE(rt.Layers().IsEnabled(2));
    EXPECT_EQ(changed, 1);                 // onChanged fired on apply
    EXPECT_EQ(rt.Undo(), DispatchResult::Ok);
    EXPECT_TRUE(rt.Layers().IsEnabled(2)); // reverted
    EXPECT_EQ(changed, 2);                 // onChanged fired again on undo
}
```

- [x] **Step 2: Run to verify it fails.** `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp` → FAIL (`Layers()`/`ToggleLayer` undefined on runtime).

- [x] **Step 3: Write the implementation.** Add `LayerController layers_;` member to `AppFlowRuntime` + `LayerController& Layers() { return layers_; }`. Implement `ToggleLayer(index, onChanged)`:

```cpp
DispatchResult AppFlowRuntime::ToggleLayer(uint32_t index, std::function<void()> onChanged) {
    // Self-inverse: the same apply toggles both forward and inverse; onChanged fires on both.
    auto apply = [this, index, onChanged](bool /*forward*/) {
        layers_.Toggle(index);
        if (onChanged) onChanged();
    };
    const DispatchResult r = stack_.Dispatch(FlowActionId::ToggleLayer, apply);
    if (r == DispatchResult::Ok) {
        Publish(AppFlowChangedEvent::Kind::ActionApplied, fsm_.Current(), FlowActionId::ToggleLayer, 0);
    }
    return r;
}
```

Because `stack_.Undo()` runs `apply(false)` (which toggles back + fires onChanged) and Inc-1's `Undo()` already publishes `ActionUndone`, no extra wiring is needed for undo — verify `onChanged` fires on undo via the test.

- [x] **Step 4: Verify compile.** `g++ -std=c++23 -fsyntax-only -I VIXEN/libraries/AppFlow/include -I VIXEN/libraries/EventBus/include VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp` → clean.

- [x] **Step 5: Commit.**

```bash
git add VIXEN/libraries/AppFlow/include/AppFlowRuntime.h VIXEN/libraries/AppFlow/src/AppFlowRuntime.cpp VIXEN/libraries/AppFlow/tests/test_snapshot_undo.cpp
git commit -m "feat(appflow): AppFlowRuntime owns LayerController + ToggleLayer/undo dispatch (Inc-2)"
```

---

### Task 5: Editor re-flatten seam + CMake for the offline test targets → build green

**Files:**
- Modify: `VIXEN/application/editor/include/EditorDocumentModel.h` (read mask from a mask param; remove `enabledOverride_`/`ToggleLayer`/`IsEnabled`/`ConsumeDirty`)
- Modify: `VIXEN/application/editor/source/EditorApplication.cpp` (adjust the two call sites that used the removed members so the editor still compiles)
- Modify: `VIXEN/libraries/AppFlow/tests/CMakeLists.txt` (register `test_layer_controller` + `test_snapshot_undo`)

**Interfaces:**
- Consumes: `LayerController` (Task 1), the AppFlow runtime (Task 4).
- Produces: `EditorDocumentModel::Flatten(uint32_t enabledMask, std::vector<uint8_t>& out, std::string& err)` and `FlattenToRecipeEntry(uint32_t enabledMask, RecipeEntry& out, std::string& err)` — the mask replaces the removed internal `enabledOverride_`.

- [x] **Step 1: Write the failing test** — the CMake build itself is the gate here; there's no new unit beyond Tasks 1–4. First register the offline targets so the previous tasks' tests can actually run. Add to `VIXEN/libraries/AppFlow/tests/CMakeLists.txt`'s test-name list: `test_layer_controller` and `test_snapshot_undo` (mirroring the existing `foreach(t ...)` pattern that builds one exe per test, links `GTest::gtest_main AppFlow`).

- [x] **Step 2: Refactor `EditorDocumentModel`.** Change `Flatten` to `bool Flatten(uint32_t enabledMask, std::vector<uint8_t>& outVrc1Blob, std::string& err) const` — build a `std::vector<uint8_t> ovr(view_.header.layerCount)` from the mask (`ovr[i] = (enabledMask>>i)&1u`) and pass `&ovr` to `FlattenVoxelDocument`. Same for `FlattenToRecipeEntry(uint32_t enabledMask, ...)`. Delete `enabledOverride_`, `ToggleLayer`, `IsEnabled`, `ConsumeDirty`, and the `enabledOverride_` init in `Load`. `Save` currently reads `enabledOverride_` — change it to take an `enabledMask` param too (`bool Save(uint32_t enabledMask, const std::string& outPath, std::string& err) const`) and derive the per-layer `.enabled` from the mask bit.

- [x] **Step 3: Fix `EditorApplication.cpp` call sites** so it compiles (full windowed rewire is Inc-2b — here, minimal): `ApplyDocumentToScene` calls `doc_.FlattenToRecipeEntry(mask, entry, err)` where `mask` comes from a new `LayerController layers_;` member on `EditorApplication` (init `layers_.SetLayerCount(doc_.LayerCount())` after load). `ToggleLayer(i)` becomes `layers_.Toggle(i)` + set a re-apply flag. `Update`'s `ConsumeDirty()` gate becomes a local `bool dirty_` the toggle sets (EditorApplication owns the dirty flag now that the model doesn't). `SaveDocument` calls `doc_.Save(layers_.Mask(), outPath, err)`. Keep the existing `DrainClickedElementId`/`ParseLayerToggleId`/`glfwGetKey(S)` input path AS-IS (Inc-2b replaces it) — just have it drive `layers_` instead of `doc_`.

- [x] **Step 4: Configure + build the offline targets, run them.**

Run:
```
cmake -S VIXEN -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAUTO_LOCATE_VULKAN=ON -DBUILD_TESTS=ON   # if not already configured
cmake --build build --config Debug --target AppFlow test_layer_controller test_snapshot_undo --parallel 16
```
Then run the binaries directly (KI-014 — no ctest):
`build/libraries/AppFlow/tests/test_layer_controller --gtest_brief=1` → Expected: all PASS
`build/libraries/AppFlow/tests/test_snapshot_undo --gtest_brief=1` → Expected: all PASS
Also build the editor to confirm the refactor compiles: `cmake --build build --target vixen_editor` (or the editor target name — find it in `VIXEN/application/editor/CMakeLists.txt`) → 0 errors.

- [x] **Step 5: Commit.**

```bash
git add VIXEN/application/editor/include/EditorDocumentModel.h VIXEN/application/editor/source/EditorApplication.cpp VIXEN/libraries/AppFlow/tests/CMakeLists.txt
git commit -m "refactor(appflow): editor reads layer mask from LayerController; offline test targets green (Inc-2)"
```

---

### Task 6: Headless GPU render-gate — toggle→re-flatten→render→undo through AppFlow

**Files:**
- Create: `VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp`
- Modify: the CMakeLists that registers `test_editor_document_render` (same dir) to add the new test, linking `AppFlow` alongside RenderGraph + SVO.

**Interfaces:**
- Consumes: `AppFlowRuntime` + `LayerController` (Tasks 1–4); the render harness pattern from `test_editor_document_render.cpp` (Vulkan bring-up via `VixenSelectWslGpuIcd` + software device, `BodyOctreeSceneNode`, bake via `RecipeRegistry`/`RecipeBaker`, PNG via `stb_image_write`, bore-column pixel-diff).

- [x] **Step 1: Write the test.** Model it on `test_editor_document_render.cpp` (read that file first). Structure:

```cpp
// Headless GPU gate: a ToggleLayer dispatched through AppFlowRuntime re-flattens + renders;
// Undo restores the render byte-for-byte. The authoritative Inc-2 proof.
// Reuses test_editor_document_render.cpp's Vulkan bring-up + bore-column pixel-diff helpers.
TEST(AppFlowEditorToggleRender, ToggleThenUndoRestoresRender) {
    // 1. Load the golden document (same doc test_editor_document_render.cpp uses).
    // 2. AppFlowRuntime rt(nullptr, 1); rt.Load(); rt.Layers().SetLayerCount(doc.LayerCount());
    // 3. auto renderMask = [&](uint32_t mask)->std::vector<uint8_t> {
    //        flatten doc with `mask` → RecipeEntry → Register → bake → render to RGBA8 buffer (the
    //        harness copied from test_editor_document_render.cpp); return the pixels. };
    // 4. auto pixels_initial = renderMask(rt.Layers().Mask());   // all enabled
    // 5. int changed = 0;
    //    rt.ToggleLayer(CUT_LAYER_INDEX, [&]{ ++changed; });
    //    auto pixels_toggled = renderMask(rt.Layers().Mask());   // cut disabled
    // 6. rt.Undo();
    //    auto pixels_undone = renderMask(rt.Layers().Mask());    // back to all enabled
    // 7. ASSERT: bore-column of pixels_toggled differs from pixels_initial (toggle rendered);
    //            pixels_undone == pixels_initial (byte-for-byte — undo restored exactly).
    // Write all three to /tmp/appflow_toggle_{initial,toggled,undone}.png for inspection.
}
```

Extract the render-a-mask-to-pixels body verbatim from `test_editor_document_render.cpp` (its flatten→register→bake→render path), parameterized by the enabled mask. Use the SAME `CUT_LAYER_INDEX` (the "cut" layer, index 2 in the golden doc) whose toggle produces the bore-column difference that test already relies on.

- [x] **Step 2: Register + build.** Add the test to the CMake in `VIXEN/libraries/RenderGraph/tests/Nodes/` (find how `test_editor_document_render` is added — likely a `foreach`/`add_executable` + `target_link_libraries(... RenderGraph SVO ...)`); add `AppFlow` to its link libraries. Build: `cmake --build build --target test_appflow_editor_toggle_render --parallel 16` → 0 errors.

- [x] **Step 3: Run the gate (headless, lavapipe/software).**

Run: `build/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render --gtest_brief=1` (or wherever it lands — mirror `test_editor_document_render`'s output path).
Expected: PASS — `ToggleThenUndoRestoresRender`. Inspect `/tmp/appflow_toggle_*.png`: initial and undone identical, toggled visibly different at the bore.
If the ICD isn't found, set the Dozen ICD as `test_editor_document_render` does (it calls `VixenSelectWslGpuIcd` in-process).

- [x] **Step 4: Commit.**

```bash
git add VIXEN/libraries/RenderGraph/tests/Nodes/test_appflow_editor_toggle_render.cpp VIXEN/libraries/RenderGraph/tests/Nodes/CMakeLists.txt
git commit -m "test(appflow): headless GPU render-gate — toggle→re-flatten→render→undo through AppFlow (Inc-2)"
```

---

### Task 7: Inc-2 close-out — full-suite verify + docs

**Files:**
- Modify: `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2-Plan-2026-07.md` (mark DONE + Progress Log)

- [x] **Step 1: Full offline suite from fresh output.** Rebuild + run all AppFlow test binaries (Inc-1 + Inc-2): `test_appflow_golden`, `test_flow_state_machine`, `test_action_stack`, `test_binding_store`, `test_appflow_loader`, `test_layer_controller`, `test_snapshot_undo`. Paste the pass counts. Confirm no Inc-1 regression.

- [x] **Step 2: Confirm the GPU gate** `test_appflow_editor_toggle_render` PASSes (the authoritative proof; live-run-is-authoritative rule).

- [x] **Step 3: Commit the close-out.**

```bash
git add VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2-Plan-2026-07.md
git commit -m "docs(appflow): Inc-2 COMPLETE — LayerController + snapshot-fallback + GPU render-gate green"
```

---

## Self-Review

**Spec coverage (Inc-2 design §2–§6 vs. plan):**
- §2.1 LayerController → Task 1. ✓
- §2.2 snapshot-fallback engine (generic by footprintBytes) → Task 2; parity + grouping → Task 3. ✓
- §2.3 re-flatten seam (EditorDocumentModel reads mask from LayerController) → Task 5. ✓
- §3 data flow (toggle→re-flatten→render→undo) → Task 4 (dispatch) + Task 6 (GPU gate). ✓
- §4.1 GPU live gate → Task 6. §4.2 offline units → Tasks 1/2/3/4 (+ built in 5). ✓
- §5 error handling (out-of-range no-op, empty-stack results) → Task 1 test + carried from Inc-1. ✓
- §5 constraints (≤32 layers; EditorDocumentModel dep) → Task 1 (clamp) + Task 5 (seam). ✓
- §6 deferred (Inc-2b windowed rewire) → explicitly NOT in these tasks (Task 5 keeps the windowed input path as-is). ✓

**Placeholder scan:** No TBD/TODO. Task 6's test body is a structured skeleton with an explicit "extract verbatim from test_editor_document_render.cpp" instruction — the render harness is large and copied, not re-invented; this is a directed reuse, not a placeholder.

**Type consistency:** `LayerController` API (`SetLayerCount`/`Toggle`/`Mask`/`SetMask`/`Snapshot`/`Restore`) used identically in Tasks 1/3/4/6. `DispatchWithSnapshot(id, void*, uint32_t, ApplyFn, onRestore)` consistent in Tasks 2/3. `AppFlowRuntime::ToggleLayer(uint32_t, std::function<void()>)` + `Layers()` consistent in Tasks 4/6. `EditorDocumentModel::Flatten(uint32_t mask, ...)` / `FlattenToRecipeEntry(uint32_t, ...)` / `Save(uint32_t, ...)` consistent in Task 5. `LayerState{enabledMask}` (Inc-1) consumed, not redefined. ✓

---

## Execution Handoff

Inc-2 plan complete. Same shape as Inc-1 — a good fit for the **post-brainstorm-context-manager** pipeline (fresh Sonnet implementer per milestone, Opus validator, discarded contexts). The GPU gate (Task 6) is the one milestone whose validator must actually run the render on lavapipe.
