# vixburn2 lane report

Branch `lane-vixburn2`, tip `c867c728` on top of `f750d6e9`. The branch already contains `origin/wave/authoring-convergence` (wave has 0 commits that HEAD lacks), so the required merge was a no-op.

## LANDABLE NOW

- `f01bee96` test(rendergraph): write the 30-AU closeup capture as PNG (T-2899)
- `ba6e9779` fix(vixen): run windowed captures with the provisioned Dozen ICD (T-2692)
- `c867c728` fix(vixen): give standalone GPU harnesses the CTest Vulkan environment (T-2614)
- This report commit

## Per-task outcome

**T-2899: PNG captures from the headless GPU test.** The 30-AU closeup in `test_tier_crossing_lod_residency.cpp` now writes `aurender-30au-closeup.png` through `stbi_write_png` instead of a P6 PPM. Verified: the `gpurender2` binary builds without tier-file warnings, and the PNG header reads 501x501, colour type 2.

**T-2692: windowed capture runner uses the provisioned Dozen ICD.** `tools/run-vixen-windowed-captures.sh` exports `VK_ICD_FILENAMES` from `VIXEN_WSL_DZN_ICD` and `VK_LAYER_PATH` from `VIXEN_VULKAN_LAYER_PATH` when they are cached, and keeps the inherited values otherwise. `tools/tests/test_run_vixen_windowed_captures.py` checks this with stub executables. Before the fix the override test fails with the inherited stale ICD; after, both tests pass. The end-to-end capture is not proven because `vixen_editor` cannot build here (see CONSOLIDATION ISSUES).

**T-2614: standalone GPU harness Vulkan environment.** New `tools/vixen_gpu_env.py` reads the build's `CMakeCache.txt` and returns the same `VK_ICD_FILENAMES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` that `VixenTesting.cmake` gives CTest. `tools/multiinstance_cache_gate.py` uses it when starting the app. `tools/tests/test_vixen_gpu_env.py` has 3 tests, all passing. The gate itself was not run live on the GPU.

## Test results

Queued GPU run of the touched target `test_rendergraph_criticalnodes_gpurender2` (9 entries, exit 8):

- Passed: BodyInstanceOcclusionRejectTest.OccludedInstanceHasZeroTraversalIterations, BodyInstanceOcclusionRejectTest.NonOccludedInstancesStillTraverse, EditorDocumentRenderTest.FlattenIsDeterministic, TierCrossingLodResidencyTest.NonResidentChildNeverCrossesResidentChildDoes, TierCrossingLodResidencyTest.SubPixelFootprintSkipsCrossingEvenWhenChildResident, TierCrossingLodResidencyTest.ThirtyAuAddressRendersCloseupFromTierLocalCamera
- Failed: EditorDocumentRenderTest.GoldenDocumentAllLayersRendersVisibleBody, EditorDocumentRenderTest.DisablingCutLayerChangesTopFaceSilhouette, EditorDocumentRenderTest.LayeredSlabSubtractSphereGuardsAreExactAndCaptureHasHole

Before/after for the three EditorDocumentRenderTest failures: I rebuilt `test_rendergraph_criticalnodes_gpurender2` with the pre-change tier file restored from `f750d6e9` and reran `EditorDocumentRenderTest.*`. The same three fail with the same assertions (42082 vs 50000; 0 vs 3000; 0 vs 0), and `FlattenIsDeterministic` passes. These failures are therefore present before T-2899 and are not caused by it. The C++ sources differ from `f750d6e9` only in the tier file, so this is the base result. Their root cause is not investigated.

The T-2614 and T-2692 Python tests run through `python3 -m unittest`. The runner test is skipped unless `/tmp/.X11-unix/X0` exists.

## SPT DISPOSITION

- T-2614: CLOSE. Fix committed; unit tests pass; gate not run live.
- T-2626: OPEN. Needs a native Windows run to prove the runner's `safe.directory` for pinned WSL sources. Proving command: `VIXEN/build.bat configure vixen-ninja` from a native Windows shell against the pinned WSL source tree, then confirm configure succeeds with no "dubious ownership" error.
- T-2645: OPEN. Cross-repo: the change belongs in gaia-ecs, not VIXEN. Proving command: in the gaia-ecs repo, a GCC 15 build with the null-dereference warning policy enabled, expecting zero new warnings.
- T-2646: OPEN. Needs a native MSVC configure through a mapped worktree path. Proving command: `VIXEN/build.bat configure vixen-ninja` run from a Windows shell with the worktree mapped to a drive letter.
- T-2678: OPEN. Needs a native Windows run to prove host detection of Dozen provisioning. Proposed one-line fix for the gate in `VIXEN/cmake/ProvisionWslVulkan.cmake:21` (check `CMAKE_HOST_SYSTEM_NAME` as well as `/dev/dxg`). Proving command: `VIXEN/build.bat configure vixen-ninja` on native Windows, expecting the provisioning step to be skipped.
- T-2692: PARTIAL. Runner fix committed and proven by stub test. End-to-end capture needs `vixen_editor` and `VIXEN` built (blocked by FidelityFX_SC.exe). Proving command once built: `tools/run-vixen-windowed-captures.sh` on WSL, expecting HUD and editor PNGs from the Dozen ICD.
- T-2833: OPEN. Needs a native Windows configure to confirm the `safe.directory` scoping for VIXEN's WSL FetchContent cache. Proving command: `VIXEN/build.bat configure vixen-ninja` on native Windows.
- T-2836: OPEN. Needs a native lookdev capture run to confirm the TBB runtime DLL is found on PATH. Proving command: build and run the lookdev capture target from `VIXEN/application/main/CMakeLists.txt` on native Windows (exact target name not verified in this lane).
- T-2867: OPEN. Needs the native R6 editor launch. Proving command: `VIXEN/temp/run_editor_script.bat` on native Windows with `VIXEN_EDITOR_EXE` pointing at the configured editor target, expecting the script to launch that target and not `binaries\vixen_editor.exe`.
- T-2869: OPEN. Design choice, not proven here. The pin needs a decision on the numpy and scipy versions the planetref fit witnesses are validated against. Proving command after the decision: `tools/planetref/run_reference.sh` run twice in a clean environment, with `tools/planetref/measure_disc.py` producing identical witnesses.
- T-2899: CLOSE. Fix committed; GPU test verified.

## CONSOLIDATION ISSUES

- `VIXEN/.gitignore:83` ignores `tools/`, so new files under `VIXEN/tools/` need `git add -f`. `vixen_gpu_env.py` was force-added for this reason. Consider narrowing the ignore rule.
- `VIXEN/third_party/fidelityfx-sdk-1.1.4/sdk/tools/binary_store/FidelityFX_SC.exe` is not provisioned here, so `VIXEN/vixen_editor` and the app targets fail to build in WSL. This blocks end-to-end proof for T-2692 and any app-level capture.
- `EditorDocumentRenderTest` has three failures on the Dozen GPU that also occur on `f750d6e9`. They are outside this lane but will block any gate that runs the whole `gpurender2` target.
- No `spt propose` items were filed in this run. Candidates: the `tools/` ignore rule, the FidelityFX provisioning gap, and the T-2678 gate fix.
