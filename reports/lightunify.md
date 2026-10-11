# lightunify — SDF-recipe shadows and material-driven shading (salvage under R502)

Owner ruling R502 changed this lane: lighting is agnostic to the number of lights. Lights are emitters and cost follows a fixed sample or photon budget, never lights x pixels. Direct light will use stochastic many-light sampling through the existing reservoirs; indirect light will use photon emission into the photon cells. A separate design pass covers that path and this lane does not build it. This pass keeps only what holds for any light count.

Lane branch `lane-lightunify`. Base `ed36300c`; this pass adds new commits on top (no rewrite, no push).

## LANDABLE NOW

Owner review of the first salvage: midday shadows approved, the -18 degree cel hue shift stays as authored, and the base night lights come back. This pass restores them and keeps the shadow-reach fix and the receiver-material shading. Per-pane windows return as emissive instances when R502 slice S1 lands reservoir sampling; nothing here works around that.

### Kept

- **SDF-recipe shadows through the shared traversal.** The lighting-family shaders splice the registered recipes with the same `SpliceProceduralRecipesIntoSource` the primary march uses, and the wave and the shade debug path declare binding 48, `recipe/SdfCoreKernels.g.glsl` and the splice marker ahead of `SceneBindings.glsl`.
- **Shadow-reach fix.** The lighting splices bind an empty occupancy buffer, so the 8x relaxed-step boost overshot occluders farther than about 1 unit. `g_recipeProvenStepOnly` (`SdfRecipes.glsl`, set around the shadow any-hit call in `TraceWorldShadowImpl`) limits the boost to a real per-cell bound. The primary march is unchanged.
- **Shading from the receiver's own material and cel band**, no fixed tint; the authored +/-18 degree cel hue shift stays.

### Restored to base in this pass

