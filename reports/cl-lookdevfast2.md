# cl-lookdevfast2: look-dev captures part 2 (R812.15, R812.16)

Branch `lane-lookdevfast2` (worktree `VBVS--VIXEN/.claude-worktrees/lookdevfast2`), from `lane-lookdevfast1` (c63f3729). Commits: `bcc1e930` (derived manifest), `a8935424` (tile sizing). Nothing pushed.
Evidence: `/home/liory/codeman-cases/undertow/reports/cl-lookdevfast2-evidence/`. All timings are Dozen (dzn) and correctness-lane only; they are not native performance numbers.

## SUMMARY

- **The "about 1.2 s per frame between submissions" is not a steady-state cost.** Measured on a quiet GPU, one `State.midday` process:
  - Frames 5 to 34 take 1.7 to 1.9 s of wall against 1.6 to 1.8 s of GPU (about 0.1 s of overhead).
  - Frames 1 to 4 take 2.9, 2.8, 2.6 and 2.7 s. That is the first use of each frame-in-flight's 81 tile command buffers, not submit or fence round trips.
  - The part 1 figure of 3.0 s per frame came from 4-frame runs (all four frames cold) and from a box at load average 50.
- **Host submit and fence are not the cost.** `submit` is 0.8 ms mean, and the host sits in `submit-retire-oldest-timeline` waiting for the GPU (20 ms per wait, 1.9 s per frame).
- **What is real is the tiling latency on the GPU.** Each of 81 tiles pays about 5.4 ms that does not shrink with tile size: the sum of tile times is 1.70 s against 1.27 s for one untiled dispatch.
- **Fix landed (renderer, applies to the game too):** tile size is now derived from measured per-command launch latency. On `State.midday` frame wall is 61.4 s to 52.3 s for 32 frames (-15%); on `Presentation.midday` it is 226.2 s to 204.5 s for 128 frames (-9.6%). PNG SHA-256 are identical for all 4 angles in both.
- **Affected-test selection is now derived, and the result is uncomfortable.**
  - What a capture process actually opens is nearly the same for every family, so for 394 of 398 source files the generated closure is "all 7 families".
  - Hand-declared narrowing (for example `ExposureMeter.comp` affects only exposure and tonemap) was wrong: every capture process opens it.
  - Only 4 sources narrow: a font asset, the capture runner script, a measurement tool and the capture root wrapper.
  - Opening is not executing, so a shader that is loaded but not dispatched is over-selected. The next step is an execution closure (below).
- **GPU wall:** the full `-L gpu` tree is not built here (only `vixen_lookdev_capture`), so it is not measured. The look-dev subset (73 tests, 4178 s serial COST) ran on 2 queue shards: before 3899 s, after 3595 s makespan (-7.8%). Most of that is the tile fix. The cost-weighted planner is worth about 1% at 2 shards.

## FRAME OVERHEAD

One fresh process, `vixen_lookdev_capture`, all four angles, default frames per angle (State 8, Presentation 32), run as its own queue GPU job. "Before" for `State.midday` is the unmodified binary. "Before" for `Presentation.midday` is the new binary with `VIXEN_GPUYIELD_EDGE=7`, the old cold-floor edge (81 submissions); the `State` before run confirms the unpinned binary also used 81 chunks per frame.

| Process | Frames | Frame wall total | Frame p50 | Mean GPU per frame (sum of tiles) | Mean tiles per frame | Process total | PNG SHA-256 |
|---|---|---|---|---|---|---|---|
| State.midday before | 32 | 61.4 s | 1809 ms | 1707 ms | 81.0 | 81.1 s | 4 angles |
| State.midday after | 32 | 52.3 s | 1574 ms | 1477 ms | 51.4 | 63.6 s | identical to before |
| Presentation.midday before (edge 7) | 128 | 226.2 s | 1720 ms | 1634 ms | 81.0 | 243.0 s | 4 angles |
| Presentation.midday after | 128 | 204.5 s | 1534 ms | 1472 ms | 59.9 | 216.8 s | identical to before |

Frame counts were not touched. Process total includes about 7 s startup and some teardown, and varied more between runs than frame time (box load), so treat the frame columns as the comparison.

Where the wall time goes (`full-base/run.log`, `phase*/`, `trace/`):

