# Lane exposure: dynamic exposure (eye adaptation)

Branch `lane-exposure`. Round 1 (3-hour checkpoint, `99f533e9`) is kept below as §R1. Round 3
(R670.1/R670.2) is first, then round 2 (owner rulings of 2026-10-20). All captures ran on Dozen and count for correctness only;
no timing in this report is representative. Evidence (logs, captures, CSV, scripts):
`/home/liory/codeman-cases/undertow/reports/exposure-evidence/round2/` (round 1: the parent
directory). Contact sheets: [`exposure-visual/`](exposure-visual/). Prior art:
[`exposure-priorart.md`](exposure-priorart.md).

## Round 3 (owner rulings R670.1, R670.2 of 2026-10-20)

Merged `origin/wave/authoring-convergence` `f20e97c2` first (merge `9e26febf`, no conflicts); a fetch
before the final checks showed no newer tip, so every round-3 capture and test ran on the merged tree.
Code commit: `c3aaa417`. Evidence: `/home/liory/codeman-cases/undertow/reports/exposure-evidence/round3/`
(sheets in `round3/sheets/`, logs in `round3/logs/`). All captures on Dozen: correctness only.

### R3.1 SceneLook declares its exposure mode (R670.1)

- `SceneLook.h:38` `ExposureDeclaration { Mode mode (Auto | Fixed); const char* preset; float fixedEV100; }`,
  carried as `SceneLook::exposure` (`SceneLook.h:47`). `ApplySceneLook` applies it
  (`SceneLook.h:87`) through `ExposureSettings()` (`:55`) and `ApplyExposureSettings()` (`:63`).
  This is the one new field the ruling allows. No new GPU field, attribute or node.
- **Fixed means meter bypassed, with no LightingConfig change.** `LightingConfigNodeConfig.h:133`
  `FixedExposure(ev100)` is the degenerate preset: min EV100 = max EV100 = the set value, and both
  speeds 0. `ExposureReduce.comp` then targets `log2(key) - (EV100 - 3)` whatever the histogram
  reads, and a zero step snaps to it. The meter still records `meteredEV100` for diagnostics, but
  that reading has no effect on the image.
- **Planet** declares `Fixed` at EV100 `-1.05893` (`PlanetSceneDefinition.h:54,91-92`): log2(0.18) + 3
  - 1.585, so the exposure is 3 (planetref D7/D8). The per-state meter table, `ExposureFor()`,
  `kFloorMeterLog2Exposure` and `ResolvedState::exposureCompensationEV` are gone, and the capture no
  longer sets a compensation. `test_planet_capture.cpp:305` fails the capture if the applied log2
  exposure differs from the declared one by more than 1e-4. The scene sidecar records
  `exposureMode`/`exposureFixedEV100`.
- **Look-dev** declares `Auto` + `"default"` explicitly (`LookdevSceneDefinition.h:35-36`). The
  ExposureRoom scenes use the look-dev look and keep it.

### R3.2 Dark scenes stay dark: `space` vs `interior` (R670.2)

`LightingConfigNodeConfig.h:119-122`:

| preset | floor EV100 | max lift (log2 exposure at the floor) | for |
|---|---|---|---|
| default | -8 | +8.53 | ordinary scenes (look-dev, room) |
| **interior** (was `dark`) | -12 | +12.53 | dark interiors; lifts the most |
| bright | -4 | +4.53 | exteriors |
| **space** (new) | **0.5** | **+0.03 (≤1.02×)** | intentionally dark scenes: bodies on a night sky |

- `space` keeps round 1's floor and default window (key 0.18, 50-95 %, 3/1 per s). A dark frame
  is shown about as authored and never lifted toward middle grey. Above its floor it behaves
  exactly like `default`.
- `FindExposurePreset()` (`:125`) replaces the old `find_if`. The unknown-name warning now lists
  the table.

**Which scenes declare space, and where.** The default-constructed `SceneLook` is the application's
built-in scenes. Its environment defaults (navy 0.02/0.02/0.06 gradient) are already the
`LightingConfigNode` defaults those scenes render, so `ExposureDeclaration`'s default preset is
`space` (`SceneLook.h:41`). `BuildRenderGraph.cpp:290` `ConfigureExposure` starts from
`SceneLook{}.exposure` (`:292`). So **HUD, editor, starfield and Cornell are space**.
`VIXEN_HDR_EXPOSURE_PRESET` and the per-value `VIXEN_HDR_EXPOSURE_*` overrides still apply to
them. Look-dev (and room) declare `default`; planet declares `fixed`.

Behaviour change: a scene that applies its own SceneLook (look-dev, planet) now sets its preset
values after `ConfigureExposure`. The env preset and per-value overrides therefore no longer reach
those two scenes. `VIXEN_HDR_EXPOSURE_DT` (the step) still does.

### R3.3 Re-captured and looked at (MEASURED)

