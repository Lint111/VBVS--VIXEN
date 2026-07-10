# R6 Finding — the editor render does not reflect the layer mask (and why the R6 visual gate was reworked)

**Date:** 2026-07-10
**Context:** Inc-4 R6 (AppFlow kernel-glue-transplant reframe close-out). See
`AppFlow-Kernel-Glue-Transplant-Reframe-Design-2026-07.md`.
**Status:** RESOLVED — not a render/sync bug (unchanged). The follow-up content/LOD matter this
finding flagged (editor mip-fallback → mask invisible) is ALSO now RESOLVED — see
`Editor-Brick-Residency-Fix-Plan-2026-07.md`: `EditorApplication::ApplyDocumentToScene` now grants
brick residency unconditionally, so the fine SDF march (where the mask lives) runs, and the mask IS
visible. Live WSL/Dozen verification: `capture_5` vs `capture_45` differ by 62 pixels (all inside
the object's own on-screen bounding box, since the editor's document body is small on screen — see
that plan doc for the measurement), and the undo/redo round-trip is byte-exact
(`capture_75==capture_5`, `capture_105==capture_45`). `test_editor_toggle_undo_capture.cpp`'s R6
gate now asserts the real visual round-trip again (`UndoRedoRestoresRenderByteExact`) plus the mask
delta itself (`FirstEditReachesRenderPipeline`, calibrated to the measured 62px, not a residency-
transition smoke check). The sections below are the original investigation record, kept for
context on why the assertions were removed before they were restored.

## TL;DR

The R6 windowed gate originally asserted a **byte-exact visual undo/redo round-trip**
(`capture_75 == capture_5`, `capture_105 == capture_45`). A max-effort GPU-boundary investigation
proved that assertion tested an **invisible-by-design quantity**: at the editor's orbit camera the
body renders the **mask-invariant mip-fallback path** (`OctreeConfig.brickResident == 0`), so the
layer mask (a CSG cut that only changes SDF field values) has **~0 visible effect**. The mask is
provably correct everywhere the software controls it; it just never reaches visible pixels in the
editor's current camera + LOD path. The gate was reworked to assert what IS real and observable.

## What was proven CORRECT (hard, instrumented, live WSL/Dozen evidence)

Every software-controllable layer of the render path is correct on a runtime layer toggle/undo/redo:

1. **CPU state** — LayerController/ActionStack mask trail `7 → 3(toggle) → 7(undo) → 3(redo)`,
   correct undo/redo depths (already proven by impl-r6).
2. **Shell cache derivation + upload** — `Rematerialize()` re-derives + re-uploads the shell
   channelPool (binding 11) every edit, to BOTH double-buffer slots.
3. **GPU buffer CONTENTS** — mapping and checksumming the *actual GPU buffer memory* shows the shell
   channelPool holds mask-differentiated bytes `A / B / A` across toggle/undo/redo
   (`gpuMemCksum == srcCksum` every time).
4. **Descriptor path** — the FRESH, mask-correct `VkBuffer` is written into the exact
   `VkDescriptorSet` the dispatch binds (dstSet == dispatch's descriptorSet, 1:1 across all 4
   swapchain sets). The command buffer (`ComputeDispatchNode`, node "test_dispatch") re-records
   every frame.
5. **The shader reads it fresh** — a channelPool-hash tap injected into the REAL pipeline output
   reads back `A / B / A` per mask. So the SDF-field input the shader sees is correct and fresh.

Two candidate "fixes" were tested and changed **nothing** (confirming the descriptor/sync layer is
already correct): forcing an all-frames descriptor rebind every frame; draining the device before
every descriptor update.

## The actual behaviour

- The one visible change in the whole scripted run — `d3db…` (baseline) → `449027…` (post first
  edit) — is the **non-resident → resident transition** (mip fallback → resident), triggered by the
  first edit's `Rematerialize`. It is **not** the mask.
- **Decisive control:** two consecutive `toggle:2` (mask7 → mask3 → mask7, both post-residency)
  render **byte-identical**. And with NO edits, the baseline render is stable forever (residency is
  only requested on camera movement; scripted runs never move the camera).
- A shader tap of `configs[0].brickResident` reads **0** at the drawn octree → the body renders via
  `shadeFromMipSample` (the coarse, mask-invariant mip fallback), never the fine SDF march where the
  mask lives.

**Why the headless gate (`test_appflow_editor_toggle_render`) sees the mask and the editor doesn't:**
the headless gate uses a bespoke bore-aligned camera AND builds a fresh `BodyOctreeSceneNode` per
mask; the live editor uses a general orbit camera where the cut delta is ~6px at best, AND renders
the mip fallback. So the mask is effectively invisible in the editor render.

## What the R6 gate asserted BEFORE the residency fix (`test_editor_toggle_undo_capture.cpp`, historical)

1. **`ToggleUndoRedoStateTrailThroughWindowedRun`** — parses the running editor's own
   `[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=..` lines and asserts the mask trail
   `7 → 3 → 7 → 3` with correct ActionStack depth movement. This is the real proof undo/redo work,
   through the windowed registry-dispatch path. (Unchanged by the residency fix — kept as-is.)
2. **`FirstEditReachesRenderPipeline`** — a one-shot smoke check that SOME visible change happened
   when the first edit hit the render pipeline (`capture_5 != capture_45`). Explicitly asserted the
   **residency transition**, NOT the mask. Could not assert undo≠redo (residency latched).
3. **`BackButtonReachesReturnInRunningEditor`** — back-button → Return (`afterBack == Editing`).
   Unchanged; proven + real.

## Follow-up — RESOLVED 2026-07-10 (Editor-Brick-Residency-Fix-Plan-2026-07)

The follow-up this finding originally flagged ("making the editor body request residency would let
the SDF march run and surface the mask delta") is now DONE:
`EditorApplication::ApplyDocumentToScene` calls the new `VulkanGraphApplication::
RequestBodyBrickResidency(true)` forwarder unconditionally after every edit, and
`EditorApplication::SkipResidencyHeuristic()` overrides the base class hook so the main app's
camera-driven `UpdateBodySceneResidency` (which a static editor session never satisfies) cannot
stomp the grant back to `false`. Live WSL/Dozen verification confirmed brick-level traversal
(`BRICK_ENTER`/`BRICK_EXIT`) now occurs where only mip-fallback ran before, and the R6 gate's
`FirstEditReachesRenderPipeline` now asserts the mask delta itself (62px measured, calibrated
threshold) plus a NEW `UndoRedoRestoresRenderByteExact` test restoring the byte-exact visual
round-trip this finding originally removed. See `Editor-Brick-Residency-Fix-Plan-2026-07.md` for
the fix + full live-verify evidence.
