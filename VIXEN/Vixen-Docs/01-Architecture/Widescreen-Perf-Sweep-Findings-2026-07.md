# Widescreen Perf Sweep — Verified Findings & Measurements (2026-07)

**Status:** ANALYSIS OF RECORD for the widescreen-FPS-drop fix program. Companion plan: `Widescreen-Perf-Fix-Plan-2026-07.md`.
**Provenance:** 10-subsystem multi-agent sweep (88 raw findings → 63 canonical → top-32 verified by two adversarial lenses each → 12 confirmed / 10 plausible / 10 rejected), followed by empirical A/B measurement on the real GPU (Release/Ninja build, validation off, IMMEDIATE present). Full machine-readable result: session scratchpad `sweep_result.json`.

---

## Empirical measurements (2026-07-03, Release build, real GPU)

**Fixed-size matrix (600 frames each, fresh window per run):** requested 500x500 / 1280x720 / 1920x1080 / 2560x1440 — ALL ran with an **800x600 swapchain** (see D1) at ~0.19 ms dispatch, 630-1300 FPS. Frame cost did NOT scale with requested size because the request never reached the window/swapchain.

**Resize probe (start default, programmatic resize → 2560x1440 at frame 600, 1500 frames):**

| Phase | Swapchain | Dispatch GPU | Frame avg | p99 | FPS |
|---|---|---|---|---|---|
| Pre-resize | 800x600 | 0.20 ms | ~1.6 ms | ~6.5 ms | ~630 |
| Transition (120f window) | — | — | 7.3 ms | 40.8 ms | 137 |
| Post-resize steady | 2560x1440 | 1.45 ms (7.3x ≈ pixel ratio 7.7x) | ~4.8 ms | **18-22 ms** | ~210 |

- Mrays/s constant (~2550) across sizes → ray-march scaling is **cleanly linear** on this GPU; no atomic-contention collapse *on this hardware* (the counters remain pure waste — see rank 2).
- Exactly **1** `RECOMPILATION TRIGGERED` per resize on this driver — no SUBOPTIMAL storm *here*; the storm remains a code-verified hazard on Dozen/WSL2-class surfaces (rank 3).
- Non-dispatch frame cost grew ~1.4 → ~3.3 ms with size (UI composite pass + present + pick-ID are area-linear).
- **Chronic post-resize p99 hitches**: 18-22 ms spikes (~4x median) every ~1% of frames — unattributed; needs instrumentation (plan M5).
- User-context amplifier: user's day-to-day exe was a **Debug + validation-layers** build (validation defaults ON for Debug via ProvisionVulkan.cmake), multiplying both CPU constants and per-frame overhead.

## Discoveries the static sweep missed (found by measurement)

### D1 — App window size silently ignored: fresh windows are ALWAYS 800x600
`NodeParameterManager::GetParameterValue<T>` (libraries/RenderGraph/include/Data/NodeParameterManager.h:55-66) does a strict `std::get_if<T>` on the ParamTypeValue variant and **silently returns the default on type mismatch**. The app stores `int` (Int32 alternative) via `window->SetParameter(WindowNodeConfig::PARAM_WIDTH, width)` (BuildRenderGraph.cpp:259-260, `width` is `int`); WindowNode reads `GetParameterValue<uint32_t>(PARAM_WIDTH, 800)` (WindowNode.cpp:66-67) — Int32 ≠ UInt32 → default **800x600 window + swapchain**, always.
**Consequences:** (a) the app *never* renders at the size it asks for from a fresh start; (b) a **resize is the only path to true window resolution** — which is exactly why "entering wide screen mode" is the moment full-res cost (and any resize pathology) first appears; (c) any other SetParameter call site with an int literal is silently defaulting too.

### D2 — GPU timing instrument was dead for all slots ≥ 1 (FIXED during measurement prep)
`GPUTimestampQuery::ReadResults` hardcoded a `[0,2)` query read while `GPUQueryManager` maps slot i → queries 2i/2i+1 — every consumer except slot 0 read zeros, and `BenchmarkRunner` fabricated `gpuTimeMs = 0.9 x frameTime` when that happened. Fixed 2026-07-03 (availability-based full-pool read + per-query availability check in GetElapsedNs); real dispatch timings confirmed live on lavapipe and real GPU. Historical `gpu_time_ms`/Mrays exports predating this fix are untrustworthy.

### D3 — Perf instrumentation added (measurement plan Step 0/1)
`[FrameTimer]` avg/p99/FPS every 120 frames in main.cpp; `VIXEN_EXIT_AFTER_FRAMES` clean-exit; `VIXEN_WINDOW_WIDTH/HEIGHT` overrides (currently defeated by D1 - they ride the same broken param path; work after M1); `VIXEN_RESIZE_AT_FRAME` + `VIXEN_RESIZE_WIDTH/HEIGHT` one-shot programmatic resize through the real GLFW-callback path. Windows driver scripts: `VIXEN/temp/run_perf_matrix.bat`, `run_resize_probe.bat`; Release tree `build/ninja-release`.

---

## Ranked fixes (from verified synthesis)

### Rank 1: Ray-march dispatch hard-locked to swapchain extent — no render-scale decoupling
- **Where:** VIXEN/libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp:360
- **Scaling:** per-pixel (linear in window area, very high per-pixel constant)
- **Severity:** critical | **Fix cost:** medium
- **Expected contribution:** ~60-80% of the settled large-window frame-time growth — this IS the linear pixel-scaling term (4.0x GPU work at 720p→1440p)
- **Fix direction:** Introduce a dedicated offscreen storage image sized extent×renderScale (scale ≤ 1.0) as a first-class graph resource; ray-march into it and add an upsample/composite pass to the swapchain (the auto-sync PassGroupNode infrastructure already handles the extra barrier/submit). Make renderScale a graph parameter; optionally drive it dynamically from GPUPerformanceLogger dispatch time once the timestamp slot bug is fixed. This also removes the storage-usage/GENERAL-layout swapchain write and unlocks correct pick-ID sizing — the architecturally pure resolution seam the engine currently lacks.