| scene | sheet (`round3/sheets/`) | result |
|---|---|---|
| HUD (frame 75) | `hud-off-r1-r2-r3.png`, `hud-stats.txt` | **round 3 is pixel-identical to round 1** (RGB hash `45ee4b4227`): shaded beige/blue/grey bodies on navy, sphere (167,171,178), sky (18,18,51), luma p50 26, 0 % clipped. Round 2 was near-white on periwinkle (p50 81). |
| editor (frame 75) | `editor-off-r1-r2-r3.png` | pixel-identical to round 1 (`8d54721337`); round 2 was slightly lifted (p50 36 vs 28). |
| starfield (HDR on) | `starfield-off-r1-r3.png` | pixel-identical to round 1. The scene meters EV100 1.03, above every floor, so the space preset does not engage. No round-2 starfield frame exists. |
| Cornell (HDR on) | `cornell-off-r1-r2-r3.png` | rounds 1, 2 and 3 are pixel-identical (above every floor). |
| look-dev 4 states × 4 angles | `lookdev-<state>-off-r2-r3.png`, `lookdev-r3-vs-r2.txt` | all 16 **pixel-identical to round 2**, which matched the reviewed hdrmeter frames within ±0.29 EV per angle (round 2 §R2.2). Looked at midday: the reviewed look (white hull with red door, blue-violet sky, readable trees). |
| planet 12 states, fixed | `planet-states-{1,2}-off-r2-r3.png`, `planet-r3-vs-r2.txt` | the applied log2 exposure is **1.58500 in every state**. EV delta to round 2 is 0.000 in all 11 measurable states; C2 is a near-black frame with no unclipped samples, 0 pixels differ. Max pixel difference is 1 LSB (C1, C1-epic, C7; ≤0.003 % of pixels), the round-2 table's small reading error. Disc tones, C2-term grey face, C4a station and limb glow, C7 stone and ground, and the stars match. |
| ExposureRoom door / window / walk | `room-r2-r3.png`, `room-r3-vs-r2.txt` | all three pass. Every frame (door, window, 26 walk frames) is pixel-identical to round 2: the doorway is near black from outside, the window is blown from inside, and the walk goes black → readable → blown → settled. |

### R3.4 Tests (MEASURED; `round3/logs/`)

- `test_exposure_meter_mirror` 1/1, with three new cases reading the engine's real preset table
  (the target now links RenderGraph for the header; asserts are on, `-UNDEBUG`):
  - `PresetTableNamesSpaceAndInterior` (`:255`): `dark` is gone; `interior < default < bright <
    space` floors.
  - `SpacePresetLimitsTheLift` (`:268`): a HUD-like navy frame and a near-black frame settle at
    exactly the +0.026 cap under space, while default lifts them by more than 1.5 and 8 EV and
    interior by 3 EV more. A frame above the floor meters identically under space and default.
  - `FixedExposureBypassesTheMeter` (`:294`): near-black, mid-grey, 4096 and a sun frame all give
    exactly log2 exposure 1.585, from the first frame and with no lag after any scene switch. The
    meter reading still varies.
- Layout and drift: `LightingConfigParity.*` 4/4, `LightingConfigSdiParity.ReflectedLayoutMatchesCppStruct`
  1/1, `sdi_merged_drift_check` 1/1 (`layout.log`; the struct is unchanged at 272 bytes).
- `HudRenderCapture.*` 3/3, `EditorToggleUndoCapture.*` 7/7, both producers 2/2 (`hud-editor.log`).
- Planet: `Application.PlanetCapture.<state>` 12/12, one queue job each (`planet-<state>.log`),
  each including the new fixed-exposure assertion.
- `Application.LookdevCapture.ExposureRoom.{door,window,walk}` 3/3.
- Look-dev 16 states × angles in one process: passed (`lookdev-r3.log`). The registered
  64-frame × 16 × 2-run repeat-pixel suite was **not run** (as in round 2).
- Tone-map parity: **all 32** `Application.ExposureTonemapParity.*` passed (round 2 ran 2), and
  16/16 reference/system PNG pairs are byte-identical (`tonemap-parity-bytes.txt`).
- `Application.ReconstructionInputs.*` 6/6, including `Depth.fixed-exposure`.
- `HeadlessStarfieldGraph.*` 3/3 and `HeadlessCornellGraph` 1/1 (both registered with HDR off).
  With HDR on, Cornell fails at `test_headless_cornell_graph.cpp:313,369`: the pre-existing
  mining-beam composite issue from round 2, unchanged.
- Textures/RenderGraph/VulkanResources:
  - CPU part 1249/1259. The 10 failures are the same RmlUi "No font engine interface set!" set as
    round 2.
  - GPU part 176/190. The 14 failures are the same families as round 2: `ProceduralRecipeRenderTest.*`
    (10, test-local GLSL), `EditorDocumentRenderTest` (3) and `AppFlowEditorToggleRenderTest` (1).
  - Round 2's text-shader timeout and `UndoRedoRestoresRenderByteExact` are now green.
