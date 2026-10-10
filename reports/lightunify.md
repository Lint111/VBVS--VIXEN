# lightunify — shared SDF shadows and local night lighting

## LANDABLE NOW

**Checkpoint state: implementation and fresh build/targeted shadow witnesses are ready; visual and broad runtime gates remain unfinished.** Lane branch: `lane-lightunify`. Merged base SHA: `3bdcefb7b07bd3da3266d948f6f8b5862326b37f` (`origin/wave/authoring-convergence`). No other worktree was edited.

### Diagnosis and repair

The look-dev fixture contains 39 authored primitives plus four figure appendages. `VIXEN/application/main/include/graph/LookdevSceneDefinition.h:89-158,247-320` groups these into registered SDF recipes and body instances; the hull, trees, rocks, figure, plinth, water and props are all opaque procedural instances. There is no sprite path in this fixture. Stored SVO bodies outside the fixture also use the common world traversal.

The primary marcher already compiled the registry's generated SDF functions. The original default `ShadowVisibilityWave.comp` source did not include the recipe-splice marker and so compiled out `TraceWorldShadowImpl`'s registered-recipe branch. That shared traversal already handles stored octree occupancy and registered recipes (`VIXEN/shaders/TraceWorld.glsl:879-1010`); the gap was that the default wave omitted the recipe declarations and shared occupancy binding. The lighting-family source provider now calls the same `SpliceProceduralRecipesIntoSource` used by the marcher when a shader declares the marker (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:2294-2310`), and the wave declares binding 48 and the marker (`VIXEN/shaders/ShadowVisibilityWave.comp:57-63`). No second recipe evaluator or hand-written caster list was added. Recipe registration invalidates every registered shader-family consumer (`VIXEN/application/main/source/VulkanGraphApplication.cpp:4378-4395`).

The wave and receiver shader share one visibility word. Bits 0–3 remain lights 0–3; bits 16–27 carry lights 4–15, preserving the reservoir answer at bit 4 and the primary policy stencil at bits 8–15 (`VIXEN/shaders/HitRecord.glsl:46-81`). `ShadowVisibilityWave.comp:246-299` traces active declared lights through `TraceWorldShadow` and writes only that visibility field. `SpatialReuseShade.comp:596-633` gates the receiver's own material colour and cel band by that visibility and the authored range falloff; it does not replace shadow pixels with a fixed tint.

The earlier night fixture used one point source for twelve emissive windows. `LookdevSceneDefinition.h:105-124,251-295,329-391` now derives one source and one source-exclusion recipe per registered pane. Each pane receives one twelfth of the old window-row radiance and a short local range; the roof worklight and flood remain separate, longer-range sources. The 15-light set fits the generated 16-light `LightingConfig`. `LightEvaluation.glsl:19-34` supplies point-source range falloff; cel spill remains scaled by per-light purpose and attenuation in `SpatialReuseShade.comp:612-629`. The scene has no facade-specific brightening term. The look-dev test does not enable DDGI/reservoir lighting, so this report does not attribute night energy to those disabled paths. Emissive appearance remains the registered material's own emission channel.

The exact per-emitter/source assertion is in `VIXEN/application/main/tests/test_lookdev_capture.cpp:70-103`; isolated source and ambient captures are provided by `:476-484`. The production-chain shadow correctness witness dispatches march → shared shadow wave → shade, including a registered-recipe caster/receiver through the last declared light (`VIXEN/libraries/RenderGraph/tests/Nodes/test_shadow_correctness.cpp:1550-1600`).

### Witnesses and gates

| Check | Result at checkpoint |
|---|---|
| Fresh merged-tree CMake build | **PASS** — queued build, exit 0; log `/home/liory/.local/state/undertow/undertow-box-logs/1791656838-build-lightunify:merged-full-build-j2.log`. |
| 22 VIXEN generation checks | **PASS through the fresh default build graph**; changed `LightingConfig.g.h/.glsl` golden check ran. The prescribed `tools/check-content-codegen.sh` is absent from both consumer `tools/` locations; this gap has an SPT proposal. |
| Targeted `ShadowCorrectnessTest.*` | **PASS, 5/5** after fixing the direct test builder's shared SVO include root and matching its Vulkan descriptor type/pool to shader reflection. |
| Emitter declaration, recipe splice and LightingConfig parity witnesses | **PASS** in the focused runs recorded before checkpoint; full suite remains the required final witness. |
| Before captures | **PASS, 4 states × 4 angles**, fresh pre-change binary on base SHA above. `before/merged-base-sha.txt` records that SHA; instrumentation-only captures matched the stored repeated baseline pixels. |
| After captures/contact sheet | **INCOMPLETE.** A fresh midday state completed with the repeat-pixel CTest and four captures. I opened all four raw midday images; new dark receiving-surface footprints are visible on the plinth. Other three states did not complete. The interrupted late-afternoon queued CTest had not started its payload; my own pending waiters were stopped at the checkpoint. |
| Shadow-mask, family coverage and receiver-colour gates | **NOT EVALUATED on the complete 16-tile after set.** The before mask is all-white for visible surfaces, consistent with the missing registered-recipe wave path. The four midday after images alone are not represented as a full gate pass. |
| Night radial falloff per source and explained-energy gate | **NOT RUN.** The queued isolated-source/ambient test was not admitted before checkpoint. |
| GPU timestamps, before/after p50/p95/max | **PARTIAL.** Before has 896 steady samples (16 tiles × 56 post-warmup samples). The corresponding full after aggregate is unavailable because only one after state completed; no regression claim is made. Baseline values (ms): |

| Pass | Before p50 | Before p95 | Before max |
|---|---:|---:|---:|
| `direct_lighting_ms` | 0.000480 | 0.000560 | 0.000760 |
| `esvo_traverse_shade_ms` | 882.936000 | 999.570000 | 1048.510000 |
| `probe_apply_ms` | 0.000480 | 0.000560 | 0.000760 |
| `probe_gather_ms` | 0.000480 | 0.000560 | 0.000760 |
| `shadow_ray_trace_ms` | 0.000480 | 0.000560 | 0.000760 |
| `shadow_visibility_wave_ms` | 14.802300 | 29.525000 | 36.046800 |
| `spatial_reuse_ms` | 0.093540 | 0.108000 | 0.130800 |

| Check | Result at checkpoint |
|---|---|
| editormaterials fill and lighting-ratio properties | **NOT RUN** — requires all 16 fresh after tiles. |
| R434 visibility | **NOT RUN in this lane.** Its test is present in the RenderGraph/SVO label set. |
| R6 | **NOT RUN in this lane.** All eight named R6 tests are present in the RenderGraph/SVO label set. |
| RenderGraph/SVO suites | **NOT RUN in this lane.** A queued full label run was cancelled before payload admission at the checkpoint; no result from another worktree is substituted. The SVO opcode-94 allowance remains the previously documented accepted baseline only if the same signature recurs. |
| R477/gpuyield budget | **Superseded by the controller.** This report does not wait for gpuyield or claim a bounded-dispatch budget. The replacement GPU-time gate is incomplete as described above. |

### Reviewed visual evidence

The base 4×4 contact sheet and shadow-mask sheet were opened and checked. The tracked copies are [before contact sheet](lightunify-visual/captures-before/contact-sheet.png) and [before shadow-mask sheet](lightunify-visual/captures-before/shadow-mask-sheet.png). The mask sheet shows visible receivers as unoccluded; it is the pre-change result. I also opened the four completed post-change midday images. There is no fabricated or mixed-state after contact sheet: the required 16-tile after sheet and its mask still need to be produced.

One observation per before tile:

| Tile | Observation |
|---|---|
| Midday angle 0 | Front view, tree and figure in frame; no resolved cast footprint on the plinth. |
| Midday angle 1 | Tree partly covers the cab; plinth has no distinct receiving shadow. |
| Midday angle 2 | Rear view with tree and rock; no distinct cast footprint. |
| Midday angle 3 | Side view and figure visible; no distinct cast footprint. |
| Late-afternoon angle 0 | Warm front view; foliage/figure shadows are not resolved on the plinth. |
| Late-afternoon angle 1 | Oblique view; plinth has no distinct cast footprint. |
| Late-afternoon angle 2 | Rear/side view; no distinct caster footprints. |
| Late-afternoon angle 3 | Side view; no distinct receiver shadow. |
| Overcast angle 0 | Front view; no distinct cast footprint. |
| Overcast angle 1 | Oblique view; no distinct cast footprint. |
| Overcast angle 2 | Rear view; no distinct cast footprint. |
| Overcast angle 3 | Side view; no distinct cast footprint. |
| Night-service angle 0 | Dark front pool; no measured per-source radial falloff. |
| Night-service angle 1 | Dark oblique pool; the shared window source has no local-bin measurement. |
| Night-service angle 2 | Rear pool; no measured receiver shadow mask. |
| Night-service angle 3 | Side pool; no per-light isolation evidence. |

The four opened post-change midday frames show dark footprints around the projected tree/figure regions at angles 0–3. This is visual evidence only; colour tolerance and projected-family coverage await the complete analyzer run.

### Checkpoint and remaining work

At approximately 19:15 UTC on 2026-10-10, the lane was approaching the three-hour checkpoint measured from its 16:24 UTC start. The global queue showed another lane's full RenderGraph/SVO work running and a 15-token GPU matrix ahead of this lane. My own queued post-change state, source-isolation, property-analysis and suite requests had not started their payloads; I stopped those waiters rather than leave GPU work running beyond the checkpoint. No required check failed in this lane; these gates are unrun, not classified as product regressions.

Remaining items for the continuation lane: capture late-afternoon, overcast and night-service on the merged tree; create/open the 4×4 after contact and mask sheets; run the complete shadow/colour/night-energy properties and editormaterials fill/ratio checker; compute matching per-pass after timestamps and assess any rise against the newly shadowed recipe bodies and individual night emitters; run R434, R6, and the complete RenderGraph/SVO suite; then refresh this report with exact results.

The original audit's R477 STOP is removed. The captured before data and this implementation are independently useful and committed; no gpuyield contract change is requested.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- Publish a reusable VIXEN CodeGraph index for engine lanes
- Derive live recipe invalidation for every shader registry consumer
- Apply the canonical GPU environment to every application capture test
- Provide GPU timing isolation without reserving every CPU queue token
- Declare pass-output snapshots before pipelined buffer reuse
- Resolve the worktree FetchContent cache from the VIXEN preset
- Make dotnet run honor its worktree-local CodegenTool artifacts path
- Generate the analytic visibility bit layout from the light contract
- Provide the prescribed content-codegen light gate in consumer trees
- Centralize shared SVO shader include roots for direct shader test builders
- Derive direct Vulkan test descriptor layouts from shader reflection
- Offer a single-pass queued look-dev capture artifact mode
