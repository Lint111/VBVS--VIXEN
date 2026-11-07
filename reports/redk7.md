# redk7 — VIXEN dzn GPU red repair

## SUMMARY

Fixed the five `EditorToggleUndoCapture` dzn reds and the headless upscaler-provider red on base `be4dcfaa`. The exact editor/headless repro is green by name: 10/10 tests passed. The five localized glyph captures plus the RTL proof pair also passed by name. The cold-bound glyph wrapper was repaired so its expected-file path follows the queue capture root; its post-fix witness remained blocked by GPU admission and was externally terminated before payload start.

## PER RED

- `EditorToggleUndoCapture.ToggleUndoRedoStateTrailThroughWindowedRun` — `VIXEN_EDITOR_CAPTURE_SCRIPT` was overwritten with the generated `run_editor_capture.cmake` path, so the editor parsed a CMake file instead of the action timeline. Fixed by keeping the producer path in `_editor_capture_cmake_script`; green in `cx-redk7:fix-dzn-repro`.
- `EditorToggleUndoCapture.ParameterEditReachesRenderPipeline` — same action-script variable collision; green in `cx-redk7:fix-dzn-repro`.
- `EditorToggleUndoCapture.LayerEditReachesRenderPipeline` — same action-script variable collision; green in `cx-redk7:fix-dzn-repro`.
- `EditorToggleUndoCapture.ParameterSnapshotAndSaveReopenStayCanonical` — same action-script variable collision; green in `cx-redk7:fix-dzn-repro`.
- `EditorToggleUndoCapture.BackButtonReachesReturnInRunningEditor` — same action-script variable collision; green in `cx-redk7:fix-dzn-repro`.
- `HeadlessUiGraph.RendersAndReadsBackAnOffscreenFrame` — the direct moduleless test path built the graph without selecting the native provider before the candidate-set check. It now calls `SelectNativeUpscalerForModulelessGraph()` after `BuildUIGraph`; green in `cx-redk7:fix-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.latin-carved` — generator/runtime glyph row path is healthy on dzn; green in `cx-redk7:glyph-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.latin-embossed` — same; green in `cx-redk7:glyph-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.qps-rtl-carved` — same; green in `cx-redk7:glyph-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.qps-rtl-embossed` — same; green in `cx-redk7:glyph-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.RtlProof.Pair` — same; green in `cx-redk7:glyph-dzn-repro`.
- `Application.LookdevCapture.GlyphSdf.latin-carved.ColdBound` — the nested capture passed but the direct registration checked a baked build-tree `--expect-file` while the queue rebased captures elsewhere. Routed registration through `vixen_capture_command`; post-fix witness `cx-redk7:cold-bound-fixed` remained GPU-admission blocked for 4,199 seconds and ended with exit 143 before payload start.

## IMAGES

Read before/after captures; no golden was re-baselined.

- Before: `/home/liory/.local/state/undertow/captures/cx-redk7/20261107T183739.2591930/editor/editor_capture_45.png` shows the gray square with a centered gray ring and a dark lower opening; scripted edits were absent.
- After: `/home/liory/.local/state/undertow/captures/cx-redk7/20261107T184352.2744753/editor/editor_capture_45.png` shows the post-toggle smooth pale surface with the dark opening removed and a broad light gradient. Frame 5 still shows the initial ring/opening; frames 75, 105, and 115 show the canonical post-edit surface after undo/redo/save-reopen.

## LANDABLE NOW

- Changed files: `VIXEN/application/editor/CMakeLists.txt`, `VIXEN/application/main/tests/test_headless_ui_graph.cpp`, `VIXEN/application/main/tests/test_lookdev_capture.cmake`, and `VIXEN/cmake/run_with_capture_root.cmake`.
- Fresh queued target build passed: `cx-redk7:fix-target-build`, `/tmp/cx-redk7-fix-target-build.log`.
- Exact dzn editor/headless repro passed 10/10: `cx-redk7:fix-dzn-repro`, `/tmp/cx-redk7-fix-dzn-repro.log`.
- Glyph named repro passed 5/5 regular cases plus RTL pair in `cx-redk7:glyph-dzn-repro`, `/tmp/cx-redk7-glyph-dzn-repro.log`; the cold-bound entry was the only initial additional red and its fix is committed in `ff12b2f5`.
- Commit: `ff12b2f5` (`Fix dzn editor and headless capture reds`).

## REDS

- `Application.LookdevCapture.GlyphSdf.latin-carved.ColdBound` — post-fix dzn witness did not run: queue status showed `gpu-device sharing=on shared-holders=0/2`, `gpu-admission admitted=0`, and `blocked=gpu-suite`; the wrapper ended with exit 143 after 4,199 seconds without a payload result. Initial failure was the baked expected-file path mismatch described above.

## STOPs

- `velocity-1` merge was attempted once before semantic edits and aborted because `VIXEN/codegen/CMakeLists.txt` conflicted. It was not retried or hand-resolved.
- Initial configure was red because auto-derived `/home/liory/projects/undertow` lacked `transpiled/textshader`; recovery used the paired generated provider `/home/liory/projects/undertow/.claude-worktrees/pair-kernel-858f41b873` through the queue.
- Full VIXEN suite was not run; lane rules require the named GPU cases only. The required editor/headless scope is green. The cold-bound verification could not establish a result because the authorized GPU queue route never admitted the payload; no claim of green is made.

## STATUS

Implementation is complete and committed in `ff12b2f5` for the dispatched editor/headless and glyph generator reds. The exact cold-bound red remains unverified because its authorized GPU witness was admission-blocked and terminated before payload start.

## CONSOLIDATION ISSUES

- proposed: VIXEN configure does not provision the required Undertow textshader provider
- proposed: VIXEN GPU capture fixtures do not prepare their producer runtimes
- proposed: VIXEN cold capture witness must rebase standalone expected-file paths
