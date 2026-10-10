# gpuyield run 2 — R477 timing isolation and R486 contracts

## LANDABLE NOW

**PARTIAL.** The submission/accounting fix is committed as `3cfea5fd9191a8a53c1eb34151edd633ebfdf0aa`. Hardware-ray tiling is committed as `35201e4f`, compiled, and verified by the real shader-contract test; hardware output and timing remain unmeasured. Shell and indirect/grouped conversions are unfinished. This report does not certify every GPU path or Windows desktop presentation.

Current tip at report generation: `35201e4f645eaee69c5aeacedec0695014687b4f`. Starting tip: `e1ba5b5296ca26c2e9649517b291b113f401f253`. Merged wave: `1252e3705a8247f5b0b1478f0164da55c7cd44ba`; merge commit `222c28a928feedcb32733d3706c5539fef3f4850`. Latest fetch still matches this wave. Kernel pin remains `c3fb3847979e3f9eef04783510c42b70d7058ae7`. No other repository was edited; no branch was created or switched; nothing was pushed. [Run 1](gpuyield-run1.md) is preserved.

### Classification evidence

The first red remains preserved: a whole-tile interval of 1,103.538086 ms against 16.666666 ms, with only 198 frames before the 240-second bound. That original interval starts before entry/image barriers and the acquire wait at COMPUTE_SHADER. It is elapsed GPU-clock time, including waits/preemption, rather than an exclusive active-execution counter.

Before semantic fixes, diagnostic-only four-query instrumentation retained the original TOP/BOTTOM interval and added a COMPUTE_SHADER pair immediately around each dispatch. The isolated fresh merged producer completed 500 frames / 32,000 chunks, with zero invalid pairs: whole-chunk p99 1.713280 / max 2.188440 ms; dispatch interval p99 1.705600 / max 2.180720 ms. Its render loop lasted about 48.031 seconds. The peer failed after 2,397 attempts when Dozen returned VK_NOT_READY from timestamp retrieval after a successful fence wait; its 2,396 complete post-first rows are explicitly partial, not a successful 90-second probe.

The same tiny SPIR-V probe ran on the native Windows AMD driver, compiled with provisioned MSVC 19.44.35229 and SDK 1.4.350.0. Contended runs had host-service maxima 1,135.410 and 1,180.120 ms while GPU intervals stayed below 0.007400 ms; polling still had a 1,090.430 ms host-service maximum and 0.008040 ms GPU maximum. Ordinary GPU CTest jobs reserved `test` resources and bypassed the queue's GPU mutex. A 15-token exclusive control reduced native service p99/max to 0.485200/1.161200 ms, GPU p99/max to 0.006960/0.008120 ms, and cadence p99/max to 31.642000/31.781100 ms.

**Cause class supported by these controls: scheduling/contention delays outside the tiny GPU dispatch, with an independently proven unretired VIXEN submission backlog and free unknown-cost accounting.** The isolated producer establishes that 64×64 tile work itself fits the budget. The exact active/preemption/acquire contribution to the historical 1,103.538 ms sample cannot be reconstructed: WPR GPU/DesktopComposition start failed 0xc5585011, stop failed 0xc5583000, and no ETL was produced. GPUView/PresentMon were unavailable. Later vmwp counter snapshots are not synchronized traces of the red sample. No exclusive active-GPU or DWM presentation claim is made.

### Changes and limits

SubmitRecord retires prior queue work before every new chunk, including the first chunk of a record: at most one submitted command chunk remains outstanding. Acquire/dependency waits stay on the first submission, and completion signals/fence on the last. Retirement failures stop subsequent submissions and propagate device loss. This uses the existing queue-owner channel and vkQueueWaitIdle, with no registry; R478 can later derive retirement from graph timelines.

