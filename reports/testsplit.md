# R517 testsplit: derive each test's GPU need, split suites into GPU and CPU jobs

Lane `testsplit`, branch `lane-testsplit`, base `a48a42df`. Dozen (WSL) is correctness-only: every timing below is non-representative.

## LANDABLE NOW

Tip: 4 feature commits on `a48a42df`, plus one `docs(reports)` commit with this report (SHAs in the final message).

**What it does**
- The class of a test is what it did when it ran. A Vulkan implicit layer (`VkLayer_vixen_gpu_witness`, `VIXEN/tools/gpu-witness-layer`) records instance/device creation, device-memory allocation and queue submit per process, tagged with the test that owns the process (`VIXEN_TEST_NAME`, ancestry as fallback).
- `tools/classify-tests-gpu.py` runs the suite and writes `VIXEN/tests/gpu-class-manifest.tsv` (test, class, target, source fingerprint, first GPU action as evidence). CMake reads it and adds a `gpu` or `cpu` label, `classified-by-run` or `classified-by-link`, `VIXEN_TEST_CLASS` in the environment, and the `vixen_gpu_device` lock for gpu. GoogleTest cases are labelled when CTest loads (per-target generated stub), plain `add_test` entries at configure time.
- Link closure is the bootstrap and the fallback: a test with no manifest row, or whose target sources changed (fingerprint mismatch), keeps the link class and gets `classified-by-link`.
- Guard: a `cpu` test that creates a Vulkan instance or device exits 86 with `[VIXEN_TEST_NO_GPU] MISCLASSIFIED TEST ...`, naming the call and process.
- `tools/run-tests-split.sh --label 'RenderGraph|SVO' --agent <lane>` submits `--resource test` (`ctest -j N -L ^cpu$`) and `--resource gpu` (`-L ^gpu$`, serial) as two queue jobs and combines exit codes. Lane call: `bash tools/run-tests-split.sh --agent "$LANE" --label 'RenderGraph|SVO'`. Batch call: the same with no `--label`. `--witness` records GPU actions in the same run for the next manifest pass.
- No new attribute class. Labels, environment and the existing lock only.

**Classification table** (`sn-testsplit-class-table.tsv`, 2228 rows: test, class, link guess, manifest state, run state, evidence, link evidence). 153 targets, 2106 cpu / 122 gpu. 31 targets have at least one gpu test; 122 are cpu only. Largest gpu groups: `gpurender1` 12, `windowedcapture` 10 (fixture rule), `procedural_recipe_render` 10, `gpurender2` 9, `shell_revalidate_node` 7, `cel_shading_graph` 7 (unmeasured, kept gpu). Hand spot-checks: `BodyInstanceOcclusionRejectTest.*` and `test_b1_occlusion_ab` include `VulkanDevice.h` and create a VkInstance (gpu, right); `test_slot_task` and `test_connection_rule` include no device code (cpu, right; link closure called both gpu); `HudRenderCapture.BaselineIsNonEmpty` only reads captures but needs the editor/HUD capture fixture, whose setup creates a device (gpu by the fixture rule).

**Manifest pass** (full suite, `-j4`, 2255 s on Dozen; `sn-testsplit-pass2-disagreements.txt`). Link-closure guess: gpu 1409, cpu 819. Measured: gpu 122, cpu 2106. Disagreements: 1287, all one direction (link said gpu, run said cpu), 0 the other way. Top: `test_rendergraph_dispatch` 191, `criticalnodes_infra1` 135, `criticalnodes_infra2` 124, `coresystems_b` 115, `test_connection_rule` 109. The link-closure guess over-approximates because RenderGraph links a library that owns a device even when a test never creates one. An earlier first pass (before the fixes below) showed 1394 guessed vs 109 measured, and exposed `vixen_editor_capture_producer` as really gpu. In the pass-2 run: narrowing 0, widening 1 (`vixen_editor_capture_producer`). Hand `vixen_gpu_device` locks on measured-cpu tests: 0 remain.

**Keeping it current (each shown on real data)**
- Guard: set one gpu test (`BodyInstanceOcclusionRejectTest.NonOccludedInstancesStillTraverse`) to `cpu` in the manifest. CTest labelled it `cpu`; the run printed `[VIXEN_TEST_NO_GPU] MISCLASSIFIED TEST: ... called vkCreateInstance (pid ...)`, `Run tools/classify-tests-gpu.py to relabel it 'gpu'`, and the process exit code was 86. Manifest reverted.
- Narrowing and widening: in a scratch manifest, 3 cpu tests marked gpu and 1 gpu test marked cpu. The pass (`-L SVO`, `--no-write`) reported "narrowing 3: no GPU action in a passing run" and "widened 1: first vkCreateInstance". Real manifest untouched.
- Stale: appended a comment to `libraries/SVO/tests/test_lod.cpp` and reconfigured. Exactly its 16 tests fell back to `classified-by-link`; everything else stayed `classified-by-run`. Reverted; link-labelled count back to 0.

