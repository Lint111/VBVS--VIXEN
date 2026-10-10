# R464 Run 2 — pixel-footprint refinement for the editor rim

## LANDABLE NOW

**Result:** The editor crescent remains fixed without the run 1 global 1024-step / `5e-6` march. The tracer now uses a pixel-footprint trigger, a verified crossing bracket with one bisection and secant refinement, and a budget scaled to each ray's bound interval. Both analytic masks have zero disagreements, all guarded and unguarded hit/RGBA oracles agree, and the captured white strip remains a valid cel-lit cylinder normal.

**Status:** The mean-step gate is met: both scenes are below the original and substantially below run 1. Timestamped procedural dispatch is within a small margin of the original, but is still about 0.6% slower than run 1 in both scenes. Since the brief also asks for frame time well below run 1, that part of the cost gate remains a **STOP**; see below. The timestamps use the available non-conformant dzn test Vulkan implementation, so they are not a production-GPU claim.

**Base:** VIXEN lane tip before this run's changes `c0f76cb0f771cf58eb0592c22c7af9ff96f04ff2`, with current `origin/wave/authoring-convergence` already merged. Run 1 report tip: `bbe71c53`.

### Convergence strategy

`VIXEN/shaders/TraceWorld.glsl` passes the existing vertical pixel-angle coefficient (`pc.raySizeCoef`) into procedural tracing. `SdfRecipes.glsl` sets `hitEpsilon = max(5e-6, t * pixelAngle * 0.25)`. This cone threshold starts refinement; it does not accept a hit. The tracer samples a one-pixel-forward probe span, fits a quadratic candidate for grazing crossings, and accepts only when an actual field sample establishes a positive-to-nonpositive bracket. A single bisection tightens that bracket before the secant estimate, after which the field gradient is evaluated at the refined point. If no bracket is found, conservative distance/grid marching continues.

The loop budget is derived from the ray's clipped interval: `clamp(32 + ceil((tFar - tNear) * 160), 32, 2048)`. It is not a fixed 1024-step allowance. The editor capture's maximum was 474 steps; the heavier `twist_sphere` fixture reached 1004. No over-relaxed stepping was introduced.

