# gpuyield — R477 bounded GPU work and paced rendering

## LANDABLE NOW

Status: **R477 PARTIAL / NOT READY TO LAND AS COMPLETE WORK.** The candidate preserves standard output and improves several measured runs, but a repeated interactive witness exceeded the submission budget and timed out. Its cause is **unclassified**. The merged full-suite cache gate also cannot launch because its script is absent. Independently useful inventory, probes, captures and recovery evidence are preserved; no universal bounded-submission claim is made.

Original base: `5da61a1897c40ae2f5b7affc1a44d594c4976d24`. Implementation commit: `b4cacedf`. Merged wave: `742fddb6`; tested merged source: `a5cc7f3330ec888a929880a7467a888c04760235`. The latest fetch at 13:19 UTC still resolved to that wave. The lane remains `lane-gpuyield`; no branch was created and nothing was pushed. Kernel pin remains `c3fb3847979e3f9eef04783510c42b70d7058ae7`.

The standard full-frame `ComputeDispatchNode` records native dispatch-base tiles as separate primary command buffers. Existing submit records issue each buffer separately; acquire/dependency waits occur once, and completion signals and the binary fence occur only on the final buffer. Both sequential and lowered graph execution share this submission method. The existing compute pipeline cacher enables Vulkan's dispatch-base creation flag.

The tile budget comes from `TimelineCapacityTracker::Config::gpuTimeBudgetNs` (default 16,666,666 ns). The candidate Dozen/AMD default is eight workgroups per tile edge, or 64×64 pixels. Smaller configured budgets reduce the tile area. No configuration surface, attribute, registry, node family, or shader origin payload was added. Missing shader bundles and shaders consuming `NumWorkgroups` retain their original dispatch and are explicitly logged as unbounded; changing this builtin would change output. The check uses the existing SPIRV-Reflect dependency, not a handwritten bytecode parser.

FIFO replaces the preference for IMMEDIATE/MAILBOX. The existing frame-completion timeline marker is retired before `RenderFrame` returns, bounding outstanding graph work to one frame while retaining the four-slot resource allocation ring. This also protects the next host `PreTick` parameter writes. Successful graph frames use the existing configured frame budget without catch-up bursts; aborted/error frames return promptly.

Two meaningful gates were added: the procedural GPU test compares RGBA, hit-record bytes, and SDF-trace bytes for tiled versus original dispatch; the live editor fixture checks timestamp-measured submission maxima against the budget reported by that graph. The final flight-ring samples are collected at cleanup. CPU tests cover the submit wait/signal/fence contract, stop-on-error, semaphore-only submits, pacing in both execution modes, and unchanged frame counts.

The independent client in `tools/gpu-cadence-probe.cpp` submits a tiny Vulkan compute frame at a requested cadence. `tools/measure-gpu-cadence.py` owns both children, limits their runtime, and summarizes only samples overlapping the observed VIXEN render loop. This measures another Vulkan client's GPU completion cadence, **not DWM presents or Windows input latency**. The render-marker boundary has 50 ms polling resolution; the first overlap interval is discarded.

## Measured device and submission inventory

Fresh Linux/WSL Release build, GCC 15.2, Vulkan SDK 1.4.350.1, provisioned Dozen ICD, `Microsoft Direct3D12 (AMD Radeon(TM) 8060S Graphics)`. Standard windowed and offscreen captures are 500×500. Hardware ray tracing is unavailable on this driver. No synthetic CPU load was used.

