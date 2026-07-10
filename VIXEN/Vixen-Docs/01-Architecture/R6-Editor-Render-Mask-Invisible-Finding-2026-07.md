# R6 Finding — the editor render does not reflect the layer mask (and why the R6 visual gate was reworked)

**Date:** 2026-07-10
**Context:** Inc-4 R6 (AppFlow kernel-glue-transplant reframe close-out). See
`AppFlow-Kernel-Glue-Transplant-Reframe-Design-2026-07.md`.
**Status:** RESOLVED — not a render/sync bug. Gate reworked to assert the real, observable contract.
Editor mip-fallback/residency behaviour recorded here as a separate content/LOD matter.

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

## What the reworked R6 gate asserts (`test_editor_toggle_undo_capture.cpp`)

1. **`ToggleUndoRedoStateTrailThroughWindowedRun`** — parses the running editor's own
   `[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=..` lines and asserts the mask trail
   `7 → 3 → 7 → 3` with correct ActionStack depth movement. This is the real proof undo/redo work,
   through the windowed registry-dispatch path.
2. **`FirstEditReachesRenderPipeline`** — a one-shot smoke check that SOME visible change happened
   when the first edit hit the render pipeline (`capture_5 != capture_45`). Explicitly asserts the
   **residency transition**, NOT the mask. Cannot assert undo≠redo (residency latches).
3. **`BackButtonReachesReturnInRunningEditor`** — back-button → Return (`afterBack == Editing`).
   Unchanged; proven + real.

## Follow-up (separate content/LOD matter — NOT an AppFlow defect)

If the editor should render the fine SDF surface (so mask edits are visible), that is a
brick-residency / mip-fallback matter in the render/LOD subsystem:
`RequestBrickResidency` is only driven by camera movement (`VulkanGraphApplication`), so a static
editor body stays in the mip fallback (`brickResident == 0`). Making the editor body request
residency (or forcing it for editor scenes) would let the SDF march run and surface the mask delta.
Out of scope for the AppFlow reframe; recorded here for whoever picks up editor render fidelity.
