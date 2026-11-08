# cx-tu03 — VIXEN heavy RenderGraph test TU audit

Base: `a2bc3ccd` (`origin/wave/authoring-convergence`), with `origin/lane-vixrebuild1` merged at `44a9cd87`.
Implementation commit: `7a4a2955`.

## LANDABLE NOW

The six audited RenderGraph tests now share a test-only `RenderGraphHeavyTestFixtures` implementation library for Vulkan instance/device/queue/command-pool bring-up and teardown. The audited fixtures retain their existing device-selection policy, feature requests, GPU resource locks, timeout behavior, assertions, output paths, and digest checks.

Changed files:

- `VIXEN/libraries/RenderGraph/tests/Nodes/RenderGraphHeavyTestFixtures.{h,cpp}` — shared fixture implementation.
- `VIXEN/libraries/RenderGraph/tests/test_critical_nodes.cmake` — test-only library and six target links.
- The six requested test TUs — common setup removed; test-specific setup retained.
- `.spt-proposals/cx-tu03.jsonl` — one incidental provisioning/consolidation proposal.

No production or shader-generator source was changed.

## BEFORE/AFTER

Measurements are from the same WSL machine, Release/Ninja flags, and queued compiler invocation. Object timings are the exact target object entries from `build/tu03-wsl/.ninja_log`; preprocessing sizes are the corresponding `-E | wc -c` measurements.

| TU | Before compile | After compile | Delta | Before preprocessed | After preprocessed |
|---|---:|---:|---:|---:|---:|
| `test_recipe_multi_bucket_compositing.cpp` | 130.198 s | 71.472 s | -58.726 s (-45.1%) | 12.942 MiB | 12.918 MiB |
| `test_shadow_correctness.cpp` | 108.643 s | 54.860 s | -53.783 s (-49.5%) | 12.821 MiB | 12.812 MiB |
| `test_recipe_bucketing_perf.cpp` | 115.902 s | 63.863 s | -52.039 s (-44.9%) | 12.734 MiB | 12.712 MiB |
| `test_baked_vs_virtual_parity.cpp` | 149.036 s | 75.399 s | -73.637 s (-49.4%) | 13.562 MiB | 13.555 MiB |
| `test_procedural_recipe_render.cpp` | 103.293 s | 59.675 s | -43.618 s (-42.2%) | 5.844 MiB | 6.737 MiB |
| `test_switch_cost_isolation.cpp` | 78.517 s | 43.534 s | -34.983 s (-44.6%) | 6.606 MiB | 6.727 MiB |

Baseline full-build invocation: 1339 steps, 1398.430 s, green. Final merged-tree full-build invocation: 140 pending steps, 116.833 s, green. The latter is an incremental rebuild in the same build directory, so these full-build wall values are reported as invocation measurements, not a clean-vs-clean claim.

## WITNESS

Baseline/recovery:

- Base SHA: `a2bc3ccd`.
- First queued configure failed before implementation because the default Undertow root lacked generated `transpiled/textshader`; log: `/home/liory/.local/state/undertow/undertow-box-logs/1793954701-build-cx-tu03:configure-baseline.log`.
- Recovery rung A: selected the clean generated Undertow worktree `/home/liory/projects/undertow/.claude-worktrees/cgsuitevel` and reconfigured with `UNDERTOW_ROOT=...`, `VIXEN_AUTO_DERIVE_UNDERTOW_ROOT=OFF`; configure passed.
- The recovered baseline full build passed all 1339 steps and codegen/golden checks.

Target build and tests:

- Final queued full build: `cx-tu03:final-full-build`, exit 0; all 140 steps and codegen/golden checks passed.
- Focused queued CTest: `ctest --test-dir build/tu03-wsl -R "(RecipeMultiBucket|ShadowCorrectness|ProceduralRecipe|RecipeBucketing|BakedVsVirtual|SwitchCost)" --output-on-failure -j16`.
- Focused result: 22/28 passed. All multi-bucket (1), shadow (5), procedural (10), bucketing (3), and parity (3) tests passed. The separate CPU assertions `HotnessGate`, `DispatchBaseEligibility`, and `ParityCorpusTest` passed 3/3.
- The six switch-cost tests all fail at runtime shader compilation with the same error: `glyphOutlineInstances` undeclared at generated shader line 223. This is outside this source-only fixture refactor and matches the documented issue `Recipe GLSL dispatch does not stringify typed glyph indices` in `/home/liory/codeman-cases/undertow/reports/consolaudit-evidence/consolaudit-proposals.jsonl`.

Full-suite attempt:

- Ran once, queued as `cx-tu03:full-ctest-final`: `ctest --test-dir build/tu03-wsl --output-on-failure -j16`.
- It reached 3220/3255 tests and was terminated by the queue/runtime watchdog with exit 143 while the suite was in `Application.LookdevCapture.StoredSdf.Brickmap`. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1793960081-test-cx-tu03:full-ctest-final.log`.
- Before termination, the log recorded unrelated/unclassified reds in `ShaderCompilerTest.CompileCache_IncludedFileChangeIsAMiss`, `SdiLifecycleTest.BuildVertexShaderOnly`, five `EditorToggleUndoCapture` cases, `HeadlessUiGraph.RendersAndReadsBackAnOffscreenFrame`, both `LookdevCapture.SaturationChannel` cases, and the six switch-cost cases. The cold-bound GlyphSDF wrapper also failed because its expected PNG was absent even though its nested CTest passed. These are not attributed to this lane; only the switch red has the documented pre-existing generator diagnosis above.
- The required native digest IDs `10fc17f580f12ea7` and `d0399b15c0de4b59` were not present in the assigned brief, repo, or audit evidence searched by this lane, so no native digest comparison is claimed here. This refactor changed only test fixture setup and has no production/shader output delta.

## STOPs

No implementation STOP. The full-suite witness is incomplete due watchdog termination, and the remaining unclassified suite reds are recorded above; no rerun was attempted. The scoped non-switch GPU parity/output witnesses and the final merged-tree build are green.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN configure requires a manually selected Undertow worktree for generated textshader