| Work | Existing implementation / partition | Timing coverage and remaining boundary |
|---|---|---|
| Full-frame procedural and baked raymarch | `RenderGraph/src/Nodes/ComputeDispatchNode.cpp`; native 8×8 shader groups | Existing whole-dispatch timestamps; this lane adds timestamp pairs for separate tile command buffers. Standard main/editor path changed. |
| Shadow, lighting, spatial reuse, probe/photon stages | `RenderGraph/src/Nodes/ComputeStageNode.cpp` | Existing stage timestamps. Standard active stages measured below; generic stage dispatch remains whole-grid. Disabled stages' tiny timestamp markers are not workload measurements. |
| Procedural bucket splice/grouped direct or indirect passes | `RenderGraph/src/Nodes/MultiDispatchNode.cpp` | Existing task admission budget does not split the recorded command buffer. Production graph does not supply its optional query manager. Unmeasured/unbounded residual. |
| Recipe specialization | `SVO/include/Recipe/RecipeTileSpecialization.h` | CPU pruning over a 3D recipe domain; not a screen-dispatch partition or queue-submission boundary. |
| CPU bake, octree build, rematerialisation and dirty shell repair | `RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp`, `SVO/include/ShellDerive.h`, `RevalidateShellBricks` | Standard path is CPU work plus buffer upload. This lane does not alter bake/octree algorithms. Matching before/after native hash traces cover uploaded nodes, brick pools, lookup and configs. Serialized octree/bake-file byte identity remains unverified. |
| Optional GPU shell classify/dilation | `RenderGraph/src/Nodes/ShellRevalidateNode.cpp`, `shaders/ShellDerive.comp` | One submit contains classify and all dilation layers. No timestamp queries. Dilation reads/updates the same frontier; splitting can change propagation/order. Requires a governing decision before claiming byte identity. |
| Hardware rays | `RenderGraph/src/Nodes/TraceRaysNode.cpp`, `shaders/VoxelRT.rgen` | Existing dispatch timestamps, but unsupported on measured Dozen. `gl_LaunchIDEXT` has no tile-origin payload. No hardware-duration distribution or bounded trace claim. |
| BLAS/TLAS builds | `CashSystem/src/AccelerationStructureCacher.cpp`, `CashSystem/src/DynamicTLAS.cpp`, scene TLAS path | Native build commands; no timestamp coverage or interruptible individual-build partition. Unsupported hardware capability on measured driver. |
| Grouped passes / proxy raster | `RenderGraph/src/Core/PassRecorder.cpp`, `RenderGraph/src/Nodes/PassGroupNode.cpp`, `ProxyRasterStageNode.cpp` | Existing whole command buffers; generic compute/draw workload not certified. |
| Uploads / texture and sky transfers | `ResourceManagement/src/Memory/BatchedUploader.cpp`, command-buffer utilities, texture loader and sky nodes | Existing byte/count/deadline batching caps; a byte cap is not a measured duration cap. No new duration claim. |
| Capture and readback | `application/main/source/FrameCapture.cpp`, `RenderTargetReadback.h`, `application/editor/source/AppFlowReadback.cpp` | Separate copy/readback submits, existing fence/idle waits. Capture bytes checked below; copy-submission timestamp distributions remain unmeasured. |
| Standalone GPU-test renderers | GPU fixtures under RenderGraph/SVO tests | Most bypass `RenderFrame`; graph pacing cannot bound these automatically. The changed procedural feasibility test is explicitly identified, not represented as a conversion of every GPU harness. |

### Baseline duration distributions

Existing GPU timestamp columns, discard first eight rows to exclude the resource-ring warm-up. Editor: 492 rows from the 500-frame concurrent-probe run. HUD: 77 rows from the unchanged 85-frame standard script without the second client. Values are milliseconds; these are distributions, not golden expectations.

| Scene / pass | Mean | p50 | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|---:|
| Editor full-frame march | 79.883 | 79.376 | 83.548 | 85.087 | 89.175 |
| Editor shadow visibility | 4.950 | 4.917 | 5.441 | 5.571 | 5.697 |
| Editor spatial reuse | 0.152 | 0.151 | 0.162 | 0.170 | 0.183 |
| Editor blit | 0.0097 | 0.0095 | 0.0110 | 0.0116 | 0.0361 |
| Editor UI | 0.0491 | 0.0485 | 0.0558 | 0.0573 | 0.0592 |
| HUD full-frame march | 189.186 | 187.892 | 195.063 | 248.206 | 248.206 |
| HUD shadow visibility | 2.677 | 2.655 | 2.835 | 3.284 | 3.284 |
| HUD spatial reuse | 0.148 | 0.147 | 0.159 | 0.161 | 0.161 |
| HUD blit | 0.0097 | 0.0097 | 0.0106 | 0.0109 | 0.0109 |
| HUD UI | 0.0269 | 0.0270 | 0.0290 | 0.0311 | 0.0311 |

The existing trace-enabled procedural oracle separately measured a 500×500 editor dispatch at 97.752 ms and a 400×400 twisted-sphere dispatch at 76.137 ms. These are single samples, not distributions. Extra SDF-trace writes and the analytic test scene distinguish this harness from the runtime column above.

