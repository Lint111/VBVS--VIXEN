# sn-editorchannel1: the editor's agent channel tells the truth (R468 / R468.4; art-editorreq E6b, E15, E18)

Lane: Claude Sonnet, laptop. Worktree `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/editorchannel1`, branch `lane-editorchannel1`, base `origin/wave/authoring-convergence` = `c27282c4` (re-fetched at the end: unchanged, so no merge was needed). Build tree `build/wsl-debug` (Debug, `-DVIXEN_WITH_FSR3=OFF`, `UNDERTOW_ROOT=/home/liory/projects/undertow/.claude-worktrees/pair-kernel-c2e1188c5e`, read-only; the shared `/home/liory/projects/undertow` has no `transpiled/textshader`). Evidence: `/home/liory/codeman-cases/undertow/reports/sn-editorchannel1-evidence/` (scripts, logs, PNGs, traces; no build output).

CHECKPOINT-quality: all six steps are done and witnessed, except one leg of step 5 (the reopen-parity check), which is a STOP below.

## LANDABLE NOW

Branch `lane-editorchannel1` (VIXEN repo only), six commits on `c27282c4`:

| SHA | What |
|---|---|
| `2a40edba` | E6b readback after the action, E18 undo groups + `appflow.navigate`, E15 event bus + diagnostics, chord grammar, script injector deleted from `EditorApplication`, docs. (Also carries the deletion of `temp/run_editor_script.bat` and `editor_capture_contract.cmake`, which were staged earlier.) |
| `a2e05c89` | Producer, CMake, Windows `cmake -P` runner, multi-instance cache gate and `test_editor_toggle_undo_capture` ported to the channel (R468.4) |
| `2f834084` | `appflow.quit` (orderly shutdown), readback ring freed before device teardown, producer waits for pixels to change, gate asserts groups/diagnostics/chords |
| `87df4bf2` | Gate asserts the runtime's bus events through `navigate` |
| `35de9ff2` | `tools/run-vixen-windowed-captures.sh` editor leg and `tools/measure-editor-latency.py` guard (repo-root `tools/`, missed by the first grep) |
| (tip) | this report (`reports/sn-editorchannel1.md`); proposals are in `35de9ff2` |

Targeted checks that passed (MEASURED, details under WITNESS): `test_keychord`, `test_action_stack`, `test_binding_store`, `test_binding_pattern`, `test_appflow_loader`, `test_appflow_blob`, `test_appflow_data_seam` (all pass); channel gate PASS (3 runs); capture producer RC 0 and 6 of 7 `EditorToggleUndoCapture` cases pass (the 7th is a Dozen condition that is independent of this change, see WITNESS); zero warnings from VIXEN sources in four builds (86 warnings, all `vk_mem_alloc.h` in third-party fetch).

Pushed to ws only (`git push ws:/home/liory/projects/VBVS--VIXEN lane-editorchannel1`). Never pushed to origin.

## CHANGE

All paths under `VIXEN/`.

