# lightunify — SDF-recipe shadows and material-driven shading (salvage under R502)

Owner ruling R502 changed this lane: lighting is agnostic to the number of lights. Lights are emitters and cost follows a fixed sample or photon budget, never lights x pixels. Direct light will use stochastic many-light sampling through the existing reservoirs; indirect light will use photon emission into the photon cells. A separate design pass covers that path and this lane does not build it. This pass keeps only what holds for any light count.

Lane branch `lane-lightunify`. Base `ed36300c`; this pass adds new commits on top (no rewrite, no push).

## LANDABLE NOW

### Kept

- **SDF-recipe shadows through the shared traversal.** The lighting-family shaders splice the registered recipes with the same `SpliceProceduralRecipesIntoSource` the primary march uses (`BuildRenderGraph.cpp`, `VulkanGraphApplication.{h,cpp}`). `ShadowVisibilityWave.comp` and the debug path of `SpatialReuseShade.comp` declare the occupancy buffer (binding 48), include `recipe/SdfCoreKernels.g.glsl` and carry the splice marker ahead of `SceneBindings.glsl`. `DirectLighting.comp`, `ProbeGather.comp` and `ShadowRayTrace.comp` include the recipe declarations. No second recipe evaluator.
- **Shadow-reach fix.** The lighting-family splices pass no occupancy blob (the primary march owns the upload), so binding 48 is empty in the wave and `sampleRecipeOccupancy` returns `kNoGridSentinel`. With `gridDim != 0` the relaxed-step boost (8x) then overshot occluders farther than about 1 unit from the receiver, so shadows stopped short. A global `g_recipeProvenStepOnly` (`SdfRecipes.glsl`, set around the shadow any-hit `traceUberRecipeBody` call in `TraceWorldShadowImpl`, `TraceWorld.glsl`) restricts the boost to a real per-cell bound. The primary march is unchanged.
- **Shading from the receiver's own material and cel band.** A shadowed pixel shows the receiver's albedo, hue-shifted by the authored `celShadowHueShiftDegrees` (-18 degrees; lit +18) at a lower band. There is no fixed shadow tint.
- Scene data stays: per-pane light declarations and per-emitter recipes in `LookdevSceneDefinition.h`. Nothing requires one mask bit per light.

### Dropped

- The per-light visibility-mask growth: `LightingConfig` capacity back to 4 (`LightingConfig.cs`, `LightingConfig.g.h`, `LightingConfig.glsl`), `HitRecord.glsl` and `PhotonCellsCommon.glsl` back to the base layout (bits 0-3 lights, bit 4 reservoir answer, bits 8-15 policy stencil), the two parity tests back to capacity 4.
- Per-source night isolation captures and the radial falloff / timing / `--isolation` helpers in `tools/check-lightunify-captures.py`; the test `LookdevLighting.CapturesEachNightSourceAndAmbientControl` and its registration.

### Known gap (closed by R502 sampling, not worked around)

The look-dev night scene declares 15 lights; `LightingConfigNode::SetLights` clamps the upload to the base capacity of 4. In declaration order that is the key light and three window panes; the remaining panes, the roof worklight and the flood are dropped. The night captures therefore show the warm floor pool and flood/worklight glow of the base gone, a nearly black plinth, and large dark shadow regions on the hull. They are labelled as this gap and are not gated. I did not reorder the declarations to hide it.

### Witnesses

All on the lane worktree; the GPU runs use dzn (Dozen), so they prove correctness only and **this report makes no timing claims**.

| Check | Result |
|---|---|
| Build, 22 `codegen/*_check` generation checks plus `test_rendergraph_criticalnodes_gpurender2b`, `..._sdiparity`, `test_rendergraph_core`, `test_uber_shader_splice`, `vixen_lookdev_capture` (`nice -n 10 ninja -j4`) | PASS, exit 0; log `.tmp/lightunify/salvage-build.log` |
| Focused tests via the test queue | PASS, 18/18: LightingConfigParity x4; ShadowCorrectnessTest x5 (occluded, registered recipes through the last declared slot, cel point-light falloff, two emissive); LightingConfigSdiParity; UberShaderSplice x8; log `.tmp/lightunify/salvage-tests.log` |
| Midday and night-service captures, one job per state | PASS: `Application.LookdevCapture` and `Application.LookdevLighting.EmittersHaveDistinctDeclaredSources` 2/2 per state |
| `tools/check-lightunify-captures.py ... --gate` (midday gated; night recorded only) | PASS, exit 0, 8 tiles, no failures |