### Rank 2: Shader debug counters compiled in unconditionally — 3-4 same-address atomics per pixel into HOST_VISIBLE memory, never read
- **Where:** VIXEN/shaders/BodyInstanceRayMarch.comp:147
- **Scaling:** per-pixel (linear thread count; serialization surfaces at large windows)
- **Severity:** high | **Fix cost:** small
- **Expected contribution:** ~10-25% of the large-window frame time (hardware-dependent: ~0.2-0.5ms warp-aggregated to ~5-10ms serialized/host-visible at 1440p); pure waste in the live app
- **Fix direction:** Gate the counters behind a specialization constant / separate metrics pipeline variant instead of the hard #define (the benchmark harness is the only consumer); delete the recordVoxelSteps(0u) no-op outright. If counters must stay available, allocate DEVICE_LOCAL and reduce per-subgroup/per-workgroup (one atomic per workgroup, ~64x fewer).

### Rank 3: SUBOPTIMAL acquire triggers full surface+swapchain recreation + transitive recompile with no extent-unchanged guard — can loop every frame on Dozen/WSL2
- **Where:** VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp:354
- **Scaling:** resize-transient (escalates to sustained per-frame recreation storm if the driver re-reports SUBOPTIMAL)
- **Severity:** medium (critical if the per-frame loop is confirmed in logs) | **Fix cost:** small
- **Expected contribution:** ~0% of the settled drop on conformant drivers; potentially the ENTIRE sustained collapse on Dozen/WSL2-class surfaces — must be excluded by the log check before attributing everything to per-pixel scaling
- **Fix direction:** On VK_SUBOPTIMAL_KHR, query surface currentExtent and skip recreation when it equals the existing swapchain extent (recreate only on a genuine extent change); log every actual recreation so a per-frame loop is immediately visible. Root-cause-pure: recreation should be driven by state divergence, not by the driver's advisory flag.

### Rank 4: Procedural bodies: up to 128-step sphere trace with 3x sin per eval and 6-eval gradient — the dominant per-pixel workload of the default scene
- **Where:** VIXEN/shaders/SdfRecipes.glsl:56
- **Scaling:** per-pixel (linear in body-covered pixels)
- **Severity:** medium | **Fix cost:** small
- **Expected contribution:** Bulk of the per-pixel constant inside rank 1's scaling term for the default 3-procedural-body scene; reducing it directly shrinks the slope (ms-class on lavapipe/iGPU, tens of us on fast discrete GPUs)
- **Fix direction:** Early-out the sphere trace when t exceeds the caller's current bestT (pass bestT in); replace the 6-eval central-difference gradient with analytic sphere gradient + 4-eval forward difference for the displacement term; adopt relaxation sphere tracing to cut step counts on the Lipschitz-shrunk displaced body.

### Rank 5: 23-entry dynamically-indexed local traversal stack, fully re-initialized per instance per pixel — static scratch/occupancy tax on every dispatch
- **Where:** VIXEN/shaders/ESVOTraversal.glsl:24
- **Scaling:** per-pixel (occupancy/scratch allocation is static per pipeline, so all frames pay it; init writes on ESVO-hit pixels)
- **Severity:** medium | **Fix cost:** small
- **Expected contribution:** ~5-15% amplifier of the ray-march pass (0.1-1ms class at 1440p plus latency-hiding loss); applies even to procedural-only frames via static register/scratch allocation
- **Fix direction:** Size the stack to actual octree depth (userMaxLevels ~7-8) via a specialization constant instead of the hardcoded 23; drop the full-array init in favor of lazy writes on push (the reference Laine-Karras h-value scheme); consider the packed uint2 stack layout to cut the per-thread footprint further.

### Rank 6: MAX_ITERS=512 traversal cap applies PER INSTANCE — worst-case pixels pay instances x 512 dependent SSBO fetches
- **Where:** VIXEN/shaders/ESVOTraversal.glsl:25
- **Scaling:** per-pixel (ESVO/stored-SDF scenes only; zero in the default all-procedural scene)
- **Severity:** low in default scene / medium with stored or many-body scenes | **Fix cost:** small
- **Expected contribution:** ~0% for the default-scene repro; material (ms-class silhouette-band cost, x N with overlapping bodies) once stored-SDF/undertow content is on screen
- **Fix direction:** Share one iteration budget across the whole per-pixel instance loop instead of resetting per traverseOctreeInstanced call; pass bestT into the traversal as a tMax clip so it terminates when t_min exceeds the current best; empirically lower the cap (typical scenes converge <200).

### Rank 7: No resize debounce: full surface+swapchain recreation + transitive Cleanup/Setup/Compile cascade per size change for the whole drag
- **Where:** VIXEN/libraries/RenderGraph/src/Core/RenderGraph.cpp:1631
- **Scaling:** resize-transient (per size-change event; quiescent once settled)
- **Severity:** medium | **Fix cost:** medium
- **Expected contribution:** The multi-frame stalls felt DURING the drag/maximize (tens-to-hundreds of ms per size event, repeated per event); ~0% of the settled drop
- **Fix direction:** Coalesce resize events and recreate on settle (N-ms stability or drag-release); pass the old swapchain as oldSwapchain to vkCreateSwapchainKHR so the driver recycles images; keep ONE persistent VkSurfaceKHR (owned by WindowNode) instead of destroying/recreating it per recompile; narrow the dependent cascade to genuinely extent-dependent resources.

