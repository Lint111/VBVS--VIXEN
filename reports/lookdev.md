# R432 look-dev turntable

## LANDABLE NOW

- Run 2 implementation tip: `bff5507184efcfb6bbbcaf6a079d1e28b741f2fc` — R424 affine scene placement and before/after capture sheets. The final Run 2 witness and capture disposition are recorded below.

## Scene and capture

The fixture is authored through the existing procedural SDF recipe provider and body-instance API. It contains an angular industrial crawler with off-white/orange hull colors, tracks, command module and glass; a rock outcrop; a soft foliage mass; a thin calm water slab; a ground plane; and a yellow maintenance figure for scale. The renderer has no wave or reflection form for this water surface, so the fixture uses a flat SDF slab. No new node family or content system was added. Scene data is in `VIXEN/application/main/include/graph/LookdevSceneDefinition.h:64`; the capture application and test are in `VIXEN/application/main/tests/test_lookdev_capture.cpp:52,164`, registered by `VIXEN/application/main/CMakeLists.txt:231`.

The shared `LightingConfigNode` carries one directional key and one point floodlight on the hull. The camera is a fixed four-angle orbit at 500×500 with no HUD. Each state warms for 64 frames, captures four yaws, and the CTest runner repeats each state in a fresh process and compares all 16 PNGs byte-for-byte.

| State | Key direction | Key color | Intensity | Ambient | Exposure compensation | Floodlight |
|---|---:|---:|---:|---:|---:|---:|
| Midday | (-.35, .88, .32) | `#FCC664` | 1.10 | .14 | −3.25 EV | 0 |
| Late afternoon | (-.78, .38, .22) | `#E9B163` | 1.00 | .13 | −3.25 EV | 0 |
| Overcast | (-.18, .96, .16) | `#D6DFE7` | .78 | .24 | −3.50 EV | 0 |
| Night/service | (-.28, .78, .38) | `#99B5CA` | .10 | .05 | −4.75 EV | 6.0 |

The floodlight uses the lighting-track sodium color `#F9B55C` at grid position (39, 36, 25). Exposure compensation is applied after the existing scene meter and before the existing ACES transform. The global shader default is −3.25 EV; the test supplies the per-state value.

Visual targets: T01 shared lighting, T04 palette/color, T06 materials, T07 edges, T10 scale/density, T13 machine lights, T18 cast shadows, T21 cel bands, and T22 figure. The five selected reference images and their takeaways are:

- C16, `/home/liory/codeman-cases/undertow/reports/visual/images/C16.png` — the overall world palette and readable machine/nature balance.
- R2-09, `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-09-bimodal-midday-four-groups.png` — midday contrast across machine, rock, foliage and water.
- R2-10, `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-10-bimodal-late-afternoon-hue.png` — warm key with a distinct late-day palette.
- R2-11, `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-11-bimodal-overcast-fill.png` — diffuse fill while preserving form.
- R2-16, `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-16-split-night-lifted-shadow.png` — night readability and local service-light contrast.

## R427 visual loop

The contact sheets put each matching C16 panel and lighting-track frame beside the four engine captures:

- [Midday](lookdev-visual/midday-contact-sheet.png)
- [Late afternoon](lookdev-visual/late-afternoon-contact-sheet.png)
- [Overcast](lookdev-visual/overcast-contact-sheet.png)
- [Night/service](lookdev-visual/night-service-contact-sheet.png)
- [Earlier shadow-repeat comparison](lookdev-visual/shadow-repeat-comparison.png)

Luma is Rec.709 on RGB/255. Values below are the median of the four per-angle p5/p50/p95 measurements from the latest passing run. The emissive share is the R427 proxy `HSV V > .85 && S > .45`; it also counts saturated painted colors, so it is an upper-bound proxy rather than a material-ID measurement.

| State | C16 p5 / p50 / p95 | Measured p5 / p50 / p95 | Emissive proxy | Per-state read |
|---|---:|---:|---:|---|
| Midday | .07 / .38 / .88 | .252 / .373 / .859 | 13.01% | Median and highlights are close; the low tail is much too bright. Saturated hull accents inflate the emissive proxy. |
| Late afternoon | .09 / .38 / .89 | .231 / .371 / .816 | 1.37% | Low tail is too bright and highlights are low; one exposure shift cannot correct both. |
| Overcast | .08 / .35 / .82 | .245 / .329 / .893 | 3.23% | Low tail is too bright, median is low, and highlights are high. The machine and sky dominate this spread. |
| Night/service | .03 / .15 / .40 | .045 / .154 / .284 | 0.00% | Median is close; highlights are too low, while the low tail is slightly high. The flood pool reads weakly. |

