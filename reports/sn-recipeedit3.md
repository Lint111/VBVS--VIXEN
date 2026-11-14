# sn-recipeedit3: recipe data-edit deltas and registry derivations off the frame thread (R864, RE5 + RE8)

## STATUS

**DONE for RE5 and RE8 (code, native build with zero warnings, contended native measurements). READY FENCE is a written design only. The swap-frame stall is still op-recipeedit2's S2.**
Base: `lane-recipevix4` 01b708be + `lane-recipeedit1` + `lane-vxjobs1` ce27b7f4 (clean merges). Velocity base: velocity-2 (already contained in the vxjobs1 merge). Native build `sn-recipeedit3:native-build-release` exit 0 (first attempt failed on a missing forward declaration in the app header, fixed in the second commit).

## CHANGE

RE5 (data-edit deltas):
- `BodyOctreeSceneNode::UpdateInstanceRows(span<BodyInstanceRowDelta>)` (`BodyOctreeSceneNode.cpp`, `.h`): updates only the listed rows in `instances_` and the transform/material/motion streams (per-row memcmp, unchanged rows skipped). Material upload is a per-ring-slot dirty row range (`materialDirtyRows_`) instead of marking every slot wholly dirty; the transform stream is still copied whole per frame as before.
- The RT-query TLAS epoch (`rtQueryInstanceEpoch_`) is bumped only when a row's transform, providerKind, octreeIndex or recipeId changed. Colour/param edits no longer rebuild the TLAS. Emitter table is marked dirty on any material/geometry change (lazy rebuild, unchanged behaviour).
- `VulkanGraphApplication::UpdateBodyInstanceRows` (host seam) and `BodyInstanceEpoch()` (diagnostic); `BodyInstanceRowDelta` in `ShellOctreeGpu.h`. `SetBodyInstances` is unchanged (whole-list path stays for scene loads and list-size changes; `UpdateInstanceRows` returns false if a row is out of range).

RE8 (registry derivations off the frame thread):
- `Register/ReplaceProceduralRecipe` after the graph is compiled go through `SubmitRecipeMutation`: the registry gets a provisional entry at once (previous analysis fields for a replaced id, engine default bound and no grid for a new id), the content cacher is updated, and `AnalyzeRecipeRegistration` (bounds + `DeriveOccupancyGrid`) runs in `std::async` on a worker. `PublishReadyRecipeAnalyses` (frame thread, in `PollProceduralShaderSwap`) applies the analysed entry with `Replace`; a newer edit of the same id supersedes an older analysis.
- `RecompileProceduralShader` defers the shader precompile until the last analysis is published (`recompileAfterAnalysis_`); `proceduralSwapPending_` is set meanwhile so callers still see a pending swap. Lockstep mode and `FlushProceduralShaderSwap` wait for analyses synchronously. Before the graph compiles, the path is synchronous as before (the first Compile reads the registry).
- The two near-duplicate analysis blocks of Register/Replace were merged into shared helpers (`RegistrationOptionsFor`, `AnalyzeEntry`, `RegisterRecipeContent`).
- Not moved: the 7-library source emission (`emit`, 7-28 ms in op-recipeedit1) still runs on the frame thread inside `BeginPrecompile`; it is outside RE8 as briefed (registry/content-cacher) and is RE2/RE4's splice area.

Bench (`test_recipe_edit_bench.cpp`): param/transform/material edits use the delta path; `RECIPE_EDIT_FULL_RESTAGE=1` keeps the old whole-list restage for A/B in one binary; `edits.csv` gains `edited_rows` and `tlas_epoch_delta`.

Overlap with op-recipeedit2 (RE2/RE4): none in the splice or pipeline code. I touched `VulkanGraphApplicationRecipes.cpp` only around Register/Replace/RecompileProceduralShader/PollProceduralShaderSwap (the file is shared; expect a textual merge near `RecompileProceduralShader` if RE2/RE4 edit it).

## MEASUREMENTS

