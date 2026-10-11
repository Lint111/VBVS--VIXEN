# lightunify — SDF-recipe shadows and material-driven shading (salvage under R502)

Owner ruling R502 changed this lane: lighting is agnostic to the number of lights. Lights are emitters and cost follows a fixed sample or photon budget, never lights x pixels. Direct light will use stochastic many-light sampling through the existing reservoirs; indirect light will use photon emission into the photon cells. A separate design pass covers that path and this lane does not build it. This pass keeps only what holds for any light count.

Lane branch `lane-lightunify`. Base `ed36300c`; each pass adds new commits on top (no rewrite, no push).

## LANDABLE NOW

Owner review of the restored night: the base lights sit inside or behind the hull and light nothing once shadows are real, so the owner chose to move them. This pass is an authoring change in `LookdevSceneDefinition.h` (3 constants) plus a comment fix in the analyzer. The shadow code is untouched.

### The move (grid units; world = grid x 10/64)

| Light | Base | Now | Why |
|---|---|---|---|
| Window light | (32, 37, 24.75) inside the hull box | (32, 37, 20) | 2 units in front of the front face (hull box z 22..42), at pane height, facing out; the panes themselves stay where they were |
| Flood (night only) | (40.6, 36, 32) above the hull top | (47, 26, 16) | front-right of the machine, clear of the hull footprint |
| Worklight | (32, 47.3, 32) above the tower | (17, 26, 46) | rear-left, clear of the hull footprint and the tower |

Colour, intensity and range are unchanged (window 3.5 at 0xe9bf6b, worklight 5.0 and flood 8.0 in the flood colour, range 8). No intensity adjustment was needed. Each emitter's instance origin, `EmitterOrigin` and light position still come from the same constant, so the shadow ray's source exclusion keeps working. Positions were chosen by a CPU segment test against the authored primitives over the plinth hit positions of the previous capture, then confirmed on the GPU. Midday constants (`kFloodlightGridPosition`) are unchanged. The emitter bodies (Lamp, Worklight) move with the lights, so two small glowing blocks now float beside the plinth at night (visible in the sheet); there is no pole.

### Witnesses

All on the lane worktree. The GPU runs use dzn (Dozen), so they prove correctness only and **this report makes no timing claims**.

| Check | Result |
|---|---|
| Build: 22 `codegen/*_check` generation checks plus `test_rendergraph_criticalnodes_gpurender2b`, `..._sdiparity`, `test_rendergraph_core`, `test_uber_shader_splice`, `vixen_lookdev_capture` (`nice -n 10 ninja -j4`) | PASS, exit 0; `.tmp/lightunify/lights-build.log` |
| Focused tests via the queue | PASS, 18/18 (LightingConfigParity x4, ShadowCorrectnessTest x5, LightingConfigSdiParity, UberShaderSplice x8); `.tmp/lightunify/lights-tests.log` |
| Fresh midday and night-service captures, one queue job per state | PASS: `Application.LookdevCapture` repeat-pixel test passed in both |
| Midday unchanged | confirmed: all four midday PNGs and hit records are byte-identical to the previous pass's captures (`cmp`) |
| `tools/check-lightunify-captures.py --gate` (midday gated, night recorded) | PASS, 8 tiles |

**Occlusion probe: share of plinth (Soil, top-facing) pixels each light reaches**, from the visibility bits in the hit records, only counting pixels inside the light's range (all four angles, `night-service`):

| Angle | Key | Window | Worklight | Flood | Any point light |
|---|---|---|---|---|---|
| 0 | 80.7% | 4.0% | 58.9% | 31.2% | 72.1% |
| 1 | 43.0% | 38.3% | 18.6% | 82.4% | 88.2% |
| 2 | 35.2% | 62.1% | 16.5% | 80.5% | 86.8% |
| 3 | 66.7% | 50.2% | 42.9% | 53.8% | 81.8% |

Previous pass (restored base lights): key 35-81%, window 0%, worklight 0%, flood 0% at every angle, any point light 0%. A CPU segment test against the primitives agrees with the GPU bits for 99.2% (angle 0) and 99.6% (angle 2) of sampled plinth pixels; the rest is the GPU seeing the light where the CPU test says blocked (shadow-ray bias at the plinth).

Plinth mean luma (angles 0-3), base / restored / moved: 37.0, 44.1, 48.1, 39.6 / 10.5, 7.5, 6.9, 9.4 / 19.0, 37.1, 41.9, 34.2. So the pool is back at about 85-90% of the base brightness at angles 1-3. At angle 0 it is about half: that tile views the plinth from the side where the window light is hidden by the hull (4% reach), so only the worklight and flood light it.