### §7 checks

| # | Check | Result | Read |
|---:|---|---|---|
| 1 | Capture metadata and framing | Pass | Fixed 500×500 output, 64 warm-up frames, named state and angle, no HUD. |
| 2 | Shared light sources and shadows | Pass with finding | Shared star key and hull floodlight are used. Latest two repeat runs matched; one earlier late-afternoon repeat differed. |
| 3 | Luma range | Fail | Every state misses at least one C16 percentile; per-state values are above. |
| 4 | Haze and depth | N/A | No atmosphere form exists in this render path. |
| 5 | Nature and domain separation | Fail | Rock and foliage read as smooth masses; the plant lacks fine branching and leaf detail. |
| 6 | Voxel stepping | Pass | The procedural SDF surfaces are smooth, with no visible voxel grid. |
| 7 | Intrusion read | N/A | No faction or world comparison is represented. |
| 8 | Machine lights and emissive share | Fail | Midday and overcast proxies exceed the 1.5% target; the night flood pool is weak. |
| 9 | Machine weight | Pass, limited | The low crawler body reads heavy, but the shapes are too simple for production detail. |
| 10 | Silhouette | Pass, limited | Hull, rock, foliage, water and figure separate at all four angles; machine silhouette remains coarse. |
| 11 | Materials | Fail | Color blocks separate, but material response and hue/value nuance are limited. |
| 12 | Human scale | Pass, limited | The yellow maintenance figure is visible beside the hull; it is a primitive scale cue. |
| 13 | Clean frame | Fail | No HUD is present, but a large purple-blue fallback sky dominates the frame. |
| 14 | Hull palette ratio | Pass by authored data | Major off-white and minor orange plates follow the bible palette; cel lighting shifts the accent toward red in captures. |
| 15 | Edge treatment | Fail | Hard edges and the current 3-band cel look are visible. Band count and shading-model changes remain owner-gated. |
| 16 | Extraction/site read | N/A | No extraction site or resource system is represented. |

### Per-state visual summary

- **Midday:** the hull and figure read clearly, but the low-luma tail is far brighter than C16 and saturated hull accents push the emissive proxy above target.
- **Late afternoon:** the warm key reads, while p5 is high and p95 low. The water and ground carry much of the lower frame; the engine output is flatter than the reference.
- **Overcast:** the fill softens shadows, but the foliage and hull separate weakly. The sky and bright surfaces drive p95 above target while p50 remains low.
- **Night/service:** the scene is legible but too dark at the high end; the flood pool is weak and the sky remains purple-blue rather than the reference's deep blue.

## Ambient and exposure tuning

The fixture uses the ambient and exposure values in the preset table. Ambient controls fill on lit surfaces and shadows; exposure compensation moves the full radiance range. The measured day and late-afternoon low tails are already above their targets, while late-afternoon highlights are below target. Raising exposure to fix those highlights would push the low tail farther away. Overcast also has a high p95 with a low p50, which one global exposure shift cannot fix. Night needs more highlight range, but an exposure increase would also raise its already-high p5.

The purple-blue sky is the renderer's fallback miss color, not ambient-lit geometry. Ambient cannot change it, and exposure changes brightness without correcting its hue. These defaults are a measured first pass, not a C16 match. No cel-shading model or band-count changes were made.

## Witness and findings