- Build: the full tree builds (`build-all.log`, 0 failed targets: the wave fixed round 2's base
  reds). Zero new warnings: every remaining warning is in files this lane did not touch.
- Canonical-shape grep on the added lines: no EntityId, `Id`, `KindRef`, attribute, interner or throw.

### R3 STOPs

None. Rulings 1 and 2 settled the design.
- **Decided (INFERRED, owner may revisit):** the preset is named by string (the existing table's
  key), not a new enum. An unknown name falls back to `default`.
- **Decided (INFERRED, owner may revisit):** `space` copies round 1's floor (0.5) exactly. The
  ruling's acceptance target was the round-1 HUD look, and that is now byte-identical.

## Round 2

Merged `origin/wave/authoring-convergence` `13358fe3` first (merge `ad3bc4c8`, no conflicts), so
every round-2 build, capture and test below ran on the merged tree.

### R2.1 Brightening floor (ruling 2)

`LightingConfigNodeConfig.h:115` `EXPOSURE_PRESETS`: the EV100 floor is now default **-8**,
`dark` **-12**, `bright` **-4** (was 0.5 / 1.0 / 0.5; key, percentiles and speeds unchanged).

Why these values (prior-art table, `exposure-priorart.md` §3/§6). VIXEN EV100 is in scene units,
where look-dev midday meters about -1. So the floor is best read as "how many EV a frame may be
lifted below midday":
- default -8 allows about 7 EV of brightening. That is UE 4.27's `min_brightness`..`max_brightness`
  span (0.03..8, log2 ≈ 8 EV).
- dark -12 allows about 11 EV, toward HDRP's -1..14 span (15 EV), for interiors and space.
- bright -4 allows about 3 EV, so exterior presets never lift a dark corner far.

Round 1's 0.5 floor was above every current fixture's reading, so the meter could only darken.
Every fixture now meters inside the window (HUD -1.79, editor 0.04..0.23, look-dev -0.8..-2.5,
room interior -4.1). Only near-black space frames reach the floor (§R2.3).

### R2.2 Look-dev compensation re-derived (ruling 1; MEASURED)

**Method.** The old and new paths share the same ACES (Narkowicz) + sRGB tone map. Inverting it per
pixel gives log2(L·2^net), so for the same scene the median per-pixel difference between two
captures is their net-exposure difference.
- **Validation.** On round 1's midday sheet the method reads +4.638 EV between the old-meter and
  new panels. The logged values give 4.668 - 3.25 - 0.026 = +4.642.
- **Measurement run.** Each state × 4 angles at 500², new meter, old compensation; adaptation
  pinned to the 0.5 s maximum step (`VIXEN_HDR_EXPOSURE_DT=10` is clamped to 0.5 s by the node),
  16 frames per angle, so converged.
- **Comparison.** Against the reviewed hdrmeter HDR-on frames (`reports/hdrmeter/visual-*-off-on-500.png`),
  both full-frame and on an 80-px sky strip (geometry-free).
- **Night.** The day states agree full-frame and on the sky strip to 0.01 EV. Night's geometry and
  service lights changed since the review (full-frame IQR about 2 EV), so night uses the sky strip
  (four angles 2.72..2.84).

| state | old comp | mean Δ to reviewed | new comp (`LookdevSceneDefinition.h:97-100`) |
|---|---|---|---|
| midday | -3.25 | +2.87 | **-0.38** |
| late-afternoon | -3.25 | +2.84 | **-0.41** |
| overcast | -3.50 | +3.00 | **-0.50** |
| night-service | -5.10 | +2.76 (sky) | **-2.34** |

So about 2.9 EV of each old value only cancelled the old meter. What remains is look intent: day
sits about 0.4 EV under key 0.18, and night 2.3 EV under it.

**Round 2 against the reviewed frames.** Per angle within ±0.29 EV, per-state mean about 0
(`round2/lookdev-r2-vs-reviewed.txt`). The per-angle spread is the new meter weighting sky-heavy
angles differently from the old key-1.0 meter.

**Looked at** (`exposure-visual/lookdev-<state>-reviewed-off-r1-r2.png`; columns reviewed /
HDR off / round 1 / round 2, one row per angle):
- **midday:** round 2 is the reviewed look: bright, saturated, blue-violet sky, white hull with red
  door, readable trees and figure. Round 1 was crushed to near black. The only difference from the
  review is the dark-red cast shadows on the ground, which are the shadow pass that landed after
  the review (HDR-off shows them too).
- **late-afternoon:** same verdict; warm ground, readable foliage, sky matches.
- **overcast:** same; the pale water and the pale-sand ground match the review.
- **night-service:** the review's dark navy night: hull and figure readable, warm service-light
  pools. The pools are broader than in the review because of the lighting changes since then, not
  exposure. Round 1 was black.