Raw CSV, summaries and producer logs: `reports/gpuyield-evidence/baseline/`. The baseline probe used the saved fresh executable `vixen_editor.baseline`; the otherwise unused `VIXEN_EDITOR_TEST_OFFSCREEN` environment name did not select offscreen presentation. Its log shows a windowed swapchain. The real offscreen fixture uses `VIXEN_EDITOR_OFFSCREEN_CAPTURE`.

### Responsiveness probe

Requested period: 16.666666 ms. Successful summaries exclude the first probe frame and first render-overlap interval. Every producer was requested to run the same 500 frames. Values below are milliseconds. The probe measures second-client compute completion, not Windows input latency or DWM presents.

| Run | CPU cores reserved | Samples | Cadence mean | p50 | p95 | p99 | Max | Gaps > 33.333 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Unloaded baseline | 2 | 900 | 16.666 | 16.667 | 16.825 | 17.506 | 19.221 | 0 |
| Original windowed editor | 2 | 423 | 86.225 | 87.956 | 95.716 | 100.116 | 102.958 | 407 |
| Candidate before wave merge | 2 | 3,103 | 16.667 | 16.590 | 19.501 | 21.777 | 29.950 | 0 |
| Merged candidate, completed run | 4 | 3,780 | 16.687 | 16.629 | 21.068 | 25.094 | 40.637 | 2 |
| Merged candidate, failed repeat* | 2 | 1,585 | 56.991 | 16.698 | 61.021 | 1,104.950 | 1,160.600 | 105 |

*The failed repeat is the **entire 90-second peer trace after its first frame**, including producer startup; it is not a completed render-overlap summary. The producer was killed after its 240-second bound and did not finish 500 frames. Its 198 collected frame-max samples contain one reported budget violation: **1,103.538086 ms against 16.666666 ms**, at `[16:34:15.648]`. Raw evidence is retained under `after-merged-matched/editor-concurrent/`; `failed-partial-summary.json` explicitly labels the failure. This result prevents a clean R477 responsiveness/budget verdict.

The original baseline helper did not retain terminal process lifetime when the editor outlived the probe. The native log supplies its 44.728-second render interval; the successful selected samples remain inside that interval. The completed merged four-core interval was 62.221 seconds. A post-failure Windows GPU-engine/process snapshot was collected at 13:48:51 UTC while another queued GPU job was active; it is diagnostic evidence, **not a causal measurement of the earlier failed run**. It found no process named VIXEN/cadence. GPU clock/power and DWM frame statistics were not correlated with the failure.

### Budget, identity and cost witnesses

The declared existing capacity budget is 16.666666 ms. All scenes below are 500×500, with 64 separately submitted native tiles per frame. Runtime columns summarize **maximum submission per frame**; the GPU oracle separately records all 64 tile intervals.

| Producer | Frame samples | p50 of frame maxima | p95 | p99 | Maximum | Budget result |
|---|---:|---:|---:|---:|---:|---|
| Merged offscreen editor / R6 fixture | 140 | 1.996 | 3.412 | 3.956 | 3.965 | PASS |
| Merged offscreen HUD fixture | 85 | 3.694 | 5.011 | 7.314 | 7.314 | PASS |
| Merged windowed editor, 2 cores | 120 | 1.757 | 2.957 | 3.432 | 3.514 | PASS |
| Merged windowed HUD, 2 cores | 85 | 3.948 | 7.368 | 7.426 | 7.426 | PASS |
| Merged concurrent editor, 4 cores | 500 | 1.873 | 2.350 | 3.100 | 5.812 | PASS |
| Merged concurrent editor, failed 2-core repeat | 198 / incomplete | 1.744 | 2.191 | 2.260 | **1,103.538** | **FAIL** |

The full-suite procedural oracle passed exact RGBA, hit-record and SDF-trace byte comparisons. Its 64 individual submission intervals had p50 1.354920, p95 1.525800, p99/max 1.949640 ms, total GPU time 87.511560 ms. The paired cost measurements below use an untiled reference and tiled version in the **same process**, summing GPU timestamp intervals rather than CPU fence waits.

| Paired editor oracle | Untiled GPU ms | Sum of tiled GPU ms | Measured change | Max tile ms |
|---|---:|---:|---:|---:|
| Before wave merge, 2 cores | 79.608040 | 86.463920 | +8.61% | 1.545760 |
| Merged candidate, 4 cores | 94.643880 | 199.819440 | +111.13% | 4.316880 |
| Merged candidate, 2 cores | 89.481680 | 120.160840 | +34.29% | 3.190800 |