- Untouched lane base: `fc69e0c33be2321f52c0c1fb3c260d42a7de88a4`. Merged VIXEN wave: `92804a8f67c48653514c042a8f7b87b70bbb642b`.
- Configure recovery: queued `cmake --preset vixen-wsl` first failed with exit 1 because pinned Yeroket `8d68e983` was absent from the shared checkout (`.tmp/lookdev-merged-configure.log`). A worktree-local bare fetch and explicit `-DYEROKET_ROOT=.tmp/lookdev-kernel.git -DVIXEN_KERNEL_CACHE_DIR=.tmp/lookdev-kernel-cache` recovered configure (`.tmp/lookdev-merged-configure-recovered.log`).
- The full queued `cmake --build build/wsl -j3` passed all 22 `*_check` targets. The queued RenderGraph CTest label passed 1,339/1,339. The final queued no-op `cmake --build build/wsl -j1` reported `ninja: no work to do`.
- The queued RenderGraph CTest label passed 1,339/1,339 tests. The SVO label ran 751 tests and failed only `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` on opcode 94. The merged upstream `reports/vixkpin.md` records the same failure; it is outside this lane's changed code.
- The look-dev repeat-pixel test first failed at late-afternoon angle 0 (`.tmp/lookdev-merged-scene-capture-ctest.log`). After the harness stopped recursively deleting its caller-supplied output directory, two consecutive queued runs passed (200.57s and 200.36s; `.tmp/lookdev-final-review-capture.log`, `.tmp/lookdev-confirm-repeat-capture.log`), with all 16 pairs matching. The cleanup change only creates the output directory; the existing writer replaces each named PNG. The cause of the earlier shadow-on mismatch is not established. `ShadowVisibilityWave.comp` references KI-050 as a separate stored-control image-bistability issue, but that does not prove it is the same cause here.
- The standard WSLg capture witness produced all 11 expected captures with `DISPLAY` and Vulkan environment variables unset. The three `hud_capture_{5,45,75}.png` files stayed byte-identical. Four editor frames changed in each editor output set: `editor_capture_5.png`, `editor_capture_45.png`, `editor_capture_75.png`, and `editor_capture_105.png` (under both `editor/` and `native/editor/`). Their small center-only diffs come from the merged `editorbase` lane switching the editor to shared cel defaults; that lane's report records the same changes.
- CodeGraph reported that VIXEN has no index; no index was built. Navigation continued with `rg` as required.
- KFR follow-up is to compose this VIXEN capability from `KernelFederationRenderer/app/src/session_renderer.cpp`; KFR was not edited in this lane.

**STOPs:** None after the two consecutive passing look-dev witnesses. Keep the earlier repeat mismatch as an intermittent renderer finding; if it recurs, investigate the shadow visibility path before treating pixel determinism as stable.

## SPT DISPOSITION

Four consolidation proposals were appended through the SPT CLI and included in the implementation commit. They cover pinned-kernel provisioning, queue-credit sizing, a typed exposure setting, and the headless Vulkan capture lifecycle.

## CONSOLIDATION ISSUES

- proposed: Provision the pinned Yeroket snapshot when a VIXEN wave advances its kernel pin
- proposed: Size queued VIXEN build admission to the actual Ninja job count
- proposed: Expose HDR exposure compensation as typed render configuration
- proposed: Keep repeated look-dev captures in fresh Vulkan processes

## Run 2 — merge R424 instance transforms and re-witness

Merged `origin/wave/authoring-convergence` at `846ab1a999a542f663ee08764fe69b3de2beade3` in merge commit `2e136a3525a3599561c2430d1a285d160665dfbf`. Git reported no textual conflicts. `LookdevSceneDefinition.h` now composes translation, rotation, and scale into one affine matrix and submits it with `SetInstanceTransform`; material values stay in the cold record. All look-dev subjects use the transform stream. Presets, orbit angles, no-HUD capture, ambient values, and exposure presets are unchanged.

The implementation and visual sheets are committed as `bff55071` (`feat(lookdev): place scene instances through affine transforms`). Generated interfaces were regenerated from source: `sdi_tool merge-variants` and `--check` passed. CodegenTool restore and Release build passed; MiningBeamBuffer `--check` passed. The queued fresh merged-tree build passed all 174 steps and all 22 codegen checks (`.tmp/lookdev-run2-build-after-affine.log`).

### Look-dev capture comparison

The deterministic look-dev capture test passed twice. Each run compared all 16 state/angle image pairs; both runs were 16/16 byte-identical internally (`.tmp/lookdev-run2-lookdev-test-1.log`, `.tmp/lookdev-run2-lookdev-test-2.log`). Compared with Run 1, angle 0 is byte-identical for all four presets. Angles 1–3 have the following localized deltas in each preset:

