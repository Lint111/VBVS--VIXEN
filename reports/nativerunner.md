# Native Windows capture runner witness

Lane: `lane-nativerunner`  
Final capture build: `2e8c008bd4fa54ed5074bb82bdcc9c04f468b146`  
Queue deployment: box-queue `master` at `4a3c638` (controller-reported on ws and laptop)

## Native capture jobs

Both states ran as separate `gpu-native` shared correctness jobs. Neither job used `--exclusive`.

| State | Queue job | CTest result | Frames per process | First/repeat bytes |
|---|---|---:|---:|---|
| `lookdev/midday` | `nativerunner:native-lookdev-midday` | Passed | 32 | Equal (5 files each) |
| `planet/C1` | `nativerunner:native-planet-C1` | Passed | 8 | Equal (5 files each) |

Native first-pass artifacts are under `build/native-captures/2e8c008bd4fa54ed5074bb82bdcc9c04f468b146/runtime-captures/`; repeats are in each state's `.repeat` directory. Look-dev's five files are four angle PNGs and its lighting readback. Planet's five files are the PNG, hits, lighting, meter, and scene outputs. The native CTest logs also report both repeat comparisons as byte-identical.

## Pixel comparison with Dozen

Compared native first-pass RGB pixels with the existing Dozen files in `build/wsl/runtime-captures/`. A pixel counts as different if any RGB channel differs. Bounding boxes are inclusive; max delta is the greatest absolute per-channel difference (0–255).

| State/image | Size | Differing pixels | Difference bounding box | Max channel delta |
|---|---:|---:|---:|---:|
| `lookdev/midday`, angle 0 | 500×500 | 143 | (57, 212)–(472, 480) | 13 |
| `lookdev/midday`, angle 1 | 500×500 | 631 | (0, 134)–(499, 449) | 1 |
| `lookdev/midday`, angle 2 | 500×500 | 33 | (85, 189)–(419, 395) | 1 |
| `lookdev/midday`, angle 3 | 500×500 | 55 | (94, 192)–(405, 490) | 17 |
| `planet/C1` | 1080×1080 | 3,278 | (128, 128)–(951, 951) | 245 |

Look-dev totals 862 differing pixel instances across its four files (counts are summed per image); the union bounding box is (0, 134)–(499, 490), with max channel delta 17. **Native and Dozen do not agree pixel-exactly for either state.**

One pair per state was opened during the witness: `build/wsl/runtime-captures/lookdev/midday/midday-angle-0.png` with `build/native-captures/2e8c008bd4fa54ed5074bb82bdcc9c04f468b146/runtime-captures/lookdev/midday/midday-angle-0.png`, and `build/wsl/runtime-captures/planet/C1.png` with `build/native-captures/2e8c008bd4fa54ed5074bb82bdcc9c04f468b146/runtime-captures/planet/C1.png`.

## Timing

Process wall is the capture application's `process_total_ms`/`wall_ms`; it excludes queue admission. Frame percentiles are host wall-time samples around each successful `VulkanGraphApplication::Render()` call. Native timings are shown for the first and repeat processes.

| State | Frames | Native process wall, first / repeat | Native frame p50, first / repeat | Native frame p95, first / repeat | Existing Dozen process wall |
|---|---:|---:|---:|---:|---:|
| `lookdev/midday` | 32 | 36.245 s / 3.700 s | 17.15 ms / 17.13 ms | 28.71 ms / 27.11 ms | 51.98 s per process |
| `planet/C1` | 8 | 35.684 s / 3.660 s | 69.36 ms / 17.16 ms | 146.10 ms / 77.67 ms | 38.806 s first; 32.990 s repeat |

The supplied Dozen comparison values are 51.98 s for look-dev and 32.99 s for planet/C1. The original Dozen planet test logged process wall but did not emit frame p50/p95, so no Dozen per-frame percentile is claimed for C1. Look-dev's existing Dozen log reported frame p50 1,517.95 ms and p95 1,799.98 ms.

## Golden source

Use each state's native first-pass output as its golden source: the four native look-dev angle PNGs and the native `planet/C1.png`. Keep the existing property assertions and thresholds; the cross-driver diff above is a witness, not a replacement for those assertions. No golden assertions or expected property values changed.

## Recovery and verification

The first `gpu-native` attempts on the requested starting commit `bf2fc4d0aea3e5922433ab7dd24a83eee12634ed` failed before rendering: both capture executables exited with Windows status `0xc0000135` (CTest exit 1). Their import tables included `tbb12.dll`; the DLL was present in the commit-keyed build tree but its compiler-specific directory was absent from the capture process `PATH`.

Commit `122b549a` updated `native-capture.ps1` to locate `tbb12.dll` under the build tree and add only that directory to `PATH`. The queue-mediated native build then succeeded, and both states passed. Commit `2e8c008b` added planet per-frame timing output; the scoped native build and both separate capture jobs passed again on that final capture build. The captured CTest names were `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` and `Application.PlanetCapture.C1`.

## CONSOLIDATION ISSUES

- proposed: Native capture runner omits the staged TBB DLL directory
