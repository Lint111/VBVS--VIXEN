# Widescreen Perf-Fix Implementation Plan (2026-07)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This plan is executed via the post-brainstorm-context-manager pipeline: one milestone per worker, validated per milestone, progress persisted into this doc.

**Goal:** Eliminate the drastic FPS drop when the VIXEN window is enlarged ("wide screen mode"), by fixing the verified bottlenecks from `Widescreen-Perf-Sweep-Findings-2026-07.md` (the spec of record for this plan — READ IT FIRST).

**Architecture:** Six milestones, ordered so each is independently shippable: (M1) parameter-integrity fix so the app actually controls window size; (M2) compile the never-read per-pixel shader-counter atomics out of the live shader; (M3) resize-path robustness (SUBOPTIMAL guard, persistent surface, oldSwapchain reuse, wave dedup, pick-ID recreation); (M4) render-scale decoupling — ray-march into an offscreen target at `scale × extent` and blit to the swapchain; (M5) attribute + fix the post-resize p99 hitches (incl. the confirmed DescriptorSetNode unbounded growth); (M6) CPU-floor hygiene (lazy log macros, keyed lifecycle hooks).

**Tech Stack:** C++23, Vulkan 1.3, CMake/Ninja (preset `vixen-wsl` for compile+unit-test gates AND real-GPU app runs via Mesa Dozen — NOT lavapipe; `build/ninja-release` MSVC for native-Windows real-GPU gates), GLFW, glslang runtime shader compile, GoogleTest.

## Global Constraints

- **Read the spec first:** `Vixen-Docs/01-Architecture/Widescreen-Perf-Sweep-Findings-2026-07.md` — every milestone cites its findings (D1-D3, ranks 1-13).
- **NEVER use lavapipe, under any condition** (standing rule, 2026-07-04 — another agent is removing lavapipe support from the codebase entirely; treat it as gone). Do NOT set `VK_ICD_FILENAMES` to `lvp_icd.json`. WSL app runs are still fine — just target a REAL GPU: WSL's real-GPU path is Mesa Dozen (Vulkan-over-D3D12, `/dev/dxg`), gated by `-DVIXEN_AUTO_PROVISION_WSL_VULKAN=ON` on the `vixen-wsl` configure (default OFF; one-time from-source Mesa build, slow but a one-time cost — see `cmake/ProvisionWslVulkan.cmake`). Once provisioned, run with `VK_ICD_FILENAMES=<cache>/dzn_icd.json` (the exact path is cached at `VIXEN_WSL_DZN_ICD` after a successful provision — check CMake cache or the provision log for it) instead of the lvp ICD; leaving `VK_ICD_FILENAMES` unset lets the loader enumerate all installed ICDs (Dozen among them) which also works.
- **Live-run gate is authoritative for GPU work.** Static review repeatedly passes runtime bugs in this codebase. Every milestone ends with an actual real-GPU app run (WSL/Dozen and/or native-Windows Release), not just tests.
- **Build (WSL dev loop):** `ninja -C /mnt/c/cpp/VBVS--VIXEN/build/wsl <target>` (`VIXEN`, `RenderGraphCore`, test targets — ALWAYS name targets, never bare `ninja`; ALWAYS `-j4` on this box, higher parallelism crashes cc1plus; exactly ONE ninja invocation at a time, concurrent invocations corrupt `.ninja_log`). Tests: the gtest binaries under `build/wsl/libraries/RenderGraph/tests/`, run directly.
- **Real-GPU perf gate (Windows Release):** rebuild via `cmd.exe /c 'temp\win_rebuild_release.bat'` from `VIXEN/`; run probes via `cmd.exe /c 'temp\run_resize_probe.bat'` / `temp\run_perf_matrix.bat`; outputs land in `VIXEN/temp/perf_*.out` + `.applog.txt`. WSL env vars do NOT reach `.exe` — set env inside the `.bat`.
- **Perf numbers protocol:** every perf-relevant milestone records before/after `[FrameTimer]` avg/p99 + `Dispatch: … ms avg` at 800×600-equivalent and 2560×1440 into the findings doc appendix (M-appendix table).
- **Prefer architecturally pure fixes** (user rule): fix root causes in the owning component, never call-site band-aids alone.
- **Nodes use the base `NodeInstance` device member** (SetDevice/GetDevice), never a private `device_`.
- **Do not break `vixen_benchmark`** (it is a separate graph/shader path; it must keep compiling and running).
- **Commit at the end of every task** (milestone workers: `git add` only files you touched; message prefix `perf(widescreen):`). Do not push.
- **std140/std430 gotcha** (from project memory): if you touch shader SSBO/UBO blocks, re-check offsets against the reflection tests.

