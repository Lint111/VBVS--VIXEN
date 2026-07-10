# Editor Brick-Residency Fix — Plan (2026-07-10)

**Goal:** make layer-mask edits VISIBLE in the windowed editor. Follow-up from the Inc-4 R6
finding (`R6-Editor-Render-Mask-Invisible-Finding-2026-07.md`): the editor body renders the
mask-INVARIANT coarse mip-fallback (`OctreeConfig.brickResident==0`) because brick residency is
only granted by the camera-motion/frustum heuristic (`VulkanGraphApplication::UpdateBodySceneResidency`,
`InstanceWantsBrickResidency`), and a scripted/static editor session never moves the camera, so the
single edited body stays non-resident → the fine SDF march (where the layer mask lives) never runs.

**This is NOT an AppFlow defect** — the mask is proven correct at every layer (CPU/GPU-buffer/shader-input,
GPU-memory checksums + shader-output tap). It's a content/LOD wiring gap: the editor's own body is
never asked to be resident.

## Root cause (verified via codegraph, main = 5d253e2c)
- `BodyOctreeSceneNode::RequestBrickResidency(bool)` (BodyOctreeSceneNode.cpp:177) stashes the request
  (dirty-flag); `ExecuteImpl` performs the `BatchedUploader` upload next frame and sets
  `OctreeConfig.brickResident=1`. Mechanism is complete and correct — it's just never triggered for
  the editor body.
- The main app drives residency in `VulkanGraphApplication::UpdateBodySceneResidency()`
  (VulkanGraphApplication.cpp:~940-1019): per-frame `InstanceWantsBrickResidency` (frustum +
  resolvability + occlusion, orbit-tuned) → `bodyScene->RequestBrickResidency(anyInstanceWantsBricks)`.
- `EditorApplication::ApplyDocumentToScene()` (EditorApplication.cpp:238) sets up the document body
  (`SetRecipePool` + `SetBodyInstances({inst})`) but NEVER calls `RequestBrickResidency(true)`, and the
  editor's single static body at its framed orbit camera does not reliably pass the heuristic. So it
  stays mip-only.

## The fix (single milestone)
The editor's document body is the ONE object being directly edited and is always in view — it should
have residency UNCONDITIONALLY granted, not gated behind the multi-body orbit heuristic.

### Task 1 — grant residency for the editor body
- In `EditorApplication`, after the body instance is set (`ApplyDocumentToScene`, right after
  `SetBodyInstances({inst})`), request brick residency on the editor's body-octree scene node:
  `bodyScene->RequestBrickResidency(true)`. Reuse the SAME node lookup the editor already uses to
  reach `SetRecipePool`/`SetBodyInstances` (trace how those resolve the node — likely
  `VulkanGraphApplication::SetRecipePool`/`SetBodyInstances` forward to `bodyOctreeSceneNode_`; add a
  parallel forwarder e.g. `VulkanGraphApplication::RequestBodyBrickResidency(bool)` if the editor has
  no direct node handle, mirroring the existing `SetRecipePool` passthrough at VulkanGraphApplication.cpp:865).
- **Re-request after every edit.** A layer toggle/undo/redo → `ApplyDocumentToScene` → `SetRecipePool`
  → `Rematerialize` rebuilds the octree/brick buffers, which resets `brickPoolUploaded_`/residency
  state. Confirm `RequestBrickResidency(true)` is (re)issued on the edit path so residency re-lands
  after each Rematerialize (either call it again in ApplyDocumentToScene which already runs per edit,
  or verify Rematerialize preserves the residency request — READ the code and choose the correct spot;
  do not assume).
- **Do NOT let the main-app `UpdateBodySceneResidency` heuristic override it back to false.** Check
  whether the editor runs `UpdateBodySceneResidency` in its tick (it inherits VulkanGraphApplication).
  If it does and would call `RequestBrickResidency(false)` for the static body, the editor must either
  (a) skip/override that eval, or (b) the unconditional grant must win. Pick the cleanest: e.g. an
  editor-scoped "always resident" flag the eval respects, or the editor not invoking the heuristic
  eval for its single authored body. READ `UpdateBodySceneResidency` + the editor's Update/PreTick to
  see if/when it runs, and choose a fix that can't be stomped. Keep it minimal and editor-scoped —
  do NOT change the main-app residency heuristic's behavior for the demo scenes.

### Task 2 — LIVE verification (real GPU, WSL/Dozen)
- Build `vixen_editor` WSL-side (poll a foreground loop ~15-30s, never blind-wait; -j4 max; never
  overlap builds of one target). Env gotcha: do NOT set `VK_ICD_FILENAMES` (VixenSelectWslGpuIcd
  no-ops if set; leave unset → auto ICD).
- Run `VIXEN/temp/run_editor_script.bat` WSL-side (script `toggle:2@30,undo@60,redo@90,settings@100,back@110`,
  captures 5,45,75,105). With residency granted, the editor now renders the fine SDF march, so the
  layer mask IS visible:
  - **capture_45 (mask3, toggled) must DIFFER from capture_5 (mask7 baseline)** by MORE than the
    ~6px residency-only delta — a real mask-sized delta (the CSG cut removes/changes a layer).
  - **capture_75 (mask7, post-undo) must byte-EQUAL capture_5 (mask7 baseline)** — undo restores the
    render. This is the REAL visual round-trip that was impossible under mip-fallback.
  - **capture_105 (mask3, post-redo) must byte-EQUAL capture_45.**
  Report the actual sha256 of all four + the boreDiffPixels(5,45).

### Task 3 — restore the real visual round-trip assertion in the R6 gate (if Task 2 proves it works)
- The R6 gate `test_editor_toggle_undo_capture.cpp` currently asserts only the state-trail +
  residency-transition smoke check (because the visual round-trip was invisible-by-design). Now that
  residency makes the mask visible, RESTORE the real visual assertions:
  `EXPECT_EQ(png75.rgb, png5.rgb)` (undo restores) + `EXPECT_EQ(png105.rgb, png45.rgb)` (redo) +
  a calibrated `EXPECT_GT(boreDiff(png5,png45), <live-measured>)` — now testing the MASK, not the
  residency transition. KEEP the state-trail test + BackButton test. Update the header comment + the
  finding doc `R6-Editor-Render-Mask-Invisible-Finding-2026-07.md` to record that the residency fix
  landed and the visual round-trip is now asserted (the finding is RESOLVED, not just documented).
- If Task 2 does NOT produce a clean byte-exact round-trip (e.g. residency is nondeterministic frame-to-frame),
  STOP and report — do not weaken the gate; keep the state-trail assertions and report what you saw.

## Gates / guardrails
- No-regression: build `vixen_editor` clean; the R6 windowed gate PASSES live with the restored
  assertions; run the RenderGraph test suite that covers BodyOctreeSceneNode/residency if present
  (`test_appflow_editor_toggle_render`, `test_editor_document_render` if they exist) — green.
- The main-app demo scenes' residency behavior must be UNCHANGED (this is editor-scoped). If a
  `test_*residency*`/tier-crossing test exists, run it — must stay green.
- Byte-guards are irrelevant here (no AppFlow codegen touched) but do NOT touch generated artifacts.
- Windows-side build is faster for non-windowed gtests; the windowed editor gate needs WSL/Dozen.
- rtk is unreliable for evidence — use /usr/bin/git, /usr/bin/diff, sha256sum, cmp.

## Milestone Map
- **Milestone 1 (Tasks 1-3):** editor body residency grant + live verification + restore R6 visual
  round-trip. One implementer (Sonnet), one Opus validator.