MultiDispatch charges missing/zero/uncalibrated estimates conservatively against its configured frame budget while retaining mandatory work. Its timestamp commands now lie inside command-buffer begin/end, and its sequential submit fallback shares retirement. Generic CPU TaskQueue zero-cost semantics are unchanged. Host clocks separately measure retirement, submit, acquire, frame completion and presentation. Probe queries explicitly wait for availability, complete CSV rows are flushed, and the summarizer rejects missing frames, invalid pairs and empty/invalid peer samples.

TraceRaysNode derives uvec3 tileOrigin/fullLaunchExtent offsets from ray-generation reflection, preserves the existing 64-byte camera prefix, records bounded-size launch tiles in separate primary buffers and retains first-acquire/final-completion ownership. The ray shader uses original global pixel coordinates and full launch dimensions. This is compiled source plus an ABI test, not hardware-ray timing or byte proof. The dormant legacy VoxelRT manifest was not hand-edited.

Shell audit: FRONTIER is explicitly exposed in the public flags SSBO, although existing production consumers found do not inspect it and the GPU test masks bits 0/1. In-place read/OR/clear across invocations is not an immutable frontier and partition changes can alter propagation and raw bits. The canonical CPU derivation has separate current/next frontiers and normalizes zero dilation to one. The optional clarification received no answer; all-32-bit parity remains the conservative assumption. No raw bytes or golden masks were changed.

Indirect/grouped audit: preserve the bucket's original rounded-up invocation domain, empty-bucket zero commands, pass order and barriers. Chunk commands must derive from existing bucket and tile partition data; merely splitting around vkCmdDispatchIndirect does not bound it. GPU command expansion and grouped record-only consumer handling are not implemented. See [contract audit](gpuyield-run2/r486-contract-audit.md).

### Timing table per driver

All times below are milliseconds. Both driver routes use the same physical AMD Radeon 8060S, vendor 4098/device 5510. Dozen driver integer 104865800/API 4202814; native AMD driver integer 8388967/API 4211008. The declared chunk budget is 16.666666 ms. Timestamp intervals include possible preemption.

| Producer driver | Phase | Chunk samples | GPU p99 | GPU max | Frames | Budget pass |
|---|---|---:|---:|---:|---:|---|
| Dozen / AMD | fresh diagnostic baseline | 32000 | 1.713280 | 2.188440 | 500 | True |
| Dozen / AMD | candidate with Dozen peer | 32000 | 1.741000 | 2.257480 | 500 | True |
| Dozen / AMD | candidate with native AMD peer | 32000 | 1.771040 | 2.249600 | 500 | True |
| Native AMD | full VIXEN / hardware rays | unavailable | — | — | — | not measured |

The native overlap harness starts its peer after the producer's render marker and rejects it unless the peer finishes while the producer remains alive; Windows and Linux steady-clock epochs are not compared.

| Completion peer driver | Context | Valid samples | GPU p99 / max | Fence-service p99 / max | Cadence p99 / max |
|---|---|---:|---:|---:|---:|
| Native AMD | pre-fix exclusive control | 899 | 0.006960 / 0.008120 | 0.485200 / 1.161200 | 31.642000 / 31.781100 |
| Dozen / AMD | candidate whole 90-second peer trace | 5399 | 0.006600 / 0.010600 | 3.794940 / 9.068320 | 19.828600 / 38.327000 |
| Native AMD | candidate exclusive control | 897 | 0.008080 / 0.008120 | 0.678200 / 0.969400 | 31.612700 / 32.829600 |
| Native AMD | during candidate Dozen producer | 886 | 0.007600 / 0.008000 | 3.661500 / 5.020800 | 32.269800 / 33.628900 |

### Required verification and recovery

Scope: SubmitRecord and all affected RenderGraph consumers, MultiDispatch accounting/timing, shared ShaderManagement reflection consumers, ray shader reflection ABI, main/editor captures and R6, 22 pinned generation checks, RenderGraph/SVO suites, cache gate and no-op build. Native full-VIXEN execution requires a usable Windows build; the standalone probe validates the native client scope only. There is no published matching GPU timing baseline, so the fresh merged diagnostic baseline was required; the kernel CodegenTool suite was not rerun.