---

## Milestone M1 — Parameter integrity: the app must control its window size

**Fixes:** finding D1 (fresh windows silently 800×600; `int` vs `uint32_t` variant mismatch silently defaults).

### Task M1.1: Integral-tolerant, loud-on-mismatch `GetParameterValue`

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/include/Data/NodeParameterManager.h:55-66`
- Create: `VIXEN/libraries/RenderGraph/tests/Core/test_node_parameter_manager.cpp`
- Modify: `VIXEN/libraries/RenderGraph/tests/CMakeLists.txt` (register the new test — mirror the registration of `test_gpu_query_manager`)

**Interfaces:**
- Produces: `NodeParameterManager::GetParameterValue<T>` — same signature, new semantics: exact variant match wins; otherwise `int32_t`↔`uint32_t` values convert when in-range; any other mismatch logs one `stderr` warning naming the parameter and returns the default.

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "Data/NodeParameterManager.h"

using Vixen::RenderGraph::NodeParameterManager;  // adjust to the actual namespace in the header

TEST(NodeParameterManager, Int32StoredUInt32ReadConverts) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", 1280);                       // stores int32_t alternative
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 1280u);
}

TEST(NodeParameterManager, UInt32StoredInt32ReadConverts) {
    NodeParameterManager mgr;
    mgr.SetParameter("count", 42u);                        // stores uint32_t alternative
    EXPECT_EQ(mgr.GetParameterValue<int32_t>("count", -1), 42);
}

TEST(NodeParameterManager, NegativeInt32DoesNotConvertToUInt32) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", -5);
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 800u);  // default, not wraparound
}

TEST(NodeParameterManager, GenuineMismatchReturnsDefault) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", std::string("wide"));
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 800u);
}
```

- [ ] **Step 2: Run to verify it fails** (first two tests fail against current strict `get_if`)
- [ ] **Step 3: Implement** in `NodeParameterManager.h` (replace lines 55-66):

```cpp
    template<typename T>
    T GetParameterValue(const std::string& name, const T& defaultValue = T{}) const {
        auto it = parameters.find(name);
        if (it == parameters.end()) {
            return defaultValue;
        }

        if (auto* value = std::get_if<T>(&it->second)) {
            return *value;
        }

        // int32<->uint32 is the canonical silent failure: call sites store int literals,
        // nodes read uint32_t (see Widescreen-Perf-Sweep-Findings-2026-07 D1 — this
        // defaulted every fresh window to 800x600). Convert when the value is in range.
        if constexpr (std::is_same_v<T, uint32_t>) {
            if (auto* v = std::get_if<int32_t>(&it->second)) {
                if (*v >= 0) return static_cast<uint32_t>(*v);
            }
        } else if constexpr (std::is_same_v<T, int32_t>) {
            if (auto* v = std::get_if<uint32_t>(&it->second)) {
                if (*v <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
                    return static_cast<int32_t>(*v);
            }
        }

        // A silent default here cost weeks of "why is the window 800x600" — be loud.
        std::fprintf(stderr,
            "[NodeParameterManager] WARNING: parameter '%s' stored with a different type "
            "than requested — returning default\n", name.c_str());
        return defaultValue;
    }
```
(Add `#include <cstdio>`, `#include <limits>`, `#include <type_traits>` to the header if absent.)

- [ ] **Step 4: Run the new test target — all 4 pass; run the full RenderGraph test suite — no regressions**
- [ ] **Step 5: Commit**

### Task M1.2: Explicit `uint32_t` at the window call sites + live gate