- **E6b (readback after the action).** `application/editor/source/EditorApplication.cpp`: `channelUpdateSerial_` is bumped at the end of every `Update()` that ran (after the catch blocks, so a failed Update still counts). `appflow.dispatch` records `required = serial + 1` (`channelActionPending_`); `PostTick` (about line 1326) settles `channelActionFrames_[action]` to the first frame presented after that Update. A dispatch-attached readback (`PendingReadback::requiredSerial`) and `await_frame(actionId)` wait for it; a paused editor waits for `step`/`run` because no Update runs. Root cause of the stale UI is in `StartPendingReadbacks` (about line 1382): `IRenderTarget::GetCurrentIndex()` of the swapchain is never advanced after an acquire (`VulkanSwapChain.h:84`, `currentColorBuffer` is only set to 0), so the readback always read swapchain image 0. It now takes `SwapChainNode::GetCurrentImageIndex()` and passes the index into `AppFlowReadbackRing::Enqueue` (`AppFlowReadback.h/.cpp`).
- **E18 (undo groups).** `appflow.begin_group {label}` / `appflow.end_group` (`EditorApplication.cpp`, about line 1932) call `ActionStack::BeginGroup/EndGroup`; typed `group.result` values `Ok`, `NestedGroupRefused`, `NoOpenGroupRefused`; `Undo`/`Redo` handlers refuse while a group is open (diagnostic `GroupOpen`); events `group.begin`/`group.end`. `libraries/AppFlow/src/ActionStack.cpp` `EndGroup`: an empty group is no longer an undo step.
- **`appflow.navigate {edge}`** (about line 1959): takes a generated transition edge id (`appflow.list` `transitions`); needed so the channel can enter Settings.
- **E15 (event bus, diagnostics).** `AttachAppFlowEventBus()` hands `graph->GetMessageBus()` to `rt_.SetEventBus` (from `PreTick`) and mirrors `AppFlowEdgeEvent`/`AppFlowDiagnosticEvent`/`AppFlowChangedEvent` into the channel event log (`flow.edge`, `flow.diagnostic`, ...). `ReportEditorDiagnostic` / `ReportEditorError` / `ClearEditorError` (about line 1612) replace all 36 direct `lastEditorError_` writes: set the status line (`[Code] message` through `RefreshStatusView`, the existing `statusMessage` carrier), log, and publish a `diagnostic` event with `detail.code`/`detail.message` (model `DocumentDiagnosticCode`; plain failures use `EditorError`). Toggle/CreateLayer/Delete/Move/ProgramField/Parameter rejections that only logged before now report typed diagnostics. `appflow.read` returns `diagnostic` and `group`.
- **Chord grammar.** `application/editor/include/KeyMap.h`: `ChordModNames(KeyMod)` and `ParseChordMods(...)` (via `magic_enum` over the generated `KeyMod`); `appflow.list` emits `mods` as an array of names, `appflow.dispatch` parses with the same function. Tests in `libraries/AppFlow/tests/test_keychord.cpp` (CMake: `test_keychord` links `magic_enum`).
- **`appflow.quit`** (about line 1976): `HandleShutdownRequest()`, the window-close path. Added because the capture producer must end the editor cleanly (a killed process extracts no logs). `EditorApplication::DeInitialize()` now resets the readback ring before the device goes (a clean channel session logged `vkFreeCommandBuffers: Invalid device`).
- **R468.4 retirement.** Deleted from `EditorApplication.h/.cpp`: `ParseEditorScript`, `ScriptedAction`, `scriptedActions_`, the `VIXEN_EDITOR_SCRIPT` env parse and the whole scripted-action injector in `PreTick` (about 330 lines). Deleted files: `temp/run_editor_script.bat`, `application/editor/editor_capture_contract.cmake`. New `tools/editor_capture_producer.py` drives the editor over the channel. `application/editor/CMakeLists.txt` (producer fixture, drift check removed), `application/editor/run_editor_capture.cmake.in` (Windows runner), `tools/multiinstance_cache_gate.py` (editor child now runs the producer), `libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` (reads `appflow_state_trail.log`, `[CHANNEL/state]` lines). Two comments in `application/main` that named the retired script were reworded.
- Tools: `tools/appflow_client.py`, `tools/appflow_mcp_server.py` gain `begin_group`, `end_group`, `navigate`, `quit`; `tools/appflow_channel_gate.py` asserts groups, diagnostics, chords and bus events.
- Docs: `Vixen-Docs/01-Architecture/AppFlow-Agent-Channel.md` (operations table, frame synchronization, retirement section).

Not minted: no attribute, declaration form, kind, EntityId, or new editor-only abstraction. `ChordModNames`/`ParseChordMods` are two inline helpers beside the existing `ReadMods`. `appflow.begin_group/end_group/navigate/quit` are generic channel verbs over existing runtime paths (flagged in STOPs: `appflow.quit` is the one the controller should look at). The added `throw std::runtime_error` lines are inside `HandleAppFlowRequest`, the channel's existing request-error path caught by `AppFlowChannel::Poll` (about 20 such throws already there); they are not undertow runtime.