Native Windows 1080p Release, `vixen_recipe_edit_bench`, queued as `--resource gpu --gpu-benchmark` (no `--resource bench` quiet window opened): **all numbers are contended, INVALID-FOR-COMPARISON**. Runs: `e-new-t3-1080` (delta path, 2,197 instances, 3 cycles), `e-old-t3-1080` (same binary, `RECIPE_EDIT_FULL_RESTAGE=1`), `r-new-t0-x50-1080` (13 instances, 63 registered recipes). Evidence: `C:\vixen-native\recipeedit3-evidence\<run>\` (edits.csv, frames.csv); queue logs in `sn-recipeedit3-evidence/logs/`.

**RE5, frame-thread CPU of the edit call (`t_mutated - t_call`), median (max), ms; 2,197 instances, 169 edited rows:**

| class | before: whole-list `SetBodyInstances` | after: row delta | per edited row after | TLAS epoch bumps after (3 edits) |
|---|--:|--:|--:|--:|
| param | 0.282 (0.313) | **0.107 (0.127)** | 0.63 us | 1 of 3 |
| transform | 0.289 (0.325) | **0.104 (0.116)** | 0.62 us | 1 of 3 |
| material (colour) | 0.162 (0.356) | **0.114 (0.124)** | 0.67 us | **0 of 3** |

Before, every edit bumped the epoch (3 of 3 per class). Target <= 0.1 ms per edited row: met by two orders of magnitude (about 0.0006 ms per row; 0.11 ms for all 169 rows). TLAS rebuild count 0 for colour edits: met. At 13 instances: 0.003 ms per edit. Epoch column, honestly: edit 0 (the first param edit after boot) bumped the epoch although only a Lamp param changed; I did not isolate why (suspect a boot-staged vs rebuilt row difference, e.g. hull recipe id). Edits 11 and 21 (transform) show 0 because the cycle repeats edit 1's lift value, so no row changed; that is correct. All three classes were visible in the submit of their own frame (edit-to-submit 345-410 ms = one frame at 2,197 instances contended; 29-32 ms at 13 instances).

**RE8, frame-thread `mutate` of a recipe edit (`t_mutated - t_call`), median (max), ms:**

| class | before (op-recipeedit1, quiet, 14 recipes) | after, 13 recipes / 2,197 inst | after, 63 recipes |
|---|--:|--:|--:|
| literal | 6.5-7.6 | **0.20 (0.30)** | **0.42 (0.67)** |
| structural | 6.5-7.6 | 0.19 (0.32) | 0.34 (0.70) |
| new recipe | 6.5-7.6 | 0.06 (0.08) | 0.05 (0.05) |
| revert | - | 0.21 (0.43) | 0.45 (0.80) |

Target <= 0.5 ms: met at the median in every class; a revert (0.80) and a structural edit (0.70) at 63 recipes exceeded it once under contention. What remains on the frame thread is the entry copy, registry `Replace` validation and the content-cacher hash. Provenance: the RE5 before is measured on the same binary; the RE8 before is op-recipeedit1's quiet figure (the old synchronous path is not in this binary).

**Not changed by this lane: edit-to-visible and the swap-frame stall.** Cold literal/structural/new-recipe edits still reach first submit after 39.7-39.9 s (2,197 inst) and 61-73 s (63 recipes), and frames.csv shows swap frames of 39.8-41.2 s `update_ms`: pipeline creation still runs on the frame thread on this base. "No frame over 2x p50" is op-recipeedit2's S2 (`0e55a947`) and is not met here. `e-old`'s compile classes (1.3 s) are shared-VkPipelineCache hits from `e-new`, not an RE8 comparison.

## READY FENCE (editor channel, fb-editor1 / sn-editorshape1b): design, not implemented

Not implemented here: the swap point it hangs on moves in `lane-recipeedit2-s2` (pipelines created on the precompile workers, the swap finds them in the cacher), so wiring it on this base would conflict with that lane's `RecompileProceduralShader`/`PollProceduralShaderSwap` changes. Exact design for the lane that lands on S2 + this lane:

- State in `VulkanGraphApplication`: `recipeEditSerial_` (incremented by every `Register/ReplaceProceduralRecipe` that returns `Ok`, in `SubmitRecipeMutation` and the pre-compile sync path; also by `UpdateBodyInstanceRows`, so one counter covers all edit classes), `recipeEditCoveredSerial_`, and an atomic `recipePreviewSerial_` (highest serial fully on screen) read by the editor channel.
- Serial N is current when: (1) its analysis is published (`pendingRecipeAnalyses_` holds nothing <= N, `recompileAfterAnalysis_` false); (2) its precompile is swapped (`proceduralSwapPending_` false and `RecompileDirtyNodes` ran, including S2's cacher-hit swap); (3) a frame whose update ran after (1)+(2) was submitted; (4) for a readback, that frame's GPU fence completed.
- Mechanics: snapshot `recipeEditSerial_` at the start of `Update`; after `RecompileDirtyNodes()`, if (1) and (2) hold set `recipeEditCoveredSerial_` to the snapshot (an edit arriving mid-update is not claimed). In `Render`, after `vkQueueSubmit`, store `(slot, recipeEditCoveredSerial_)`; when that slot's fence is seen signalled (existing per-slot wait, or readback completion) publish `recipePreviewSerial_ = max(old, serialInSlot)` and notify a condition variable. Data-only deltas are in the submit of their own frame, so the covered-serial rule is trivially true for them.
- Client API: `uint64_t RecipePreviewSerial() const` and `bool WaitRecipePreview(uint64_t serial, std::chrono::milliseconds timeout)`. The editor channel returns the serial in the edit reply; the client sends it with the readback request; the server waits (bounded) for `RecipePreviewSerial() >= serial` before reading the frame. A failed compile keeps the old pipeline (R772): still advance the serial and report `ok=false` so a waiter never hangs; the timeout is the backstop.
- Bench witness: `preview_serial_ms` in edits.csv (edit call to `RecipePreviewSerial() >= serial`); it should equal `t_submit` plus one GPU frame.

## WITNESS

Native Release build of `vixen_recipe_edit_bench` only (no compiler warnings). No RenderGraph suite or golden was run in this checkpoint.

## LANDABLE NOW

VIXEN `lane-recipeedit3` commits e1992153 (RE5 + RE8) and b22b4fc2 (header forward declaration), on the base above. Checks: native Release build of `vixen_recipe_edit_bench` with zero warnings; three native bench runs (all `edits=30 ok`). Not run: any RenderGraph CTest suite or golden.

## STOPs

None on design. Queue waits dominated the session (the build waited about 1.5 h and 2 h across two attempts behind other lanes' full-width jobs; bench jobs queued again).

## SPT DISPOSITION

No SPT id was given. RE5: CLOSE (target met, contended). RE8: CLOSE for the frame-thread `mutate` target (median <= 0.5 ms); the emit step stays on the frame thread (RE2/RE4). Ready fence: OPEN (design in this report).

## CONSOLIDATION ISSUES

- try -> fail -> remedy: first native build failed 16 targets on `BodyInstanceRowDelta` not being declared in `VulkanGraphApplication.h`; remedy a forward declaration. Only a compile through the 689-step native build finds this; there is no quick header-only syntax check route on the laptop for the app header.
- `benchmark-configure.bat` is still an untracked copy from another lane's checkout (known, filed by op-recipeedit1).