**Files:**
- Modify: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:259-260`

- [ ] **Step 1:** Change the two SetParameter calls to pass `static_cast<uint32_t>(width)` / `static_cast<uint32_t>(height)` (explicit even though M1.1 now converts — call sites should store the type the reader expects).
- [ ] **Step 2:** Grep for other integer `SetParameter(` call sites in `application/` and `libraries/RenderGraph/src/` whose reader uses a different integral width (`grep -rn "SetParameter(" | grep -v uint32_t`, then check each reader's `GetParameterValue<...>` type). Fix each the same way. List what you changed in the commit message.
- [ ] **Step 3: Live gate (lavapipe):** from `VIXEN/binaries/`: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VIXEN_WINDOW_WIDTH=1280 VIXEN_WINDOW_HEIGHT=720 VIXEN_EXIT_AFTER_FRAMES=150 ./VIXEN`
  Expected: dispatch-dims log shows `160x90x1` AND the GPU summary in the extracted log (`binaries\vulkan_app_log.txt`, literal backslash filename on WSL) shows `Resolution: 1280x720` (NOT 800x600). This is the D1 proof.
- [ ] **Step 4: Real-GPU gate:** rebuild Release (`cmd.exe /c 'temp\win_rebuild_release.bat'`), re-run `temp\run_perf_matrix.bat`, confirm the four runs now show four DIFFERENT resolutions and scaling dispatch times. Record the table in the findings-doc appendix.
- [ ] **Step 5: Commit**

---

## Milestone M2 — Compile the shader counters out of the live app

**Fixes:** rank 2 (3-4 same-address HOST_VISIBLE atomics per pixel per frame, never read by the live app; `recordVoxelSteps(0u)` is an atomicAdd of literal zero).

### Task M2.1: Gate `ENABLE_SHADER_COUNTERS` behind an injected define

**Files:**
- Modify: `VIXEN/shaders/BodyInstanceRayMarch.comp:147` (the hard `#define ENABLE_SHADER_COUNTERS`) and the counter call sites `:656` (`recordRayStart`), `:820-821` (`recordVoxelSteps(0u)`, `recordRayEnd`) — plus the binding-8 SSBO declaration block (near the ShaderCounters include at `:149`).
- Modify: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:454-463` (shader-builder registration — do NOT add the define here; live app compiles counters OUT) and `:1134-1139` (binding-8 gatherer wiring — make conditional/remove).

**Interfaces:**
- Produces: shader honors an externally injected `ENABLE_SHADER_COUNTERS` preprocessor define (ShaderBundleBuilder supports preprocessor defines — see `ShaderManagement`'s builder API). Without it: no counters SSBO in the SPIR-V interface, no atomics.

- [ ] **Step 1:** Delete line 147's hard `#define ENABLE_SHADER_COUNTERS`. Verify `ShaderCounters.glsl` internally guards its buffer declaration AND function bodies with `#ifdef ENABLE_SHADER_COUNTERS` (it declares no-op stubs otherwise — if it does not, add the `#else` no-op stubs so call sites compile either way).
- [ ] **Step 2:** Delete the `recordVoxelSteps(0u)` call at `:820` outright (it adds zero — pure waste even when counters are on; real step-counting sites pass non-zero elsewhere).
- [ ] **Step 3:** Make the app-side binding-8 wiring conditional: the descriptor layout is reflected from SPIR-V, so with counters compiled out binding 8 no longer exists — remove the counter-buffer connection at `BuildRenderGraph.cpp:1134-1139` (and the ShaderCountersBuffer node creation that feeds it, if the live graph creates one — trace it; keep the class, it stays available for metrics builds). If a `VIXEN_SHADER_COUNTERS=1` env opt-in is trivial to thread through (inject the define + keep the wiring), do it; otherwise removal is fine — the benchmark uses a different shader.
- [ ] **Step 4: Live gate (lavapipe):** run 150 frames; expected: shader compiles at runtime (glslang), image renders (the counters don't affect the image — any pixel diff vs pre-change = failure), no Vulkan errors in the log.
- [ ] **Step 5: A/B perf gate (real GPU):** Release rebuild; `run_resize_probe.bat`; record post-resize `Dispatch: … ms avg` at 2560×1440 before/after this milestone in the findings appendix. (Expectation on this GPU: modest; the point is removing waste + the superlinear hazard on weaker/host-visible-atomics hardware.)
- [ ] **Step 6: Commit**

---

## Milestone M3 — Resize-path robustness

**Fixes:** rank 3 (SUBOPTIMAL recreation loop hazard), rank 7 (no debounce; surface + swapchain fully recreated per event; `oldSwapchain=VK_NULL_HANDLE`), rank 8 (wave cascade recompiles deep nodes multiple times), rank 5's correctness half (pick-ID ring never recreated on resize → OOB `imageStore` after maximize).

### Task M3.1: SUBOPTIMAL guard — recreate only on a real extent change

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp:343-358` (AcquireNextImage SUBOPTIMAL branch)

- [ ] **Step 1:** In the `VK_SUBOPTIMAL_KHR` branch, before `MarkNeedsRecompile()`: query `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` for the surface's `currentExtent`; if it equals the live swapchain extent (`swapChainWrapper->scPublicVars.Extent`), log once at DEBUG and do NOT mark recompile (the swapchain is already correctly sized — recreating it can never clear the flag and previously looped forever on Dozen-class drivers). If extents differ, keep the existing behavior and log at INFO: `"[SwapChainNode] SUBOPTIMAL with extent change WxH -> W2xH2 — recreating"`.
- [ ] **Step 2:** Add an INFO log at the actual swapchain-recreation site in CompileImpl (`"[SwapChainNode] swapchain (re)created WxH"`) so any recreation storm is immediately visible in logs. (This is the discriminator the measurement plan greps for.)
- [ ] **Step 3: Test:** run the lavapipe resize probe (`VIXEN_RESIZE_AT_FRAME=100 VIXEN_RESIZE_WIDTH=900 VIXEN_RESIZE_HEIGHT=700 VIXEN_EXIT_AFTER_FRAMES=400`): expected exactly ONE `swapchain (re)created` after the probe fires and ZERO in the following 200+ frames.
- [ ] **Step 4: Commit**

### Task M3.2: Persistent surface + oldSwapchain reuse

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/SwapChainNode.cpp` (CompileImpl surface path, `:433` `LoadExtensionsAndCreateSurface` + `:463-506` recreate sequence)
- Modify: `VIXEN/libraries/VulkanResources/src/VulkanSwapChain.cpp:244-249` (unconditional `glfwCreateWindowSurface`), `:422` (`scInfo.oldSwapchain = VK_NULL_HANDLE`)

- [ ] **Step 1:** Make surface creation idempotent: if `scPublicVars.surface != VK_NULL_HANDLE`, reuse it (WindowNode already keeps the OS window + surface persistent across recompiles — SwapChainNode destroying/recreating the surface each compile defeats that design; see the WindowNode comment at `WindowNode.cpp:62-65`). Destroy the surface only in real teardown (CleanupReason != Recompile).
- [ ] **Step 2:** Pass the previous swapchain as `scInfo.oldSwapchain` in `VulkanSwapChain::CreateSwapChain`, and destroy the OLD handle after successful creation (keep it in a local; the driver can then recycle images). Mind the destruction order vs. per-image views/semaphores: destroy views/sync of the old chain after `vkCreateSwapchainKHR` succeeds, before overwriting the stored handle.
- [ ] **Step 3: Live gates:** (a) lavapipe resize probe — clean resize, no validation errors (run with the layer if installed); (b) real-GPU resize probe — transition-window `[FrameTimer]` p99 recorded before/after in the findings appendix (expected: transition hitch shrinks vs the 40.8 ms baseline).
- [ ] **Step 4: Commit**

### Task M3.3: Wave-cascade dedup — each node recompiles once per wave

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Core/RenderGraph.cpp:1631-1733` (`RecompileDirtyNodes` wave loop)
- Test: extend an existing RenderGraph recompile unit test (find the test covering `RecompileDirtyNodes` / dependents marking in `libraries/RenderGraph/tests/` — e.g. the resize/recompile suite) with a diamond-dependency case.

- [ ] **Step 1: Write the failing test:** graph A→B, A→C, B→D, C→D; mark A dirty; count `Compile()` calls per node via the node's compile counter (or a test hook). Expected after fix: D compiles exactly once. Current behavior: D compiles per-parent (multiple times).
- [ ] **Step 2:** Implement: maintain a `std::unordered_set<NodeInstance*> recompiledThisWave`; when the wave loop pops a node already in the set, skip it; when `GetAllDependents` re-marks a node already recompiled this wave, allow it only if it was recompiled BEFORE its dependency in topological order (simplest correct form: compute the full dirty transitive closure first, then recompile once in topological order — the closure + topo sort already exist in `GraphTopology.cpp`).
- [ ] **Step 3:** Run the new test (passes) + full RenderGraph suite (no regressions) + lavapipe resize probe (single `swapchain (re)created`, render still correct).
- [ ] **Step 4: Commit**

### Task M3.4: Pick-ID ring follows the swapchain extent

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/PickIdTargetNode.cpp:74-76` ("persistent across recompile — only create once") and its CompileImpl.

- [ ] **Step 1:** Store the extent the ring was created at; in CompileImpl, when the incoming extent differs, destroy and recreate the ring images at the new extent (the existing compile-time `vkQueueSubmit + vkQueueWaitIdle` drain at `:245-255` already guarantees safety and now only runs on genuine recreation). Log `"[PickIdTargetNode] ring recreated WxH"`.
  **Mechanism constraint:** the trigger is the STANDARD resize flow — `WindowResizedMessage` → SwapChainNode recompile → dependents-cascade reaches this node's CompileImpl. The current "only create once" comment is precisely a node opting OUT of the recompilation phase; the fix is to rejoin it. Do NOT poll the extent in ExecuteImpl and do NOT add a new event type.
- [ ] **Step 2: Live gate:** lavapipe resize probe, then grep: ring recreation log matches the swapchain extent after the resize (no more OOB `imageStore` at enlarged windows — this was undefined behavior before this task).
- [ ] **Step 3: Commit**

---

## Milestone M4 — Render-scale decoupling (the architectural fix, rank 1)

**Fixes:** rank 1 (ray-march hard-locked to window extent; no render-scale knob), rank 6 (stale `raySizeCoef` frozen at the 500-px init height — a live LOD-correctness bug), the swapchain-storage write tax (verified minor, removed for free), and the pick-coordinate mapping that scale introduces. **Read `Vixen-Docs/01-Architecture/RenderTarget-Design-2026-06.md` and the auto-sync docs before starting.**

**Design (locked):** the compute pass writes an offscreen `RenderTargetNode` storage image sized `ceil(swapchainExtent × renderScale)` (`renderScale ∈ (0,1]`, param + `VIXEN_RENDER_SCALE` env, default 1.0). `ComputeDispatchNode` then records a `vkCmdBlitImage` (LINEAR filter) from the offscreen image to the swapchain image, with node-managed barrier2 transitions (offscreen GENERAL→TRANSFER_SRC_OPTIMAL, swapchain →TRANSFER_DST_OPTIMAL→ then GENERAL for the UI composite pass, preserving today's `leaveImageInGeneral` contract). Dispatch dims, shader `imageStore` bounds, pick-ID extent, and `raySizeCoef` all derive from the RENDER extent; only the blit and UI pass touch the window extent.

### Task M4.1: `RenderTargetNode` follow-swapchain mode

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/RenderTargetNode.cpp` (+ its config header) — implement the "future followSwapchainExtent mode" the comment at `:75` reserves: optional `EXTENT_SOURCE` input (`IRenderTarget*`, the swapchain public vars) + `PARAM_SCALE` (float, default 1.0); when connected, CompileImpl computes `ceil(extent×scale)`, recreates the image when it changes, and republishes WIDTH/HEIGHT outputs.
  **Mechanism constraint:** resize reaches this node through the STANDARD events + recompilation phase only — the `EXTENT_SOURCE` connection makes RenderTargetNode a transitive dependent of SwapChainNode, so `WindowResizedMessage` → SwapChainNode recompile → dependents cascade → this CompileImpl. No per-frame extent checks in ExecuteImpl, no new events, no side-channel.
- Ensure the image is created with `VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT` and a storage-capable format (`VK_FORMAT_R8G8B8A8_UNORM` matches today's swapchain-write assumptions — check the shader's `imageStore` format qualifier and keep them consistent).
- Test: extend `RenderTargetNode`'s existing test (or add `tests/Nodes/test_render_target_follow.cpp`) — connect a fake `IRenderTarget` with extent 1000×500, scale 0.5 → expect 500×250 target and recreation on extent change to 800×600 → 400×300.

- [ ] Steps: failing test → implement → suite green → commit.

### Task M4.2: Rewire the live graph — compute writes offscreen, blits to swapchain

**Files:**
- Modify: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` (create the RenderTargetNode; connect swapchain→EXTENT_SOURCE; feed the offscreen image to the compute descriptor gatherer's output-image binding instead of the swapchain image; read `VIXEN_RENDER_SCALE` env into PARAM_SCALE; keep binding indices identical — only the image source changes).
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp` — new optional input `RENDER_TARGET_INFO` (IRenderTarget*). When connected: dispatch dims derive from ITS extent (`:360-361`), timestamps/Mrays use its extent, and after the dispatch record the barriers + `vkCmdBlitImage` to the swapchain image (which remains the `SWAPCHAIN_INFO` input), ending in the same layout contract as today (`leaveImageInGeneral` for the UI pass). When not connected: behavior identical to today (voxel-only paths, benchmark graph untouched).

- [ ] **Step 1:** Implement + WSL build green.
- [ ] **Step 2: Syncval gate:** lavapipe run with validation layer (if installed): zero sync validation errors across 200 frames + one programmatic resize.
- [ ] **Step 3: Visual gate:** lavapipe 150-frame run at `VIXEN_RENDER_SCALE=1.0` — capture the swapchain (existing debug-capture path or screenshot) and compare non-black pixel count vs pre-M4 baseline within 1% (same scene, same camera). Then `VIXEN_RENDER_SCALE=0.5` — image still fills the WHOLE window (blit upscales), just softer.
- [ ] **Step 4: Perf gate (real GPU):** at 2560×1440 post-resize: `Dispatch ms` at scale 0.5 ≈ 25-30% of scale 1.0. Record both in the findings appendix.
- [ ] **Step 5: Commit**

### Task M4.3: `raySizeCoef` derives from the render extent, live

**Files:**
- Modify: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:394-407` (one-shot computation from the INITIAL height — the bug) — move the computation to wherever the render extent is (re)published: compute `raySizeCoef = 2·tan(fovY/2)/renderHeight` from the RenderTargetNode's live height and route it through the existing push-constant/uniform path that carries it today (trace where the value lands — the push-constant gatherer or camera UBO — and rewire the source).
  **Mechanism constraint:** the recompute happens in the Compile phase of whichever node publishes the value, driven by the same resize→recompile cascade (it is a dependent of the render target). Not per-frame in ExecuteImpl.

- [ ] **Step 1:** Implement; log at INFO on every recompute: `"[LOD] raySizeCoef recomputed for height H"`.
- [ ] **Step 2: Live gate:** resize probe → log shows recomputation at the new height; visual: distant octree detail INCREASES after enlarging (was frozen at 500-px coarseness). Note in the findings appendix that this legitimately raises large-window cost (rank 6) — record the dispatch-ms delta it causes at 1440p, scale 1.0.
- [ ] **Step 3: Commit**

### Task M4.4: Pick coordinates map window→render space

**Files:**
- Modify: pick-ID sizing — `PickIdTargetNode` consumes the RENDER extent (same source as M4.1), not the swapchain extent.
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/UISelectionProviderNode.cpp` (or wherever cursor→pixel lookup happens — trace `GetImage`/readback in `VoxelSelectionProviderNode.cpp`): scale cursor coords by `renderExtent/windowExtent` before sampling the pick image.

- [ ] **Step 1:** Implement both; WSL build green.
- [ ] **Step 2: Live gate:** lavapipe at `VIXEN_RENDER_SCALE=0.5`: click the center body (drive via the existing UI-selection test path if present, else document a 30-second manual gate for the user) — selection hits the correct body.
- [ ] **Step 3: Commit**

---

## Milestone M5 — Post-resize p99 hitches: attribute, then fix what's confirmed

**Fixes:** the measured 18-22 ms p99 spikes after resize (unattributed), plus rank 9 (confirmed: `DescriptorSetNode` `perFrameImageInfos`/`perFrameBufferInfos` grow without bound — session-time slowdown).

### Task M5.1: Instrument — UI pass GPU timer + outlier frames

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/UIRenderNode.cpp` — add a `GPUPerformanceLogger` exactly like `ComputeDispatchNode.cpp:116-140` (allocate a query slot, RecordDispatchStart/End around the render pass, CollectResults per frame; the D2 fix makes multi-slot timing work now).
- Modify: `VIXEN/application/main/source/main.cpp` frame-timer block — when a frame exceeds 3× the rolling window median, log `"[FrameTimer] OUTLIER frame N: X.XXX ms"` (compute median from the sorted copy already produced each 120-frame window; keep last window's median for the check).

- [x] Steps: implement → WSL build green → real-GPU resize probe run → collect: per-outlier, does UI-pass GPU ms or dispatch GPU ms spike, or neither (⇒ CPU/present)? Write the attribution verdict + numbers into the findings appendix. Commit.
  - Done 2026-07-04 on lavapipe (real-GPU re-run still pending, controller gate). UI-pass GPUPerformanceLogger added (slot 2), outlier logger added and caught the resize hitch (frame 154, 28.7ms). GPU-summary lines don't fire on this lavapipe ICD for either logger (pre-existing, confirmed also true of the already-shipped ComputeDispatchNode timer) — attribution is CPU/log-correlation only in this environment. See findings appendix M5 gate.

### Task M5.2: Fix the confirmed growth: per-frame descriptor scratch is bounded

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Nodes/DescriptorSetNode.cpp` — locate `perFrameImageInfos`/`perFrameBufferInfos`; they must be reset (`.clear()`, capacity retained) at the top of each frame's update instead of growing forever. Mind pointer stability: `vkUpdateDescriptorSets` consumes the arrays synchronously, so clearing at frame start is safe; growth *within* one frame must still `reserve()` up front so `VkDescriptorImageInfo*` pointers into the vector don't dangle mid-build (reserve to the binding count before filling).
- Test: if a DescriptorSetNode unit test exists, extend it: run the update path 3 times, assert the containers' `size()` does not grow monotonically across frames.

- [x] Steps: failing/extended test → implement → suite green → lavapipe 500-frame soak (frame time at frame 450 not worse than at frame 50) → commit.
  - Done 2026-07-04. No dedicated DescriptorSetNode unit test exists to extend; demonstrated via a temporary stderr size-probe soak instead (removed before commit) — see findings appendix M5 gate for before/after numbers. RenderGraph suite green post-fix.

### Task M5.3: Act on the M5.1 verdict (bounded scope)

- [x] If the outliers attribute to a component with a **confirmed** finding (present path, UI geometry rebuild, descriptor updates), implement that specific fix in this milestone IF it is `fixCost: small` per the findings doc; otherwise write it up as a follow-up in the findings appendix with the measured evidence and STOP (do not improvise unverified fixes). Acceptance either way: the appendix contains the attribution table; if a fix landed, post-resize p99 ≤ 2× median sustained on the real-GPU probe.
  - Done 2026-07-04: L2 root-caused precisely (a cache-key granularity mismatch between `PipelineLayoutCacher` (correctly keyed on the live descriptor-set-layout handle) and `ComputePipelineCacher` (keyed on a resize-invariant shader/interface string) — the pipeline cache hands back a stale wrapper referencing the just-destroyed layout for one frame). This is a fix to shared `CashSystem` cache infrastructure, not a cheap ordering/skip-frame change, so per the milestone's bounded scope it is filed as a follow-up (not fixed) with the full mechanism and reproduction steps in the findings appendix. L1 attribution is inconclusive (extent-confounded in this probe; GPU-summary gap blocks a cleaner read) — also filed, not fixed. No unverified fix was improvised.
- [x] Commit.

---

## Milestone M6 — CPU-floor hygiene (ranks 11-12; cheap, permanent)

### Task M6.1: Log macros stop building strings for disabled levels

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/include/Core/NodeLogging.h:16-24` — each `NODE_LOG_*` macro gains a cheap pre-check before evaluating `msg`. Add the missing query to `Logger` (`VIXEN/libraries/logger/include/Logger.h` — `IsEnabled()` exists at `:28`; add `static LogLevel GetGlobalMinLevel()` if absent so the macro can compare).

```cpp
// Pattern (repeat per level; keep existing behavior when the check passes):
#define NODE_LOG_INFO(msg) \
    do { \
        if (nodeLogger && nodeLogger->IsEnabled() && \
            Vixen::Log::Logger::GetGlobalMinLevel() <= Vixen::Log::LogLevel::LOG_INFO) { \
            nodeLogger->Info(msg); \
        } \
    } while (0)
```

- [ ] Steps: implement all levels → WSL build green + full test suite green (log-behavior tests, if any, still pass: the observable output for ENABLED levels is unchanged) → commit.

### Task M6.2: Lifecycle hooks keyed by node

**Files:**
- Modify: `VIXEN/libraries/RenderGraph/src/Core/GraphLifecycleHooks.cpp:65-90` (+ header) — replace the flat per-phase hook list self-filtered by identity with `std::unordered_map<NodeInstance*, std::vector<Hook>>` so only the owning node's hooks run; keep a separate global-hook list for hooks registered without a target node (preserve the public registration API; adapt call sites if the registration signature carries the target node already).
- Test: extend the existing lifecycle-hook test (`libraries/RenderGraph/tests/` — find it via `grep -rl LifecycleHook tests/`): register hooks for nodes A and B, execute A, assert B's hook did NOT fire and was not invoked-and-filtered (add an invocation counter to the test hook).

- [ ] Steps: failing test → implement → suite green → lavapipe 150-frame gate → commit.

### Task M6.3: Program close-out

- [ ] Re-run the FULL measurement protocol (fixed-size matrix at four sizes + resize probe, real GPU Release): fill the final before/after table in the findings appendix (fresh-window honesty, dispatch scaling, transition p99, steady p99, FPS at 2560×1440 at scale 1.0 and 0.5).
- [ ] Update `Vixen-Docs/04-Development/Known-Issues.md`: close the items this program fixed; file anything discovered-but-deferred (e.g. M5.3 follow-up, SUBOPTIMAL-storm-on-Dozen watchpoint).
- [ ] Update the findings doc status header to PROGRAM COMPLETE with the final numbers.
- [ ] Commit.

---

## Explicit non-goals (deferred, with pointers)

- **Shader micro-optimization of the per-pixel constant** (ranks 4, 5, 6-traversal, 10: sphere-trace early-out vs bestT, 23-entry stack sizing via spec constant, shared MAX_ITERS budget, front-to-back instance sort). Measured Mrays/s is flat (~2550) on the target GPU and render-scale (M4) provides the headroom lever; these become load-bearing for the 60-300-body undertow path — schedule them with that epic. Findings: ranks 4/5/6/10 in the findings doc.
- **Resize debounce/coalescing beyond M3** (recreate-on-settle): the probe measured exactly one recompile per programmatic resize after M3's fixes; interactive-drag coalescing is only worth it if user-perceived drag jank persists — re-measure after M3.
- **FIFO/vsync quantization work**: refuted on the target machine (IMMEDIATE present confirmed); revisit only for WSLg/Dozen deployments.

## Milestone Map

Grouping = the plan's own milestone headers, verbatim (post-brainstorm-context-manager rule 1):

- M1 — parameter integrity (Tasks M1.1-M1.2)
- M2 — shader counters compiled out (Task M2.1)
- M3 — resize-path robustness (Tasks M3.1-M3.4)
- M4 — render-scale decoupling (Tasks M4.1-M4.4)
- M5 — p99 hitch attribution + bounded fixes (Tasks M5.1-M5.3)
- M6 — CPU-floor hygiene + close-out (Tasks M6.1-M6.3)

**Execution notes:** branch `feat/widescreen-perf-fix`, worktree `.claude/worktrees/widescreen-perf-fix` (based on main `8ac6b92b`). Workers run WSL builds + lavapipe gates only; every step labeled "Real-GPU gate" (Windows Release probes) is executed by the CONTROLLER after the worker's report — workers list those steps as pending in their report instead of attempting `cmd.exe`. `temp/` is gitignored: Windows-side `.bat` drivers live only in the main checkout.

## Milestone progress (updated by the execution pipeline)

- [x] M1 — parameter integrity
- [x] M2 — shader counters compiled out
- [x] M3 — resize-path robustness
- [x] M4 — render-scale decoupling
- [x] M5 — p99 hitch attribution + bounded fixes
- [ ] M6 — CPU-floor hygiene + close-out

## Progress Log

- M5 (Tasks M5.1-M5.3 + Critical fix): DONE · commits 0fa0b8e2..5e1a35fa, fix 1d86c3ef · Opus validator APPROVED (one Critical caught+fixed+re-approved: UI's whole-pool BeginFrame reset was wiping compute's GPU timestamps -> fixed to per-slot reset) · rank-9 descriptor-growth CONFIRMED+fixed (bufferInfos 16->912 unbounded -> flat 8; real-GPU p99 17-26ms->15-16ms, frame avg 4.8->3.7-4.5ms) · L2 root-caused+filed (ComputePipelineCacher key mismatch vs PipelineLayoutCacher; CleanupImpl no-Recompile-guard class noted) · L1 residual resolved as a side effect of the M5.2 fix · outlier-logger 5ms floor added · 2026-07-04
- M4 (Tasks M4.1-M4.4 + 0xFFFFFFFF guard): DONE · commits 83088039..5b2da8a4 · Opus validator APPROVED (barriers, legacy purity, descriptor cascade code-verified) · real-GPU A/B @1440p: dispatch 1.40->0.38 ms at scale 0.5 (27%, target 25-30%), FPS 172->306 · raySizeCoef now honest (exact 1/height, recomputed via cascade) — correct-LOD cost visible at scale 1.0 as predicted (rank 6) · found+fixed pre-existing barrier oldLayout=UNDEFINED assumption (per-image first-use tracking) · 2026-07-04
- M3 (Tasks M3.1-M3.4 + device-idle fix): DONE · commits 9a17879b..8f89d69d · Opus validator APPROVED (pausedForRecreation_ flag path empirically confirmed; MINOR 0xFFFFFFFF currentExtent guard deferred to M4) · live gate found+fixed a PRE-EXISTING use-in-flight teardown crash (missing device-idle wait on recreation waves) · real-GPU: transition p99 40.8->25.5 ms, 1 recompilation, 0 in-use VUIDs · NOTE for M5: post-resize steady frames ~1 ms slower than fresh-window at same size · 2026-07-03
- M2 (Task M2.1): DONE · commit 8509f58b (+comment fix) · Opus validator: functionally APPROVED, one LOW doc comment fixed controller-side · real-GPU 1440p: frame 7.63->3.76 ms (FPS 131->266), dispatch 2.18->1.38 ms · NOTE: SetStageDefines cannot inject #ifdef defines (token substitution) — no env opt-in possible · 2026-07-03
- M1 (Tasks M1.1-M1.2): DONE · commits 4af408ff..358f2c0a · Opus validator APPROVED · real-GPU gate: resolutions now honest, dispatch scales linearly (see findings appendix) · 2026-07-03

- Prep: sweep + measurements complete; instrumentation (D2 timestamp fix, frame timer, env overrides, resize probe) and both docs authored · 2026-07-03
