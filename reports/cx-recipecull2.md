# cx-recipecull2 report

## STATUS

Implementation is committed through `08d44aa6` on top of the required P845 merge (`bdf5a848`). Procedural recipe chunk BLAS/TLAS culling, transformed TLAS instances, coarse fallback, software interval parity, shared-kernel clamping, and benchmark metrics are implemented. The native 12-case matrix is green. The required 500px flag-off golden check remains red for two of four angles; see STOPs.

## MEASURED

All native captures used Windows Vulkan, `UNDERSET_NATIVE_BENCHMARK=ON`, 64 frames per angle, fresh-process repeat checks, and explicit `VIXEN_RT_LIGHTING=auto` / composed traversal.

Recipe BLAS metrics from the native log:

| recipe | chunks | AABB bytes | BLAS bytes |
|---:|---:|---:|---:|
| 100 | 1620 | 38,880 | 264,028 |
| 101 | 908 | 21,792 | 148,156 |
| 102 | 1344 | 32,256 | 219,020 |
| 103 | 884 | 21,216 | 144,220 |
| 104 | 1024 | 24,576 | 166,924 |
| 105 | 964 | 23,136 | 157,212 |
| 106 | 864 | 20,736 | 140,940 |
| 107 | 784 | 18,816 | 127,948 |
| 108 | 845 | 20,280 | 137,856 |
| 109 | 760 | 18,240 | 124,012 |
| 110 | 800 | 19,200 | 130,572 |
| 111 | 624 | 14,976 | 101,964 |
| 112 | 1296 | 31,104 | 211,276 |
| scene | 12,717 | 305,208 | 2,074,128 |

GPU p50/p95 milliseconds are `esvo_traverse_shade / shadow_visibility_wave`:

| scene | size | off | on |
|---|---:|---:|---:|
| State | 500 | 2.968 / 0.751, 3.753 / 0.941 | 3.847 / 2.874, 4.663 / 3.344 |
| State | 720 | 6.527 / 1.677, 8.588 / 2.317 | 7.675 / 5.321, 10.917 / 7.431 |
| State | 1080 | 13.738 / 3.250, 16.908 / 4.389 | 14.104 / 9.753, 16.233 / 11.069 |
| Presentation | 500 | 3.433 / 0.874, 3.793 / 1.128 | 4.246 / 3.136, 7.505 / 4.918 |
| Presentation | 720 | 9.021 / 1.895, 12.908 / 2.691 | 7.689 / 5.000, 12.318 / 8.094 |
| Presentation | 1080 | 16.553 / 3.321, 22.416 / 5.009 | 15.054 / 10.400, 20.472 / 12.111 |

Shader ledger on State 500 flag-on: `rays=377275`, `recipes=670115`, `chunks=3340343`, `recipesPerRay=1.7762`, `chunksPerRay=8.85387`, `stepsPerRay=16.344`. Flag-off ledger is zero as intended.

Queued verification: final WSL `VixenApp` build passed; merged SDI check passed; targeted `RecipeChunkBounds.*` test passed 2/2; native benchmark build passed; all 12 native matrix CTests passed with repeat images byte-identical within each case.

## IMAGES

The final State and Presentation 500px contact sheets were inspected side by side. Flag-on preserves composition and object silhouettes but changes the procedural center panel from red to saturated magenta/pink and changes the ground/shadow response to darker brown/black; the difference panels cover the rendered subject rather than showing a geometric displacement.

- [State 500 off/on/diff contact sheet](../.tmp/cx-recipecull2-final-state500-on-off.png)
- [Presentation 500 off/on/diff contact sheet](../.tmp/cx-recipecull2-final-presentation500-on-off.png)
- [canonical off-vs-golden contact sheet](../.tmp/cx-recipecull2-final-vs-golden.png)

## LANDABLE NOW

- `46617ec4`: procedural chunk registration, hardware/software culling path, and metrics plumbing.
- `bdf5a848`: required `origin/wave/authoring-convergence` merge.
- `fd3ccd65`: regenerated SDI headers and preserved chunk registration after the merge.
- `a0b83448`: benchmark keyword gating and contiguous recipe-range test.
- `be0964b0`, `08d44aa6`: flag-off traversal isolation and the follow-up compile-safe wrapper correction.

Demand-driven residency is intentionally design-only. Keep the coarse parent resident; maintain a per-chunk last-reached frame and a compact hit bitmap, promote requested chunks at a frame boundary, and evict only after N consecutive unreached frames with hysteresis. Never mutate the SSBO or BLAS during a dispatch; rebuild/upload from the next-frame snapshot and keep the parent fallback until the replacement TLAS is published.

## REDS

- The first queued SDI check failed on stale merged headers (`/home/liory/.local/state/undertow/undertow-box-logs/1794192222-light-cx-recipecull2:codegen-check.log`); queued `sdi_merged_regen` recovered it and `fd3ccd65` records the generated result.
- The first normal build failed on a stale `DebugRaySample` size assertion (`/home/liory/.local/state/undertow/undertow-box-logs/1794192393-build-cx-recipecull2:vixenapp-build.log`); the assertion was corrected and the final build passed.
- Native setup required first-run Ninja/ccache provisioning and an explicit native FetchContent cache. Direct benchmark configure attempts without `vcvars64.bat` failed with `stddef.h`; the recovered queued native build passed.
- A capture attempt from the WSL UNC working directory failed before launch (`/home/liory/.local/state/undertow/undertow-box-logs/1794197189-gpu-cx-recipecull2:pixel-golden-after-state500-off.log`); explicit `cd /d C:\vixen-native\recipecull1` recovered it.
- The first shader-isolation revision produced a native shader compile red (`/home/liory/.local/state/undertow/undertow-box-logs/1794197236-gpu-cx-recipecull2:pixel-golden-after-state500-off2.log`); `08d44aa6` restored the proven wrapper form and the matrix is green.

## STOPs

STOPPED: required flag-off State.midday 500px golden is not fully byte-identical. With 8 frames per angle, angles 0 and 1 match exactly; angle 2 differs in 144 pixels (maximum channel delta 130), and angle 3 differs in 57 pixels (maximum channel delta 1). The comparison was run against `/home/liory/codeman-cases/undertow/reports/cl-rtframe1-evidence/native/state-midday/png`; the final flag-off source path is feature-gated and the same signature was observed before the final isolation correction, but a matching clean-wave base witness for this historical golden was unavailable in this lane.

## SPT DISPOSITION

Four consolidation proposals were appended to `.spt-proposals/cx-recipecull2.jsonl`; no separate orchestrator decision is required.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: VIXEN SDI drift gate requires manual merged-header regeneration after source merges
- proposed: VIXEN queued checks do not prewarm shared dependency objects after a merged-tree sync
- proposed: Windows native capture lanes require first-run Ninja and ccache provisioning before the target build
- proposed: Native benchmark configure path needs explicit Windows cache and toolchain setup
