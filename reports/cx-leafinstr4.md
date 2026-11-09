# cx-leafinstr4 — leaf-instrumentation pixel classification and measurements

## STATUS

PARTIAL, with the pixel red classified. A clean P845 native build at `3974cb94` and the
instrumented/shader-fixed lane produce byte-identical 32-frame captures at all four angles.
The predecessor's required hashes are therefore stale for angles 1–3; the lane is pixel-neutral
against its own clean base. The 720p and 1080p shader-clock witnesses passed. Brickmap A/B and
BLAS timing remain open; BLAS timing is not present in the current source.

## CLASSIFICATION

The base was prepared in `C:\vixen-native\leafinstr-base`, built with the queued
`vixen-ninja-release` configuration and `vixen_lookdev_capture` target, then captured with
`State.midday`, 500x500, angle 0–3, 32 frames, first four dropped, readback enabled. The lane
capture came from `C:\vixen-native\leafinstr2`, at the predecessor's committed tip including
`127311ba`.

| angle | required 32-frame prefix | clean P845 base | lane | base vs lane |
|---|---|---|---|---|
| 0 | `76ce784672154c37` | `76ce784672154c37` | `76ce784672154c37` | byte-identical |
| 1 | `f2cc7c15c693667c` | `5bf44a336dcc3e58` | `5bf44a336dcc3e58` | byte-identical |
| 2 | `068b2ee553fb47ec` | `d290b51816e83ebf` | `d290b51816e83ebf` | byte-identical |
| 3 | `0bf0f7444c1132db` | `a9e357859030fda5` | `a9e357859030fda5` | byte-identical |

The full SHA-256 comparison reports `pixel_diff=0`, `max_channel_delta=0`, and no differing
bounding box for every angle. This isolates the red from the lane: P845/base convergence already
produces the predecessor's observed 32-frame angle 1–3 hashes.

To reproduce the known P845 fact at the committed-golden capture settings, I also captured the
clean base at 8 frames. Against `reports/lookdev-visual/captures-shadow-on-repeat`, angles 0 and 1
are identical; angle 2 differs by 144 pixels with max delta 130, and angle 3 differs by 57 pixels
with max delta 1. These are the previously observed clean-P845 differences, not instrumentation
output changes.

Images were inspected from these raw-evidence contact sheets and crops:

- `/home/liory/codeman-cases/undertow/reports/cx-leafinstr4-evidence/contact-sheet/base-vs-lane.png`
- `/home/liory/codeman-cases/undertow/reports/cx-leafinstr4-evidence/contact-sheet/base-vs-golden.png`
- `/home/liory/codeman-cases/undertow/reports/cx-leafinstr4-evidence/contact-sheet/base-frames8-vs-golden.png`
- `.../golden-diffs/` and `.../golden-diffs-frames8/`

The 32-frame angle-1/2 diffs are broad full-image lighting/color changes; angle 3 is sparse
low-magnitude speckling. The base/lane contact sheet has no visible or byte-level differences.

## MEASURED