**Sheets (I opened both).**
- `reports/lightunify-visual/lights/midday-before-after.png` (rows: base image, after image, base shadow mask, after shadow mask): identical to the approved midday sheet. Dark red-brown hull, tree, figure and rock shadows across the plinth; masks show the black footprints.
- `reports/lightunify-visual/lights/night-service-before-after.png` (rows: base image, after image, base mask, after mask). Base: a smooth warm orange pool across the whole plinth, no cast shadows, bright yellow windows. After: the plinth is a warm orange-brown pool again, brighter near the machine's front and the right side, with real cast shadows: the hull's dark footprint on the plinth, the tree and rock shadows, and the figure's shadow. The hull's front face gets a bright warm patch from the window light (strongest at angle 2). Windows, sky, foliage and the key-lit hull top match the base. Two small gold emitter blocks sit beside the plinth. Against the base it is a shadowed warm pool rather than a flat one, slightly darker at angle 0. The after mask rows show the plinth mostly lit (white) with black wedges behind the hull and trees.
- `after-contact-sheet.png` and `after-shadow-mask-sheet.png`: the 8 analyzer tiles. `restore/` holds the previous pass's sheets (dark plinth).

### Analyzer gate

Night stays recorded, not gated. I tried gating night tiles after the move: it still fails (foliage and glass materials show a shadow that is not a lower band, and one material's hue is 171-176 degrees from its albedo), because the "any light reaches the pixel" split mixes the key with three point lights. The tool comment is corrected (the old reason, every point light occluded, no longer holds). The midday gate is unchanged and passes.

### Kept from the earlier passes

- SDF-recipe shadows through the shared traversal (lighting-family shaders splice the registered recipes), the shadow-reach fix `g_recipeProvenStepOnly`, shading from the receiver's own material and cel band (the +/-18 degree hue shift stays), the per-light mask growth dropped (capacity 4), and the `PrimitiveForLighting` helper.

### Canonical-shape checklist (R412)

Added lines are three numeric constants in a C++ header and one Python comment. The `throw`, `KindRef`, `PipeWriter`, `.Baked()` and `static readonly` greps over the added lines are empty. `Program.Legacy.cs` line delta: 0.

## STOPs

1. **Angle 0 night plinth is dimmer than the base** (luma 19 vs 37; window light reaches 4%). The side of the plinth that view sees is behind the hull from the window light. Options: (a) accept; R502 S1 sampling covers it (my recommendation); (b) a second window light on the back face, but that is one more point light and the mask holds 4; (c) move the worklight toward that side. Not done.
2. **Floating emitter bodies.** The Lamp and Worklight bodies move with their lights and float beside the plinth with no support. Cosmetic, easy to add a pole or put them on the rocks and trees if wanted.
3. **Night is not gated** (above), and midday is the only gated state.
4. **No unit test for the shadow-reach fix.** `ShadowCorrectnessTest`'s dummy SDF buffer has `gridDim == 0`; the captures are the evidence.
5. **Not re-run in this pass:** editormaterials fill and lighting-ratio checks, R434/R6, and the RenderGraph/SVO suites; late-afternoon and overcast were not captured. Known baseline reds: `Undertow.Core.Tests` and `Undertow.Content.Core.Tests` do not build on main-wave; wizard jobs `core-callable-registries` and `transpiled-app-system-schedule`.

## SPT DISPOSITION

- `lightunify` shadow/night-light unification task (proposals in `.spt-proposals/lightunify.jsonl`): PARTIAL. Shared-traversal SDF shadows and material-driven shading are done; the night lights are base colour and intensity at new positions that light the plinth; per-pane windows and many-light sampling wait for R502 S1.

## CONSOLIDATION ISSUES

- The primary march probably shares the empty-occupancy-buffer ordering defect (the BodyOctreeSceneNode placeholder buffer feeds `gridDim != 0` with a sentinel grid). Unverified and unchanged, to avoid altering pixels; worth a separate look. Unified form: one occupancy upload owned by one node and bound identically for the march and lighting families, instead of the lighting splices passing `nullptr`.
- The shadow-reach flag `g_recipeProvenStepOnly` is a global set by the caller. Cleaner would be an argument of `traceUberRecipeBody`; I kept the global to avoid touching every generated call site.