`VIXEN_HDR_EXPOSURE_FIXED` pinned the old meter's midday reading 4.66783. It now pins the new
meter's midday angle-0 reading **1.508** (`ExposureReduce.comp:26`), so fixed-exposure captures keep
the reviewed midday look under the new compensation.

### R2.3 Planet compensation re-derived (MEASURED)

Planet pins a net exposure of 3 (planetref D7/D8), so its compensation has always been "target
minus the meter's reading". The old corner-tile readings (0, 4.059, 0.029) only described the old
meter.
- **Measurement.** All 12 states were run with the new meter (default preset, meter logged).
  Planet-filled frames settle at log2 exposure 1.37..1.65. C7-60 and C7-60-1p5au read 6.94. The
  seven mostly-black-sky frames (C2, C4a-c, C5a-moon, C9) meter below the -8 floor and settle at
  the floor's 8.526.
- **Change.** `PlanetSceneDefinition.h:425-435` now carries those readings, and `ExposureFor()`
  keeps net exposure 3.

**Looked at** (`exposure-visual/planet-states-{1,2}.png`; columns reviewed planetsky frame / new
meter with the old compensation / round 2):
- Round 2 matches the reviewed planetsky frames in all 12 states: C1 disc tones, C2-term flat grey
  face, the C4a station and limb glow, C7 stone tones, star points.
- The middle column shows what the re-derivation prevents: C1 and C7 turn white, the C4/C9 limb
  glow blows out, and C7-60 ground goes pure white.

### R2.4 Owner acceptance scenes (ruling: dark room / window / walk)

The look-dev scene machinery can express an interior with an opening, so this is not a STOP. An
SDF recipe of an outer box, minus the inner box, minus a doorway and a window
(`test_lookdev_capture.cpp`, `ExposureRoomInstructions`), registered through the same procedural
recipe path as the stage, beside the stage on a stone pad.
- **Lighting.** Midday sun.
- **Fill.** The renderer's ambient term is unoccluded sky fill, so the room scenes lower it from
  0.14 to 0.01. That puts the interior about 6.8 EV under the sun; real interiors sit 5-8 EV under
  their sunlit exterior.
- **Sky.** The stage's authored night-navy sky (0.02-0.06) would make "outside" darker than the room
  walls, so the room scenes use a daylight sky (0.05, 0.08, 0.18), about a tenth of sunlit white.
- Both are fixture values in the test, not engine defaults.
- **Registration.** Three CTests: `Application.LookdevCapture.ExposureRoom.{door,window,walk}`,
  480×320, step pinned to 0.1 s. All three passed (`round2/logs/room3-*.log`).

Looked at (`exposure-visual/room-door-window.png`, `room-walk-frames.png`, `room-walk-curve.png`):
- **door: adapted to the exterior** (exposure 0.89×). The sunlit pad is bright yellow and the sky
  blue. Through the doorway the room is **near black** (about 8/255); only the sun patch on the
  floor inside the door shows. Not lifted to grey. PASS. The doorway wall itself reads dark brown:
  at midday the sun grazes vertical walls (N·L 0.3), and the meter follows the pad.
- **window: adapted to the dark interior** (19.8×). The interior wall reads mid-grey and readable;
  the window is **blown to pale blue-white**, and the sun patch on the floor is white. PASS.
- **walk** (meter CSV `round2/room/walk-meter.csv`):
  - **0-2 s, outside:** 0.89×.
  - **2.5 s, through the door:** the frame goes black.
  - **2.5-8 s, inside:** the room lifts gradually from black to readable grey.
  - **9.0 s, back out:** the exterior is blown toward white, then settles by about 9.5-10 s.
  - Measured against the configured speeds:
    - darkening (going in, `speedDown` 1/s): 0.606 of the gap remains after 0.5 s, against
      e^-0.5 = 0.607;
    - brightening (coming out, `speedUp` 3/s): 0.222 remains after 0.5 s, against
      e^-1.5 = 0.223.
  - The inside/outside gap is 4.8 EV.
- **Intermediate captures** (`round2/logs/room-*.log`, `room2-*.log`): the first attempt (stage
  night sky, fill 0.03) gave only a 1.2 EV inside/outside gap. The interior read dark grey and the
  window view did not blow out, which led to the sky and fill values above.

### R2.5 HUD and editor under the lower floor (looked at; MEASURED)

**HUD** (`exposure-visual/hud-oldmeter-r1-r2.png`, frame 75):

| | sphere (375,250) | sky (450,400) | luma p5/p50/p95 | pixels ≥250 |
|---|---|---|---|---|
| old meter | (255,255,255) | (206,206,241) | 50/209/255 | 8.7 % |
| round 1 | (167,171,178) | (18,18,51) | 19/26/171 | 0 % |
| round 2 | (237,239,241) | (75,75,150) | 33/81/239 | **0 %** |