| Preset(s) | Angle 1 | Angle 2 | Angle 3 |
|---|---|---|---|
| Midday, late afternoon, overcast, night/service | 151 pixels; bbox x=299–309, y=238–253 | 220 pixels; bbox x=190–205, y=240–253 | 23 pixels; bbox x=198–200, y=200–211 |

The changed files are `midday-angle-{1,2,3}.png`, `late-afternoon-angle-{1,2,3}.png`, `overcast-angle-{1,2,3}.png`, and `night-service-angle-{1,2,3}.png`. The bounded changes are consistent with the translated lamp/body geometry using the affine ray and normal path; this is an inference from their location and the unchanged angle-0 frames. No visual tuning was done.

- [Run 1 contact sheet](visual/lookdev-run2-before-contact-sheet.png)
- [Run 2 contact sheet](visual/lookdev-run2-after-contact-sheet.png)

### Shared capture comparison

Fresh detached builds of the exact pre-merge look-dev tip `25b865d44d8c2bd9c41d15a7b93b96b5af2db6ed` and the R424 parent `92804a8f67c48653514c042a8f7b87b70bbb642b` produced all 26 shared images. Their capture producer CTests passed 12/12. Against the pre-merge look-dev tip, 22/26 images are byte-identical. The four differences are `offscreen/editor/editor_capture_{5,45,75,105}.png`, all confined to x=234–265, y=232–263:

- Frames 5 and 75: 82 pixels differ; max channel delta 180.
- Frames 45 and 105: 1,024 pixels differ; max channel delta 126.

The other 22 images match exactly: all six cel-shading, six headless Cornell/starfield, seven native editor/HUD, and three offscreen HUD captures. A repeated merged offscreen editor producer matched all four merged frames byte-for-byte, so the difference is deterministic. The before/after sheets show the editor document instance at the center of the frame:

- [Pre-merge shared editor captures](visual/lookdev-run2-shared-before-contact-sheet.png)
- [Merged shared editor captures](visual/lookdev-run2-shared-after-contact-sheet.png)

Against the raw `92804a8f` R424 parent, the differing set is instead the four native editor frames. The pre-merge Run 1 tree adds the retained `-3.25 EV` default in `ExposureTonemap.comp`; its native editor captures match the merged tree, while its offscreen editor captures are the four that change above. This separates the existing Run 1 exposure difference from the R424 offscreen editor delta. The exact 26-frame identity condition is therefore not fully met by the current merged tree: 22/26 match the pre-merge look-dev captures, with the four deterministic editor-frame changes documented here.

### Engine witnesses and findings

- Full RenderGraph CTest: 1,340/1,340 passed, 6 registered skips (`.tmp/lookdev-run2-rendergraph-full.log`).
- Full SVO CTest: 750 passed, one known T-1449 failure, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, diagnostic `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (`.tmp/lookdev-run2-svo-full.log`). `reports/insttransform.md` records the same failure and diagnostic on the exact `92804a8f` base; it is outside this change's dependency and verification scope.
- Final focused capture/R424/Cel run: 25/25 passed (`.tmp/lookdev-run2-focused-captures-final.log`). An earlier focused Cornell run failed once; an isolated retry passed 1/1 and the final full focused rerun passed Cornell again (`.tmp/lookdev-run2-cornell-isolated-retry.log`).
- Native WSLg capture witness: 1/1 passed with caller `DISPLAY` and `WAYLAND_DISPLAY` unset (`.tmp/lookdev-run2-native-capture.log`). Repeated native and offscreen editor captures were byte-identical within the merged tree.
- The Windows-native route was attempted through the global queue but stopped before configure because `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` is unavailable on this host. The documented WSL preset route was available and passed the required build and captures.

### STOPs

- The strict shared-capture identity check is 22/26 against the pre-merge Run 1 tree. The four deterministic offscreen editor changes are localized and consistent with the editor document instance now using the affine stream at binding 47. They are recorded with before/after sheets; the look itself was not retuned. If all 26 shared images must remain byte-identical with no R424 visual exception, that gate remains unresolved.

### Run 2 consolidation issues

- proposed: Align CodegenTool run checks with the built configuration
- proposed: Preflight Windows toolchain availability before native VIXEN builds
- proposed: Propagate cancellation through queued build process groups
- proposed: Bind shared capture baselines to a fresh source SHA