The overhead varies substantially in these real-host runs. Its cause has not been isolated; no constant overhead guarantee is asserted. Whole-dispatch CSV spans also include inter-tile gaps and must not be substituted for the tile sums.

| Capture producer | Frames unchanged | Original render wall s | Before-merge candidate s | Final merged, 2 cores s | Final change |
|---|---:|---:|---:|---:|---:|
| Windowed HUD | 85 | 17.832 | 19.305 | 22.419 | +25.72% |
| Windowed editor | 120 | 16.035 | 19.112 | 20.673 | +28.92% |
| Offscreen HUD fixture | 85 | 15.804 | — | 19.315 | +22.22% |
| Offscreen editor fixture | 140 | 38.980 | — | 23.334 | −40.14% |

The isolated pre-merge wall changes were +8.26% HUD and +19.19% editor. Final merged comparisons also include the landed cache changes and varying host conditions; they do not isolate tiling cost. The failed 500-frame repeat has no successful full-run wall-time result.

Seven windowed PNGs and nine offscreen fixture PNGs from the merged run are **byte-identical** to fresh original-base captures: zero changed pixels/channels. Both `sample_tri_layer.vxd` and the actual saved `sample_tri_layer.edited.vxd` are byte-identical. All selected baseline and completed candidate images were opened, individually or in full-size contact sheets. Native node/pool/lookup/config hash traces match exactly (2/2 HUD-windowed rows, 8/8 editor-windowed, 18/18 editor-fixture, 2/2 HUD-fixture). These hashes are supplementary data evidence, not serialized bake-file byte proof. The seven additional two-core captures were opened and their comparator passed: all byte-identical, zero changed pixels. This gives 23 completed post-merge PNG comparisons across the two windowed runs and offscreen fixtures.

Raw CSV, producer logs, PNGs, artifact SHA-256 values and statuses are in [gpuyield-evidence](gpuyield-evidence/README.md). `provenance.json` records original and merged executable hashes.

## Verification and recovery record

Required scope: changed shader reflection, cached compute pipeline creation, submit channel, full-frame dispatch, frame synchronization/pacing, their main/editor consumers, standard capture/R6 fixtures, all 22 generation checks, RenderGraph/SVO suites, and a no-op rebuild. Generation uses the unchanged tracked kernel pin. No Undertow managed solution build is required or used.

- CodeGraph-first query: no index existed; `codegraph explore --path . --max-files 4 GPU submission dispatch frame pacing timestamp` returned 1. Used `rg` afterwards; did not index.
- Fresh documented `vixen-wsl` configure succeeded, provisioning SDK/windowing dependencies inside this worktree. It reported the SDK's `Include/glslang` case mismatch. Applied the documented worktree-local `Include -> include` symlink and reconfigured successfully. No generated sources/pins were hand-edited.
- The initial queued j4 build never received admission. After over 12 minutes it was explicitly cancelled (143, no target results). The documented `--credits 1`, j2 route admitted the same fresh baseline build, which passed (602 actions, 452 s). This was admission recovery, not a product baseline red.
- The separate probe build passed strict compilation. Its unloaded 15-second runtime passed.
- Baseline standard windowed captures, the existing editor GPU oracle/benchmark, offscreen fixtures, and the standard HUD profile passed.
- An overlapping same-build-tree targeted build was cancelled (wrapper 76, payload 141); its result is inconclusive. The aggregate suite build and integrated build passed. The final build is run after freezing source inputs and includes the later timing-collection changes, so these intermediate outputs are not the final witness.
- The wave initially matched the original base. Before final verification it advanced to `742fddb6`; the feature was committed as `b4cacedf` and merged without conflicts as `a5cc7f33`. The merge retained the dispatch-base creation flag and shared submit/pacing changes. The final fresh merged native rebuild and all 22 selected check targets passed; a subsequent queued rebuild reported `ninja: no work to do`.

The full RenderGraph/SVO suite ran **once**, serially, on the merged source: 2,126 registered, 2,102 passed, 3 failed, 20 skipped and 1 disabled (CTest exit 8; 319.38 seconds). RenderGraph: 1,355 registered, 1,341 passed, 2 failed, 12 skipped. SVO: 771 registered, 761 passed, 1 failed, 8 skipped, 1 disabled. The three failures and recovery outcomes are:

