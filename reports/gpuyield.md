# gpuyield run 3 — tune bounding and finish R486

## LANDABLE NOW

**PARTIAL — final recovery witnesses pending.** Runtime changes are committed through `23037fa9`; the timestamp-validator correction is `1ece70bd`. This is not certification that all requested gates pass. The verified shell/indirect conversions and native hardware-ray artifact are complete; the latest Dozen recovery and native pacing repair still need their final execution witnesses.

Started 2026-10-10 17:17:15 UTC; checkpoint 20:17:15 UTC. Starting merged baseline `3b2482d95deef62b3fda03cac9282f9b9764ce3b`, wave `4ddea47390880152d35371193e10c7ac722101d9`, pinned kernel `c3fb3847979e3f9eef04783510c42b70d7058ae7`. Only the assigned primary checkout and explicitly authorized `C:\vixen-native\gpuyield` checkout were edited. No push, branch creation or branch switch. [Run 2](gpuyield-run2.md) is preserved.

### Classification and recovery evidence

The historical 1,103.538 ms interval includes dependency waiting and GPU preemption, not an active-execution counter. Run 2's native tiny-dispatch control measured GPU maxima below 0.009 ms while host service exceeded 1,135 ms under contention; exclusive controls reduced host-service max to 1.161 ms. That establishes scheduling/contention as a cause class, with free unknown-cost accounting and an unretired VIXEN backlog independently established. It does not reconstruct the original sample's exact active/waiting split or prove desktop presentation latency. No usable GPUView/PresentMon ETL was obtained.

The historical 22.25% render-loop increase (48.031 to 58.718 seconds / 500 frames) is a **64-tile retired/unretired comparison**, not an unbounded full-frame baseline. Summed dispatch GPU clocks changed from 41.559869 to 41.418589 seconds, while retirement contributed 103.020688 ms/frame and host submission increased from 0.915722 to 2.013455 ms/frame. Inner-to-outer timestamp/barrier difference was only approximately 0.060 ms/frame. [Attribution](gpuyield-run3/historical-overhead-attribution.json).

Fresh native unbounded controls were built from the same merged wave plus the identical portability recovery: detached control `2e7dde20053f8d8329b745f99dd3693061f852ca`. Both 500×500 and 1920×1080 completed 500 frames, exit 0, scale 1.0. There was no matching published native timing baseline, so these controls were built and measured. Binary hashes, process exit status and CSVs are retained. Cold-start compilation and teardown make total process wall time different from steady render time; the per-pass table excludes the first 10 frames.

The first full-suite attempt on this lane returned CTest 8: two capture producers segfaulted, ten dependent capture/R6 tests were not run, and AppFlow/cache gates failed. The sole accepted opcode-94 failure also appeared. The new producer failure is **this lane's regression**, not an assumed environmental or pre-existing red. The per-unit model turned latency into thousands of increasingly tiny commands. The affine recovery still exhibited a near-budget fixed-latency cliff: 441 commands became 3,969, one host frame took 12.99 seconds, then Dozen exited 139. No backtrace is available; do not attribute the segfault to query-pool lifetime as proven fact. `23037fa9` removes the subtraction that triggers subdivision collapse and releases executable query-pool references before storage growth.

Native timing exposed a separate pacing error: requested 16.67 ms deadlines used coarse Windows sleep, contributing 24.601541 ms mean pace time and 31.436691 ms CPU frame time at 500×500. The latest runtime uses a reusable per-thread high-resolution Windows waitable timer with portable fallback. Its execution results are pending below.

### Implemented contracts

- SubmitRecord greedily batches adjacent command buffers under the existing 2 ms submission contract, charges every missing/zero/uncalibrated estimate the full budget, retires prior batches, and preserves first-acquire/final-completion semaphore ownership. Outstanding submitted work remains capped at one batch.
- Compute tiling calibrates from full-tile observations, reserves clipped-command and batch gaps as fixed latency, and grows the tile within the existing extent/budget data. Four timestamp queries per chunk separate shader interval and whole command. Query pools grow by capacity instead of being recreated on every count change.
- Shell dilation uses immutable previous/next layers through the canonical derivation contract. FRONTIER is scratch for the legacy in-place dilation; production consumes surface/shell bits. The new kernel preserves every canonical frontier/surface/shell byte and compacted lookup/source-pool bytes for dilation 0–3. It does not assert that nondeterministic legacy intermediate frontier scratch was byte-identical.
- Indirect dispatch derives bucket-major/tile-major commands from existing bucket and tile data, reflects the shader tile-origin offset, preserves padded group domains and empty buckets, and publishes all recorded command buffers to consumers. Grouped accumulation reads current producers at Execute, rebuilds mappings each execution, preserves reverse-order/second-frame bytes and rejects an invalid current producer. No new registry.
- Hardware rays retain the typed tile-origin/full-launch contract. The native GPU witness executes shipped ray-generation/miss shaders with fixture intersection/hit stages and BLAS/TLAS/SBT, compares the entire output against a full launch, and reports 500-frame timing. It is not a complete production TraceRaysNode graph/hit-shader witness. The separate native full producer confirms the production ray-query TLAS backend is engaged.

