# Lane gpufix — sn-gpufix report

## LANDABLE NOW

Four commits on `lane-gpufix` (no push), all merged-clean with `origin/wave/authoring-convergence` and `origin/main`
(both already up to date; `origin/main-wave` does not exist on the remote):

- `d06df127` T-2924 — `VIXEN/application/main/tests/test_lookdev_capture.cmake:` per-state `Application.LookdevCapture.State.<state>`
  CTest entries (TIMEOUT 1200 each) plus the four-state aggregate under label `native`;
  `run_lookdev_capture.cmake:11-14` takes `-DLOOKDEV_CAPTURE_STATES`.
- `00cab4ab` T-4142 — `VIXEN/cmake/VixenTesting.cmake:60,117-121` (`PROCESSORS` keyword),
  `libraries/RenderGraph/tests/test_core_systems.cmake:104` (`PROCESSORS 4` on coresystems_b),
  `tools/run-tests-split.sh:149-172` (reads `PROCESSORS` from CTest JSON, forwards `--cpu-tokens`).
- `38b41a22` T-2871 — `Nodes/test_shadow_correctness.cpp` `ReflectComputeLayout`/`WritesForDeclaredBindings` and the
  march/shade/wave layout + pool creation: layouts and pool sizes come from `ReflectDescriptorLayout`.
- `fc073e45` T-2875 — `Nodes/TestVulkanDeviceShell.h` (`MakeTestDeviceShell`), used by shadow_correctness,
  body_instance_raymarch_render, bandwidth_ab_measurement, mip_fallback_render, partial_brick_upload.

### Proof

- FramePipeline overlap test (`taskset`, 20 repeats): fails with `maxActive=1` on 1 CPU and on 2 CPUs, passes on 4.
- `ctest --show-only=json-v1` shows `PROCESSORS 4` on the coresystems_b tests; splitter computes cpu-tokens=2 for the cpu part, 0 for gpu.
- `ctest -N` lists the four `State.*` look-dev entries and the aggregate; `run_lookdev_capture.cmake` run with a stub
  executable and `-DLOOKDEV_CAPTURE_STATES=overcast` captured exactly that state.
- GPU lane (Dozen, correctness only): 24/24 passed — BodyInstanceRayMarchRender 11, HitRecordReadback 1, MipFallbackRender 4,
  ShadowCorrectness 5, PartialBrickUpload 1, BandwidthAbMeasurement 2. coresystems_b cpu lane: 115/115 passed.
- Before/after failing names: none failing before or after in these targets. T-2871 first run failed all 5 ShadowCorrectness tests
  (march layout declared bindings 8/14 the SPIR-V does not have) until writes were filtered to reflected bindings.
- Not built or run: `vixen_lookdev_capture` (needs `FidelityFX_SC.exe`, absent on this ws), so the per-state tests are
  verified by registration and the stub run only.

## STOPs

None. Not converted (scope): 14 other fixtures set only `deviceShell_->device` (13) or a different field set
(`test_recipe_bucketing_perf.cpp`); they work as-is and were not touched.

## SPT DISPOSITION

- T-2924: DONE d06df127 — test_lookdev_capture.cmake, run_lookdev_capture.cmake:11
- T-4142: DONE 00cab4ab — VixenTesting.cmake:117, test_core_systems.cmake:104, run-tests-split.sh:149
- T-2871: DONE 38b41a22 — test_shadow_correctness.cpp (ReflectComputeLayout, layout/pool creation)
- T-2875: DONE fc073e45 — TestVulkanDeviceShell.h:12

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: the box queue ignores CTest PROCESSORS; only the in-repo splitter forwards `--cpu-tokens`, other callers must pass it by hand
- proposed: `origin/main-wave` named in the lane pack does not exist on the VIXEN remote
- proposed: 14 GPU fixtures still hand-set the VulkanDevice shell; migrate them to `MakeTestDeviceShell`
- proposed: the march pass's precompiled SPIR-V and the old hand table had drifted (bindings 8/14); other direct-Vulkan tests may carry the same hand tables