Shadow footprint (occluded / tested), midday, before -> after: machine 0 -> 7/7, 91/91, 560/560, 22/22 over the four angles; foliage 184/184, 746/746, 310/310 (0 before); figure 83/83, 44/44; rock 46/46, 13/13. Night-service is also 100% after, 0 before. Fill is unchanged (midday 0.416 / 0.343 / 0.349 / 0.359). Unexplained-energy pixels: 0 in every tile.

Receiver colour (midday, observable materials): shadow hue is 17-26 degrees from the receiver's own albedo hue (allowed: authored shift 18 + 20 tonemap drift), and the shadow luma is below the lit luma. Near-grey materials (Structure, DarkMetal) carry no measurable hue and are recorded as unobservable.

### Sheets (I opened each image)

- `reports/lightunify-visual/salvage/midday-before-after.png`: rows are before image, after image, before shadow mask, after shadow mask; columns are the four angles. Before: almost no cast shadows beyond about 1 unit. After: hull, tree and figure shadows fall on the plinth, in the receiver's own dark hue; the mask rows show black footprints where before they were white.
- `reports/lightunify-visual/salvage/night-service-before-after.png`: same layout. After shows the 4-light truncation described above (known gap).
- `reports/lightunify-visual/salvage/after-contact-sheet.png` and `after-shadow-mask-sheet.png`: the 8 analyzer tiles (rows: midday angles 0-3, night-service angles 0-3, four per row pair as written by the analyzer). Midday tiles match the midday sheet; night tiles show the truncated lighting.
- `reports/lightunify-visual/captures-before/*` and `owner-{before,after}-contact-sheet.png` are the earlier committed sheets (15-light plan; superseded by the above).

### Canonical-shape checklist (R412)

Added lines are GLSL, C++ tests, CMake and a Python tool; none touches C#/Undertow code. The `throw`, `KindRef`, `PipeWriter`, `.Baked()` and `static readonly` greps over the added lines are empty. `Program.Legacy.cs` line delta: 0.

## STOPs

1. **Night scene under the 4-light mask.** Options: (a) leave the scene as is and wait for R502 sampling (recommended; zero extra work, honest picture); (b) reorder `LookdevSceneDefinition.h` so key, worklight and flood come first and the panes after, which makes the night look nearer the base but is a workaround of exactly the kind R502 rules out; (c) merge the panes back into one emitter. I took (a).
2. **Cel shadow hue shift of -18 degrees is authored, not derived.** A hue-shifted shadow is a style decision; if the owner wants the shadow to keep the albedo hue, the value lives in `LightingConfig` (`celShadowHueShiftDegrees`).
3. **No unit test for the shadow-reach fix.** `ShadowCorrectnessTest`'s dummy SDF buffer has `gridDim == 0`, so it cannot hit the empty-grid boost. Evidence is the captures (footprints 0% -> 100%). A test would need a registered-recipe case with `gridDim != 0` and an empty occupancy buffer; I did not add one in this pass.
4. **Not re-run in this redirected pass:** editormaterials fill / lighting-ratio checks, R434/R6, and the RenderGraph/SVO suites; only midday and night-service were captured (late-afternoon and overcast were not). Earlier attribution: minor-material fill failures are base-red, and the angle-1 fill drop is expected from the new plinth shadows. Known baseline reds: `Undertow.Core.Tests` and `Undertow.Content.Core.Tests` do not build on main-wave; wizard jobs `core-callable-registries` and `transpiled-app-system-schedule`.

## SPT DISPOSITION

- `lightunify` shadow/night-light unification task (proposals in `.spt-proposals/lightunify.jsonl`): PARTIAL. Shared-traversal SDF shadows and material-driven shading are done; the many-light night path is R502's sampling design and is not built here.

## CONSOLIDATION ISSUES

- The primary march probably shares the empty-occupancy-buffer ordering defect (the BodyOctreeSceneNode placeholder buffer feeds `gridDim != 0` with a sentinel grid). Unverified and unchanged, to avoid altering pixels; worth a separate look. Unified form: one occupancy upload owned by one node and bound identically for the march and lighting families, instead of the lighting splices passing `nullptr`.
- The shadow-reach flag `g_recipeProvenStepOnly` is a global set by the caller. Cleaner would be an argument of `traceUberRecipeBody`; I kept the global to avoid touching every generated call site.
