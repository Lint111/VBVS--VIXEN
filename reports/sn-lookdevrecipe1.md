# sn-lookdevrecipe1 (ws): the pack-driven look-dev capture now renders the recipe

Base: `lane-ucrecipes2` 44dea338 (not yet landed). Branch `lane-lookdevrecipe1`, change commit a6418c88 + this report. Not pushed.
Evidence: `~/codeman-cases/undertow/reports/sn-lookdevrecipe1-evidence/`. Hard stop (4 h) was passed while waiting on the last queued job; see STOPs.

## LANDABLE NOW
VIXEN `lane-lookdevrecipe1` @ a6418c88 (on top of ucrecipes2 44dea338). Proven: `core:asteroid` and the template boulder render in all 4 states x 4 angles, repeat runs byte-identical. **Not proven on this build:** the 16 existing goldens and the R796 unknown-id path (see STOPs). The diff touches only `LoadNamedRecipePreview`, the `recipePreview` camera branch, and adds one header function, so the lander should still run the golden compare before landing.

## CAUSE
**(a), framing/bounds in recipe space. Not (b), not (c).** The flattened program is a valid field, and the body-instance path draws it once its bound is right.
- CPU probe (`g++` against `EditorDocumentModel.cpp` and `SdfRecipeEval.h`, flatten + `evalRecipe`, scratch only, not committed), `core:asteroid`: ops=7, field inside samples span (7,21,7)..(57,43,57), `d(32,32,32) = -12`, `d(44,32,32) = 0`, `d(0,0,0) = +34.8`. The document is authored about the bake-grid centre (32,32,32), not the origin.
- Template boulder: inside span (19,19,19)..(49,45,45), `d(32,32,32) = -7.6`, `d(0,0,0) = +41.4`. Same convention.
- The capture's fallback (because `DeriveConservativeBounds` refuses programs with `Transform`) was a bound sphere at the origin, radius 24. The nearest asteroid surface is ~37 units from the origin, so the whole body lay outside the bound sphere and was culled: sky only. Scale 1.0 had the same bound, and the camera orbited the origin, not the body.
- Fix: `Vixen::SVO::Recipe::DeriveSampledBounds` (`RecipeBounds.h`) samples the field over the bake domain for programs the conservative derivation refuses. Result: asteroid center (32,32,32) r=40.7, boulder center (34,33,32) r=28.0. The capture frames with the existing `Vixen::Editor::FitEditorCameraToBounds`.
- Unit tests added (`RecipeSampledBounds.*` in `test_recipe_bounds.cpp`) but **not built or run** (only `vixen_lookdev_capture` was built); the same cases were checked in the g++ probe (sphere at 32,32,32 r=12 gives center 32,32,32 r=22.8; a far sphere and InvokeRecipe are refused).

## WITNESS
Native Release build, queued (`native-build2`, exit 0; 106 min wall, mostly queue wait on a cold tree; a first attempt failed because I overrode `VIXEN_FETCHCONTENT_CACHE` with a WSL path). Device class: **integrated** (AMD Radeon 8060S, driver 25.20.18), `capture-device.json` in each run dir.

| Capture | Run 1 | Run 2 | Byte-identical |
|---|---|---|---|
| `core:asteroid` from `core/content/core`, 4 states x 4 angles | `asteroid-run1/asteroid/` | `asteroid-run2/asteroid/` | 16/16 |
| `lookdev:boulder` (template `boulder.vxd`, see STOP 2), 4 states x 4 angles | `boulder-run1/boulder/` | `boulder-run2/boulder/` | 16/16 |
| `asteroid` midday-angle-0 single-angle run (`try1/`) vs run1 | | | identical sha256 |

Images opened (a subset, not all 32): `try1/asteroid/midday-angle-0.png` shows a lit yellow/red ringed body (a sphere with a torus ring through it) against a violet sky; `asteroid-run1/asteroid/night-service-angle-2.png` shows the same ringed body in dark amber tones against a dark navy sky; `boulder-run1/boulder/midday-angle-1.png` shows a lumpy sphere with a chipped facet, yellow top and red underside, centred. Shading is the look-dev rig's hard-banded toon look; I did not judge art quality.