### Native overhead — last complete runtime `9e9550f2`

All rows are scale 1.0, 500 frames, mean GPU-clock pass spans after 10 warmup frames. Diagnostic logging was enabled for bounded runs. A later recovery rerun, including logging-disabled measurements, is queued; this table must not be represented as its result.

| Resolution / pass | Unbounded ms | Tuned bounded ms | Change |
|---|---:|---:|---:|
| 500×500 / cpu_frame_time_ms | 4.388762 | 31.436691 | +616.3% |
| 500×500 / whole_frame_gpu_span_ms | 0.945043 | 1.193271 | +26.3% |
| 500×500 / esvo_traverse_shade_ms | 0.337087 | 0.366588 | +8.8% |
| 500×500 / direct_lighting_ms | 0.000280 | 0.000280 | -0.2% |
| 500×500 / spatial_reuse_ms | 0.065224 | 0.078232 | +19.9% |
| 500×500 / probe_gather_ms | 0.000280 | 0.000280 | -0.1% |
| 500×500 / shadow_visibility_wave_ms | 0.052609 | 0.067572 | +28.4% |
| 500×500 / shadow_ray_trace_ms | 0.000280 | 0.000280 | -0.2% |
| 500×500 / probe_apply_ms | 0.000281 | 0.000280 | -0.3% |
| 500×500 / render_target_blit_ms | 0.006421 | 0.008052 | +25.4% |
| 500×500 / ui_composite_render_ms | 0.032687 | 0.039848 | +21.9% |
| 1920×1080 / cpu_frame_time_ms | 5.079638 | 31.404069 | +518.2% |
| 1920×1080 / whole_frame_gpu_span_ms | 3.934913 | 4.540448 | +15.4% |
| 1920×1080 / esvo_traverse_shade_ms | 1.838277 | 2.205455 | +20.0% |
| 1920×1080 / direct_lighting_ms | 0.000280 | 0.000279 | -0.4% |
| 1920×1080 / spatial_reuse_ms | 0.745820 | 0.775138 | +3.9% |
| 1920×1080 / probe_gather_ms | 0.000281 | 0.000280 | -0.6% |
| 1920×1080 / shadow_visibility_wave_ms | 0.843663 | 0.838212 | -0.6% |
| 1920×1080 / shadow_ray_trace_ms | 0.000281 | 0.000280 | -0.3% |
| 1920×1080 / probe_apply_ms | 0.000280 | 0.000280 | -0.3% |
| 1920×1080 / render_target_blit_ms | 0.032601 | 0.033638 | +3.2% |
| 1920×1080 / ui_composite_render_ms | 0.038272 | 0.042713 | +11.6% |

At 500×500, primary steady submissions fell from 64 to 1.012/frame; at 1080p they settled at 4.016/frame. Including cold frames, means are 1.704 and 7.524. Native host retirement/submission fell from 1.502507/1.045525 ms/frame for static 64 tiles to 0.028326/0.080937 ms/frame at 500×500. At 1080p they are 1.869940/0.227745 ms/frame including cold start. Outer-minus-inner mean was 2.048 µs/chunk at 500×500 and 0.796 µs/chunk at 1080p; it includes barriers and surrounding command timing, not a separately measured query-only cost. [Attribution](gpuyield-run3/coarse-pace-overhead-attribution.json).

The tuned primary GPU span improved 87.5% against the native static-tile candidate but remains +8.8% at 500×500 and +20.0% at 1080p against the true unbounded base. Whole-frame GPU span is +26.3%/+15.4%. Near-zero overhead and full-resolution real-time output are therefore not yet claimed.

### Timing by driver — last completed measurements

| Driver / workload | Frames / chunks | Chunk p99 / max ms | Submission p99 / max ms | Primary aggregate max ms | Verdict |
|---|---:|---:|---:|---:|---|
| AMD native / 500×500 | 500 / 864 | 0.369760 / 0.773280 | 0.369760 / 0.773280 | 1.270240 | PASS at 9e9550f2 |
| AMD native / 1920×1080 | 500 / 3789 | 0.808000 / 0.967240 | 0.808000 / 0.967240 | 5.272640 | PASS at 9e9550f2 |
| AMD native / bucketed | 500 / 907 | 0.499480 / 0.788640 | 0.499520 / 0.788640 | 1.657080 | PASS at 9e9550f2 |
| AMD native / hardware query | 500 / 852 | 0.389360 / 0.409480 | 0.389360 / 0.409480 | 1.415400 | PASS at 9e9550f2 |
| AMD native / hardware launch fixture | 500 / 40,500 | 0.003640 / 0.016400 | Separate 81-tile fixture | 0.344840 | 1,000,000 RGBA bytes identical |
| Dozen / run-2 fixed 64 tiles | 500 / 32,000 | ~1.74 / 2.257480 | Unbatched | ~83 ms mean | Passes old 16.67 ms chunk gate; fails new 2 ms maximum |
| Dozen / affine near-budget cliff | incomplete | Retained raw logs | Some chunks exceed 2 ms | Not a 500-frame witness | exit 139; new failure |

