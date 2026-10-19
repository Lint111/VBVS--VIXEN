# Lane exposure: dynamic exposure (eye adaptation)

Branch `lane-exposure` off `origin/wave/authoring-convergence` `e4a18398`. This is a 3-hour
checkpoint, not a final report. All captures ran on Dozen and count for correctness only.
Evidence (logs, captures, contact sheets, CSV):
`/home/liory/codeman-cases/undertow/reports/exposure-evidence/`. Prior art:
[`exposure-priorart.md`](exposure-priorart.md).

## 1. Why the default-on meter whited out (MEASURED)

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

## 2. What changed (commit `4fb2d687`)

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

## 3. Witness (looked at; sheets in `exposure-evidence/sheets/`)

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

## 4. Tests

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

## 5. R619.1: should the exposure math be a kernel callable?

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

## STOPs

1. **Look-dev and planet compensation are calibrated against the old meter.** The look-dev
   presets use −3.25 / −3.25 / −3.5 / −5.1 EV (`LookdevSceneDefinition.h:94-97`), and
   `PlanetSceneDefinition.h:52-93` derives its compensation from old meter readings.
   - The old midday net exposure was about +1.42 EV. This is INFERRED from the fixed midday
     constant 4.66783 minus 3.25.
   - Under the new default, midday nets 0.03 − 3.25 = −3.2 EV, which crushes it.
   - These are owner-tuned look values (R419), so I did not retune them.
   - Options: (a) re-derive each scene's compensation so its net exposure matches the reviewed
     hdrmeter sheets (midday ≈ +1.4 EV); (b) lower the default floor so auto-exposure brightens
     dim VIXEN scenes, and give HUD/editor the `dark` preset; (c) both.
   - Recommendation: (a), plus an owner pick of the floor from a contact sheet.
2. **Default floor value.** 0.5 EV100 in scene units fixes the white-out but makes auto-exposure
   darken-only for every current fixture. The owner should choose this deliberately (see §2).

## SPT DISPOSITION

No SPT id was given for this lane. The work is PARTIAL: the white-out is fixed and the adaptation
is witnessed. Still open: the look-dev/planet compensation (STOP 1), the CPU/GPU suite runs and
the final merge with the wave.

## CONSOLIDATION ISSUES

- With HDR on, the cornell fixture's beam assertions fail
  (`test_headless_cornell_graph.cpp:313`, `:369`): the mining-beam overlay doesn't reach the
  tone-mapped image. INFERRED pre-existing: the fixture is registered with
  `VIXEN_HDR_EXPOSURE=0`, so default-on HDR was never tested with the beam.
- `frame_targets_schema_regen` fails against the undertow pair (`SubstrateKinds.cs:173:13:
  unsupported schema field fluent call 'Rate'`), which blocks `exposure_tonemap_gpu_regen` through
  its dependency. I ran the tone-map codegen command directly.
- The meter field is still named `ev100`, but it holds a log2 exposure multiplier, not EV100.
  Renaming it touches the tone-map kernel source and the look-dev tests.
- The queue's `test` and `light` resources were head-blocked for more than 10 minutes twice
  during this lane.
