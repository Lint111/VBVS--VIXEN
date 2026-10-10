# Night-service lighting

## LANDABLE NOW

**Status:** ready to land. Night/service now has warm machine emitters and visible spill from the shared light set. No lighting node, shader mechanism, or new lighting attribute was added.

### Diagnosis

- Before the change, `LookdevSceneDefinition.h` authored one `Lamp` primitive. `Lights()` already put a real point light in the night preset (sodium `#f9b55c`, intensity 6, range 7) alongside the directional key. `test_lookdev_capture.cpp` uploaded that set through `LightingConfigNode::SetLights()`. The lamp was therefore a light source in the shared framework, not an unlit decal.
- The fixture had no window row or roof worklight. The night set included only the flood source, so it had no window or worklight pools. Its existing lamp material emission was 2.0, too weak to make a useful highlight in the capture.
- The baseline night mean was already in range (four-angle average 40.5/255), and all p95 values were below 0.40. The gap was visible highlights and local lighting: p99 was 0.431 in every angle, and three of four views had less than 0.01% qualifying emissive pixels.

### Source change and implementation tips

- Added twelve small warm `Window` primitives around all four cab sides and a roof `Worklight`. The window tint and point source use bible warm `#e9bf6b`; the roof worklight and floodlight use sodium `#f9b55c`.
- The night preset now uses three point sources in the same `LightingConfig` set as its directional key: window row (intensity 3.5, range 8), roof worklight (5, range 8), and side floodlight (8, range 8). The set stays within the existing four-light capacity. Night uses the existing `celSpillPurposeScales`; no new property or renderer path was needed.
- Each night emitter recipe is expressed relative to its point-source origin, then placed through the existing instance transform. Keeping that source/instance origin aligned lets the renderer's existing point-source shadow handling recognize the emitter correctly.
- The night emitter geometry uses the existing `recipeParams[3]` emission channel at 0.25. Night exposure is `-5.10 EV` to keep broad lit surfaces below the p95 cap while retaining the warm highlights. Day, late-afternoon, and overcast settings and emitter recipes remain unchanged.

### Night measurements

Metrics use normalized Rec.709 luma over the 500×500 8-bit PNG (`(.2126R + .7152G + .0722B) / 255`), nearest-rank percentiles, and emissive area where HSV value `> 0.85` and saturation `> 0.45`.

| Angle | Before mean / p90 / p95 / p99 | Before emissive | After mean / p90 / p95 / p99 | After emissive |
|---:|---|---:|---|---:|
| 0 | .175 / .287 / .357 / .431 | 0.000% | .169 / .301 / .375 / .720 | 0.355% |
| 1 | .146 / .177 / .262 / .431 | 0.054% | .141 / .203 / .231 / .723 | 0.340% |
| 2 | .153 / .177 / .271 / .431 | 0.002% | .152 / .223 / .301 / .719 | 0.257% |
| 3 | .162 / .271 / .296 / .431 | 0.000% | .155 / .235 / .313 / .719 | 0.300% |

All gates pass: maximum night p95 is **0.375** (≤0.40), minimum p99 is **0.719** (≥0.65), and emissive area is **0.257–0.355%** (within 0.04–1.1%). The four-angle mean averages **39.4/255** after the change.

### Pool measurement

In angle 0, a 9×9 median ROI on the water near the side floodlight (screen center `223,407`, about 4.5 world units from the source) measures **0.418** luma. A farther 9×9 ROI on the same water surface (`127,447`, about 6.1 units away) measures **0.262**. Before the change those ROIs measured **0.311** and **0.262**. The lit patch rises while the farther water remains at its prior level.

### Night sheets and capture witnesses

- [Before night captures beside the three bible references](nightservice-visual/night-service-before-contact-sheet.png)
- [After night captures beside the same references](nightservice-visual/night-service-after-contact-sheet.png)
- Reference images: `r2-04-bimodal-night.png`, `r2-12-bimodal-night-local-pools.png`, and `r2-08-split-night.png` from the visual-bible lighting iteration directory.
- On the final source tree, state-scoped CTest runs passed through the global queue's exclusive GPU resource: night **450.52s**, midday **406.10s**, late-afternoon **405.93s**, and overcast **406.19s**. Each run captured four angles twice and passed the byte-repeat check. All 16 first/repeat pairs also hash-match the saved after PNGs.
- The twelve non-night after captures match their saved before PNGs byte-for-byte. The night captures are the intended visual delta.
- Before using exclusive GPU admission, two final night reruns using the general test resource were killed by the idle-stall guard at **456s** (after seven frames) and **213s** (after three frames), while other worktrees ran VIXEN rendering jobs. The supported `--resource gpu --exclusive` queue path recovered the final witness; see the consolidation proposal below.
- The initial full-matrix CTest invocation timed out at **1200.11 seconds** before implementation. Recovery used the supported `VIXEN_LOOKDEV_CAPTURE_STATE` selector; the untouched night baseline passed, then all four final state-scoped runs passed. This was a matrix-duration timeout, not a capture failure.

### Build and engine witnesses

- Fresh queued configure and full build passed before semantic edits. The final source rebuild passed, including the look-dev capture executable. All 22 generated-config targets' `CodegenTool --check` commands passed; the later cleanup only derived the night gate from the existing flood-intensity field and did not touch codegen inputs.
- RenderGraph CTest: **1,344/1,344 passed**, 11 registered skips.
- SVO CTest: **770 passed, 1 failed**, 8 skipped, 1 disabled. The only failure was `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`, diagnostic `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. This is the documented T-1449 opcode-94 baseline finding in `reports/lookdev.md`, outside this fixture-only change.
- The generic queued `cmake --build build` no-op command failed with `could not load cache` because this preset places its cache in `build/wsl`. The corrected queued `cmake --build build/wsl` passed and reported `ninja: no work to do`.

### STOPs

None for the lighting change. The SVO opcode-94 failure is the known T-1449 baseline result; the aggregate capture timeout was recovered with four passing state-scoped witnesses.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Partition the look-dev capture matrix to fit its CTest timeout
- proposed: Add a reusable dependency-free PNG metric tool for visual witnesses
- proposed: Select the worktree FetchContent cache without an explicit environment override
- proposed: Use the VIXEN preset binary directory for queued rebuild witnesses
- proposed: Route VIXEN GPU capture CTests through exclusive queue admission