Round 2 lifts the dim HUD frame toward middle grey (median 26 → 81), with no clipping. The old
white-out does not return.

**Editor** (`editor-off-r1-r2.png`): almost the same as round 1. The editor meters about 0.1 EV100,
close to the old floor.

### R2.6 Checks (MEASURED unless marked)

- **CPU checks, all passed:**
  - `test_exposure_meter_mirror` 1/1, updated for the -8 floor; the floor case now uses a 1e-5
    frame.
  - `LightingConfigParity.*` 4/4 and `LightingConfigSdiParity.ReflectedLayoutMatchesCppStruct` 1/1
    (the 272-byte layout).
  - `sdi_merged_drift_check` 1/1, after regenerating `ReconstructionExposure-SDI.g.h` and
    `ExposureTonemapReference-SDI.g.h`. The rename changes their layout hash, and the first run
    caught it.
- **Textures/RenderGraph/VulkanResources, CPU part:** 1250/1260. The 10 failures are all
  `Rml::Initialise()` returning false ("No font engine interface set!") in
  `UiCompositionHostTest.*` (6), `ViewEditorLayersGolden/Reconcile` (2) and `BlobView.*` (2). This
  is an RmlUi environment fault, unrelated (INFERRED; not rerun on the base).
- **The same suites, GPU part:** 173/189 passed.
  - `ProceduralRecipeRenderTest.*` (10) fail on a test-local GLSL compile error
    (`shader:6: ')' Expected`).
  - `EditorDocumentRenderTest` (3) and `AppFlowEditorToggleRenderTest` (1) fail identically at the
    round-1 floor (0.5) and the new one. Their sources never reference exposure, tone mapping or
    HDR, so they are unrelated (INFERRED pre-existing).
  - `vixen_text_shader_capture_producer` hit its timeout under GPU contention.
  - `EditorToggleUndoCapture.UndoRedoRestoresRenderByteExact` is fixed below.
- **`HudRenderCapture.*` 3/3, `EditorToggleUndoCapture.*` 7/7, producers 2/2**
  (`round2/logs/editor-instant.log`).
  - `UndoRedoRestoresRenderByteExact` failed at the -8 floor and passed at 0.5 and with HDR off.
    With the lower floor the editor frame adapts, and an exponential approach never lands
    exactly, so undo's frame differed in the fourth decimal of log2 exposure.
  - **Fix:** the producer's registration (`application/editor/CMakeLists.txt`) makes adaptation
    instant (0.5 s step × 100/s), so each capture is a function of what it shows. The test still
    asserts byte equality.
- **Planet:** all 12 `Application.PlanetCapture.<state>` passed (the two-run repeat-pixel witness).
  C1, C1-epic and C7 passed on rerun: the first attempt lost the device with
  `VK_ERROR_OUT_OF_DEVICE_MEMORY` in the same second as a concurrent room capture (Dozen
  contention).
- **Look-dev:** 16 states and angles captured and passed in one process on the round-2 binary. The
  registered repeat-pixel suite (`RendersFourStatesAtFourAnglesDeterministically` via
  `run_lookdev_capture.cmake`, 64 frames × 16 × 2 runs) was **not run** this round.
- **Tone-map parity** (spot check, 2 of the 32): `Application.ExposureTonemapParity.{reference,system}.midday.angle0`
  both passed, and the two PNGs are byte-identical. That covers the renamed field in
  `Reference/ExposureTonemapReference.comp`. The other 30 were not run.
- **Cornell mining beam with HDR on** (`test_headless_cornell_graph.cpp:313,369`): **pre-existing,
  not this lane.** With HDR on, the beam frame is byte-identical to the no-beam frame.
  - Cause, from the code (not a run on the base): `SpatialReuseShade.comp`
    `CompositeMiningBeams` writes only `sceneRadianceImage`. The HDR tone map
    (`ExposureTonemapSystem`) reads `sceneRadianceHistoryOutput`, which is stored before the beam
    composite.
  - The composite came with `3dcdef4f` (mining-beam feedback); the fixture is registered with
    `VIXEN_HDR_EXPOSURE=0`.
- **Build:** zero new warnings in touched targets. Three build reds, all base:
  - the `no-new-mutex` gate's stale allowlist row for `Textures/src/TextureLoader.cpp`;
  - the `test_registration_api` link (TextureCacher), both from the wave's `fd89b971` texture
    extraction;
  - `frame_targets_schema_check` (`SubstrateKinds.cs:173 'Rate'`, the undertow pair; known since
    round 1).

## §R1 Round 1 (checkpoint `99f533e9`)

### 1. Why the default-on meter whited out (MEASURED)

The old path (`ExposureMeter.comp` / `ExposureReduce.comp` before this lane) worked like this:

