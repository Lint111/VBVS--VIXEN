# Exposure prior art (lane exposure)

Links and short paraphrases only (R542.1): no code or long text is copied. Values marked
"default" were read from the cited page or source file on 2026-10-19.

## 1. Metering

- **Average log luminance (geometric mean).** Reinhard et al. 2002 meter a scene by the log-average
  luminance and scale it so that this average maps to the "key" (see §2). One very dark or very
  bright pixel moves the geometric mean much less than it moves the arithmetic mean, but a large
  dark area still drags it down.
  [Reinhard, Stark, Shirley, Ferwerda 2002, Photographic Tone Reproduction for Digital Images (ACM DOI)](https://doi.org/10.1145/566654.566575)
- **Luminance histogram with percentile rejection.** Unreal Engine's default metering mode builds a
  log2 luminance histogram and averages only the bins between a low and a high percentile. The low
  percentile throws away the darkest pixels (dark background, shadows) and the high percentile
  throws away highlights (sun, emissives, specular).
  [UE: Auto Exposure](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine).
  UE 4.25 moved its histogram defaults from 80 % / 98.3 % to 10 % / 90 %
  ([Epic tech blog, auto exposure in 4.25](https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25)).
  The UE 5 docs suggest starting at 70–80 % low and 80–95 % high when bright spots should get more
  weight.
- **Unity HDRP** offers Average / Spot / Center-weighted / Mask metering and an "Automatic
  Histogram" mode with a percentile range (`histogramPercentages`).
  [HDRP exposure reference](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-override-exposure.html),
  defaults in [HDRP `Exposure.cs`](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/PostProcessing/Components/Exposure.cs).

## 2. Key value, middle grey and compensation

- **Key 0.18.** Reinhard 2002 maps the log-average to a = 0.18 for a normal-key scene. Low-key
  (dark) scenes use smaller keys (0.09, 0.045) and high-key scenes use larger ones (0.36, 0.72).
  0.18 is the photographic 18 % grey card.
- **Calibration constant.** A reflected-light meter uses K = 12.5 (ISO 2720), so
  EV100 = log2(L · 100 / 12.5) for scene luminance L in cd/m². Frostbite uses this with a
  saturation-based exposure 1 / (1.2 · 2^EV100), which places the average near 0.10 instead of 0.18.
  [Lagarde & de Rousiers 2014, Moving Frostbite to PBR, §4 (course notes PDF)](https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf)
- **Compensation** is a plain EV offset after metering. UE 4.27's `CameraExposureSettings` default
  `bias` is 1.0 ([UE Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/CameraExposureSettings?application_version=4.27));
  HDRP's `compensation` default is 0. HDRP also has a target mid-grey setting (`Grey125` default).

## 3. EV100 clamps

Clamping the metered EV keeps the meter from boosting a nearly black frame to white, or from
crushing a frame that is mostly sun.

- HDRP: `limitMin` = −1, `limitMax` = 14 EV100 (defaults in `Exposure.cs` above).
- UE 4.27: `min_brightness` = 0.03, `max_brightness` = 8.0. These are luminance limits, not EV100.
  UE 5 exposes Min/Max EV100 instead and documents no fixed default.
- Film reference: the "sunny 16" rule gives about EV100 15 in direct sun. An indoor scene is a
  single-digit EV100 and moonlight is negative.
  [Exposure value table (Wikipedia)](https://en.wikipedia.org/wiki/Exposure_value)

## 4. Adaptation (eye adaptation)

- **Separate rates.** Both UE and HDRP adapt faster from dark to bright than from bright to dark.
  UE 4.27 defaults are `speed_up` = 3.0 and `speed_down` = 1.0. HDRP defaults are
  `adaptationSpeedDarkToLight` = 3 and `adaptationSpeedLightToDark` = 1, in Progressive mode.
- **Human data.** Krawczyk, Myszkowski & Seidel 2005 model temporal adaptation as an exponential
  approach with time constants of about 0.1 s for cones (photopic) and 0.4 s for rods (scotopic).
  Real dark adaptation takes up to tens of minutes, and the paper deliberately does not simulate it.
  [Krawczyk et al. 2005, Perceptual Effects in Real-time Tone Mapping (PDF)](https://resources.mpi-inf.mpg.de/hdr/peffects/krawczyk05sccg.pdf),
  [ACM DOI](https://dl.acm.org/doi/10.1145/1090122.1090154).
  Pattanaik et al. 2000 give the time course of both light and dark adaptation (seconds versus
  minutes): [Time-Dependent Visual Adaptation for Fast Realistic Image Display (PDF)](http://luthuli.cs.uiuc.edu/~daf/courses/Rendering/Papers3/sig00.pdf).
- **Frame-rate independence.** An exponential approach, adapted += (target − adapted) ·
  (1 − e^(−rate·dt)), gives the same result for any split of the elapsed time when the target is
  constant. This is the form Krawczyk's time constants assume.

## 5. Physically based camera exposure

EV100 = log2(N² / t) − log2(S / 100) for aperture N, shutter time t (s) and ISO S. The exposure
multiplier is 1 / (1.2 · 2^EV100) (Frostbite §4, linked above). UE's "Apply Physical Camera
Exposure" uses the same aperture/shutter/ISO model. With it disabled, UE assumes ISO 100, f/1.0
and 1 s.

## 6. Recommended defaults and where each comes from

The engine values are in §7 of `reports/exposure.md`. VIXEN's radiance is not calibrated in cd/m²,
so the EV clamps there are measured in VIXEN scene units. That report explains the mapping.

| Parameter | Recommended | Source |
|---|---|---|
| Metering | log2 luminance histogram, percentile-window average | UE Auto Exposure (histogram is the default mode); HDRP Automatic Histogram |
| Low / high percentile (general) | 10 % / 90 % (UE 4.27 default) to 40 % / 90 % (HDRP default) | UE Python API 4.27; HDRP `Exposure.cs` |
| Low / high percentile (bright subjects on dark backgrounds) | 70–80 % / 80–95 % | UE 5 Auto Exposure docs, starting ranges |
| Key (middle grey) | 0.18 | Reinhard 2002 |
| Low-key / high-key keys | 0.09 / 0.36 | Reinhard 2002 |
| Meter calibration K | 12.5 | ISO 2720 via Frostbite course notes §4 |
| Compensation | 0 EV (HDRP) / +1 EV (UE 4.27 bias) | HDRP `Exposure.cs`; UE Python API 4.27 |
| EV100 clamp, physical units | −1 … 14 | HDRP `Exposure.cs` |
| Adaptation, dark → bright | 3 /s | UE 4.27 `speed_up`; HDRP `adaptationSpeedDarkToLight` |
| Adaptation, bright → dark | 1 /s | UE 4.27 `speed_down`; HDRP `adaptationSpeedLightToDark` |
| Perceptual bound on rates | cones ≈ 10 /s (τ 0.1 s), rods ≈ 2.5 /s (τ 0.4 s) | Krawczyk et al. 2005 |