Timing (measured, kept separate):
- Capture payload, warm: 16 images (4 states x 4 angles) in ~12 s (asteroid-run1 log 22:15:04 to 22:15:16), about 3 s per state. Boulder-run1 payload 44 s (cold shaders). A one-image cold run was 43 s.
- Queue wait: asteroid-try1 ~19 min, asteroid-run1 ~37 min, boulder-run1 ~7 min, boulder-run2 ~1.5 min, asteroid-run2 ~3 min (end-to-end job totals 1177 / 2276 / 461 / 91 / 171 s).

## FOR THE EDITOR LEAD
- Framing function: `Vixen::Editor::FitEditorCameraToBounds` (`VIXEN/libraries/VoxelDocument/include/EditorDocumentFraming.h`). The capture calls it with `worldGridSize = bakeResolution` and `renderScale = scale`, then adds the instance's frame centre.
- **The editor has the same bound defect.** `ApplyEditorPreviewBounds` (`EditorApplication.cpp:305`) falls back to `boundCenter = 0, radius = 64*0.866`. For `core:asteroid` that sphere only half-contains the body (centre 55 units away, body radius ~26 beyond), and the camera frames the origin. Use `DeriveSampledBounds` there (sn-editorview1).
- `VIXEN_LOOKDEV_RECIPE_SCALE`: still useful, but only as the world size of one recipe unit (default 0.08 gives a ~2 m asteroid). It is no longer needed to make the recipe visible. Framing now follows the bounds, so any scale frames correctly.

## STOPs
1. **Goldens and R796 path not re-run on this build.** The queued golden job (`sn-lookdevrecipe1:goldens`) sat starved behind other native jobs and my 25-minute shell timeout ended it, past the 4 h stop. Not run: 16-golden compare, `UnknownRecipeIdFailsWithR796` (the preflight code path is unchanged by this diff, but I did not run it). Re-run both before landing.
2. **The `undertow-author new render_recipe` scaffold is unusable with the built CLI.** `tools/author.sh new render_recipe lookdev <dir>` (the Release `undertow-author.dll` in the undertow tree dates from Sep 19) wrote `boulder.vxd` at the pack root, not `recipes/`, with the binary mangled to UTF-8 (bytes `EF BF BD`; the file is longer than the template), and the lint failed: `unknown body_kind 'core:planet' (expected: star, planet, moon)`. I could not rebuild the CLI (undertow is read-only for this lane). The boulder witness therefore uses `modpacks/_templates/render_recipe/boulder.vxd` copied byte-for-byte (cmp identical) into `boulder-pack-ok/recipes/` with the scaffold's `manifest.utdl`. So "from a scaffold" is only half true; the mangled output is kept as `boulder-pack/recipes/boulder.vxd.scaffold-corrupt`.
3. `with-test-lock.sh --resource light` run from the undertow tree was refused on a stale kernel pairing (`kernel.local.props` points at the deleted `wizdeps1` worktree); I ran it from the evidence dir instead.

## CONSOLIDATION ISSUES
- Stale built `undertow-author.dll` mangles binary template assets and writes them to the wrong directory (STOP 2); and the template's `render_recipes.utdl` fails its own lint (`core:planet` body_kind). Both detectable earlier by a scaffold-then-lint test in the author test suite.
- Editor fallback bounds (`ApplyEditorPreviewBounds`) cull documents authored about the grid centre; replace with `DeriveSampledBounds`.
- No queued wrapper for a named-recipe capture (still raw `cmd.exe` with UNC paths); `run-capture.bat` only drives the four-state test.
- A shell timeout on a queued job (`timeout` < queue wait) kills the wait silently; the queue's `starved` stall report is the only hint.
- Wait estimates: native Windows jobs wait behind `full-width` heads for 20-40 min even for 5-minute GPU captures.

## SPT DISPOSITION
No SPT task id was assigned in the brief; none closed. (sn-ucrecipes2 STOP 1 is resolved by this report's CAUSE, pending the golden re-run.)