**Split equals combined** (`RenderGraph|SVO`, 2158 tests ran in both)
| | tests | passed | failed | not run | wall (Dozen, non-representative) |
|---|---|---|---|---|---|
| combined, serial (`-j1`, one gpu queue job) | 2158 | 2136 | 2 | 20 | 2066 s |
| split cpu part (`-j4`, `--resource test`) | 2048 | | 1 | | 33 s |
| split gpu part (`-j1`, `--resource gpu`) | 110 | 110 | 0 | | 1870 s |
| split union | 2158 | 2137 | 1 | 20 | 1870 s (parts overlap) |
The union has the same 2158 names, no overlap between parts, and the same pass/fail/not-run status for every test except one. Failures: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` fails in both (the accepted opcode-94 case). `LODRayCastingTest.NoPerformanceRegressionWithoutLOD` failed in the serial combined run and passed in the split run; it is a wall-clock comparison (0.01 s) and passed 3 of 3 reruns alone, so it is timing noise, not a split effect. The 20 not-run are the same capture-fixture tests in both runs.
Gain: the gpu queue slot is held 1870 s instead of 2066 s, and the 2048 cpu tests (95% of the suite) no longer need a gpu slot at all; the cpu part ran in 33 s. The gpu part is dominated by 110 tests running serially.

**R412 checklist**: all mechanical greps (1, 2, 3, 5, 6, 9, 10, and the throw and pipe checks) on the added lines: 0 hits. (Checks are C#/Undertow shaped; none apply to this C++/CMake/Python/bash change.) `git status` clean.

## STOPs

No blocking STOP. Decisions and limits the lander or owner should know:

1. **`add_test` is overridden globally** (`VixenTestClass.cmake`): CMake cannot read a test's COMMAND, so the wrapper records it and forwards every argument unchanged via `_add_test`. Only `add_test(NAME ... COMMAND ...)` and the old form are recorded. Alternative would be listing tests by hand, which the brief rules out.
2. **Fingerprints cover the target's direct sources only** (and for plain tests the command text, launched executables' sources and script contents), not its transitive libraries. A library change that makes a cpu test start using the GPU is not seen as stale; the guard catches it at run time (exit 86), and the next pass relabels it.
3. **Unmeasured tests keep their previous class.** A test that failed or was skipped without any GPU action (the Not Run capture-fixture tests, `cel_shading_graph`, `pushconstant`/`coresystems_c` skips) is not narrowed to cpu on that evidence. They stay on the link/previous class.
4. **57 `*_NOT_BUILT` placeholder tests carry no class**: their targets are not built in this tree (only `rendergraph_svo_tests` and its dependencies were built). They get classified once built and passed through a pass.
5. **The guard works only where the layer builds** (Linux, not Apple). On native Windows the labels, lock and split apply, but a misclassified cpu test is not caught. Not tested on native Windows.
6. **GPU part ran serial; the shared cap of 2 concurrent correctness GPU jobs was not exercised** (`--gpu-jobs 2` is available).
7. `VK_LAYER_PATH` in test environments points at the op94 worktree's Vulkan SDK. Pre-existing; not touched.
8. Configure prints `Fatal Error: glslang directory not found ...` from op94's SDK path and continues. Pre-existing.

## SPT DISPOSITION

- R517 (derive each test's GPU need and split suites into GPU and CPU jobs): CLOSE. Done when: every test is classified by a manifest derived from a measured run with link closure as fallback; the guard fails a misclassified cpu test with a named diagnostic (shown, exit 86); a split runner submits cpu and gpu queue jobs (shown); the split union equals the combined serial run on `RenderGraph|SVO` (2158 names, one status difference traced to a timing-noise test); evidence above.

## CONSOLIDATION ISSUES

- Global `add_test` override in `VIXEN/cmake/VixenTestClass.cmake`: consolidate into the repo's test-registration helper if a `vixen_add_test` wrapper is introduced later (design question).
- `VIXEN/.gitignore` ignores `tools/`, so the layer sources under `VIXEN/tools/gpu-witness-layer` were added with `git add -f`; moving the layer to a tracked directory would remove the trap (code-deletion/velocity).
- The `vixen_gpu_device` locks hand-declared via `vixen_set_suite_properties(... GPU ...)` and `GPU_TESTS` patterns are now redundant with the derived lock for all but fixture setups and can be removed (feature).
- `LODRayCastingTest.NoPerformanceRegressionWithoutLOD` compares wall-clock time at 0.01 s and fails intermittently under serial runs (red-state).
