# sn-recipeedit3: recipe data-edit deltas and registry derivations off the frame thread (R864, RE5 + RE8)

## STATUS

**CHECKPOINT: code complete and building (native Release, zero warnings); bench runs queued but had not started when this report was written (the queue head `sn-genccache1:native-boot-build` held full width for 28+ min), so there are no measurements in this file.**
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

Pending: queued as `sn-recipeedit3:bench-e-new-t3-1080` (delta path, 2,197 instances), `...e-old-t3-1080` (same binary, `RECIPE_EDIT_FULL_RESTAGE=1`), `...r-new-t0-x50-1080` (63 registered recipes, `mutate`). Native Windows, 1080p, `--resource gpu --gpu-benchmark` (a `--resource bench` quiet window did not open in this session; numbers will be labelled contended/INVALID-FOR-COMPARISON). Before numbers for RE8 come from op-recipeedit1 (quiet window Q: `mutate` 6.5-7.6 ms).

To collect: the chain `sn-recipeedit3-evidence/chain1.sh` runs detached (one queue job per run, sequential; progress in `sn-recipeedit3-evidence/logs/chain1.status`). Outputs land in `C:\vixen-native\recipeedit3-evidence\<run>\` (`edits.csv`, `frames.csv`). Read from edits.csv: `t_mutated - t_call` is the frame-thread `mutate`/delta cost (RE5 per edited row = that / `edited_rows`; RE8 target <= 0.5 ms for literal/structural/newrecipe rows), `tlas_epoch_delta` must be 0 for param/material rows and 1 for transform rows, and `frames.csv` max work time during an edit against the steady p50. The old-path row set is `e-old-t3-1080` (same binary, `RECIPE_EDIT_FULL_RESTAGE=1`).

## WITNESS

Native Release build of `vixen_recipe_edit_bench` only (no compiler warnings). No RenderGraph suite or golden was run in this checkpoint.

## LANDABLE NOW

Nothing yet: not measured.

## STOPs

None on design. Queue waits dominated the session (the build waited about 1.5 h and 2 h across two attempts behind other lanes' full-width jobs; bench jobs queued again).

## SPT DISPOSITION

No SPT id was given. RE5: PARTIAL (code done, not measured). RE8: PARTIAL (code done, not measured).

## CONSOLIDATION ISSUES

- try -> fail -> remedy: first native build failed 16 targets on `BodyInstanceRowDelta` not being declared in `VulkanGraphApplication.h`; remedy a forward declaration. Only a compile through the 689-step native build finds this; there is no quick header-only syntax check route on the laptop for the app header.
- `benchmark-configure.bat` is still an untracked copy from another lane's checkout (known, filed by op-recipeedit1).