All heavy work uses `/home/liory/.local/bin/with-test-lock.sh --agent gpuyield`. Exact helpers and arguments are preserved in [reproduction](gpuyield-run2/reproduction). Fresh merged baseline build passed (402 actions); fresh 22 generation checks passed. Candidate aggregate build passed (164 actions), the corrected shader-path target rebuild passed, and the integer-vector reflection rebuild passed (105 actions, payload 27 seconds, exit 0). A final fetch still reports wave `1252e370`.

The first filtered run found zero ray tests in an older executable while rebuilding and is invalid. Fresh six retirement/accounting cases passed, but the first actual ray case compiled the main-checkout shader through relative __FILE__ and failed missing tileOrigin. The fixture was fixed to receive the existing CMake shader source root, without weakening any expectation. The next actual shader case exposed INT|VECTOR reflection losing signedness and width. Shared reflection now retains UInt/Int scalar kind, width, vector byte size and C++/GLSL names. Discovery requires all seven cases. Fresh CPU validation passed all seven, including the compiled lane shader, and timing smoke checks passed (exit 0). The first queued exclusive retry was cancelled before it started (143) to let that same-agent CPU validation run; the final controlled driver witness was then requeued. Both candidate 500-frame producers passed the chunk gate, with 32,000 chunks and zero invalid timestamp pairs each. Both peers completed, and the native AMD peer finished entirely while its Dozen producer remained running. All seven standard windowed PNGs are byte-identical and were opened. Full suites, the merged cache gate and no-op rebuild were not run at the checkpoint.

Full-suite recorded status: **NOT RUN**. Only the exact permitted opcode-94 signature may be accepted; every other failure remains required. The restored cache launcher is tracked in the merged wave and is included in this suite. Fresh windowed PNG references are under captures-before; the completed after comparison found zero byte and decoded-pixel differences for all seven images. Offscreen fixtures compare against the preserved run 1 original-base reference and are labeled as such, not a new merged-base fixture capture.

Native recovery A: queued build.bat configure vixen-ninja exited 1 because vswhere did not discover the installed MSVC. Explicit provisioned vcvars compiled the standalone probe. Native configure then recovered UNC linker cwd through an own-worktree drive mapping, disabled WSL Dozen provisioning for the native route, used process-local Git trust for owned dependency clones/readonly kernel, and supplied the provisioned kernel root. These are identifiable setup changes, not product fixes.

Native recovery B: unchanged source setup reached VixenKernelSnapshot before generation and stalled on Windows CMake file(LOCK) over the WSL worktree. The queue killed it after 231 seconds (wrapper 75, payload 141). A fresh own-file lock with TIMEOUT 5 also never returned a result and was idle-killed after 199 seconds (75/141). Linux pinned generation was fresh and green; there is no safe native regeneration route past this lock. No generated pins were invented and no gates were disabled.

Native recovery C: fresh native probe compilation/execution validates its narrower driver-client scope, but cannot validate full native VIXEN or hardware-ray output. Windows descendants surviving Linux queue cleanup were identified by owned command lines, stopped, and checked absent. The native full-build result is inconclusive beyond the evidenced lock stall, not a product compile failure. See native-invocation-status.txt and invocation-*.log.

### Cost and responsiveness limits

The fresh diagnostic baseline sums **41.589438 seconds** of whole-chunk GPU-clock intervals; the candidate with the Dozen peer sums **41.448568 seconds** (**−0.34%**). Its 500-frame render interval grows from **48.031** to **58.718 seconds** (**+22.25%**). The candidate with the native peer takes **58.700 seconds** in its render loop. Retirement bounds the backlog at a measurable host/wall-time cost; GPU work remains approximately unchanged.