- `FramePipelineTest.AdjacentFramesOverlapButCommitInSubmissionOrder`: two-core reservation yielded `maxActive=1`. The same fresh binary passed the unchanged assertion with `--resource test --credits 2` (four cores); its CTest case also passed with four cores. `FramePipeline.h`, `TaskExecutor` and the fixture are unchanged from the original base. This was invocation/resource recovery; no product/test assertion was weakened.
- `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`: exact permitted `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` / T-1449 finding.
- `vixen_multiinstance_cache_gate`: Python could not open `VIXEN/tools/multiinstance_cache_gate.py`. This file is absent from the merged tree and latest fetched wave. `VIXEN/.gitignore:80` ignores `tools/`. **Unresolved required gate**, not excluded as a clean full-suite result.

The live editor/R6 fixture, parameter propagation/save/reopen/undo/redo/navigation checks, new budget test and AppFlow channel gate passed. The merged focused submit/pacing run passed 3/3. The full-suite shader eligibility test passed. Native budget/cost and 16-artifact identity checks passed before the repeated concurrent-probe failure described above. The actual failed producer log was replayed through the existing `EditorToggleUndoCapture.GpuSubmissionsRespectDeclaredBudget` gate, using its existing capture-directory override and a worktree-local log alias. The gate failed exactly on `1103.538086 > 16.666666` (exit 1). The valid 140-frame fixture log was preserved separately and was not replaced.

Recovery ladder for the missing launcher: **A** checked both documented script locations, tracked tree, ignore rule and fresh origin fetch; no launcher is available. **B** not applicable: it is a handwritten Python launcher, not generated output; all 22 generation contracts are green. **C** the remaining native checks and standard producers can be validated independently, but the brief requires the full RenderGraph gate, so this missing test cannot be excluded. Owner action: publish the intended script (force-add it or fix the ignore rule), then rerun that gate. No substitute test or skip guard was created.

Recovery ladder for the repeated interactive timing failure: **A** the same loader/ICD, work directory and queue route were valid; the producer rendered 198 timestamp samples before its 240-second cleanup bound. The supported offscreen alternative was run on both saved original-base and candidate executables, same scene, two reserved cores, 500 requested frames, 90-second peer trace and 120-second producer bound. **Both producers timed out**, and neither completed 500 frames or produced a success summary. The candidate yielded only seven collected tile samples, max 2.166480 ms, before its timeout. This demonstrates a contemporary completion failure on the original executable too; it does **not** classify the distinct 1,103 ms interval as pre-existing. The diagnostic batch itself exited 0 because it collected both child statuses; `status.txt` records each child as failed. Native Windows tooling is not in the merged wave used here; the lane was instructed to use Dozen. A read-only PowerShell engine/process snapshot succeeded. **B** no stale generated input, missing native binary or pin mismatch was observed; generation is green and the exact merged binaries are hashed. No blind regeneration/cache deletion was performed. **C** the failure is inside the requested GPU-budget/responsiveness scope; it remains **unclassified** and cannot be excluded as environmental or pre-existing. No claim of a clean full R477 change is made.

All builds and tests used `/home/liory/.local/bin/with-test-lock.sh`, stable agent `gpuyield`. Queue requests cancelled before admission had no test results and are not counted as suite executions. The later two-core probe request executed and failed; its failure is retained. Logs/statuses are under `gpuyield-evidence/verification/`, with durable queue logs under `~/.local/state/undertow/undertow-box-logs/`.

Reproduction tips: configure the existing `vixen-wsl` preset against the unchanged tracked kernel pin; use j2/one build credit for native compilation. Mixed full-suite CPU pipeline checks require at least four reserved cores. Standard capture producers use the real `VIXEN_EDITOR_OFFSCREEN_CAPTURE=1` and `VIXEN_HUD_OFFSCREEN_CAPTURE=1` flags; `VIXEN_EDITOR_TEST_OFFSCREEN` is not the offscreen selector. Compile the second-client tool with the provisioned SDK (`glslc tools/gpu-cadence-probe.comp` and C++20/Vulkan), through the build queue. Run `tools/measure-gpu-cadence.py --seconds 90 --timeout 240 --command ./vixen_editor` through the GPU queue from the binary directory, with the configured Dozen runtime environment and `VIXEN_EXIT_AFTER_FRAMES=500`. Use `VIXEN_RIMLEAK_BENCHMARK=after` for paired oracle cost output.

## STOPs / governing decisions still needed