| Item | Measured |
|---|---|
| Steady frame (frames 5 to 34) | wall 1.7 to 1.9 s; GPU 1.6 to 1.8 s |
| Cold frames 1 to 4 | 2.9, 2.8, 2.6, 2.7 s |
| `tile-record` (recording 81 command buffers) | p50 590 ms per frame while cold; per-command host cost decays from 7 ms to 0.2 ms by about frame 12 (about 16 ms per frame warm) |
| `vkQueueSubmit2` | 0.8 ms mean, 587 submits |
| `submit-retire-oldest-timeline` | 494 waits, mean 23 ms, 11.5 s over 6 frames: the host waiting for the GPU |
| GPU gap between consecutive tiles (timestamps) | about 0.5 ms |
| Per-tile GPU latency, fitted from two scales | about 5 ms (4.9 to 5.0 ms measured by the node; 5.4 ms from the edge experiment) |

Tile-edge experiment (part 1's `edge/run.sh`, midday angle 0, 4 frames, all cold; `edge/run.log`): PNG SHA-256 `a72fc017eea2cd59a5ba` is identical for every edge.

| Edge | Submissions | GPU total | Frame p50 |
|---|---|---|---|
| default (7) | 81 | 1685 to 1727 ms | 2441 to 2734 ms |
| 32 | 4 | 1384 ms | 1613 ms |
| 16 | 16 | 1370 ms | 1670 ms |
| 0 (untiled) | 1 | 1268 ms | 1438 ms |

**Fix (`a8935424`):** `GpuChunkCost` gains `ObserveScale`, `LaunchLatencyNs` and `SubmissionBudgetNs`. `ComputeDispatchNode` feeds it the mean cost of its full tiles per frame.
- When the budget is unattainable at the cold launch and no latency is known, it probes one scale up (edge 7 to 14).
- With two scales at least 2x apart it fits cost = latency + slope x workgroups.
- The submission budget is raised to 16x the latency, capped at 200 ms, and never lowered. A command's own cost then stays above about 16x its latency.
- In the 16-frame run it converges to 36 tiles per frame and stays there; max submission is about 50 to 60 ms.
- Devices where latency is far below the budget keep their budget and edge; unit test covers a 30 us native case.
- The `[GPUYIELD]` line now also prints `launch_latency_ms`.

Tests: `GpuChunkCost.*` 9 of 9 pass (one new, one expectation updated: over-budget with no latency data now probes edge 14 instead of staying at 7). Run through the queue as a `light` job.

What I tried and dropped: sizing from the host recording cost per command. That cost is a first-use artifact (7 ms cold, 0.2 ms warm), so the budget oscillated and the tile count flipped between 81 and 22. The GPU latency is a property of the device and is stable.

**Not changed, and why:**
- The 4 cold frames (about 4 s of 60 to 80 s per process): the cost is the driver's first use of each tile command buffer on dzn. I did not find a safe way to pre-warm; fewer tiles also cuts it (fewer buffers).
- Drain frames (`exitAfterFrames = frames + 2`) are unchanged, as in part 1.

Caveats: tile boundaries now vary run to run during warmup (frames 1 to 4 use 81 tiles, then 25 or 36). Pixels are identical for this shader, because each pixel is independent of its tile (8 of 8 State and Presentation PNG pairs match, plus every edge in the experiment). A shader whose output depended on tile boundaries would not be safe; `canTile_` already gates on workgroup builtins.

## AFFECTED SELECTION

Implemented as build graph steps with declared inputs and outputs (R812.15). The old hand-written `lookdev-affected-by.tsv` is deleted.

Pieces (VIXEN `tools/closure/`, `application/main/tests/`):
- `open_recorder.cpp`: an `LD_PRELOAD` library that appends every path the process opens (`open`, `openat`, `fopen` families) to `$VIXEN_CLOSURE_OUT/<pid>.paths`. It follows child processes, so the cmake runner wrappers are covered.
- `record_lookdev_closure.cmake`: runs one family's ctest cases with the recorder preloaded and `VIXEN_LOOKDEV_CAPTURE_FRAMES=1`. The ctest result is not the output (frames=1 breaks pixel assertions); the output is the set of recordings.
- CMake targets: `lookdev_closure_<family>` (one per family, each `OUTPUT` is a stamp, `DEPENDS` the capture exe and the recorder) and `lookdev_affected_manifest` (`DEPENDS` all stamps and `closure_manifest`). Order and parallelism come from those edges.
- `closure_manifest merge`: unions each family's opened files, keeps git-tracked files in the repository, maps build-tree files through `ninja -t inputs/deps`, adds the capture executable's own build inputs to every family (it is shared and statically links the renderer), and writes `build/wsl/generated/lookdev-affected-by.tsv` plus `closure-detail.tsv` with content hashes.
- `closure_manifest select --manifest <tsv> <changed paths>` prints the families to run. It is C++, not a script (R812).
- `Application.LookdevClosure.Manifest`: 3 unit tests (select, outside every closure, serialize round trip); passed.

Generated from a recording of 7 families (`closure/`): 398 sources.

| Families per source | Sources |
|---|---|
| all 7 | 394 |
| 5 | 1 (`VIXEN/cmake/run_with_capture_root.cmake`) |
| 2 | 1 (`run_lookdev_capture.cmake`: states, presentation) |
| 1 | 2 (`NotoSansArabic-VF.ttf` and `tools/measure-cold-pipeline.py`: glyphsdf) |

Comparison with the hand-written file (`closure/lookdev-affected-by.generated.tsv`):

| Hand-written row | Hand | Generated |
|---|---|---|
| `libraries/Text/` | glyphsdf | 11 files, all 7 (statically linked into the shared exe; this tool cannot tell they are only used by glyph captures) |
| `shaders/Exposure*.comp`, `ExposureHistogram.glsl` | exposure, tonemap | all 7 (opened by every capture process) |
| `shaders/Reconstruction*`, `FsrInputAA.comp` | presentation, reconstruction | all 7 |
| `shaders/Shadow*`, `ShadowConfigNode.cpp` | states, presentation, sdf-bake | all 7 |
| `BodyOctreeSceneNode.cpp`, `RecipeGlyphBindings.glsl` | glyphsdf (and sdf-bake) | all 7 |
| `SdfBake.h`, `libraries/SVO/include/Recipe/generated/GlyphOutline*`, `ExposureMeterNode.h`, `Data/Reconstruction*` | various | not in any closure (headers; only their compiled effect matters, and the exe-input list holds the `.cpp`s) |
| `*` rows (`BodyInstanceRayMarch.comp`, `DirectLighting.comp`, `ESVO*`, `BuildRenderGraph.cpp`) | all | all 7 (agrees) |

So the generated mapping **agrees** on the shared-renderer rows and **corrects** the narrow rows: the hand-written file under-selected. A change to `ExposureMeter.comp` would have skipped presentation, states, glyph and sdf captures that load the same shader. It is a correction toward safety, at the price of almost no narrowing.

**Limits, stated plainly:**
- "Opened" is coarser than "dispatched". Every capture builds the whole graph, so it opens shaders for passes it never dispatches. To narrow further, record the execution closure: SPIR-V hash of each pipeline that is actually dispatched, via the node-level dispatch record or the existing `tools/gpu-witness-layer`, and map hash to source with the graph. I did not build that.
- C++ granularity is the executable. Without per-function coverage a change to any linked source selects all 7. A coverage build (`-fprofile-arcs`) per family would narrow it.
- The recording needs GPU runs: about 8 to 30 min per family on Dozen with 1-frame captures (reconstruction 221 s, sdf-bake 712 s, presentation 394 s, exposure 577 s, states 832 s, tonemap 1045 s, glyphsdf 1267 s). `closure/*.log` has each.
- The manifest is a build product, not committed. It is stale when the capture exe or recorder change (the stamps re-run the recordings).

## GPU WALL

The full tree (1378 tests) is not built here, so `-L gpu` as a whole is **not measured**. Measured on the look-dev subset `-L '^gpu$' -L '^lookdev-' -LE '^(native|nightly)$'`: 73 tests, serial COST 4178 s, 2 shards (the queue's shared device accepts 2 correctness GPU jobs at once; `--status` showed `gpu-device: sharing=on shared-holders=0/3 derived-capacity=3`, and the wait was CPU pressure, not the GPU). Each shard is one queue GPU job. Scripts and logs: `gpuwall/`.

| Scheme | Shard 0 | Shard 1 | Makespan |
|---|---|---|---|
| Before: round-robin planner (`0f37c418~1`) + old edge-7 tiling (`VIXEN_GPUYIELD_EDGE=7`), planned COST 2106 / 2072 | 3899 s | 3693 s | **3899 s** |
| After: cost-weighted planner + latency-derived tiles, planned COST 2089 / 2089 | 3413 s | 3595 s | **3595 s** |

- Gain -7.8% on makespan. Per-test it is larger where tiles dominate (State.night-service 267 s to 204 s), but both runs shared the box with other lanes and the second shard saw a device loss in the after run (below), so this is one sample, not a distribution.
- **At 2 shards the planner itself is worth about 1%** (planned 2106 vs 2089). Part 1's 2957 vs 1486 s was for 6 shards, and the queue cannot run 6 correctness GPU shards at once. The gain claimed for the planner holds only if capacity widens; the measured gain here is the tile fix.
- Cost-weighted placement puts the two heaviest tests (`Presentation.night-service`, `Presentation.late-afternoon`) in the first slot of both shards, so they run together. Under the contention that is 953 s and 776 s. The round-robin placement did the same (965 s and 1031 s), so this is a shared property of 2-way sharding, not new.
- Native duplicates (`native` label, 776 s) and the planet aggregate are excluded by the `-LE`.

## LANDABLE NOW

On `lane-lookdevfast2` (VIXEN only, two commits, plus this report):
1. `bcc1e930` derived manifest: `tools/closure/*`, `record_lookdev_closure.cmake`, the CMake targets and `Application.LookdevClosure.Manifest`; deletes `lookdev-affected-by.tsv`. Safe: no capture command, environment, frame count or assertion changed.
2. `a8935424` latency-derived tile sizing in `GpuChunkCost` and `ComputeDispatchNode` (`TraceRaysNode` shares `GpuChunkCost` but never observes a scale, so its edge is unchanged). Pixel-identical on both captures run; `GpuChunkCost.*` green. Needs a native Windows run before it is claimed for the game: expect no change there (latency far below the 2 ms budget). The check is the `[GPUYIELD]` line: `budget_ms` stays 2.0 and `launch_latency_ms` is small.

Ask of the controller: land (2) only with a native `[GPUYIELD]` read, or land it as-is since it cannot move a device whose latency is below budget/16 (about 125 us).

## REDS

- Mine, measured here, none from the change itself:
  - `Presentation.night-service` exceeded the 900 s suite budget under 2-way sharding in both schemes (953 s after, 1031 s before). `Presentation.midday` also exceeded it in the before scheme (966 s). Same finding as part 1: parallel shards need a width-aware budget. I did not touch the budget.
  - `GlyphSdf.RtlProof.Pair` failed in the after run (shard 1, 204 s) with `VK_ERROR_DEVICE_LOST` on the Dozen device (3 DEVICE_LOST lines, `RecoverFromDeviceLoss` refused in-process recreation); the queue logged "GPU device loss while sharing ... retrying once as exclusive". It passed in the before run (228 s). Not rerun by me; I cannot tell whether the tile change contributes (shared device loss events happen in this box's history: `gpu-share-stats device-loss-events=10`).
- Pre-existing and seen in both schemes: `SaturationChannel.system`, `SaturationChannel.reference`, `GlyphSdf.latin-carved.ColdBound` (all in part 1's list).
- Closure recordings ran 1-frame captures, so `ctest` exit codes 8 for tonemap and glyphsdf there are not results.

## STOPs

1. **I ran about 25 minutes of GPU captures unqueued.** `./build-wsl.sh --target lookdev_affected_manifest` (a direct build, which is allowed) found the per-family stamps stale (I had rebuilt `vixen_lookdev_capture` after the queued recordings) and re-ran all 7 recordings, 4 at a time, in the build. The recordings completed and produced the manifest above, but it violates the rule that every GPU capture goes through the queue. The numbers in this report that came from queue jobs are unaffected; those 7 recordings were not timed jobs. Saved a memory note, and the fix to make is in item 3.
2. `velocity-1` merge from part 1 is still not done on this branch (kernel pin conflict in `VIXEN/codegen/CMakeLists.txt` per part 1); I did not retry it.
3. **Decisions for the owner / controller:**
   - (a) Make the stamps not depend on the exe rebuild order, or run `lookdev_affected_manifest` only through a queue GPU job. Right now a plain target build can start GPU work.
   - (b) Execution closure (dispatched SPIR-V hashes) to get real narrowing; without it the manifest says "run everything" for almost every change.
   - (c) Runtime budgets for parallel shards (900 s per suite, 900 s guard idle).
   - (d) Whether the 4 cold frames per process matter enough to chase on dzn (about 4 s of 60 to 80 s).

## CONSOLIDATION ISSUES

- Action: queue-wide planning of look-dev shards. Failure: two longest tests run at the same time on 2 shards and miss the 900 s budget. Gate that should exist: a planner that staggers the heaviest tests across shards (start-time aware) or budgets derived from `COST` and shard width.
- Selection of captures from a diff needs the execution closure (above); the opened-file closure is not enough.
- No SPT proposals filed.

## BENCH REQUEST

Native Windows (ws), where timing counts: run `edge/run.sh` and the first `[GPUYIELD]` lines of one `State.midday` process with and without this commit. Expect `budget_ms=2.0` unchanged and `launch_latency_ms` small; any difference in tile count there is the thing to look at.