The valid Dozen render-overlap subset has **3,549** peer samples: service p99/max **4.302720/9.068320 ms**, cadence p99/max **20.358500/38.327000 ms**, with **one** cadence over two periods. The table's 5,399-sample Dozen peer row covers the complete 90 seconds, including startup and time after the producer. The failed pre-fix Dozen peer is partial and cannot support a clean before/after cadence verdict. The native render-overlap probe has 886 post-first samples, service p99/max **3.661500/5.020800 ms**, cadence p99/max **32.269800/33.628900 ms**; the native exclusive before control was **31.642000/31.781100 ms**. These are Vulkan compute completion peers, not compositor presents. Host submit maxima **90.412037** and **104.527659 ms** persist despite green chunk GPU intervals. General desktop responsiveness therefore remains unproven.

All seven before/after PNGs were opened. Editor toggle/undo/redo and HUD state transitions match exactly. Capture log spans and SHA-256 hashes are retained in capture-identity.json; log spans include startup/shutdown and are not dedicated wall-clock timers.

### STOPs and checkpoint handoff

- Native full producer — STOPPED for that platform scope: supported CMake locking on the assigned WSL share, or an explicitly assigned NTFS worktree, is needed for the remaining native producer/hardware witnesses. Missing tools is no longer the cause.
- Shell — unfinished at checkpoint: the owner supplied the canonical derivation route; preserve the exposed FRONTIER contract when implementing immutable layer/chunk lowering, then compare raw and committed artifacts. The audit is complete; conversion is not.
- Indirect/grouped — unfinished at checkpoint: conversion and byte/timing witnesses are unfinished; use the existing bucket/tile data, preserve padded domains and cover record-only consumers.
- Historical red: its exact GPU-active versus dependency/preemption split lacks a synchronized trace. Controlled reference runs must not be generalized to arbitrary external GPU workloads or DWM presentation.

Stopped heavy work at **17:11:00 UTC on 2026-10-10**, at the three-hour checkpoint. The outer final queue session had returned **143 at 17:10:01 UTC**, with its termination cause unclassified and its owned Linux scope still executing the generation refresh. Ownership was checked and process group 155810 was stopped with SIGTERM; its producer/generator processes were confirmed absent. The refresh log reports **17/23 CMake steps** before `ninja: build stopped: interrupted by user`; the earlier fresh merged 22-check result remains green, but this final refresh is **incomplete**, not a passing contract. See [checkpoint status](gpuyield-run2/checkpoint-status.json).

The full ShaderManagement and RenderGraph/SVO suites, merged cache gate, offscreen fixture comparison/R6 suite gates and no-op rebuild were **not run**. The checkpoint guard was prepared before the full-suite phase, but the interrupted refresh never reached it. Remove the lane checkpoint marker when resuming the reproduction helper. Remaining work: shell and indirect/grouped conversion; those raw/data byte witnesses; native full-producer/hardware-ray witnesses; finish 22 checks and run the full required suites once. The committed candidate is not certified as complete R477/R486 work.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Windows native build discovery rejects an installed MSVC toolchain
- proposed: Windows native CMake builds need a supported worktree drive mapping
- proposed: Native Windows FetchContent needs scoped trust for WSL worktree clones
- proposed: Native Windows configure should not attempt WSL Dozen provisioning
- proposed: GPU CTest reservations bypass the queue device mutex
- proposed: Native codegen discovery needs the provisioned kernel root
- proposed: GPU chunk timings need separate dependency and dispatch intervals
- proposed: MultiDispatch timestamp commands lie outside the primary recording lifetime
- proposed: Native build summary needs worktree-scoped logs and status
- proposed: Box queue native interop cleanup leaves Windows payload descendants alive
- proposed: Dozen cadence queries need explicit availability waits and durable CSV rows
- proposed: Native CMake file locks stall on the assigned WSL worktree
- proposed: Filtered contract witnesses must reject zero selected tests
- proposed: Anchor compiled shader fixtures to their owning checkout
- proposed: Consolidate chunk retirement with declared graph timeline capacity
- proposed: Declare conservative uncalibrated GPU cost at the queue policy boundary
- proposed: Preserve integer vector traits in the shared SPIR-V reflection contract
- proposed: Queue wrapper termination leaves its nested Linux witness scope running
