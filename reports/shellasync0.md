# `shellasync0` — R504 S0 report

Base: `a31fd0774d2bafcb88be358bde8f1d880f6399bb` (`origin/wave/authoring-convergence`). Before the final witnesses, `origin/wave/authoring-convergence` was fetched and merged; it was already up to date.

## LANDABLE NOW

### S0 implementation

- `DeriveShellCache`, editor registry baking, mip generation, and normal baking now run through the shared `TaskExecutor::RunAsync` producer, sized from the existing `FrameCompute` worker profile. No new task lane was added. `Execute` polls the completion signal and publishes at its existing safe point.
- Each generation carries its own source pool, shell pool, compact config, readiness mask, and brick-residency state. Until the first publish, the Stored-SDF config remains mip-only. Replaced GPU buffers follow the existing retire/reclaim ledger; only the published and one retiring GPU generation can be live. The parity-indexed shell buffers are removed, so later frames cannot select stale alternate-slot bytes and the producer never host-writes buffers still owned by an in-flight frame.
- The editor now stages a registry-bake producer instead of baking on the edit path. `[ShellDerive]` records source bricks, shell bricks, output bytes, derive milliseconds, and worker count. `[Rematerialize]` reports the same generation data and publication milliseconds at the safe point.
- `NormalBakeIsByteIdenticalAtWorkerCountsOneTwoAndDefault` now checks the async path. `EditPublishesFreshGenerationAcrossFourFrames` replaces the two-frame case. `RematerializeEditLoop` renders eight consecutive post-flip frames and compares the defined hit-record bytes.
- Commits: `03562f31` (native build portability repairs) and `3a11cf1c` (async generation implementation and tests). The first repairs MSVC `min`/`max` macro expansion and a GLSL reserved identifier discovered while establishing the native witness.

### Verification

- The baseline full WSL build at the recorded base passed all 22 codegen golden checks; the 22 current check stamps remain present. No codegen/schema inputs changed in this lane. After the final readiness-mask correction, queued `cmake --build build/wsl` passed and rebuilt the changed RenderGraph object and dependents. The successful log is `/home/liory/.local/state/undertow/undertow-box-logs/1791701679-build-async-readiness-config-build-path.log`.
- Focused Lavapipe witnesses passed: `ShellDerive.NormalBakeIsByteIdenticalAtWorkerCountsOneTwoAndDefault`, `ShellRevalidateNodeTest.EditPublishesFreshGenerationAcrossFourFrames`, `BodyOctreeLifetimeTest.ShellCachePopulatedThroughNodeCompile`, and `BodyInstanceRayMarchRenderTest.RematerializeEditLoop`. The edit render measured A at 176 px / 24,225 hits and B at 232 px / 30,394 hits; all eight post-publication byte comparisons passed. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791701720-gpu-async-focused-lvp-fixed.log`.
- Dozen could not run the image test: its Vulkan 1.2 device rejected the generated SPIR-V 1.6 module. The same focused tests passed on Lavapipe; Dozen timing was not used for performance claims.
- Native Windows builds of `vixen_editor` and `vixen_lookdev_capture` both passed through the queue. A native 180-frame editor run used the same fixture and edit script for baseline and after, with `VIXEN_SHELL_NORMALS=1`. On the 22 rows at each scripted edit frame and its following frame (frames 30–131), CPU frame time was:

  | Measure | Baseline | Async |
  | --- | ---: | ---: |
  | p50 | 4.095 ms | 4.136 ms |
  | p95 | 147.000 ms | 10.989 ms |
  | max | 193.237 ms | 12.156 ms |

  This keeps the edit-frame median effectively unchanged while removing the baseline long frame spikes. The after CSV and editor log are `/mnt/c/vixen-native/shellasync0/reports/shellasync0/async-after-perf.csv` and `async-after-editor.log`; the matching baseline is `baseline-shader-fixed-perf.csv`.
- The native editor log emitted five producer records for `bricks=64 shell_bricks=64 bytes=857040`, with `workers=2` and derive times `11.666, 12.458, 12.102, 11.976, 12.173 ms`. Two published generations emitted `[Rematerialize]` records at `4.688 ms` and `4.230 ms`, carrying `derive_ms=11.666` and `12.173 ms` respectively. A source scan finds the derive call only inside the producer task; `Execute` only schedules, polls, and publishes.

### Featureab 375 ms attribution

- The source measurement is [featureab’s 500×500 summary](/home/liory/projects/VBVS--VIXEN/.claude-worktrees/featureab/reports/featureab/tables/500x500/ab-summary.md:44): the `s_shell_normals` maximum is `375.082 ms`.
- In its original per-frame CSV, that value is the `night-service` whole-frame GPU span at frame 90. CPU frame time was `368.191 ms`; `esvo_traverse_shade_ms` was `15.5993 ms`, shadow visibility `0.65588 ms`, and spatial reuse `0.1638 ms`. Neighboring whole-frame spans were `17.0466` and `17.1514 ms`.
- The native rerun of the same `s_shell_normals` look-dev test passed all four states. At night-service frame 90, whole-frame GPU span was `16.6587 ms` and CPU frame time `16.5799 ms`. Each state logged `bricks=816 shell_bricks=0 bytes=0 ms=0.000 workers=2`; this capture has no Stored-SDF shell payload to derive. The evidence therefore does not attribute the old GPU-span outlier to shell derivation. It is an isolated span not reflected in the individual pass timers; the after run did not reproduce it. Separately, the actual Stored-SDF continuous-edit witness measures shell derive at about 12 ms on the producer and 4–5 ms to publish.

### Recovery records

- The baseline full build completed before semantic changes. The mandated `bash tools/check-content-codegen.sh` route was absent in VIXEN (exit 127; log `/home/liory/.local/state/undertow/undertow-box-logs/1791699557-light-async-final-generation-check.log`); the full queued CMake build ran the 22 generation checks instead.
- The queued command `cmake --build build` failed with `Error: could not load cache` (log `/home/liory/.local/state/undertow/undertow-box-logs/1791701650-build-async-readiness-config-build.log`); this lane’s configured cache is `build/wsl`. The recovered queued `cmake --build build/wsl` passed. Both workflow gaps have SPT proposals.
- The first Dozen launch lacked its loader/dependency directories and failed at `vkCreateInstance` (`VK_ERROR_INCOMPATIBLE_DRIVER`); setting the Vulkan loader, Dozen, and VIXEN windowing paths let it reach the tests. The later SPIR-V target mismatch was handled with the passing Lavapipe correctness run.

## STOPs

- **R504.1 power-lane work is not in this assigned branch.** Its adopted scope includes the lighting power lane. The design’s §1.1 places `BakeEmitterPowerLane` / `EmitterPower.h` on `lane-lights0` commits `14a6be11..e6555dcf`; neither API exists on `lane-shellasync0`. I did not import another lane’s worktree. Completing that part requires the lights0 change to be merged into the assigned base, then its derived power payload added to the same producer generation and verified with the dependent config and test.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- Add a VIXEN light gate for generated-code checks
- Add a shared WSL Vulkan runtime environment for GPU tests
- Make the native default build resolve its WSL codegen launcher
- Make VIXEN MSVC builds safe from min and max macros
- Provision the assigned native checkout before dispatch
- Validate the native editor fixture path in the performance runner
- Align the queued VIXEN build command with the configured cache directory
- Make native look-dev capture runner root-configurable for lane checkouts