- Each 16×16 tile clamps its brightest 5 %.
- The reduce writes `ev100 = −mean(log2 L)`.
- The tone-map applies `2^(ev100 + compensation)`.

So the frame's geometric-mean luminance was mapped to **1.0**. That is a key of 1.0, about 2.5 EV
hotter than middle grey 0.18. There was no EV clamp and no percentile rejection of dark pixels,
and a black pixel counted as luminance 1 (log2 0).

The HUD frame was instrumented with `VIXEN_HDR_METER_DEBUG` on the base binary
(`base-hud/ctest.log`). It reads `EV100=5.21982` on **every one of the 85 frames**: a constant
2^5.22 = **37×** multiplier. The frame's geometric mean is 2^−5.22 = 0.027 (mostly the dark
sky). At 37×, a lit sphere of about 0.27 becomes about 10 and saturates the ACES curve, and the
0.02–0.06 sky becomes 0.7–2.2.

Pixel values at frame 75 (`sheets/hud-off-oldmeter-new.png`):

| HUD frame 75 | sphere (375,250) | sky (450,400) | luma p5/p50/p95 | pixels ≥250 |
|---|---|---|---|---|
| HDR off | (65,69,77) | (3,3,10) | 4/6/69 | 0 % |
| old meter | (255,255,255) | (206,206,241) | 50/209/255 | 8.7 % |
| **new default** | (167,171,178) | (18,18,51) | 19/26/171 | **0 %** |

The editor capture shows the same pattern: 32.2 % of pixels clipped with the old meter, 0 % with
the new default.

### 2. What changed (commit `4fb2d687`)

This extends the existing exposure path (meter → reduce → generated tone-map). It is not a
parallel system.

- **`VIXEN/shaders/ExposureMeter.comp`:** a per-workgroup log2 luminance histogram (128 bins,
  0.25 EV each, covering −20…+12). Bins are filled with shared integer atomics, then added into
  the meter buffer. Integer sums don't depend on order, so the result is deterministic. Black
  pixels go to bin 0, where the low percentile rejects them.
- **`VIXEN/shaders/ExposureReduce.comp`** does the metering and adaptation:
  - averages log2 luminance over the low…high percentile window;
  - computes EV100 = log2 L + 3 (K = 12.5) and clamps it to [min, max];
  - sets the target `log2(key) − (EV100 − 3)`;
  - adapts by `prev + (target − prev)(1 − e^(−rate·dt))`, using `speedUp` when the scene
    brightens and `speedDown` when it darkens;
  - clears the histogram.

  The `VIXEN_HDR_EXPOSURE_FIXED` branch is unchanged.
- **`VIXEN/shaders/ExposureHistogram.glsl`** (new): the shared bin constants and the meter state
  layout. The applied value stays at offset 0, so the tone-map and `ReconstructionExposure.comp`
  are untouched.
- **Config, in the existing mechanism** (the `LightingConfig` [GpuStruct] plus node
  parameters):
  - `codegen/config-schemas/LightingConfig.cs` adds key, min/max EV100, low/high percent,
    speedUp/Down and deltaSeconds. The struct grows from 240 to 272 bytes.
  - Regenerated with `lightingconfig_regen`: `LightingConfig.g.h/.glsl`, the
    `ExposureTonemapSystem.comp` kernel output and the merged SDI headers (`sdi_tool
    merge-variants`).
  - `LightingConfigNodeConfig.h` holds the `PARAM_EXPOSURE_*` names and the
    `EXPOSURE_PRESETS` table.
  - `LightingConfigNode.cpp` clamps each value. It takes `dt` from the graph frame time (capped
    at 0.5 s) or from a pinned step.
- **`BuildRenderGraph.cpp` `ConfigureExposure`:**
  - `VIXEN_HDR_EXPOSURE_PRESET=default|dark|bright` picks a preset.
  - `VIXEN_HDR_EXPOSURE_<KEY_VALUE|MIN_EV100|MAX_EV100|LOW_PERCENT|HIGH_PERCENT|SPEED_UP|SPEED_DOWN>`
    overrides one value.
  - Offscreen captures pin `dt` to 1/60 s, so repeat runs are pixel-identical.
    `VIXEN_HDR_EXPOSURE_DT` overrides the step.
  - The meter buffer is now a fixed 528 bytes.
  - `VIXEN_HDR_EXPOSURE` still switches the whole path on or off.
- **`VulkanGraphApplication.cpp`:** `VIXEN_HDR_METER_DEBUG=<n>` logs the applied, metered and
  target values for each of the first n frames.
- **`test_lookdev_capture.cpp`:** `VIXEN_LOOKDEV_CAPTURE_STATE` accepts an ordered comma list,
  so one process can witness a lighting switch.

### Presets (engine values; source of each value in exposure-priorart.md §6)