## WITNESS

### E6b, the three run-3 cases (windowed Dozen, 1280x800, `e6b_probe.py`; MEASURED)

Before = same tree with the swapchain index line reverted to `target->GetCurrentIndex()` (everything else as committed); after = committed code. Hashes are the readback `hash` field. Images: `e6b-before/` and `e6b-after/` under the evidence directory; every image below was opened.

| Case | Before (attached readback) | After (attached readback) | Settled state (a later plain readback) |
|---|---|---|---|
| Toggle layer 2 | hash 4838...; shows `cut` unchecked, "2 of 3 rule layers enabled", undo 1 (correct by chance: image 0 happened to be current) | `e6b-after/01-toggle-attached.png`: `cut` unchecked, "2 of 3", status "undo 1 / redo 0"; hash 4838... equals settled | 4838... |
| Undo | `e6b-before/03-undo-attached.png`: `cut` still **unchecked**, "2 of 3", status "undo 1 / redo 0" = the state after the toggle, one action behind; hash 4838... (equals the toggle image, not the settled one) | `e6b-after/03-undo-attached.png`: all three checked, "3 of 3", "undo 0 / redo 1"; hash 1694... equals settled | 1694... |
| 3x parameter up (readback on the 3rd) | `e6b-before/05-param3-attached.png`: bulgeRadius **0.6**, all checked, "undo 0 / redo 1" = the state after the undo, none of the three steps; hash 1694... | `e6b-after/05-param3-attached.png`: bulgeRadius **1.069**, "undo 3 / redo 0"; hash 9623... equals settled | 9623... |

The scene viewport (flat grey square at the default camera) is identical in all of these images, as art-editorreq already recorded; it cannot distinguish the cases. Scene-level proof is the offscreen capture producer below (top-down camera): `capture-run3/editor_capture_25.png` shows the bulge with the cut hole (mask 7), `editor_capture_45.png` the same shape with no hole (cut layer toggled off, mask 3).

### E18 undo groups (MEASURED, `gate-run1/appflow-channel-transcript.json`, gate-run2, gate-run3 all PASS)

`begin_group` Ok (undoDepth 5), nested `begin_group` -> `NestedGroupRefused`, three parameter dispatches, `end_group` Ok with **undoDepth 6 (+1)**, second `end_group` -> `NoOpenGroupRefused`, one `undo` -> undoDepth 5, redoDepth 1. Events `group.begin` (undo 5) and `group.end` (undo 6) recorded. Unit tests: `ActionStack.GroupOfThreeDispatchesAddsOneUndoDepth`, `ActionStack.EmptyGroupIsNotAnUndoStep`.

### E15 diagnostics and bus (MEASURED, same gate)

`ToggleLayer` with `layerIndex 99` returns dispatch Ok; `appflow.read` then reports `diagnostic = {code: InvalidLayerIndex, message: "ToggleLayer: layerIndex 99 is outside the 3 document layers"}`; the event list has `diagnostic` with `detail.code InvalidLayerIndex`. `appflow.navigate ToSettings` + the back-button selector produce `flow.edge` events with `to` = `Settings`, `Editing` (gate asserts the exact sequence). Event-type counts in gate-run1 before the bus assertions: `dispatch 12, view.reconcile 3, group.begin 1, group.end 1, diagnostic 1`. INFERRED (not looked at): the status-line text for the diagnostic was not captured in a windowed image; the status view reads `lastEditorError_` with the `[Code]` prefix in code only.

### Chord grammar (MEASURED)

`appflow.list` in the windowed run: `"mods": ["Ctrl"]` for ResetLayout/Undo (before: `1`). The gate dispatches the listed undo chord (`{"key":"Z","mods":["Ctrl"]}`, copied from the list) and undo depth drops by 1. CPU: `test_keychord` 5 tests pass, including `ListedModsRoundTripThroughDispatchGrammar` (every `kKeyDefaults` entry round-trips) and `ModNamesAreGeneratedNamesNotIntegers`.