- Clean P845 base build: PASS. The first queued `build build` action failed before compilation
  because the fresh checkout had no `build/ninja-release` directory; the queued `build all`
  recovery configured and built the target successfully. Logs:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794223428-build-cx-leafinstr4:base-build.log`
  and
  `/home/liory/.local/state/undertow/undertow-box-logs/1794225235-build-cx-leafinstr4:base-configure-build.log`.
- Clean-base 32-frame capture: PASS, queue label `cx-leafinstr4:base-capture-midday-500-retry`,
  log `/home/liory/.local/state/undertow/undertow-box-logs/1794227368-gpu-cx-leafinstr4:base-capture-midday-500-retry.log`.
- Clean-base 8-frame capture: PASS, queue label `cx-leafinstr4:base-capture-midday-500-frames8`,
  log `/home/liory/.local/state/undertow/undertow-box-logs/1794228357-gpu-cx-leafinstr4:base-capture-midday-500-frames8.log`.
- Existing 500x500 measurement evidence from `cx-leafinstr2` remains valid for the gated
  instrumentation: counters report 8,500,000 pixels, steps `b5=429216 b6=6157434 b7=1876426
  b8=34884 b9=1768 b10=272`, bodies `b4=8500000`, candidates `b0=8500000`, path classes
  `b0=4963762 b1=3536238`, and brick steps are zero in the non-brickmap scene.
- Existing 720p perf CSVs report the following representative steady-state frame values
  (`esvo_traverse_shade_ms / shadow_visibility_wave_ms / spatial_reuse_ms`, frames 5–7):

  | variant | frame 5 | frame 6 | frame 7 |
  |---|---:|---:|---:|
  | search | 25.6741 / 1.61895 / 1.4664 | 25.9643 / 1.61344 / 1.47168 | 29.3798 / 1.63072 / 1.47776 |
  | march | 19.3137 / 1.59984 / 1.51236 | 18.6129 / 1.56348 / 1.4984 | 18.6756 / 1.5426 / 1.54296 |

  Evidence: `/home/liory/codeman-cases/undertow/reports/cx-leafinstr2/{search-720,march-720}/perf.csv`.
  The old 500x500 search/march/full CSVs are also retained under that report directory.
- Fresh 720p shader-clock capture: PASS, queue label `cx-leafinstr4:clock-720`, evidence
  `/home/liory/codeman-cases/undertow/reports/cx-leafinstr4-evidence/clock-720/`. It reports
  `wave_start_min=1056382392`, `wave_end_max=1280836759`, `wave_ticks=224454367`.
  The shade fields are sentinel values (`shade_start_min=4294967295`, `shade_end_max=0`), so
  `shade_ticks=1` is not a usable measurement: this scene did not write shade clock slots.
- Fresh 1080p shader-clock capture: PASS, queue label `cx-leafinstr4:clock-1080`, evidence
  `/home/liory/codeman-cases/undertow/reports/cx-leafinstr4-evidence/clock-1080/`. It reports
  `wave_start_min=115079151`, `wave_end_max=4213192494`, `wave_ticks=4098113343`.
  The shade fields are the same sentinels, so `shade_ticks=1` is not usable. Admission took
  approximately 49 minutes behind a live GPU/native-build drain; the application then passed in
  8.8 seconds. Queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794232670-gpu-cx-leafinstr4:clock-1080.log`.

No fresh 1080p perf CSV or brickmap A/B result is claimed here. The BLAS build path in
`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp` submits `vkCmdBuildAS`, but the
current lane has no GPU timestamp query around those submissions, so no BLAS timing number was
fabricated.

## LANDABLE NOW

The report and classification are landable now. No product source change was needed: the existing
gated instrumentation and shader-call-site fix are pixel-neutral against a clean P845 base. The
relevant prior commits remain:

- `09f777bd`, `56a0b6f5`, `a4513455` — gated instrumentation/readback slice;
- `99aefdbf` — P845 merge;
- `127311ba` — recipe-bound shadow-trace call-site fix.

## REDS

- The old required four-angle 32-frame gate is stale for angles 1–3. A clean P845 base reproduces
  the lane's observed hashes exactly, so this is not a leaf-instrumentation pixel regression.
- The initial fresh-checkout `build build` invocation was not a valid configured-tree witness;
  `build all` recovered it. This launcher friction is filed below as a consolidation issue.
- The shader-clock shade result is unavailable for this scene because no shade slots were written.
- The 1080p shader-clock job required approximately 49 minutes of GPU admission because of
  unrelated queue contention; it passed once admitted. No quiet-window request was needed because
  admission stayed below the brief's 60-minute stop threshold.
- Brickmap hardware-vs-DDA A/B was not run before the lane checkpoint; no A/B number is claimed.
- The full solution/test suite was not required for this native measurement/report-only lane and
  was not run.

## STOPs

STOP: BLAS build timing remains unmeasured. The current implementation has no GPU timestamp
instrumentation around the existing BLAS submissions. Completing that item requires an authorized
source change behind `UNDERSET_NATIVE_BENCHMARK`, a fresh native build, and a new queued GPU
witness; this lane did not invent a timing value from CPU submission logs.

## SPT DISPOSITION

- Prior `cx-leafinstr2`/`cx-leafinstr3` consolidation proposals remain open and cover the existing
  native provisioning, queue, CMake namespace, and capture-runner gaps.
- This lane added proposal `8b92fe7b4c43`: make the native build action configure fresh VIXEN
  checkouts or give a clear fresh-tree diagnostic.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Make the native build action configure fresh VIXEN checkouts