| preset | key | min / max EV100 (scene units) | low / high % | up / down (1/s) |
|---|---|---|---|---|
| default | 0.18 (Reinhard) | 0.5 / 12 | 50 / 95 | 3 / 1 (UE 4.27, HDRP) |
| dark (interior/space) | 0.18 | 1.0 / 12 | 75 / 98 (UE5 starting range) | 3 / 1 |
| bright (exterior) | 0.18 | 0.5 / 16 | 10 / 90 (UE 4.27 default) | 3 / 1 |

VIXEN radiance is not calibrated in cd/m². Scenes are authored so that a lit surface is about
0.1–0.3 at exposure 1, which is how the legacy HDR-off path displays them. So the EV100 clamps are
in scene units, and the default floor of 0.5 means "never boost above about 1×". As measured,
every current fixture meters below that floor: HUD −1.79, editor 0.04–0.23, look-dev night −2.10,
look-dev midday −0.97. Only starfield/cornell (1.03) meters above it. The default therefore fixes
the dark scenes and only darkens bright ones. **The floor is the main value for the owner to
tune.**

### 3. Witness (looked at; sheets in `exposure-evidence/sheets/`)

| scene | sheet | result |
|---|---|---|
| HUD | `hud-off-oldmeter-new.png` (off / old meter / new) | White-out fixed. All three bodies are shaded, the sky is dark navy (the authored sky colour, now display-encoded), and nothing clips. |
| editor | `editor-off-oldmeter-new.png` | Old meter: 32 % clipped and a lavender sky. New: the object is pale but shaded, p95 208, 0 % clipped. |
| starfield fixture | `starfield-off-new.png` | Metered EV100 1.03, exposure 0.70×. Brighter display encoding than off, with no extra clipping. The 1.4 % clipped is the ceiling emitter, the same as off. |
| cornell fixture | `cornell-off-new.png` | Same as starfield (same scene, starfield off). |
| bright scene: look-dev midday | `lookdev-midday-off-oldmeter-new-unclamped.png` (off / old-on / new default / new with floor −8) | **Not acceptable yet; see STOPs.** The meter holds 1.02×, but the scene's authored compensation of −3.25 EV (tuned against the old key-1.0 meter) makes it darker than HDR-off. |

**Time series (MEASURED):** `series-unclamped/exposure-series.png` and `.csv`. Look-dev
night-service → midday → night-service, 64 frames each, 1/60 s steps, floor lowered to −8 so the
meter is unclamped.

- **Night → midday (brightening):** the remaining error after 20 frames is 0.368 = e^(−3·0.333),
  which matches speedUp 3/s.
- **Midday → night (darkening):** after 64 frames, 65.6 % of the gap is closed = 1 − e^(−1·1.067),
  which matches speedDown 1/s.

With the default floor, the same switch stays flat at 1.02× (`series-night-to-midday`), because
both states meter below the floor. The three-state run failed the look-dev harness's 300 s
runtime budget (406 s on Dozen). The images and meter log are complete.

### 4. Tests

- `test_exposure_meter_mirror`, rewritten as a CPU mirror of the new shaders. It covers:
  - percentile rejection of a dark background;
  - high-percentile rejection of a sun;
  - order independence;
  - full-frame coverage (regression from the tile meter);
  - the EV100 clamps;
  - frame-rate independence at 30, 60, 144 and 240 Hz;
  - separate up/down speeds;
  - zero-step snapping;
  - empty-histogram hold.

  **Passed 1/1** (`exposure-evidence/ctest-mirror2.log`). Two earlier queued attempts sat behind
  other lanes (`head-order`).
- Layout tests updated for the 272-byte struct: `test_lightingconfig_parity.cpp`,
  `test_lightingconfig_sdi_parity.cpp`. **Not run yet.**
- `vixen_hud_capture_producer` and `vixen_editor_capture_producer` passed with HDR on and with HDR
  off (ctest 1/1 each). The windowed-capture file assertions (`HudRenderCapture.*`,
  `EditorToggleUndoCapture.*`), Textures, RenderGraph and VulkanResources: **not run**. The full
  test build was at 84/366 when the checkpoint was taken.
- Build warnings: none new. The only warning in touched targets is the pre-existing
  `DeviceNode.cpp:683`. No golden was recaptured.

### 5. R619.1: should the exposure math be a kernel callable?

INFERRED from scanning the pinned kernel source (`96570e48`):

- The GPU-system emitter (`[KernelSystem(Emit = Glsl)]`, as used by `ExposureTonemapSystem`)
  takes per-element bodies. I found no workgroup shared memory or atomics, so the histogram
  meter and the percentile reduce can't be expressed there today.
- The adaptation step is a pure scalar function of (metered EV100, previous, settings, dt). It
  could be a kernel callable emitted to GLSL and C++. Then the CPU tests would exercise the same
  source instead of a hand mirror.