### Script path retired (MEASURED)

`git grep` on the final tree for `ParseEditorScript|ScriptedAction|VIXEN_EDITOR_SCRIPT|VIXEN_EDITOR_CAPTURE_SCRIPT`: no code reads or sets them (only comments/error text that name the retired path, plus `Vixen-Docs`, `reports/` history). Channel-driven producer (`producerrun.sh`, same environment as the CTest fixture, run 4 on the final binary): producer RC 0; `appflow_state_trail.log` = `parameter_up` x4 (undo 1..4), `toggle mask=3 undo 5`, `undo mask=7 undo 4 redo 1`, `redo mask=3 undo 5`, `save`, `settings flowState=Settings`, `back`, `afterBack=0`. `test_rendergraph_criticalnodes_windowedcapture --gtest_filter=EditorToggleUndoCapture.*`: 6 pass (`ToggleUndoRedoStateTrailThroughWindowedRun`, `ParameterEditReachesRenderPipeline` diff 39132 px, `LayerEditReachesRenderPipeline` diff 6890 px, `ParameterSnapshotAndSaveStayCanonical`, `UndoRedoRestoresRenderByteExact`, `BackButtonReachesReturnInRunningEditor`), 1 fails.

- The failing case is `GpuSubmissionsRespectDeclaredBudget`: "live timestamp-query samples are required", 0 `[GPUYIELD]` lines. MEASURED independent of this lane: a plain `VIXEN_EXIT_AFTER_FRAMES=140` editor run with no channel and no script (`yield-baseline/`) also logs 0 `[GPUYIELD]` lines under Dozen. It is the translated-device condition; native Windows is the meaningful witness (R503/R477).
- A first producer run (before the pixel-change wait) had `LayerEditReachesRenderPipeline` fail with diff 0 (`capture-run1`): the toggle had not reached the pixels 15 presented frames later on Dozen. The producer now re-reads until the image changes (180 s bound). Run 2/3/4 identical, 6890 px.
- Pre-existing reds: not compared against a base build in the same tree by test name for the ctest set, because these tests are DISABLED on Dozen by policy (`cmake/VixenTesting.cmake:323,342`); the same producer/gate commands were run directly under the queue GPU resource instead. The base timeline itself (script) cannot be rerun on the new code since it is deleted.
- Not run: `vixen_multiinstance_cache_gate` (nightly label; its editor child now runs the producer, only Python-parsed), the Windows `cmake -P` producer path, native Windows anything, the reopen leg.

Other builds: zero VIXEN-source warnings (`logs/build-*.log`: 0 / 0 / 0 / 86 third-party VMA).

## FRAMEWORK GAPS

1. **Swapchain `IRenderTarget::GetCurrentIndex()` is wrong** (`VulkanSwapChain.h:84`, `currentColorBuffer` only ever 0). The editor works around it by asking `SwapChainNode`; the source fix needs an audit of `SwapchainBarriers.h:139` and `Fsr3Upscaler.cpp:421,442`.
2. **Frame-count settling is not enough for a structural edit.** A toggle takes more than 15 presented frames to reach pixels on Dozen (recipe pipeline recompile). The channel can say when the action was reflected in the UI and the scene *request*, not when the scene render has caught up (R463/R465 latency); the producer polls pixel hashes instead.
3. **No AppFlow action for the reopen witness** (STOP 1).
4. **Python driver code** (`editor_capture_producer.py`, client, gate, MCP server) is R812/R821 conversion debt.
5. **Channel/capture CTests are disabled on Dozen** and one budget test fails (not skips) off native drivers.
6. A diagnostic stays on the status line until the next successful save or load; a later successful edit does not clear it. A View noun for the diagnostic does not exist (the status message carries it); minting one is a View-schema decision.

