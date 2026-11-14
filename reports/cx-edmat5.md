# cx-edmat5 — material editor showability report

## LANDABLE NOW

The lane has a coherent, committed partial implementation. Implementation tip before this report artifact: `a3fa59b2c` on `lane-edmat3`; the report commit is the final follow-up. Kernel pin: `23f8eb553236df9908b1cc735fc4b38cc4a8e9e3`. Paired Undertow lane: `1ef8cb536`. Landing order: kernel → Undertow → VIXEN.

The framing, compact layer rows, outline naming, remove diagnostics, glyph contract, and wrap-picker deduplication are landable. The material-coverage/default-material and visual two-colour acceptance criteria are not complete; see STOPs.

## BEFORE / AFTER

- BEFORE: [`cx-edmat4 05-two-materials.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat4-evidence/editor/05-two-materials.png) has an empty viewport, repeated `DanglingSocket` text, `material 0`, and an inline eight-row default list.
- AFTER at 1280×720: [`final9 01-open.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/01-open.png) visibly shows the stored body, readable compact layer rows, named `core:hull_metal_major`, and author-worded removal refusal. [`final9 05-two-materials.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/05-two-materials.png) remains a single green/turquoise stored silhouette, not the required two-colour hard boundary.
- Neutral comparison: [`final9 02-neutral.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/02-neutral.png) is a visible light procedural body, but its rectangular cutout silhouette and bounds do not match the stored material silhouette within 1–2 pixels.
- 1920×1080: the full reviewed set is under [`editor-final7-1920`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final7-1920). The asset is visible and centered in every reviewed state; the same stored/procedural silhouette and absent two-colour boundary remain.

## CHANGE

- `VIXEN/libraries/VoxelDocument/include/EditorDocumentFraming.h:31` and `VIXEN/application/editor/source/EditorApplication.cpp:2623`: shared stored/procedural placement and world-bound camera frame; normalized stored scale is `10/64*6.4`.
- `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp:431` and `:446`: derive preview bounds from geometry while ignoring MaterialRef/Attach suffixes; neutral preview flatten uses a material-neutral resolver.
- `VIXEN/libraries/VoxelDocument/tests/test_editor_document_model_preview.cpp:1190`: material suffix does not change preview bounds.
- `VIXEN/codegen/view-schemas/EditorLayers.cs:202`: material name/chip/default/coverage rows; `editor_layers.rml:22` and `editor.rcss:59` make Inputs/Outputs pinned summaries and layer rows compact.
- `VIXEN/application/editor/source/EditorApplication.cpp:615`, `:713`, `:813`, `:2606`: author-worded remove refusal, named MaterialRef/parameter/port displays, one wrap choice per operation, and coverage instrumentation. `VIXEN/shaders/StoredSdf.glsl:314`/`:390` decodes numeric MaterialId output values.
- `VIXEN/libraries/RenderGraph/tests/test_ui_kit.cpp:66`: ASCII/font-safe glyph contract for kit fragments; kit disclosures and layer marks no longer use missing-glyph symbols.
- `.spt-proposals/cx-edmat5.jsonl`: three workflow/consolidation proposals.

## WITNESS

| Step | Result |
|---|---|
| Post-merge model tests | PASS: `EditorDocumentFraming.StoredAndProceduralPlacementShareTheCameraBoundsSource`; `EditorDocumentModelPlacement.MaterialSuffixDoesNotChangePreviewBounds`. |
| Post-merge UI guards | PASS: two UiKit tests and two `ViewEditorLayersReconcile` tests, run from the staged test directory through the GPU queue. |
| Final committed full build | PASS: queued `cmake --build .tmp/build`; 317 steps. Codegen golden checks, shader/interface checks, no-new-mutex, and `windows_identifier_check` passed. Full CTest inventory was not run. Registered red rows 13, 16, 53 were not run. |
| 1280×720 material action witness | PASS mechanically: windowed, material IDs `{5,8}`, trim change within two frames, `compileCount=0`, undo restores the pair hash, save/reopen bytes stable. Visual acceptance FAIL: no visible two-colour boundary and stored/procedural bounds differ. Final log: [`material-actions-editor.log`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/material-actions-editor.log). |
| 1920×1080 material action witness | PASS mechanically with the same visual limitation. Final reviewed images: [`editor-final7-1920`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final7-1920). |
| Coverage | FAIL/diagnostic: every bake log line reports `[EDITOR/material-coverage] total=0`; no slot counts were published. The requested percentages therefore cannot be claimed. |
| Flatten parity | NOT RUN: no `undertow-author bake` VRC1 comparison was executed in this VIXEN lane. |
| Default persistence / combined Provide profile | NOT RUN. Current generated VDC1 payload has no document-default field; `LoadDocument`/`AdoptDocument` deliberately clear the process-local default and report located legacy major/minor/percent fields. |

## STILL WRONG

- [`final9 05-two-materials.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/05-two-materials.png) shows the stored green/turquoise body, but not `core:hull_metal_major` + `core:emissive_trim` as two visible colours with a hard boundary. The material coverage source is empty (`total=0`), indicating the stored output set currently has no surface samples reaching this path.
- [`final9 02-neutral.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/02-neutral.png) versus [`final9 05-two-materials.png`](/home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/05-two-materials.png): both are visible, but their actual silhouettes/bounding boxes are materially different. The shared affine/camera source is correct at the model-contract level; provider geometry parity is not.
- The Outputs panel has the compact summary and the default row contract, but the final witness has no populated coverage rows and no persisted default row because the codec does not yet carry that document value.
- The wrap list is now emitted once per operation, but at 1280×720 the expanded inspector can still push the compact picker below the visible panel edge. The Add flyout is closed in the reviewed frames.
- Subtract-with-and-without-own-material and the two-layer Provide-readback witness were not exercised by the available material action driver.

## STOPs

- **STOP — material channel contract:** coverage is `total=0` in all final bake logs, and the screenshots fail the required visible `{5,8}` two-colour/hard-boundary witness. The numeric ID list is only palette metadata, not visual proof.
- **STOP — document default contract:** generated VDC1 has legacy major/minor/percent fields but no document-default reference field. Persistence and reopen restoration cannot be truthfully claimed until the codec/schema carries it.
- **STOP — flatten/combined witness:** the paired `undertow-author bake` parity and combined Provide profile were not run, so this lane does not claim them.
- Recovery record: initial wrong-working-directory build/test probes were recovered by using the repository-root `.tmp/build` and CMake test executable directory. After the required convergence merge, the first queued post-merge build failed because incoming generated headers did not match the pinned opcode/document contract; restoring the lane’s generated headers and rerunning with `UT_QUEUE_BREAKER=0` passed. The queue’s identical-command diagnostic reuse refusal was logged at `/home/liory/.local/state/undertow/undertow-box-logs/1794660022-build-w-edmat3:post-merge-build.log`. The final GPU job experienced a 157-second admission stall, then ran well within the 30-minute limit and passed mechanically.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN UI target tests need a canonical staged-asset working directory
- proposed: Box queue should expose a scoped recovery for reused diagnostics
- proposed: Provision a read-only CodeGraph query route for isolated VIXEN worktrees