- That needs a kernel reduction/histogram primitive, or a single-invocation GPU-system form. Both
  are new kernel capability and need a ruling.

For now the math stays in the existing hand shaders, which is the existing renderer path.

## STOPs (round 2; STOP 1 resolved by R670.1 in round 3, STOP 2 by R670.2)

1. **Planet-style pinned exposure under an auto meter.** Planet keeps a physical net exposure of 3,
   so its compensation must cancel the meter's settled reading per state. That is what it always
   did, now with the new meter's readings. Seven states sit at the -8 floor, so changing the
   default floor changes their compensation.
   - The unified form would be a scene-declared exposure mode (auto vs pinned) in `SceneLook`. That
     is a new declaration field, so I did not mint it.
   - Options: (a) keep the measured table (done; re-measure if the floor moves); (b) an owner ruling
     for a pinned mode in `SceneLook` (R507.3's declaration).
   - Recommendation: (b).
2. **Floor values are defaults, not final.** -8/-12/-4 come from the prior-art spans. HUD now reads
   brighter (median 26 → 81, 0 % clipped). The owner should judge HUD and editor from
   `exposure-visual/hud-oldmeter-r1-r2.png` and `editor-off-r1-r2.png`.
3. **Acceptance-room fixture values** (fill 0.01, daylight sky) are test fixture values, needed
   because the renderer's ambient is unoccluded sky fill and the stage sky is night-navy. An
   occluded sky fill (AO) is renderer work outside this lane.

## SPT DISPOSITION

Round 3:
- **T-2878** (fixed exposure capture mode): PARTIAL. R670.1's `ExposureDeclaration::Mode::Fixed`
  is the declared fixed-EV mode that T-2878 asks for (planet uses it). The test-only
  `VIXEN_HDR_EXPOSURE_FIXED` shader variant (`ExposureReduce.comp:24`, `sdi-variants.json:138`)
  and its `Depth.fixed-exposure`/temporal/jitter users still exist. Converting them to a declared
  fixed EV100 (log2 exposure 1.508 = EV100 -0.982) is the remaining step; it was outside this round's scope.
- Owner rulings R670.1 and R670.2: done (§R3.1-R3.4).

No SPT id was given for this lane. Round 2's rulings 1-2, the acceptance scenes, the skipped
checks, the `ev100` rename and the merge are done. PARTIAL:
- the registered look-dev repeat-pixel suite and 30 of the 32 tone-map parity captures were not rerun;
- STOP 1 needs an owner decision.

## CONSOLIDATION ISSUES

Round 3:
- `VIXEN_HDR_EXPOSURE_FIXED` duplicates the new fixed mode; see T-2878 above (not re-filed).
- Python on ws no longer has Pillow/NumPy (round 2's sheet scripts used them). Round 3 used a
  private venv at `<worktree>/.tmp/pyenv`.


Filed as proposals (`.spt-proposals/exposure.jsonl`): the mining beam with HDR on, the wave's
texture-extraction base reds, and the stb shallow-fetch pin.

- **Mining beam with HDR on** (pre-existing): the beam composite lands in `sceneRadianceImage`, but
  the HDR tone map reads `sceneRadianceHistoryOutput`. So with default-on HDR the beam is invisible
  (`test_headless_cornell_graph.cpp:313,369`). The fix is a renderer decision: tone-map the
  composited image, or composite into the history the tone map reads (with ghosting).
- **Base reds on the wave** (`13358fe3`): `no-new-mutex` stale allowlist row
  (`Textures/src/TextureLoader.cpp`); `test_registration_api` link failure (TextureCacher); both
  from `fd89b971`.
- **stb is pinned to a SHA that `GIT_SHALLOW TRUE` cannot fetch** (`dependencies/CMakeLists.txt:33`,
  `fd89b971`). A fresh fetch cache fails configure; I worked around it with
  `-DFETCHCONTENT_SOURCE_DIR_STB=~/.cache/vixen-fetchcontent-cache/stb-src`. Configure also needs
  `UNDERTOW_ROOT`, and the build needs `VULKAN_SDK` in its environment (the UI/text SPIR-V
  regeneration).
- **RmlUi tests fail `Rml::Initialise()`** ("No font engine interface set!", 10 tests).
- **`ProceduralRecipeRenderTest.*`** (10) fail on a test-local GLSL compile error.
- `frame_targets_schema_check` / `exposure_tonemap_gpu_regen`: the undertow pair's
  `SubstrateKinds.cs:173 'Rate'` (from round 1).
- The node clamps a pinned adaptation step to 0.5 s, so `VIXEN_HDR_EXPOSURE_DT` above 0.5 is
  silently 0.5. It is documented here; logging the clamp would help.
- The queue's two-GPU sharing caused simultaneous `VK_ERROR_OUT_OF_DEVICE_MEMORY` in two jobs
  (planet C1 and the room walk at 01:36:44); reruns passed.