- `LookdevSceneDefinition.h` is byte-identical to the base, apart from one small helper, `PrimitiveForLighting` (the night-lamp placement the recipe gather already did, split out because the capture's scene export needs it). The light set is the base one: day = key + flood; night = key, one window light at the central pane, worklight, flood. The central pane is back at z=24.75, one recipe per material, the old `RecipeId(material)` and `EmitterOrigin`.
- The per-pane test `LookdevLighting.EmittersHaveDistinctDeclaredSources` and its CTest registration are removed (it asserts the per-pane set that no longer exists). `test_lookdev_capture.cmake` equals the base.
- Earlier in the lane (`e49c578a`): the per-light mask growth is already dropped (capacity 4, base `HitRecord` and photon-cell layout).

### Witnesses

All on the lane worktree. The GPU runs use dzn (Dozen), so they prove correctness only and **this report makes no timing claims**.

| Check | Result |
|---|---|
| Build: the 22 `codegen/*_check` generation checks plus `test_rendergraph_criticalnodes_gpurender2b`, `..._sdiparity`, `test_rendergraph_core`, `test_uber_shader_splice`, `vixen_lookdev_capture` (`nice -n 10 ninja -j4`) | PASS, exit 0; `.tmp/lightunify/restore-build.log` |
| Focused tests via the queue | PASS, 18/18 (LightingConfigParity x4, ShadowCorrectnessTest x5, LightingConfigSdiParity, UberShaderSplice x8); `.tmp/lightunify/restore-tests.log` |
| Fresh midday and night-service captures, one queue job per state | PASS: `Application.LookdevCapture` repeat-pixel test passed in both |
| `tools/check-lightunify-captures.py --gate` (midday gated, night recorded) | PASS, 8 tiles, no failures |

**Midday (shadows still present).** Footprint occlusion (occluded/tested), before -> after: machine 0/7 -> 7/7, 93/93, 562/562, 22/22; foliage 184/184, 746/746, 310/310 (0 before); figure 83/83, 44/44; rock 46/46, 13/13. Fill is identical (0.416 / 0.343 / 0.349 / 0.359) and unexplained-energy pixels are 0. Shadow hue is 17-26 degrees from the receiver's own albedo and one band lower than the lit side.

**Sheets (I opened both).**
- `reports/lightunify-visual/restore/midday-before-after.png` (rows: base image, after image, base shadow mask, after shadow mask; four angles). Base: flat cel colours, almost no cast shadows. After: the hull, tree and figure cast dark red-brown (the plinth's own colour, one band down) shadows across the plinth, as in the approved sheet; the mask rows show black footprints where the base is white. Everything else (sky, hull, trees, rocks, water) is unchanged.
- `reports/lightunify-visual/restore/night-service-before-after.png`. Base: a warm orange pool glows across the whole plinth around the machine, the cab windows are bright yellow, the hull top is warm-lit and trees are dark green. After: the windows, tree and rock colours and the sky match the base, and the machine still receives the key and ambient. But the plinth is almost black (mean luma 35 -> 10 at angle 0, 41 -> 6 at angle 2), the warm pool is gone, and the hull is a little darker (111 -> 91, 101 -> 75). The after mask has the plinth entirely black and the tree/hull shadow shapes visible. So night is **not** close to the base: see STOP 1.
- `after-contact-sheet.png` and `after-shadow-mask-sheet.png`: the 8 analyzer tiles (midday angles 0-3, then night-service 0-3). `captures-before/` and `owner-*` are the earlier committed sheets.

**Why night is dark (measured, not assumed).** A throwaway probe wrote the index of the first blocking instance into the visibility word for each light on every plinth pixel (night angle 2; probe reverted, not committed). Every blocked plinth ray to the three point lights is blocked by the **Hull** body: window light 30920/30920, flood 30920/30920, worklight 30418/30920 (the rest by the Lamp body). The key is blocked mostly by Hull (15760 of the 20200 shadowed) plus trees, rocks and props, as a real cast shadow. This is geometry, not a trace error: the window light sits at z=24.75 inside the hull box (z 22-42), the flood sits at y=36 just above the hull top (y 35) at the cab side, and the worklight is above the central tower, so no line from a plinth point to any of them avoids the hull. In the base these lights never saw an occluder because the wave had no recipe path.

### Canonical-shape checklist (R412)

Added lines are GLSL, C++ (one header helper, a test removal), CMake and Python. No C#/Undertow code changed. The `throw`, `KindRef`, `PipeWriter`, `.Baked()` and `static readonly` greps over the added lines are empty. `Program.Legacy.cs` line delta: 0.

## STOPs

1. **Night plinth is dark once shadows are real.** With the base lights restored and recipe shadows working, every service point light is occluded by the machine's own hull for every plinth pixel (evidence above), so the warm pool of the base is gone. The window glow, flood and worklight still light and glow on the machine's own surfaces. The rulings do not settle this. Options: (a) accept it until R502 S1: the shadows the owner approved are the cause, and no code change is needed (my recommendation, since any fix is a stand-in for S1); (b) move the three lights out of the hull volume in `LookdevSceneDefinition.h` (scene data, like `ed36300c`'s central-pane move; a pane 2 grid units in front of the face, the flood and worklight clear of the tower), which restores a pool but is an authoring change to the base light set that you told me not to make; (c) skip the host body for an attached light's shadow ray (a new mechanism beside `isPointLightSourceInstance`, which today excludes only the emitter's own instance; it is a deviation from "emitters are instances" that S1 supersedes); (d) turn point-light shadows off at night (a workaround). I did none of (b)-(d).
2. **Night is not gated.** The analyzer records night tiles but does not gate them: with every point light occluded the any-light lit/shadow split has no lit class, and two foliage materials show a shadow that is not a lower band there. The midday gate is unchanged.
3. **No unit test for the shadow-reach fix.** `ShadowCorrectnessTest`'s dummy SDF buffer has `gridDim == 0`, so it cannot hit the empty-grid boost; the captures (footprints 0% -> 100%) are the evidence.
4. **Not re-run in this pass:** editormaterials fill and lighting-ratio checks, R434/R6, and the RenderGraph/SVO suites; late-afternoon and overcast were not captured (only the two states you asked for). Known baseline reds: `Undertow.Core.Tests` and `Undertow.Content.Core.Tests` do not build on main-wave; wizard jobs `core-callable-registries` and `transpiled-app-system-schedule`.

## SPT DISPOSITION

- `lightunify` shadow/night-light unification task (proposals in `.spt-proposals/lightunify.jsonl`): PARTIAL. Shared-traversal SDF shadows and material-driven shading are done; the night set is back to base, and the night plinth stays dark until R502 S1 (STOP 1).

## CONSOLIDATION ISSUES

- The primary march probably shares the empty-occupancy-buffer ordering defect (the BodyOctreeSceneNode placeholder buffer feeds `gridDim != 0` with a sentinel grid). Unverified and unchanged, to avoid altering pixels; worth a separate look. Unified form: one occupancy upload owned by one node and bound identically for the march and lighting families, instead of the lighting splices passing `nullptr`.
- The shadow-reach flag `g_recipeProvenStepOnly` is a global set by the caller. Cleaner would be an argument of `traceUberRecipeBody`; I kept the global to avoid touching every generated call site.