### Rank 8: Wave-based recompile cascade recompiles deep nodes multiple times per resize (O(depth^2) node-recompiles; swapchain recreated twice on the paused path)
- **Where:** VIXEN/libraries/RenderGraph/src/Core/RenderGraph.cpp:1722
- **Scaling:** resize-transient, superlinear in graph depth (not window area)
- **Severity:** medium | **Fix cost:** small
- **Expected contribution:** A multiplier (up to ~depth x) on rank 7's per-event cost, including double swapchain destroy/create per maximize; ~0% settled
- **Fix direction:** Mark only DIRECT dependents after each recompile (the wave loop already propagates level-by-level) and erase a node from dirtyNodes when it recompiles within the current wave — or, cleaner, compute the transitive closure up front and recompile each node exactly once in topological order.

### Rank 9: Enabled graph logger: endl-flushed console I/O per line + unbounded logEntries growth, bursting dozens of lines per recompile cascade
- **Where:** VIXEN/libraries/logger/Logger.cpp:57
- **Scaling:** resize-transient (log bursts only fire on recompile/abort paths; steady-state INFO path is silent)
- **Severity:** low | **Fix cost:** small
- **Expected contribution:** ~5-50ms of console I/O per resize event on a Windows console, stacked on rank 7/8; ~0% settled
- **Fix direction:** Cap logEntries with a ring buffer; write '\n' instead of std::endl and batch flushes; demote per-node recompile/dependent lines from INFO to DEBUG (cheap once the lazy-macro fix at rank 11 lands).