Native device is AMD Radeon(TM) 8060S Graphics, driver 8388967. Four inner timestamp pairs had equal, nonzero ticks and a positive enclosing interval. The validator now records these as zero-resolution dispatch pairs instead of missing results, while rejecting backwards/missing queries and nonpositive outer intervals. Vulkan requires non-decreasing timestamp values, not strictly increasing values: [official reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html). Four focused validator regression checks pass. Zero shader-resolution samples are never used as free command cost.

### Required witnesses

| Check | Current evidence |
|---|---|
| Fresh Linux merged build | PASS; final runtime recovery build exit 0 |
| 22 generation checks | PASS; all 22 canonical stamps freshly regenerated/checked; later changes affect only runtime C++/tests and Python metric tool |
| Canonical shell raw/compacted bytes | PASS, dilation 0–3 actual GPU cases; initial descriptor-count fixture failure fixed and rerun |
| Indirect/grouped actual GPU oracle | PASS; all hit bytes, bucket/tile coverage, reverse ordering, late producers, second execution and invalid current-producer rejection |
| Seven generation/typed contract tests + metric fixtures | PASS |
| Conservative accounting / submit / affine costs | 11 PASS at 9e9550f2; latest four cost cases plus three actual-node unknown-cost cases PASS (7/7); five unchanged SubmitRecord cases also passed at 9e9550f2 |
| Native full producer | Eight 500-frame runs PASS at 9e9550f2; latest pacing/query-reference repair awaits native build/rerun |
| Native hardware rays | All four tests PASS; hardware launch byte/timing evidence above |
| Full ShaderManagement | Initial full suite: 57 PASS, 1 disabled |
| Full RenderGraph/SVO | Initial 2,138-test run: 15 failures including capture regressions/dependents and accepted opcode-94 red; recovery queued |
| Merged cache gate / R6 / PNG capture comparison | Required final recovery queued; initial capture producer failures prevent a passing claim |
| No-op rebuild | PASS after runtime recovery: ninja: no work to do |

Native recovery ladder A: moved from the WSL CMake file(LOCK) stall to explicitly authorized NTFS clone; used vcvars64, SDK 1.4.350.0 and documented configure/build wrappers through the box queue. MSVC generated-header/alignment/max-macro compile problems were repaired identically for control and candidate. StreamProgress exposed real Ninja progress to the queue watchdog. Linux Git fetch resolves the WSL worktree pointer, native Git fast-forwards, and private TEMP/TMP isolates native scratch. Native exit status uses owned Diagnostics.Process handles rather than a launcher proxy. Exact commands and presets: [reproduction](gpuyield-run3/reproduce/README.md).

B: fresh pinned generation and the 22 checks passed; no generated hand edits, invented pin or disabled generation gate. C: supported targeted GPU/contract scopes passed from fresh binaries. The accepted full-suite opcode-94 signature is `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`; additional producer reds cannot be excluded. There is no baseline-unobtainable STOP claim for a failure this lane must fix.

### STOPs / checkpoint handoff

Pending at draft time: latest native build and high-resolution pacing measurement; Dozen 500-frame recovery; full-suite recovery with cache/R6; new fixture PNG byte comparison. The near-budget cliff recovery must execute before the lane can be declared clean. Remaining production hardware-ray graph coverage needs the existing offscreen graph to instantiate actual TraceRaysNode/hit stages; the fixture and production ray-query producer are independently useful verified evidence.

No new items will be taken after 20:17:15 UTC. Heavy work still running at that point will be stopped with ownership-specific cleanup, its actual point and status retained here, and all completed artifacts committed.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Treat pinned generated SIMD headers as compiler system imports
- proposed: Declare MSVC extended-alignment requirements with generated row types
- proposed: Keep editor AppFlow comparison immune to Windows macros
- proposed: Forward native compiler progress to the box queue watchdog
- proposed: Fetch WSL lane revisions through a Git transport that resolves worktree pointers
- proposed: Retain native full-producer exit status with explicit process ownership
- proposed: Let a GPU-device wait yield the lane turn to its ready compiler jobs
- proposed: Read grouped accumulation inputs at their declared execute lifetime
- proposed: Make the ConstantNode header self-contained for typed grouped producers
- proposed: Expose a production offscreen hardware-ray graph witness
- proposed: Provision author identity for the Windows-local witness checkout
- proposed: Separate shared FetchContent sources from native build directories
- proposed: Carry private temporary directories into native runtime witnesses
- proposed: Use a native high-resolution contract for renderer frame pacing
- proposed: Release recorded query-pool references when growing chunk instrumentation
- proposed: Derive native queue reservations from compiler parallelism
- proposed: Validate quantized inner GPU timestamps separately from command bounds