## STOPs

1. **Reopen-parity leg dropped from the editor capture gate.** `ReopenSavedDocument()` (reload the saved file, compare canonical program/parameters/mask, adopt it) was only reachable from the script. No `FlowAction` names it, so the channel cannot call it. Options: (a) append a `ReopenSaved` action to `codegen/appflow-schemas/AppFlowReference.cs` and regenerate `AppFlow.g.h` (conflicts with `sn-editorshape1`, which appends actions to the same file and changes the shape hash; do after it lands); (b) drop the leg for good and cover save/reopen at model level in `libraries/VoxelDocument` tests. Recommendation: (a) in the integration batch, because it also gives agents a "verify my save" verb. Until then `ReopenSavedDocument` is public and unreferenced, `ParameterSnapshotAndSaveReopenStayCanonical` became `...AndSaveStayCanonical` and capture 115 is no longer produced. Deviation named per R394: a test-only code path with no declared action; the unified form is a declared FlowAction.
2. **`appflow.quit` is a new channel verb.** I added it so a finished session ends through the window-close path (needed for the editor log's `[GPUYIELD]` lines and cache extraction). It reuses `HandleShutdownRequest()`; one handler, one docs row, one client method. If the controller does not want it, the producer would need a different clean-exit route (a finite `VIXEN_EXIT_AFTER_FRAMES` is not workable: frames pass while paused and while polling).
3. **`tools/measure-editor-latency.py` cannot be ported in this lane.** It measures edit latency from the tick-locked script timeline (captures at ticks 30/60/90/120 plus/minus, spans keyed by editor tick) and read the capture fixture command from ctest JSON. Capturing now exits with an explanatory error (analysis of existing captures still works) instead of silently running an unscripted editor. It needs a rewrite on the channel (dispatch with attached readback, frame deltas from the reflected frame); proposal filed. `tools/run-vixen-windowed-captures.sh` (editor leg) was switched to the channel producer through an 8-line `--` command form in `run_capture` (legacy shell, minimal change, R821 proposal filed); only `bash -n` was run on it, the windowed editor leg was not executed.
4. Two environment facts, not blockers: `/home/liory/projects/undertow` (the brief's `UNDERTOW_ROOT`) lacks `transpiled/textshader`, so VIXEN configure failed (`libraries/Text/CMakeLists.txt:19`); I pointed `UNDERTOW_ROOT` read-only at `.claude-worktrees/pair-kernel-c2e1188c5e`. The laptop queue held my first build 25 minutes behind host CPU pressure (`blocked=run-queue/cpu-pressure`, nothing running).

## CONSOLIDATION ISSUES

Proposals in `.spt-proposals/sn-editorchannel1.jsonl` (6 entries): swapchain current index (conversion/clarity, luna S); reopen action (design-question, owner-question); Python to C# conversion of the channel tooling (R821, sol L); Dozen-disabled CTests plus failing-not-skipping budget test (clarity, haiku S); latency tool rewrite (luna M); windowed-capture shell port (haiku S).
Try-fail-remedy hidden dependencies (R812.13): (1) configure needed a provider with `transpiled/textshader`; the brief's `UNDERTOW_ROOT` did not have one (declare the provider on the configure step); (2) the first producer run failed on the layer-edit pixel diff until the producer polled for the change.
Staged-deletion slip: the removal of `temp/run_editor_script.bat` and `editor_capture_contract.cmake` landed in `2a40edba` instead of `a2e05c89` (they were staged by `git rm` before the first commit).

## SPT DISPOSITION

No SPT task ids were assigned in the brief; art-editorreq rows E6b (CLOSE: attached readback now post-action, measured), E15 (CLOSE for diagnostics + bus + channel event; the status line carries the code in text, no new View noun), E18 (CLOSE: group verbs and chord grammar; camera verbs and pushed events are not done, events are still polled), R468.4 (PARTIAL: script path deleted; reopen leg open, see STOP 1).

## BENCH REQUEST

None.
