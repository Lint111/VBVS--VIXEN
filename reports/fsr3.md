# FSR 3 run 2 (R604, T-4587): selection fallback gap and native 8060S witness

Lane `fsr3` (taken over from Codex lane `fsr3b` by a Claude worker). Branch `lane-fsr3`. Nothing pushed.

## Resume (previous run, reviewed)

- Kept, unchanged: `395e441b`, `89bec032`, `8f82d6d5`. The worktree was clean at takeover and no work was reverted.
- Finished by this run: the native witness only. No source change was needed after review.
- Review notes on the kept work:
  - `UpscalerProviderInitializationError` and `SelectUpscalerProviderForInitialization` are small new abstractions. Flagged, not removed.
  - `Initialize()` lost some debug logging in the `CreateEngineContext()` extraction.
  - Tests cover selection logic only (FSR3 to FSR1 to native, and the profile compile-set gate). There is no end-to-end forced-FFX-failure test because no injection hook exists (proposal filed).

## What the kept commits do

1. A failed FFX context creation throws `UpscalerProviderInitializationError`. `Prepare()` logs a warning with the R554 device-variant suggestion, resets the engine, and recompiles with the next candidate (FSR 1, then native). At most 3 failures.
2. `initialized` eligibility is provisional at build time and only becomes true after the provider's real context exists.
3. The runtime machine profile is resolved from the adapter and intersected with `providerCompileSet` before an optional provider is admitted (`NotInRuntimeProfile`).
4. Tests: `test_upscaler_selection.cpp` (6 tests) and two `test_vulkanresources_basic.cpp` tests.

## Native 8060S witness (MEASURED)

Machine: native Windows Vulkan, `AMD Radeon(TM) 8060S Graphics`, resolved runtime profile `amd-8060s-windows`. MSVC 14.44, Ninja, Release, Vulkan SDK 1.4.350.0. Built in a C: clone (`C:\vixen-native\fsr3`) of the worktree at `8f82d6d5`. Test `LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`, state midday, angle 0, 64 frames, fixed exposure, HUD suppressed. Every run was its own queue job (`--resource gpu`, label containing `native-timing`, no `--exclusive`). A = `--render-scale` with default upscaler (none), B = `--upscaler=fsr3` with `VIXEN_FSR3_QUALIFICATION_CAPTURE=1` and `VIXEN_FSR3_GPU_TIMING=1`. Order A1, B, A2.

CPU frame time is `cpu_frame_time_ms`, median over frames 11-64 (frame 1 is a 36 s cold shader/cache build and is excluded).

| Output / scale | A1 CPU p50 | B CPU p50 | A2 CPU p50 | FSR3 GPU median / max | R494 2.0 ms |
|---|---:|---:|---:|---:|---|
| 500x500 @ 2/3 | 22.54 ms | 48.07 ms | 23.98 ms | 0.281 / 0.522 ms | pass |
| 500x500 @ 1/2 | 17.20 ms | 41.68 ms | 17.07 ms | 0.240 / 0.449 ms | pass |
| 1920x1080 @ 2/3 | 32.36 ms | 125.43 ms | 29.65 ms | 1.169 / 1.997 ms | pass, max within 3 us of the limit |
| 1920x1080 @ 1/2 | 21.57 ms | 139.46 ms | 17.06 ms | 1.030 / 1.383 ms | pass |

- GPU figures are the `[FSR3_GPU_TIMING] summary` lines (64 samples each, timestamps bracketing the FSR 3 command span).
- 1080p @ 2/3 has no headroom on the worst frame: one sample at 1.997 ms. The median is 1.17 ms.
- The 17 ms A figures at 1/2 are at the 60 Hz frame interval, so those A numbers may be vsync-bound (INFERRED, not tested). Read them as an upper bound on native-default cost.
- Whole-frame and per-stage GPU columns in `perf.csv` are 0 on this build too, so only the FSR 3 span has a direct GPU figure.
- Extra run: 1080p @ 2/3 B with GPU timing off, CPU p50 182.8 ms. The timing readback is therefore not the cause of the large B frame time.

### Open finding (STOP-class, not investigated)

Total CPU frame time with FSR 3 is 2-6x the native-default frame (1080p: 125-183 ms vs about 30 ms), while the FSR 3 dispatch itself is about 1.2 ms. That is not a cost of the FSR 3 call. The remaining cost is somewhere else in the FSR 3 graph (reconstruction input/motion/depth/history nodes, the qualification-capture path, or synchronization) and cannot be attributed with the zero GPU stage columns. INFERRED only. Dozen showed the same direction at a smaller ratio (1.4-1.7x). Not redesigning around it. Proposal filed.

### Disocclusion flecks and halos (MEASURED counts, INFERRED visual reading)

24-angle slow pans at 500x500, 4 frames per angle: FSR 3 at 2/3 and 1/2, plus a native-default 2/3 reference.

- Viewing pans 06 and 12 (2/3) and 03 (1/2) at 1:1: silhouettes at roof and tree edges are clean. No bright flecks or halos were visible to me. I did not zoom or crop (no image tooling in this environment), so small flecks could be missed.
- Counting pixels (script in the evidence directory) with a per-channel difference above 60 against the native reference that are also cyan-ish: at 06 and 12 the count is 0 at both scales. Isolated cyan-ish pixels do appear at angles 00-04 and 15-17, at most 10 per frame at 2/3 and 14 at 1/2 (out of 250,000).
- Conclusion: the Dozen flecks near 06/12 do not reproduce natively. A handful of isolated cyan-ish edge pixels remains at other angles. Not artifact-free, but no halos.

### Native Linux

Not run. WSL has no `/dev/dri` or `/dev/kfd`, so the only Linux route to the 8060S is Dozen, which is correctness-only. No native Linux GPU route exists on this machine.

## Verification not repeated

No source changed in this run, so no new build or test of the VIXEN unit tests was run. The native build compiled the committed code with no source edits.

## LANDABLE NOW

Repo VBVS--VIXEN, branch `lane-fsr3`:

- `395e441b` Retry upscaler graph compilation after context failure
- `89bec032` Gate optional upscalers on runtime machine profiles
- `8f82d6d5` Test upscaler initialization fallback and profile gates
- plus the commit carrying this report and `.spt-proposals/fsr3.jsonl` (tip of the branch).

## STOPs

- The large FSR 3 CPU frame time (above) needs attribution before any claim that FSR 3 is a net performance win. Not a design change I can make.
- 1080p @ 2/3 worst sample is 1.997 ms against the 2.0 ms budget. Passing, but no margin.
- No end-to-end forced-failure test, because there is no injection hook.

## SPT DISPOSITION

Proposals filed in `.spt-proposals/fsr3.jsonl` this run (4 new; 18 from the earlier run). See the consolidation list below.

## CONSOLIDATION ISSUES

- proposed: Native Windows VIXEN witness build needed a hand-built C: clone with custom configure/build/capture .bat files
- proposed: Queue stall watchdog kills the last native Windows ninja step as idle
- proposed: Provide a forced provider-initialization-failure hook for end-to-end fallback tests
- proposed: Add per-pass GPU timing to the reconstruction path to explain FSR3 frame cost beyond the dispatch
- earlier run: 18 proposals already in the inbox.
