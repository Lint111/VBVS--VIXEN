# pickinst — GPU pick returns the body-instance index (T-1126)

## Summary

The body raymarch already has the winning instance index at the covering pixel. `TraceWorld.glsl:44-57` defines `WorldHit.instIdx`, its instance loop records the winner at `TraceWorld.glsl:229-249`, and `BodyInstanceRayMarch.comp:275` receives that `WorldHit` from `TraceWorld`. The shader now writes that index beside the existing packed brick/voxel address in the binding-9 pick target at `BodyInstanceRayMarch.comp:531-535`.

`PickIdTargetNode` now allocates an `RG32_UINT` image (`VIXEN/libraries/RenderGraph/include/Nodes/PickIdTargetNode.h:85`). The address remains channel R with its original packing. Channel G carries the instance index; the two legacy non-instanced raymarch shaders write `0xFFFFFFFF` there. `VoxelSelectionProviderNode` reads both channels, keeps the old address in `SelectionId.payload`, and exposes G as `SelectionCandidate::instanceIndex` only when present (`VIXEN/libraries/RenderGraph/src/Nodes/VoxelSelectionProviderNode.cpp:155-182`). A background miss continues to emit the existing `hit=false`/invalid-ID result.

The winning candidate's optional index is also available on `SelectionChangedEvent::primaryInstanceIndex` (`VIXEN/libraries/RenderGraph/src/Nodes/SelectionCoordinatorNode.cpp:141-143` and `VIXEN/libraries/EventBus/include/SelectionEvents.h:33-49`). The body-record layout was not changed; the shader uses the existing pipeline index.

The new GPU witness, `PickSelectionReturnsInstanceForTwoBodiesAndNoneForBackground`, is in `VIXEN/libraries/RenderGraph/tests/Nodes/test_body_instance_raymarch_render.cpp:1386`. It renders two bodies, uses the rendered `HitRecord` as the independent oracle for covered pixels of instances 0 and 1, and samples the live pick image through the production `VoxelSelectionProviderNode`. It also selects a background pixel and verifies a miss with no instance. `test_hitrecord_readback.cpp` now checks both channels against the existing hit record, and `test_selection_resolve.cpp` covers candidate instance preservation.

## Verification

The implementation tree was freshly configured and built. All 22 requested `*_check` targets passed. The full build and final incremental build passed. The final focused pick, selection, hit-record, and body-instance CTest selection passed 35/35, including the new GPU witness. The full RenderGraph selection passed 1,339 tests with 0 failures and 11 configured skips.

The full SVO selection had the one known T-1449 failure: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` reports `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. The isolated test reproduces the same diagnostic. SVO, KernelDispatch, Core, and codegen inputs were not changed; the RenderGraph scope required for this task passed. This same opcode-94 failure is recorded in the base's `reports/vixcache.md`.

With caller `DISPLAY`, `WAYLAND_DISPLAY`, `VK_ICD_FILENAMES`, `VK_DRIVER_FILES`, `VK_LAYER_PATH`, and `LD_LIBRARY_PATH` unset, the 11 capture-related CTest cases passed 11/11. The native capture witness passed 1/1 on both the untouched parent `7f4db687` and the implementation tree. `tools/compare-capture-pixels.py --require-byte-identical` compared all seven HUD/editor PNG pairs: each pair was byte-identical and had zero changed pixels.

The final no-op rebuild completed with `ninja: no work to do` (0 units). The codegen gate passed all 22 targets; its no-new-mutex check reported only the two existing unclassified KernelDispatch inventory rows at `TaskExecutor.h` and `TaskExecutor.cpp`. The generated-file and pin state remained unchanged. `git diff --check` passed.

CodeGraph was queried before source inspection, but this VIXEN worktree has no index; no index was created. No merge was needed because `HEAD` and `origin/wave/authoring-convergence` were both `7f4db687` at the final-witness start.

The production KFR follow-up call site is `KernelFederationRenderer/app/src/session_renderer.cpp:28`, `SessionRenderer::BuildRenderGraph()`. Its UI selection provider is currently wired at lines 47-57. KFR was read-only and not changed. Shared VIXEN files touched were `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` and `VIXEN/shaders/BodyInstanceRayMarch.comp`.

## STOPs

None. The SVO opcode-94 result is the accepted pre-existing T-1449 finding described above, not a blocker for this RenderGraph task.

## CONSOLIDATION ISSUES

- proposed: Document aggregate CMake targets for GPU test sources