### Rank 10: Every pixel loops every body instance with no bestT occlusion early-out and no front-to-back ordering
- **Where:** VIXEN/shaders/BodyInstanceRayMarch.comp:737
- **Scaling:** per-pixel (multiplier = on-screen instance-AABB overlap; ~1.0 in the default non-overlapping scene)
- **Severity:** low (for this symptom; load-bearing for the 60-300-body undertow path) | **Fix cost:** small
- **Expected contribution:** ~0% of the current repro (default bodies don't overlap); prevents the per-pixel cost from multiplying when many overlapping bodies arrive
- **Fix direction:** Add 'if (gridT.x > bestT) continue;' after the AABB slab test (gridT.x is directly comparable per the in-shader comment at :764-769), and thread bestT into traverseOctreeInstanced/traceProceduralBody as a tMax clip; later, front-to-back instance ordering to make the cull bite.

### Rank 11: Log macros evaluate/concatenate message strings before any enabled check — ~120-180 dropped heap-allocating strings per frame engine-wide
- **Where:** VIXEN/libraries/RenderGraph/include/Core/NodeLogging.h:16
- **Scaling:** per-frame-constant (scales with binding/slot count, not resolution)
- **Severity:** low | **Fix cost:** small
- **Expected contribution:** ~0% of the size-dependent delta (0.05-0.15ms fixed floor, increasingly hidden behind GPU time at large windows); raises the small-window ceiling slightly
- **Fix direction:** Make the macros check enablement + level BEFORE evaluating msg — 'if (nodeLogger && nodeLogger->IsEnabled() && level >= globalMinLevel)' (IsEnabled exists at Logger.h:28) — zero call-site changes; optionally compile DEBUG level out of Release. Also fixes the lifecycle-hook and descriptor-path string waste at the root.

### Rank 12: PreExecute/PostExecute lifecycle-hook broadcast: O(nodes x hooks) self-filtering callbacks + dropped-log string building every frame
- **Where:** VIXEN/libraries/RenderGraph/src/Core/GraphLifecycleHooks.cpp:65
- **Scaling:** per-frame-constant (scales with node/connection count)
- **Severity:** low | **Fix cost:** small
- **Expected contribution:** ~0% of the size-dependent delta (~0.05-0.25ms fixed floor)
- **Fix direction:** Key node hooks by target NodeInstance* (map<NodeInstance*, vector<hook>>) so only the owning node's hooks run instead of every hook self-filtering by identity; guard the per-entry LOG_DEBUG behind the rank-11 macro fix.

### Rank 13: Vulkan validation layers defaulted ON for every config under multi-config (MSVC) generators, including Release
- **Where:** VIXEN/cmake/ProvisionVulkan.cmake:29
- **Scaling:** per-frame-constant (latent — dead under the current single-config Ninja build trees)
- **Severity:** low | **Fix cost:** small
- **Expected contribution:** 0% under current builds (mech lens verified all three caches are Ninja single-config); a measurement confound only if a VS generator or Debug build is used for the A/B
- **Fix direction:** Gate the default on the ACTIVE configuration via a generator expression / per-config compile definition so a VS Release build runs without validation; keep Debug ON by design. Do the perf A/B on a Release/Ninja build regardless.


---

## Full synthesis report (verified sweep)

# VIXEN — Widescreen FPS-Drop Bottleneck Report

**Symptom:** drastic FPS drop when the window is resized large / maximized. Renderer is a compute-only per-pixel voxel ray-march (`BodyInstanceRayMarch.comp`), live graph built by `VIXEN/application/main/source/graph/BuildRenderGraph.cpp`.

**Headline verdict:** The settled (post-resize) drop is dominated by strictly linear-in-area GPU work with a very high per-pixel constant, with no render-scale decoupling of any kind. Resize-transient pathologies add severe hitching *during* the maximize itself, and one conditional escalation (the SUBOPTIMAL-acquire recreation loop) could turn the transient into a sustained collapse on Dozen/WSL2-class drivers — a one-line log check discriminates. Per-frame-constant CPU overheads are real hygiene defects but resolution-invariant and increasingly hidden behind GPU time at large windows; they do not contribute to the size-dependent delta.

---

## (a) Per-pixel / superlinear costs (scale with window area) — the settled drop

**A1. Ray-march dispatch hard-locked to swapchain extent — CRITICAL (confirmed, both lenses).**
`ComputeDispatchNode::RecordComputeCommands` recomputes `dispatchX/Y = (swapchainExtent + 7)/8` from the *live* swapchain extent every frame (`ComputeDispatchNode.cpp:360-361`, `vkCmdDispatch` at `:399`; identical fallback in `ComputeStageNode.cpp:260-264`). The shader runs one full-depth divergent trace per pixel (`local_size 8x8`, bounds-checked against `imageSize(outputImage)` = the swapchain image) and `imageStore`s at swapchain resolution (`BodyInstanceRayMarch.comp:810`). Repo-wide verification found **no** offscreen render target, resolution scale, checkerboard, or upsample path anywhere (the shader's `renderScale` is instance *world* scale). App-set `DISPATCH_X/Y` params are silently ignored — the live extent always wins. 1280x720 → 2560x1440 is exactly **4.0x heavy ray count = 4.0x GPU time on the frame-dominating pass**.

**A2. Shader debug counters compiled in unconditionally — HIGH (confirmed; corrected from critical).**
`BodyInstanceRayMarch.comp:147` hard-defines `ENABLE_SHADER_COUNTERS` (no spec-constant/build-variant guard). Every in-bounds pixel executes `recordRayStart()` (`:656`), `recordVoxelSteps(0u)` (`:820` — an `atomicAdd` of literal zero, pure waste), and `recordRayEnd()` (`:821`): 3-4 same-address atomic RMWs per pixel per frame into adjacent dwords of one cache line, in a buffer allocated `HOST_VISIBLE|HOST_COHERENT` (`ShaderCountersBuffer.cpp:75-77`), bound live at binding 8 (`BuildRenderGraph.cpp:1134-1139`). Nothing in `application/main` ever reads them (`ReadShaderCounters` is benchmark-only). ~11M contended atomics/frame at 1440p. Verified magnitude range: ~0.2-0.5ms/frame in the charitable warp-aggregated case, ~5-10ms/frame without aggregation — serialized atomics are a fixed-rate queue, so this cost hides under the parallel ray-march at small windows and *surfaces* at large ones, matching the symptom's texture.

**A3. The per-pixel constant itself (constituents of A1's slope):**
- **Procedural sphere trace (medium):** the default scene is 3 procedural bodies (`BuildRenderGraph.cpp:633-640`); `traceProceduralBody` runs up to 128 steps (`SdfRecipes.glsl:56`) with 3 transcendental `sin`s per eval for the displaced sphere and a Lipschitz step factor ≈0.366 (`:59`) that pushes silhouette rays toward the cap, plus a 6-eval central-difference gradient on hit (`:27-34`). Because all 3 default bodies are procedural, **this is the dominant per-pixel workload the user actually sees** (the ESVO path `continue`s at `BodyInstanceRayMarch.comp:683` and never runs in the default scene).
- **23-entry dynamically-indexed traversal stack (medium):** `StackEntry stack[23]` (184B) declared function-local and runtime-indexed (`ESVOTraversal.glsl:24, :243, :361-362`; declared at `BodyInstanceRayMarch.comp:505`) forces scratch/local memory or indexed-VGPR allocation for **every thread in the dispatch** (register/scratch allocation is static per pipeline, so even procedural-only frames pay the occupancy tax), and the full 23-entry init loop (`ESVOTraversal.glsl:121-124`) runs per (pixel × AABB-hit ESVO instance) despite only ~7-8 entries ever being live at `userMaxLevels=7`. Verified estimate: 0.1-1ms plus latency-hiding loss at 1440p; a 5-15% amplifier of ray-march cost.
- **MAX_ITERS=512 per instance (low in default scene / medium with stored bodies):** the 512-iteration cap (`ESVOTraversal.glsl:25`) is per `traverseOctreeInstanced` call, i.e. per AABB-overlapping instance per pixel with no shared budget and no bestT clip (`BodyInstanceRayMarch.comp:520, :770`). Zero cost in the default (all-procedural) scene; material in stored-SDF/undertow scenes where grazing pixels burn `512 × N` dependent 8-byte SSBO fetches.
- **No bestT occlusion early-out (low today, load-bearing for undertow):** `gridT.x` is never tested against `bestT` (`:737` tests only `gridT.y < 0`), so occluded instances still run full traversal, discarded at `:775`. With the default 3 non-overlapping bodies the waste is ~0; with 60-300 overlapping bodies it multiplies the per-pixel cost.

**Refuted superlinear suspects (important for expectations):** the stale LOD constant `raySizeCoef` (computed once from the initial height=500, `BuildRenderGraph.cpp:397-401`, never updated on resize) runs in the *cost-reducing* direction after enlarging — the too-fat cone terminates traversal ~1.5 levels early, so it partially **masks** the drop (it is a real under-detail *quality* bug, and fixing it will make large windows proportionally *more* expensive — bake this into before/after expectations). The swapchain-in-GENERAL compression concern was verified immaterial (~0.02-0.07ms delta at 1440p). The FIFO vsync-quantization mechanism was refuted **on the repro machine**: `binaries/stdout_log_3.txt:549-551` shows `Selected IMMEDIATE mode (uncapped FPS)`.

### Does plain linear pixel scaling + vsync quantization already explain a "drastic" drop?

**Linear scaling alone: yes.** Under IMMEDIATE present (confirmed active), once GPU-bound, FPS ∝ 1/pixel-area. Maximizing 720p→1440p quadruples the dominant pass: e.g. 120 FPS → ~30 FPS, or 60 → ~15. A 4x FPS cliff *is* "drastic" — no superlinear mechanism is required. The per-pixel constant is enormous (128-step transcendental sphere trace or ≤512-iter/instance octree traversal + 3-4 contended host-visible atomics + pick-ID store per pixel), so the absolute slope is steep on any hardware and brutal on iGPU/WSL paths.
**Vsync quantization: not on this machine.** IMMEDIATE is selected, so no 60→30→20 step function applies here. It remains a real amplifier on FIFO-only surfaces (compositors, WSLg/Dozen without IMMEDIATE/MAILBOX — `VulkanSwapChain.cpp:355-378`), where crossing one vblank of GPU time steps throughput down in display-rate divisors.
On top of the settled 4x, the atomic-counter serialization (A2) plausibly adds a further chunk that only becomes visible at large sizes, and the resize-transient storm below makes the transition *moment* feel catastrophic.

---

## (b) Per-frame-constant overhead (raises the floor at all sizes; ~0% of the size-dependent delta)

All confirmed but verifier-corrected to **low** for this symptom — they are resolution-invariant, so at large windows they shrink as a fraction of frame time:

- **Eager log-string construction:** `NODE_LOG_*` macros evaluate the message whenever the (always-constructed, disabled) logger is non-null (`NodeLogging.h:16-17`, `NodeInstance.cpp:42`, enabled check inside `Logger.cpp:44-49` *after* the string exists). ~120-180 discarded heap-allocating strings/frame across DescriptorSetNode/gatherers ≈ 0.05-0.15ms.
- **Lifecycle-hook broadcast:** flat per-phase hook list means every node's Execute invokes *every* registered hook (`GraphLifecycleHooks.cpp:65-90`); ~18-25 identity-filtered PreExecute hooks × ~28 nodes ≈ ~800 string builds + indirect calls/frame ≈ 0.05-0.25ms.
- **Validation layers defaulted ON for multi-config generators including Release** (`ProvisionVulkan.cmake:28-34`): the mechanism lens found this **dead in every current build tree** (all three caches are single-config Ninja); it is a latent footgun and a Debug-build measurement confound, not a live cost.
- Refuted-for-impact but real hygiene items: per-frame `vkUpdateDescriptorSets` of unchanged descriptors (`DescriptorSetNode.cpp:376-398`), by-value container copies through the slot system (`CompileTimeResourceSystem.h:979-984`), and the genuine **unbounded `perFrameImageInfos/BufferInfos` growth** (`DescriptorSetNode.cpp:888-889`, no `clear()`) — the latter is a time-correlated slow leak worth a one-line fix, not an area-correlated cause.
- The historical "~11ms per-frame constant dominates" prior (`Profiling.md:256-259`, 2025-12 suite) was **refuted as stale**: the newer 2025-12-28 finalized run on the same RTX 3060 shows frame time 8.08ms@720p → 19.18ms@1080p (2.38x for 2.25x pixels) — the per-pixel dispatch now dominates, pointing the same way as A1.

## (c) Resize-transient pathologies (hitching during the drag/maximize; one conditional sustained mode)

- **No resize debounce (medium):** every GLFW size event → `WindowResizedMessage` → SwapChainNode recompile per Update tick (`WindowNode.cpp:257-264/144-169`, `SwapChainNode.cpp:41-52`, `VulkanGraphApplication.cpp:345`). Each cycle destroys+recreates the **VkSurfaceKHR itself** (`SwapChainNode.cpp:433`), creates the swapchain with `oldSwapchain=VK_NULL_HANDLE` (`VulkanSwapChain.cpp:422`, no image recycling), re-queries caps/formats, rebuilds per-image sync, then dirties every transitive dependent (`RenderGraph.cpp:1631-1733`) including PickIdTargetNode's compile-time `vkQueueSubmit + vkQueueWaitIdle` full-queue drain (`PickIdTargetNode.cpp:248-258`). Tens-to-hundreds of ms per size event, repeated throughout an interactive drag.
- **Wave-based recompile is O(depth²) (medium):** after each node recompiles, the **transitive** dependent set is re-marked dirty (`RenderGraph.cpp:1722`, `CleanupStack.h:117-126`), so depth-k nodes recompile ~k times per event, and on the paused/maximize path the swapchain itself is destroyed and recreated **twice** per resize (WindowNode and SwapChainNode both start dirty).
- **SUBOPTIMAL acquire → full recreation, no extent check (medium; the tail risk):** every `VK_SUBOPTIMAL_KHR` acquire calls `MarkNeedsRecompile()` with no "extent unchanged, skip" guard, debounce, or one-shot (`SwapChainNode.cpp:354-357`; in-code comment: SUBOPTIMAL "seen routinely on Mesa Dozen/WSL2"). On conformant drivers recreation at `currentExtent` clears SUBOPTIMAL and this is a one-shot transient; if the driver keeps reporting SUBOPTIMAL at the new extent, this becomes a **surface+swapchain recreation + transitive recompile cycle every frame** — the only mechanism found that would produce a sustained post-resize collapse beyond pixel scaling. Discriminator: repeated `===== RECOMPILATION TRIGGERED =====` lines after the resize settles.
- **Console-log burst (low):** each recompile cascade emits dozens of `endl`-flushed, timestamp-formatted INFO lines into an unbounded `logEntries` vector (`Logger.cpp:51-63`; burst sites `RenderGraph.cpp:1587-1593/1690/1728`) — ~5-50ms of console I/O per resize event on a Windows console; zero steady-state cost.
- Related correctness (not perf): the **pick-ID ring images are never recreated on resize** (`PickIdTargetNode.cpp:76` "only create once"), so after maximizing every pixel outside the startup extent is an out-of-bounds storage write (UB) and click readback reads outside the image. Verified immaterial to FPS, but fix it (followSwapchainExtent) alongside the resize work.

---

## Causal story, in one paragraph

At the moment of maximize the user gets hit twice. **During** the resize: a full surface+swapchain recreation with a quadratic recompile cascade and a console-log burst per size event — multi-frame stalls exactly while dragging. **After** it settles: the ray-march dispatch tracks the swapchain extent 1:1 with no render-scale escape hatch, so GPU work — an expensive procedural sphere trace (default scene) or per-instance ESVO traversal, plus ~3-4 wasted same-address host-visible atomics per pixel, plus the static occupancy tax of the 23-entry scratch stack — scales exactly with window area: 4x the pixels, ~4x the frame time, FPS/4 under the confirmed IMMEDIATE present mode. That linear cliff alone is "drastic"; the counter atomics likely steepen it (their serialization only surfaces once thread count is large), and if the platform is Dozen/WSL2 the SUBOPTIMAL loop can convert the transient storm into a permanent one. The fix hierarchy follows directly: decouple render resolution from the swapchain (A1) and delete the counter waste (A2) for the settled drop; add extent-guarded/debounced recreation and single-pass topological recompile for the transition; take the shader-constant reductions (procedural trace, stack sizing, shared iteration budget, bestT cull) as slope reducers, the last two doubling as prerequisites for the many-body undertow path.

## Measurement traps (fix/verify BEFORE trusting numbers)

1. **Per-node GPU timing is dead for every slot except 0**: `GPUTimestampQuery.cpp:182` hardcodes a 2-query read while VoxelGridNode's memory logger takes slot 0, so ComputeDispatchNode's timer (queries 2/3) is never fetched and `GetLastDispatchMs()` is 0 forever; **BenchmarkRunner then fabricates `gpuTimeMs = 0.9 × CPU frameTime`** (`BenchmarkRunner.cpp:1346-1353`) and derives Mrays/s from it. Fix the per-slot read first; until then only CPU frame time is real.
2. Benchmark default is **headless with synthetic metrics** (hardcoded 16.67ms) — `--render` is mandatory; `--quick` ignores `--width/--height`.
3. The benchmark's per-frame counter readback fence-waits every frame (`BenchmarkRunner.cpp:1366-1405`) — comment it out for pipelined timings.
4. Keep the removed per-frame `[KI004-DIAG]` `NODE_LOG_ERROR` (formerly `ComputeDispatchNode.cpp:175-178`) out of any measurement build.
5. Fixing `raySizeCoef` staleness will legitimately *increase* large-window cost — record it as a known step in before/after comparisons.


---

## Appendix: per-milestone gate measurements

### M1 gate (2026-07-03, worktree Release, real GPU, fresh windows, 600 frames)

D1 fixed — swapchain now tracks the requested size; first honest cross-size scaling:

| Requested | Swapchain | Dispatch GPU | Frame avg | FPS | p99 |
|---|---|---|---|---|---|
| 500x500 | 500x500 | 0.14 ms | 1.75 ms | 573 | 9.0 ms |
| 1920x1080 | 1920x1080 | 1.22 ms | 6.43 ms | 156 | 23.2 ms |
| 2560x1440 | 2560x1440 | 2.18 ms | 7.63 ms | 131 | 26.1 ms |

Mrays/s ~constant (~2000) => clean linear pixel scaling; 0 recompilation storms. NOTE: at 1440p the ray-march is only ~2.2 ms of a ~7.6 ms frame — non-dispatch cost (~5.4 ms) + chronic p99 spikes (26 ms) now dominate; M5's attribution targets exactly that.

### M2 gate (2026-07-03, 2560x1440 fresh window, 600 frames, vs M1 row)

Counters compiled out of the live shader (rank 2):

| Metric | M1 (counters in) | M2 (counters out) | Delta |
|---|---|---|---|
| Dispatch GPU | 2.18 ms | 1.38 ms | -37% |
| Mrays/s | 1917 | 2700 | +41% |
| Frame avg | 7.63 ms | 3.76 ms | -51% |
| FPS | 131 | 266 | +103% |
| p99 | 26.1 ms | 17.3 ms | -34% |

Frame-time halving exceeds the dispatch delta — the HOST_VISIBLE|HOST_COHERENT atomic traffic taxed the whole pipeline, not just the dispatch. Rank-2's "10-25%" estimate was conservative on this GPU. API note: ShaderBundleBuilder::SetStageDefines does whole-word token substitution (cannot inject `#ifdef` defines) — counters re-enable only by hand-editing the shader.

### M3 gate (2026-07-03, resize probe 500x500 -> 2560x1440 @ frame 600, real GPU)

Resize-path robustness (ranks 3/7/8 + rank-5 correctness):

| Metric | Pre-M3 baseline | Post-M3 |
|---|---|---|
| Transition-window p99 | 40.8 ms | 25.5 ms |
| Recompilations per resize | 1 | 1 (now deduped: 10 nodes, each exactly once) |
| In-use teardown VUIDs | 10 + segfault (exposed on lavapipe) | 0 |
| Post-resize steady | ~4.8 ms / 210 FPS | ~4.8 ms / 205-215 FPS |

Bonus root-cause fix: recreation waves never waited for in-flight GPU work before teardown (pre-existing; masked by luck until M3 made the wave deterministic). Fix: WaitForGraphDevicesIdle gated on pausedForRecreation_ — ordinary recompiles stay wait-free; validator empirically confirmed the OUT_OF_DATE acquire publishes PAUSE_START before every resize wave.

Follow-ups recorded (pre-existing, NOT M3 regressions): (a) ~10-20x VUID-vkCmdDispatch-None-08114 at startup frames 0-99; (b) one-frame ~10-20x VUID-vkCmdBindPipeline-pipeline-parameter burst right after the resize recompile (descriptor rebind timing — M5 candidate); (c) MINOR: QueryCurrentSurfaceExtent lacks the 0xFFFFFFFF undefined-extent guard (Wayland-only storm risk — folded into M4); (d) post-resize steady frames ~1 ms slower than fresh-window at the same extent (M5 attribution target); (e) NODE_LOG_INFO lines from SwapChainNode/PickIdTargetNode don't surface in app logs in this config (observability gap).

### M4 gate (2026-07-04, 2560x1440 fresh window, 600 frames, real GPU)

Render-scale decoupling (rank 1) + honest LOD (rank 6):

| Metric | scale 1.0 | scale 0.5 | ratio |
|---|---|---|---|
| Dispatch GPU | 1.40 ms | 0.38 ms | 27% (acceptance: 25-30%) |
| Render target | 2560x1440 | 1280x720 | correct |
| Frame avg / FPS | 5.80 ms / 172 | 3.26 ms / 306 | +78% FPS |
| raySizeCoef | 0.000545 @1440 | 0.001091 @720 | exact 1/height |

Notes: scale-1.0 frame is 5.8 ms vs M2's 3.76 ms — the predicted rank-6 correction (the frozen 500px raySizeCoef had been under-detailing large windows; LOD is now honest and render scale is the knob that buys the cost back). Bonus fix: the GENERAL-transition barrier hardcoded oldLayout=UNDEFINED (wrong for steady-state render-target re-entry) — replaced with per-image first-use tracking; validator confirmed fresh post-resize handles correctly re-treat as UNDEFINED. p99 remains 18-20 ms — M5's target. Visual gate: full-window image at 0.5, softer (user-eyeball + construction proof; no PNG hook in the app — observability gap noted).

### M5 gate (2026-07-04, lavapipe WSL; instrumentation + one confirmed fix + attribution)

**M5.1 (instrumentation):** `UIRenderNode` gained a `GPUPerformanceLogger` (mirrors `ComputeDispatchNode`'s: shared `GPUQueryManager` slot, `BeginFrame`/`RecordDispatchStart`/`RecordDispatchEnd` bracketing the render pass, `CollectResults` after the frame-fence wait). Confirmed live: `ui_composite_render_GPUPerf` allocates its own slot (slot 2, distinct from compute's slot 1/3) and logs the same "GPU timestamp queries enabled" init line as the compute logger. Caveat: on this lavapipe ICD, neither logger's periodic Dispatch-summary line ever fires in any run (300-600 frames) — `GPUQueryManager::TryReadTimestamps`/`ReadResults` apparently never resolves here for the ALREADY-SHIPPED `ComputeDispatchNode` timer either, so this is a pre-existing lavapipe limitation, not new breakage. GPU-level attribution is therefore CPU-only in this environment. `main.cpp`'s frame-timer block gained outlier logging: any frame costing >3x the previous 120-frame window's median logs `"[FrameTimer] OUTLIER frame N: X.XXX ms"` immediately, instead of only surfacing in the next window's avg/p99. This instrumentation is what caught the M5.3 finding below.

**M5.2 (confirmed fix, rank 9):** `DescriptorSetNode::BuildDescriptorWrites`'s `imageInfos`/`bufferInfos` parameters (the caller's persistent `perFrameImageInfos[i]`/`perFrameBufferInfos[i]`, one sub-vector per swapchain image) were `reserve()`d against their current size but never `clear()`d before each frame's `Handle*()` calls `push_back()`ed onto them — every `Execute()` call appended on top of every prior frame's entries for that image index. Fixed: `clear()` both vectors before the `reserve()` (capacity retained, so steady state incurs no repeated reallocation); mirrors the sibling `perFrameAccelInfos`/`perFrameAccelHandles`, which already did this correctly. Verified via a temporary stderr size-probe soak (800x600, 500 frames, removed before commit): without the fix, `bufferInfos.size()` grew linearly (16 at frame 1 -> 912 at frame 451, unbounded); with the fix, flat at 8 across all 500 frames. RenderGraph test suite green post-fix (descriptor_resource_gatherer_node, gpu_query_manager[_integration], resource_gatherer, render_target_node, pass_group_node_smoke).

**M5.3 (attribution):** resize probe 500x500 -> 1280x720 @ update-tick 150, 600 frames, lavapipe:

| Window | avg | FPS | p99 |
|---|---|---|---|
| 0-120 (pre-resize, 500x500) | 10.99 ms | 91.0 | 12.3 ms |
| 120-240 (transition, 1280x720) | 10.98 ms | 91.1 | 24.4 ms |
| 240-360 (post-resize steady) | 11.84 ms | 84.4 | 16.9 ms |
| 360-480 (post-resize steady) | 11.15 ms | 89.7 | 16.1 ms |
| 480-600 (post-resize steady) | 11.53 ms | 86.8 | 17.9 ms |

One OUTLIER fired: `frame 154: 28.741 ms` (4 frames after the resize event at tick 150) — no others in 600 frames. The outlier lands exactly on the L2 VUID burst (below); no GPU-summary lines were available to attribute UI-pass-vs-dispatch split (M5.1 caveat), so this is attributed by log correlation, not a GPU timer read.

- **L2 (root cause found, NOT fixed — out of bounded scope):** the "descriptor rebind timing" VUID burst is a **cache-key granularity mismatch between two shared cachers in `CashSystem`**, not a node-ordering bug. `PipelineLayoutCacher::ComputeKey` correctly hashes the live `VkDescriptorSetLayout` handle, so a resize's fresh layout IS a cache miss there and a new `VkPipelineLayout` is created. But `ComputePipelineCacher::ComputeKey` (`ComputePipelineCacher.cpp:47-56`) hashes only `shaderKey` (SHA256 of the SPIR-V) and `layoutKey` (`ComputePipelineNode.cpp:201`: `shaderBundle->uuid + "_pipeline_layout"`, a STRING tied to the shader's descriptor *interface*, not the live layout handle) — both resize-invariant, since the shader and its interface don't change across a resize. So `ComputePipelineCacher::GetOrCreate` returns the SAME cached `ComputePipelineWrapper` from before the resize, whose `pipelineLayoutWrapper` still points at the layout `PipelineLayoutCacher` just destroyed-and-replaced. `ComputeDispatchNode::BindComputePipeline` binds that stale-but-cache-hit pipeline for exactly the first post-recompile frame, producing the observed `VUID-vkCmdBindPipeline-pipeline-parameter: ... references deleted object VkDescriptorSetLayout ...` burst (confirmed self-healing: no crash, no recurrence, clean 600-frame run). `DescriptorSetNode::CleanupImpl`/`ComputePipelineNode::CleanupImpl` also unconditionally destroy their pool/layout/shader-module/pipeline on every recompile (no `CleanupReason::Recompile` persistence guard — the KI-004 pattern was never applied to these two nodes), which is what makes the layout handle churn on every resize in the first place. Real fix requires either folding the live layout handle into `ComputePipelineCacher`'s key or invalidating pipeline-cache entries when their backing layout is torn down — both touch shared cache infrastructure beyond one node, so per the milestone's bounded scope ("DO NOT restructure descriptor architecture") this is filed as a follow-up, not fixed this session. Reproduction: `VIXEN_RESIZE_AT_FRAME=150 VIXEN_RESIZE_WIDTH=1280 VIXEN_RESIZE_HEIGHT=720` on any live/validation-enabled run.
- **L1 (inconclusive, not fixed):** post-resize steady windows in this run (11.15-11.84 ms) vs pre-resize (10.99 ms) show a residual, but this specific probe resizes 500x500 -> 1280x720 (different render extents by design), so the delta is confounded with "more actual GPU/UI work at a larger resolution" (M4 already established dispatch cost scales with extent) — not cleanly isolable from a true lingering-cost bug without a same-extent before/after comparison, which the GPU-summary gap (M5.1 caveat) also blocks on this ICD. Minor secondary contributor found but not fixed: `ComputeDispatchNode::seenRenderTargetImages_` (a `std::set<VkImage>`) accumulates stale, since-destroyed image handles across repeated resizes with no pruning — real but small (a handful of `std::set` entries, `O(log n)` lookup) relative to the observed ~1 ms, not the dominant driver. Verdict: inconclusive with current instrumentation; re-attempt with a same-extent pre/post probe and (if the GPU-summary gap is fixed) real GPU-ms readouts before spending more budget here.

### M5 gate (2026-07-04, real GPU, 2560x1440)

p99 hitch attribution + rank-9 fix + Critical GPU-timing regression caught and fixed mid-milestone:

| Metric | Before M5 | After M5.2 (growth fix) | After Critical fix (1d86c3ef) |
|---|---|---|---|
| Steady p99 | 17-26 ms | ~11.5-12.6 ms (lavapipe, pre-restart) | 15-16 ms (real GPU) |
| Frame avg | ~4.8 ms | ~3.7-4.0 ms | 3.9-4.5 ms |
| L1 residual (post-resize vs fresh) | +1 ms | gone | gone |
| Dispatch: GPU summaries | present | present | RESTORED after Critical fix (were 0 mid-milestone) |
| bufferInfos growth (500-frame soak) | 16->912 unbounded | flat 8 | flat 8 |
| OUTLIER lines (noise floor) | 195 (mostly sub-5ms) | - | 114 (all genuine 11-18ms tail latency, 0 sub-5ms) |

**Critical caught mid-milestone**: M5.1's UIRenderNode GPU timer made it a 2nd per-frame GPUQueryManager::BeginFrame caller; BeginFrame did a WHOLE-POOL reset, so UI's call (after compute in the same frame) wiped ComputeDispatchNode's timestamps -> real-GPU Dispatch: summaries vanished (0 lines). Root-fixed (not a band-aid): BeginFrame changed to per-slot reset (ResetQueryRange on just that slot's query pair) -> multi-consumer coordination problem removed structurally, no "one owner" convention needed. Opus-validated twice (found the issue, then approved the fix) with a regression test proving slot isolation.

**L2 (filed, not fixed — shared CashSystem infra, correctly out of bounded scope)**: ComputePipelineCacher::ComputeKey hashes shaderKey + a resize-invariant layout STRING while PipelineLayoutCacher keys the live VkDescriptorSetLayout handle -> one-frame stale-pipeline-cache hit after every resize (the VUID-vkCmdBindPipeline burst). Also noted: DescriptorSetNode/ComputePipelineNode CleanupImpl destroy unconditionally on every recompile (no CleanupReason::Recompile persistence guard — same class as KI-004). Both -> Known-Issues follow-ups at close-out.

**Remaining real-GPU tail**: 114 outlier frames in the 11-18ms range persist post-fix — genuine driver/compositor jitter, not the rank-9 leak (which is fully closed). Not chased further; outside this milestone's bounded scope.