**STOP: required interactive timing property remains red.** The 1,103.538086 ms tile interval and incomplete 500-frame run are reproducible evidence to retain, not an approved outlier. Root cause remains unclassified. Next work must isolate the current Dozen/host scheduling and presentation behavior with a contemporary baseline/candidate replay, then establish a green budget/responsiveness witness. Investigating FIFO versus the existing alternate present choices, and the R475 native-driver route, are options; this lane has not proved either is the cause or fix.

**STOP: required merged full-suite cache gate cannot launch.** Owner must land `VIXEN/tools/multiinstance_cache_gate.py` omitted by the merge. This is separate from the permitted opcode-94 finding.

**REFER-TO-ORCHESTRATOR: optional paths cannot yet share the bounded-output guarantee.** No new abstraction was invented to hide these gaps.

1. Hardware rays need either an existing payload field designated as tile origin by an owner ruling, or an approved origin addition to the existing shader/push-constant contract. Recommendation: extend the existing typed contract; do not invent a ray-scheduling registry. Keep the software path as the measured witness until a native driver/toolchain lane supplies hardware measurements.
2. GPU shell dilation needs a decision on immutable per-layer frontier input versus routing to the existing CPU shell derivation/repair. A snapshot/second frontier changes the present implementation, while splitting the in-place loop can change bytes. Recommendation: use the existing canonical shell derivation contract and establish which frontier bits are observable before converting the GPU mirror.
3. Indirect/grouped procedural passes need a ruled use of existing bucket/tile data that supplies a safe tile grid and separately submitted work. An indirect dispatch has no Vulkan dispatch-base variant; a full-screen indirect workload cannot simply be divided by this lane's primary-buffer split. Recommendation: derive chunk commands from the existing bucket/partition contract together with the multiinstance/togglelatency lanes, not another task registry.
4. Individual acceleration builds, arbitrary raster draws, transfer batches, and raw GPU-test fixtures still lack complete duration coverage. Inventory is explicit but its full measurement/bounding obligation remains unfinished. No universal or unmeasured-device budget guarantee is asserted.

The original standard-path baseline was recovered and established before semantic implementation. This report does not stop on the original unbounded dispatch it was sent to fix. It preserves independently completed work and explicitly marks the newly observed required timing failure, omitted merged launcher, and remaining optional-path work as unfinished. R477 must not be closed or landed as fully verified from this report.

## SPT DISPOSITION

R477 — PARTIAL / REQUIRED GATES UNRESOLVED: standard full-frame compute tiling, graph pacing and probe implementation; repeated interactive timing failure, missing merged launcher, optional conversions and full inventory timing coverage remain as above. The brief supplied no SPT task identifier to close.

No kernel or Undertow declaration changes, no `Program.Legacy.cs` changes, and no new attribute/registry/id space. Native policy changes use existing `SubmitRecord`, command pools, timestamp queries, timeline completion markers and capacity configuration. R412 added-line mechanical check passed with zero hits; no new schema/system/reference declaration requires an exemplar. No native source changed after the tested merge.

## Checkpoint / remaining work

Started around 11:10 UTC; stopped taking new items before the approximately 14:10 UTC checkpoint. Final stop point: all 23 selected post-merge PNG comparisons and both documents are byte-identical; the full suite ran once; CPU overlap recovery passed; the real failed producer budget test failed; both offscreen replay producers timed out. No diagnostic child or queue request is intentionally left running. Remaining work is the required timing failure diagnosis/fix, publication/retest of the omitted launcher, optional GPU path conversions and full duration inventory; serialized bake-file byte identity and before/after identity of other scene captures remain unverified. The committed native implementation is a candidate with unresolved required gates, not a clean complete R477 result.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Make the provisioned Linux Vulkan SDK glslang include lookup case-tolerant
- proposed: Size VIXEN queued build credits from the requested compiler parallelism
- proposed: Derive compute pipeline dispatch-base capability from bounded command recording
- proposed: Preserve compute builtin dependencies in the existing shader reflection contract
- proposed: Make zero-count Vulkan submit payload copies safe without pointer arithmetic
- proposed: Make CodeGraph first queries usable in isolated VIXEN worktrees
- proposed: Share configured Vulkan runtime environment with standalone GPU witness tools
- proposed: Declare CPU concurrency requirements for mixed GPU RenderGraph suites
- proposed: Require registered CTest launcher scripts to be tracked and present