This design follows the established conservative-distance-step and pixel-cone ideas in Hart's sphere-tracing work; the bracketed root refinement here is an adaptation for this recipe marcher, not an implementation copied from that paper. The deformed-SDF context is also relevant to Seyb et al.'s nonlinear sphere-tracing work. [Hart, *Sphere Tracing*](https://graphics.stanford.edu/courses/cs348b-20-spring-content/uploads/hart.pdf), [Seyb et al., *Non-linear sphere tracing for rendering deformed signed distance fields*](https://cs.dartmouth.edu/~wjarosz/publications/seyb19nonlinear.html).

### Analytic mask and guard results

| Fixture | Pixels | Analytic surfaces | True escapes | Mask disagreements | Guard hit-bit / RGBA differences |
|---|---:|---:|---:|---:|---:|
| Editor box-with-cylinder-bore | 250,000 | 4,858 cylinder-wall pixels in the opening | 2,962 | **0** | **0 / 0** |
| Gridded slab/sphere | 250,000 | 3,588 cavity-wall pixels in the opening | 11,158 | **0** | **0 / 0** |

The editor opening has 7,820 pixels. Its center ray is a genuine through-ray; only those analytically escaping rays remain background. The editor recipe has no occupancy grid (`gridDim=0`). The separate gridded fixture uses `gridDim=16` and provides the occupancy oracle.

Guarded and unguarded RGBA are identical in both fixtures. In the slab/sphere fixture, guarded and unguarded internal hit records differ in 8,218 pixels because occupancy skips can stop at a different point on the same surface; maximum `hitT` delta is `9.68e-5`, position delta `9.38e-5`, and normal component delta `0.08335` at `(402,359)`. These diagnostic differences do not change the exact hit-bit or RGBA contract.

The lighting path was not changed. In this scene, white material, ambient `0.3`, and one white directional source with a cel band capped at `1.0` give a linear bound of `1.3`. The white-strip sample's normal is finite and has `N·L=0.737367`, consistent with the valid top cel band. Captures are RGBA8, so they establish the displayed peak of 255 but are not an HDR-buffer readback; the `1.3` ceiling is derived from the unchanged lighting equation and its configured maxima.

### Cost table

GPU time is the median of three Vulkan timestamp-query samples around the procedural dispatch. Step distributions include every image pixel, including zero-step rays. Editor frames are 500×500; the heavier `twist_sphere` scene is 400×400. Active-ray counts are stable across all three versions: 142,524 / 250,000 editor pixels and 58,844 / 160,000 heavy-scene pixels.

| Scene | Version | Dispatch ms | Mean steps | p50 | p99 | Max |
|---|---|---:|---:|---:|---:|---:|
| Editor frame 5 | Original (128, 1e-3) | 77.940200 | 4.422840 | 4 | 39 | 128 |
| Editor frame 5 | Run 1 (1024, 5e-6) | 78.158520 | 6.125764 | 4 | 70 | 867 |
| Editor frame 5 | Run 2 (cone, dynamic budget) | 78.751680 | 4.337028 | 4 | 37 | 474 |
| Heavy `twist_sphere` | Original (128, 1e-3) | 48.743160 | 2.366450 | 0 | 23 | 128 |
| Heavy `twist_sphere` | Run 1 (1024, 5e-6) | 49.231760 | 3.002594 | 0 | 33 | 1004 |
| Heavy `twist_sphere` | Run 2 (cone, dynamic budget) | 49.505360 | 2.248256 | 0 | 18 | 1004 |

Run 2 lowers mean steps by 1.9% / 5.0% versus the original and by 29.2% / 25.1% versus run 1 (editor / heavy). The editor p99 and max are also below run 1; the heavy-scene max remains 1004, equal to run 1. Dispatch timing is 1.04% / 1.56% above the original and 0.76% / 0.56% above run 1. The localized probe and fallback samples explain why fewer march iterations did not translate into lower measured dispatch time.

The available runner set `VK_ICD_FILENAMES` to the dzn Vulkan ICD, which reports that it is not a conformant implementation and is for testing use only. This same test backend was used for all three measured versions. Treat the sub-2% differences as a small-margin test result, not a hardware performance claim.

### Before and after captures

The final native capture helper completed successfully for editor frames 5, 45, 75, and 105 and HUD frames 5, 45, and 75. The frame-5 images below were opened and visually inspected. The rim crescent is now surface-lit; the dark bore center remains a true escape, and the white strip remains the valid cel band described above.

| Before | Run 2 after |
|---|---|
| ![Before editor capture frame 5](rimleak-visual/captures/before/editor/editor_capture_5.png) | ![Run 2 editor capture frame 5](rimleak-visual/captures/run2-after/editor/editor_capture_5.png) |

Capture comparison: frame 5 has 299 differing pixels (maximum channel delta 251; bounding box `x=102..397, y=97..377`). Frames 45 and 105 each have 118 differing pixels. Frame 75 has 299. HUD frames 5, 45, and 75 are byte-identical. Both before and after captures peak at RGBA8 channel value 255.

### Witnesses

- **Release build:** final quarter-trigger / one-pixel-probe source built successfully through the global queue.
- **All 22 checks:** `accumulationconfig_check`, `appflow_check`, `callables_check`, `lightingconfig_check`, `lighttreebuffer_check`, `miningbeambuffer_check`, `octreeconfig_check`, `prevcameraconfig_check`, `probegridconfig_check`, `recipe_opcode_mirror_check`, `recipe_simd_check`, `recipeparams_check`, `reservoirconfig_check`, `reservoirrecord_check`, `sdf_core_kernels_check`, `shadowconfig_check`, `view_editor_layers_check`, `view_hud_blob_check`, `view_hud_check`, `view_hud_markup_check`, `view_hud_writer_check`, and `view_noun_enum_check`: all passed. `no_new_mutex_check` passed.
- **Focused analytic oracles:** editor and gridded slab/sphere tests passed on the final source, with zero mask disagreements and zero guard-on/off hit-bit or RGBA differences.
- **RenderGraph:** 1,346 selected, 0 failed; 260.06 seconds. This includes the unchanged R6 and editor gates.
- **SVO:** 761 passed, 1 failed, 8 skipped, and 1 disabled. The only failure is `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, reporting `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (opcode 94 / T-1449). It matches the accepted same-base failure in run 1 and [`carvefix.md`](carvefix.md). The occupancy reduction test (`RecipeOccupancy.GeneratedReductionMatchesOracleAndMeasuresCellWork`) and rvcompact test (`DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels`) both passed.
- **Codegen:** all 22 VIXEN checks passed. The assigned kernel worktree's CodegenTool restore/build and opcode-mirror `--check` also passed; there were no kernel source changes. VIXEN has no `tools/check-content-codegen.sh`, so that wrapper check is unavailable here.
- **No-op rebuild:** passed on the final source; all targets were already built.
- **R463/R465 editor responsiveness:** reviewed; this change leaves editor interaction gates and rendering cadence untouched. Timestamp instrumentation is confined to the RenderGraph test harness.

### STOPs

- **Performance timing portion:** measured dispatch time is within 1.6% of the original but is not below run 1, so the brief's full “well below run 1” timing condition is not demonstrated. Mean steps are below the original and 25–29% below run 1, but the extra local field probes offset that work reduction in timestamp measurements. Further probe-cost reduction or a conformant GPU timing witness is needed before claiming the strict timing gate is cleared.
- No accuracy, analytic-mask, background-escape, lighting-bound, guard-parity, R6, or editor-gate STOP remains.

### Recovery notes

- `codegraph explore` was run before source searches; the VIXEN worktree had no CodeGraph index, so source inspection continued with the assigned files.
- The VIXEN `tools/check-content-codegen.sh` path is absent (exit 127). The documented per-target CMake checks and the kernel CodegenTool opcode-mirror check were run and passed instead.
- A queued build admission earlier encountered a missing history summarizer `.pending` file. The targeted command passed on queue retry; the issue is recorded in the SPT inbox. This was queue bookkeeping, not a product failure.
- The final native capture command exited 0 and wrote all requested final frames. No baseline image was re-recorded.

### SPT disposition

The committed `.spt-proposals/rimleak.jsonl` inbox contains the lane's consolidation proposals.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Create the asset staging stamp directory on clean builds
- proposed: Resolve native capture output paths before changing application directories
- proposed: Recover missing pending history files before queue admission
- proposed: Keep native capture witnesses alive through shader recompiles
- proposed: Provide a reusable SDF ray trace probe contract
- proposed: Route the content-codegen check to the active repository
- proposed: Keep long CTest progress streams from closing queued witnesses
- proposed: Default the configured VIXEN FetchContent cache for queued builds
