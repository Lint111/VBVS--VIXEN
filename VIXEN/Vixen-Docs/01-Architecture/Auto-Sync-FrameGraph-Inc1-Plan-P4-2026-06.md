---
title: Auto-Sync FrameGraph — P4 Implementation Plan (Generic Pass-Assembly Node)
aliases: [auto-sync P4, PassGroupNode plan, AR#21 P4]
tags: [architecture, plan, rendergraph, synchronization, AR21, framegraph, passgroupnode]
created: 2026-06-21
status: PLAN — ready for milestone-pipeline execution
related:
  - "[[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]"
  - "[[Auto-Sync-FrameGraph-Inc1-Plan-P3-2026-06]]"
  - "[[Subgraph-As-Node-Design-2026-06]]"
---

# Auto-Sync FrameGraph — P4 Implementation Plan (Generic Pass-Assembly Node)

> **For agentic workers:** REQUIRED SUB-SKILL: execute via the **post-brainstorm-context-manager** milestone pipeline (fresh implementer per milestone, Opus validator per milestone, controller stays thin). Steps use checkbox (`- [ ]`) syntax. NEVER commit unless the clean-commit gate is green (full build + tests + the change does what it claims, from fresh evidence). syncval/visual gates are HANDS-ON (user-driven), not agent-self-certified.

**Goal:** Build a generic **`PassGroupNode`** that assembles an ordered list of heterogeneous passes (compute and graphics) into **one command buffer + one submit**, with intra-pass barriers **auto-baked** by reusing the P2 scheduler core — proven by a `compute→compute→render→present` demo at 0 syncval + visual.

**Architecture:** The node declares a per-pass `AccessKind` list (the node-local twin of P3's slot-level `accessKind`). At compile it maps each pass to its own `groupId` and calls the **existing pure** `BuildScheduleFromTimelines(...)` (P2), whose `entryBarriers` become the barriers replayed *between* passes; the timeline `SyncEdge`s are ignored because everything is in one submit. A baggage-free `PassRecorder` records compute (`vkCmdDispatch`) and graphics (`vkCmdBeginRenderPass…vkCmdDraw…vkCmdEndRenderPass`) steps, replaying the baked barriers between them, then the node submits once (WSI binary semaphores when swapchain-adjacent). The trailing render pass **consumes** `VkRenderPass`/pipeline/framebuffers from the existing `RenderPassNode`/`GraphicsPipelineNode`/`FramebufferNode` — VIXEN uses traditional render passes, **not** dynamic rendering.

**Tech Stack:** C++23, Vulkan 1.3 (`synchronization2` already enabled via capability graph in P3), GoogleTest, Ninja/MSVC. Build = `cmd.exe /c _ninja_preset_build.bat` FOREGROUND (`timeout: 600000`).

---

## Context & decisions (read before starting)

**Branch off `main`** — P1+P2+P3 are merged to `main` (merge `7d28ae52`, 2026-06-21). P4 depends on P3's `barrier2` standardization, the `synchronization2` capability gate, `FindGroupForNode`, and the `BuildScheduleFromTimelines` core, all now on `main`.

**Spec deviation (approved by user 2026-06-21):** The Inc1 design (`Auto-Sync-FrameGraph-Inc1-Design-2026-06.md`, P4 row) said "generalize `MultiDispatchNode` in place." Investigation found **`MultiDispatchNode` is dead code**: it has **no `vkQueueSubmit`** (record-only), is wired into **zero** application graphs, its two tests only *simulate* its logic, and it carries benchmark baggage (`TaskQueue` budget, `TimelineCapacityTracker`, per-frame timing). The spec's justification for "in place" ("it already submits once") is therefore factually invalid. **User decision:** build a **generic pass-assembly node** (`PassGroupNode`) — "comp→comp→render was just an example." We build the generic abstraction now (the documented `PassGroupNode`/Subgraph-As-Node end-state), with the chain as its proving demo. `MultiDispatchNode` is left untouched (dead; a later cleanup may delete it — out of scope here).

**User decision — P4 image scope:** **buffer-hazard proof only.** P4 bakes/replays `compute→compute` and `compute→render` hazards on **storage buffers** (bakeable today). The compute-written **image→present** transition is handled by the render pass's `initial=General → final=PresentSrc` LOAD ops (no baked image barrier). The `PassResourceAccess` descriptor still carries an `isImage` flag so P5 can turn on baked image barriers via node-local image correlation. **Do not** pull P5's swapchain-`Resource*` identity work into P4.

**Decisions taken from investigation (no further input needed):** `std::variant` `PassStep` (compute vs graphics are genuinely different shapes); traditional `VkRenderPass` consuming existing nodes (no dynamic rendering — repo has zero `vkCmdBeginRendering`); reuse `BuildScheduleFromTimelines` verbatim; fullscreen-triangle draw whose **fragment shader reads the compute-written SSBO** (makes the compute→render dependency real + visually verifiable); node name `PassGroupNode` (existing vocab; rename is trivial if desired).

---

## Reference implementations (lift-and-adapt; implementers read these in full)

| Need | Read & follow (file:line) |
|---|---|
| Per-frame submit + WSI binary semaphore wiring (wait `imageAvailable[frame]`, signal `renderComplete[image]`, own `inFlightFence`) | `libraries/RenderGraph/src/Nodes/ComputeDispatchNode.cpp:246-269` + its FrameSync slots in `include/Data/Nodes/ComputeDispatchNodeConfig.h` |
| Baked-barrier replay into a `vkCmdPipelineBarrier2` (build `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` from `GroupBarrier`) | `ComputeDispatchNode.cpp:341-429` (`ReplayEntryBarriers`, `GetFrameSyncSchedule()`+`FindGroupForNode`) |
| Trailing render-pass recording (`vkCmdBeginRenderPass`→bind→push→viewport/scissor→`vkCmdDraw`→`vkCmdEndRenderPass`) + graphics submit | `libraries/RenderGraph/src/Nodes/GeometryRenderNode.cpp:281-412` + its graphics input slots in its config header |
| `VkRenderPass`(LOAD, `General→PresentSrc`), framebuffers over swapchain views, present wiring | `RenderPassNode.cpp:65-126`, `FramebufferNode.cpp:73-86`, `application/main/source/graph/BuildRenderGraph.cpp:1097-1128` |
| Node config X-macro slot pattern + self-registration | `include/Data/Nodes/ComputeDispatchNodeConfig.h` (`INPUT_SLOT`/`OUTPUT_SLOT`/`INPUT_SLOT_SYNC`) + the `REGISTER_NODE`-style static registration in `ComputeDispatchNode.cpp` |
| Env-gated self-contained demo-graph TU (infra + connect + compile) | `application/main/source/graph/BuildInstancingDemoGraph.cpp` + env dispatch `BuildRenderGraph.cpp:87-106` |

**SSOT types already on `main` (use exactly, do not redefine):**
- `Core/BarrierTypes.h`: `enum class AccessKind`, `AccessInfo{stage,access,layout}`, `ResolveAccess(kind)`, `AccessReads(k)`, `AccessWrites(k)`.
- `Core/FrameSyncSchedule.h`: `ResourceAccessPoint{groupId,kind}`, `ResourceTimeline{resource,isImage,accesses}`, `GroupBarrier{resource,src,dst,isImage}`, `SubmitGroup{...,entryBarriers,...}`, `FrameSyncSchedule{groups,edges,valid,...}`, `FindGroupForNode`.
- `Core/FrameSyncScheduler.h`: `FrameSyncSchedule BuildScheduleFromTimelines(const std::vector<ResourceTimeline>&, uint32_t groupCount)`.
- `Data/DispatchPass.h`: `PushConstantData{data,stageFlags,offset}` (reuse for push constants).

---

## File Structure

| File | New/changed | Responsibility |
|---|---|---|
| `libraries/RenderGraph/include/Data/PassStep.h` | **new** | `PassResourceAccess`, `ComputePassStep`, `RenderPassStep`, `using PassStep = std::variant<...>` |
| `libraries/RenderGraph/include/Core/PassGroupSchedule.h` | **new** | `BuildPassGroupSchedule(passes)` — pure node-local barrier baking (reuses `BuildScheduleFromTimelines`) |
| `libraries/RenderGraph/include/Core/PassRecorder.h` + `src/Core/PassRecorder.cpp` | **new** | baggage-free `RecordPassGroup(cmd, passes, schedule, imageIndex)` + `ReplayGroupBarriers(...)` |
| `libraries/RenderGraph/include/Nodes/PassGroupNode.h` + `src/Nodes/PassGroupNode.cpp` | **new** | the node: slots, host `SetPasses`/`AddComputePass`/`AddRenderPass`, `CompileImpl` (bake), `ExecuteImpl` (record+submit), self-registration |
| `libraries/RenderGraph/include/Data/Nodes/PassGroupNodeConfig.h` | **new** | X-macro slot config (swapchain, framesync semaphores/fence in/out) |
| `libraries/RenderGraph/include/Core/BarrierTypes.h` | **modify** | add `AccessKind::FragmentStorageRead` (+ `ResolveAccess`/`AccessReads` arms) |
| `libraries/RenderGraph/tests/test_pass_group_schedule.cpp` | **new** | pure unit tests for `BuildPassGroupSchedule` (the testable heart) |
| `libraries/RenderGraph/tests/CMakeLists.txt` | **modify** | register the new test exe |
| `application/main/source/graph/BuildAutoSyncDemoGraph.cpp` | **new** | env-gated `compute→compute→render→present` demo graph |
| `application/main/source/graph/BuildRenderGraph.cpp` | **modify** | add `VIXEN_AUTOSYNC_DEMO` env dispatch (`~:87-106`) |
| `application/main/...` shaders | **new** | `autosync_fill.comp`, `autosync_post.comp`, `autosync_fullscreen.vert`, `autosync_present.frag` |

---

## Milestone Map

> Persisted for resume. Confirm grouping before execution; do not re-segment on resume.

- **M1 ✅ DONE — Pure node-local baking core (Tasks 1–2).** `PassStep` types + `BuildPassGroupSchedule` + full unit tests. No GPU. *Gate:* `test_pass_group_schedule` green, full build green.
- **M2 ✅ DONE — Recorder core (Tasks 3–4).** Baggage-free `PassRecorder`: barrier replay + compute recording + trailing render-pass recording. *Gate:* build green (recording correctness verified live in M5).
- **M3 — PassGroupNode (Tasks 5–6).** Node class: config slots, host assembly API, `CompileImpl` bakes the node-local schedule, `ExecuteImpl` records via `PassRecorder` + submits once; self-registers. *Gate:* build green + node-registration smoke test.
- **M4 — Demo graph (Tasks 7–8).** `BuildAutoSyncDemoGraph` (`compute→compute→render→present`, fullscreen frag reads compute SSBO) + `VIXEN_AUTOSYNC_DEMO` dispatch + shaders. *Gate:* app builds; demo graph constructs + compiles without throw.
- **M5 — Live gate (Task 9, HANDS-ON).** Run under syncval; confirm visual + **0 synchronization-validation errors**. *Gate:* the P4 exit gate — user-driven, not agent-self-certified.

---

## Tasks

### Task 1: `PassStep` types

**Files:**
- Create: `libraries/RenderGraph/include/Data/PassStep.h`

- [ ] **Step 1: Write the type header**

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "Core/BarrierTypes.h"   // AccessKind
#include "Data/DispatchPass.h"   // PushConstantData

namespace Vixen::RenderGraph {

class Resource;

/// One resource touch by one pass — the node-local twin of P3's slot-level accessKind.
/// `resource` is a node-local identity used only for hazard correlation (pointer compare).
struct PassResourceAccess {
    const Resource* resource = nullptr;
    AccessKind      kind     = AccessKind::None;
    bool            isImage  = false;   // P4: false (buffer proof). P5 turns on image barriers.
};

/// A compute dispatch step.
struct ComputePassStep {
    VkPipeline                   pipeline = VK_NULL_HANDLE;
    VkPipelineLayout             layout   = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t                     firstSet = 0;
    std::optional<PushConstantData> pushConstants;
    glm::uvec3                   workGroupCount = {1, 1, 1};
    std::vector<PassResourceAccess> accesses;
    std::string                  debugName;
};

/// A graphics (render) step. Consumes a VkRenderPass + framebuffers from existing nodes.
struct RenderPassStep {
    VkPipeline                   pipeline   = VK_NULL_HANDLE;   // graphics pipeline
    VkPipelineLayout             layout     = VK_NULL_HANDLE;
    VkRenderPass                 renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   framebuffers;                  // one per swapchain image
    VkExtent2D                   renderArea = {0, 0};
    std::vector<VkClearValue>    clearValues;
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t                     firstSet = 0;
    std::optional<PushConstantData> pushConstants;
    uint32_t                     vertexCount   = 3;             // fullscreen triangle default
    uint32_t                     instanceCount = 1;
    std::optional<VkBuffer>      vertexBuffer;                  // none for fullscreen draw
    std::vector<PassResourceAccess> accesses;
    std::string                  debugName;
};

using PassStep = std::variant<ComputePassStep, RenderPassStep>;

/// Accessor for the access list of any PassStep variant.
[[nodiscard]] inline const std::vector<PassResourceAccess>& StepAccesses(const PassStep& s) {
    return std::visit([](const auto& step) -> const std::vector<PassResourceAccess>& {
        return step.accesses;
    }, s);
}

} // namespace Vixen::RenderGraph
```

- [ ] **Step 2: Verify it compiles** — add a throwaway TU or rely on Task 2's test including it; full build green.

- [ ] **Step 3: Commit** — `git commit -m "feat(rendergraph): PassStep variant types for generic pass assembly (auto-sync P4 M1)"`

---

### Task 2: `BuildPassGroupSchedule` + unit tests (the testable heart)

**Files:**
- Modify: `libraries/RenderGraph/include/Core/BarrierTypes.h` (add `FragmentStorageRead`)
- Create: `libraries/RenderGraph/include/Core/PassGroupSchedule.h`
- Create/Test: `libraries/RenderGraph/tests/test_pass_group_schedule.cpp`
- Modify: `libraries/RenderGraph/tests/CMakeLists.txt`

- [ ] **Step 1: Add `FragmentStorageRead` to the AccessKind SSOT** (fills a real gap — fragment shaders reading SSBOs; needed by the demo's frag pass). In `BarrierTypes.h`:
  - Add `FragmentStorageRead,` to `enum class AccessKind` (after `FragmentSampledRead`).
  - Add to `ResolveAccess`:
    ```cpp
    case AccessKind::FragmentStorageRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    ```
  - Add `case AccessKind::FragmentStorageRead:` to the `true` arm of `AccessReads`.

- [ ] **Step 2: Write the failing test** in `test_pass_group_schedule.cpp`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/PassGroupSchedule.h"
#include "Data/PassStep.h"

using namespace Vixen::RenderGraph;

// BuildScheduleFromTimelines only pointer-compares Resource*, never dereferences it,
// so sentinel pointers are valid test fixtures.
static const Resource* kBufA = reinterpret_cast<const Resource*>(0x1);
static const Resource* kBufB = reinterpret_cast<const Resource*>(0x2);

static ComputePassStep Comp(std::vector<PassResourceAccess> a) {
    ComputePassStep s; s.accesses = std::move(a); return s;
}

TEST(PassGroupSchedule, ComputeWriteThenComputeReadEmitsOneBufferBarrier) {
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        Comp({{kBufA, AccessKind::ComputeStorageRead,  false}}),
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_TRUE(s.groups[0].entryBarriers.empty());
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_EQ(s.groups[1].entryBarriers[0].src.access, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(s.groups[1].entryBarriers[0].dst.access, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    EXPECT_FALSE(s.groups[1].entryBarriers[0].isImage);
}

TEST(PassGroupSchedule, ComputeToFragmentReadAcrossRenderStep) {
    RenderPassStep r; r.accesses = {{kBufA, AccessKind::FragmentStorageRead, false}};
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        r,
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_EQ(s.groups.size(), 2u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_EQ(s.groups[1].entryBarriers[0].dst.stage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

TEST(PassGroupSchedule, IndependentResourcesNoBarrier) {
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        Comp({{kBufB, AccessKind::ComputeStorageWrite, false}}),
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_TRUE(s.groups[1].entryBarriers.empty());
}
```

- [ ] **Step 3: Register the test exe** in `libraries/RenderGraph/tests/CMakeLists.txt` (mirror the existing `test_frame_sync_scheduler` entry — same `add_executable`/`target_link_libraries`/`add_test` shape).

- [ ] **Step 4: Run to verify it FAILS** — `cmd.exe /c _ninja_preset_build.bat` then run; expected: compile error "PassGroupSchedule.h not found" / `BuildPassGroupSchedule` undefined.

- [ ] **Step 5: Implement** `PassGroupSchedule.h`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vector>
#include "Core/FrameSyncSchedule.h"
#include "Core/FrameSyncScheduler.h"   // BuildScheduleFromTimelines
#include "Data/PassStep.h"

namespace Vixen::RenderGraph {

/// Bake intra-pass barriers for an ordered pass list by mapping each pass to its own
/// groupId and reusing the P2 scheduler core. Consumers replay schedule.groups[i].entryBarriers
/// before pass i; the timeline SyncEdges are intentionally ignored (single command buffer).
[[nodiscard]] inline FrameSyncSchedule BuildPassGroupSchedule(const std::vector<PassStep>& passes) {
    std::vector<ResourceTimeline> timelines;
    auto findOrAdd = [&](const Resource* res, bool isImage) -> ResourceTimeline& {
        for (auto& t : timelines) if (t.resource == res) return t;
        timelines.push_back(ResourceTimeline{res, isImage, {}});
        return timelines.back();
    };
    for (uint32_t i = 0; i < passes.size(); ++i) {
        for (const PassResourceAccess& a : StepAccesses(passes[i])) {
            if (a.resource == nullptr || a.kind == AccessKind::None) continue;
            findOrAdd(a.resource, a.isImage).accesses.push_back(ResourceAccessPoint{i, a.kind});
        }
    }
    return BuildScheduleFromTimelines(timelines, static_cast<uint32_t>(passes.size()));
}

} // namespace Vixen::RenderGraph
```

- [ ] **Step 6: Run to verify PASS** — build + run `test_pass_group_schedule.exe --gtest_brief=1`; expected: `[  PASSED  ] 3 tests.`
  - If `BuildScheduleFromTimelines` does NOT emit `entryBarriers` for adjacent groups exactly as asserted, STOP and read its body (`src/Core/FrameSyncScheduler.cpp:19-53`) to confirm the producer→consumer barrier-emission contract before adapting the asserts (do not weaken the test to pass).

- [ ] **Step 7: Commit** — `git commit -m "feat(rendergraph): BuildPassGroupSchedule node-local barrier baking + tests (auto-sync P4 M1)"`

---

### Task 3: `PassRecorder` — barrier replay + compute recording

**Files:**
- Create: `libraries/RenderGraph/include/Core/PassRecorder.h`, `src/Core/PassRecorder.cpp`

- [ ] **Step 1: Declare the recorder** (`PassRecorder.h`):

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "Core/FrameSyncSchedule.h"
#include "Data/PassStep.h"

namespace Vixen::RenderGraph {

/// Replay one group's baked barriers as a single vkCmdPipelineBarrier2.
/// Buffer/memory barriers only in P4 (GroupBarrier.isImage==false); image arm is a P5 no-op stub.
void ReplayGroupBarriers(VkCommandBuffer cmd, const std::vector<GroupBarrier>& barriers);

/// Record an ordered pass list into `cmd`, replaying schedule.groups[i].entryBarriers before pass i.
/// Baggage-free: depends only on Vulkan handles, the pass list, the baked schedule, and imageIndex.
/// Caller owns vkBeginCommandBuffer/vkEndCommandBuffer and the submit.
void RecordPassGroup(VkCommandBuffer cmd, const std::vector<PassStep>& passes,
                     const FrameSyncSchedule& schedule, uint32_t imageIndex);

} // namespace Vixen::RenderGraph
```

- [ ] **Step 2: Implement `ReplayGroupBarriers` + the compute arm of `RecordPassGroup`** (`PassRecorder.cpp`). Model `ReplayGroupBarriers` on `ComputeDispatchNode::ReplayEntryBarriers` (`ComputeDispatchNode.cpp:385-429`) — for each `GroupBarrier` with `isImage==false`, fill a `VkBufferMemoryBarrier2`/`VkMemoryBarrier2` from `src`/`dst` and submit one `vkCmdPipelineBarrier2`. Compute arm of `RecordPassGroup`:

```cpp
void RecordPassGroup(VkCommandBuffer cmd, const std::vector<PassStep>& passes,
                     const FrameSyncSchedule& schedule, uint32_t imageIndex) {
    for (uint32_t i = 0; i < passes.size(); ++i) {
        if (i < schedule.groups.size())
            ReplayGroupBarriers(cmd, schedule.groups[i].entryBarriers);
        std::visit([&](const auto& step) { RecordOneStep(cmd, step, imageIndex); }, passes[i]);
    }
}
// RecordOneStep(ComputePassStep): vkCmdBindPipeline(COMPUTE) -> vkCmdBindDescriptorSets(COMPUTE,
//   step.layout, step.firstSet, ...) -> if pushConstants vkCmdPushConstants -> vkCmdDispatch(x,y,z).
```
(`RecordOneStep(RenderPassStep)` is Task 4.)

- [ ] **Step 3: Build green** (`_ninja_preset_build.bat`). No new unit test here — recording correctness is GPU-verified at M5; the compute path is exercised by the demo.

- [ ] **Step 4: Commit** — `git commit -m "feat(rendergraph): PassRecorder barrier replay + compute step recording (auto-sync P4 M2)"`

---

### Task 4: `PassRecorder` — trailing render-pass recording

**Files:**
- Modify: `libraries/RenderGraph/src/Core/PassRecorder.cpp`

- [ ] **Step 1: Implement `RecordOneStep(RenderPassStep)`** — lift verbatim from `GeometryRenderNode::RecordDrawCommands` (`GeometryRenderNode.cpp:281-412`), parameterized by the step:
  - `VkRenderPassBeginInfo` with `step.renderPass`, `step.framebuffers[imageIndex]`, `renderArea={ {0,0}, step.renderArea }`, `clearValueCount/pClearValues` from `step.clearValues`; `vkCmdBeginRenderPass(... VK_SUBPASS_CONTENTS_INLINE)`.
  - `vkCmdBindPipeline(GRAPHICS, step.pipeline)`; if `!descriptorSets.empty()` `vkCmdBindDescriptorSets(GRAPHICS, step.layout, step.firstSet, ...)`; if `pushConstants` `vkCmdPushConstants`.
  - Set viewport+scissor from `step.renderArea` (copy `GeometryRenderNode`'s `SetViewportAndScissor`).
  - If `step.vertexBuffer` `vkCmdBindVertexBuffers`; `vkCmdDraw(step.vertexCount, step.instanceCount, 0, 0)`.
  - `vkCmdEndRenderPass(cmd)`.

- [ ] **Step 2: Build green** (`_ninja_preset_build.bat`).

- [ ] **Step 3: Commit** — `git commit -m "feat(rendergraph): PassRecorder trailing render-pass step recording (auto-sync P4 M2)"`

---

### Task 5: `PassGroupNode` — config, host assembly API, `CompileImpl`

**Files:**
- Create: `libraries/RenderGraph/include/Data/Nodes/PassGroupNodeConfig.h`
- Create: `libraries/RenderGraph/include/Nodes/PassGroupNode.h`
- Create: `libraries/RenderGraph/src/Nodes/PassGroupNode.cpp`

- [ ] **Step 1: Write the config** (`PassGroupNodeConfig.h`) — mirror `ComputeDispatchNodeConfig.h`'s X-macro shape. Input slots: `SWAPCHAIN_INFO` (the swapchain `IRenderTarget`), `IMAGE_INDEX`, `IMAGE_AVAILABLE_SEMAPHORES_ARRAY`, `IN_FLIGHT_FENCE`. Output slots: `RENDER_COMPLETE_SEMAPHORE`, `COMMAND_BUFFER` (optional/debug). The per-pass pipeline/render-pass/framebuffer handles are supplied to the node via the host assembly API (Step 2), not via dynamic slots (a fixed slot set cannot express an arbitrary pass count). Copy the exact slot-macro syntax + the slot-index enum from `ComputeDispatchNodeConfig.h`.

- [ ] **Step 2: Write the node header** (`PassGroupNode.h`):

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Nodes/NodeInstance.h"            // base (follow ComputeDispatchNode.h includes)
#include "Core/FrameSyncSchedule.h"
#include "Data/PassStep.h"
#include <vector>

namespace Vixen::RenderGraph {

class PassGroupNode : public NodeInstance {
public:
    PassGroupNode();
    // Host assembly API — the builder fills concrete handles after pipelines/render passes exist.
    void SetPasses(std::vector<PassStep> passes);
    void AddComputePass(ComputePassStep step);
    void AddRenderPass(RenderPassStep step);

    void CompileImpl() override;   // bake node-local schedule from the pass list
    void ExecuteImpl() override;   // record via PassRecorder + submit once
    void CleanupImpl(CleanupReason reason) override;

private:
    std::vector<PassStep>  passes_;
    FrameSyncSchedule      intraSchedule_;   // baked in CompileImpl
    // command buffers (one per swapchain image) + per-frame submit handles — follow ComputeDispatchNode
};

} // namespace Vixen::RenderGraph
```
Match the EXACT `CompileImpl`/`ExecuteImpl`/`CleanupImpl` signatures from the `NodeInstance` base header (the override keyword will fail the build if a signature is wrong — that is the check). Use the base `SetDevice`/`GetDevice` (per project rule: never a private `device_` member).

- [ ] **Step 3: Implement `CompileImpl`** — `intraSchedule_ = BuildPassGroupSchedule(passes_);` then allocate one command buffer per swapchain image (copy `ComputeDispatchNode::CompileImpl`'s command-buffer allocation). Assert `passes_` non-empty.

- [ ] **Step 4: Build green** (`_ninja_preset_build.bat`).

- [ ] **Step 5: Commit** — `git commit -m "feat(rendergraph): PassGroupNode config + CompileImpl bakes node-local schedule (auto-sync P4 M3)"`

---

### Task 6: `PassGroupNode` — `ExecuteImpl` (record + submit) + self-registration + smoke test

**Files:**
- Modify: `libraries/RenderGraph/src/Nodes/PassGroupNode.cpp`
- Test: `libraries/RenderGraph/tests/test_node_self_registration.cpp` (extend) or a new `test_pass_group_node_smoke.cpp`

- [ ] **Step 1: Implement `ExecuteImpl`** — resolve `imageIndex` + the swapchain `IRenderTarget` from input slots (copy `ComputeDispatchNode::ExecuteImpl`'s slot reads); `vkBeginCommandBuffer(cmd[imageIndex], ONE_TIME_SUBMIT)`; `RecordPassGroup(cmd, passes_, intraSchedule_, imageIndex)`; `vkEndCommandBuffer`; then the **submit** — lift `ComputeDispatchNode.cpp:246-269` verbatim: wait `imageAvailable[frameIndex]` (from the semaphores-array slot), signal `renderComplete[imageIndex]`, submit on `vulkanDevice->queue` with the `inFlightFence`; write `RENDER_COMPLETE_SEMAPHORE` output.

- [ ] **Step 2: Self-register the node** — add the `REGISTER_NODE`-style static registration (copy the exact macro/line from `ComputeDispatchNode.cpp`).

- [ ] **Step 3: Write the smoke test** — assert the node is creatable via the registry by type name and that `AddComputePass`/`AddRenderPass` grow the pass list (no GPU). Mirror `test_node_self_registration.cpp`'s registry-lookup pattern.

- [ ] **Step 4: Build + run the smoke test** — expected PASS.

- [ ] **Step 5: Commit** — `git commit -m "feat(rendergraph): PassGroupNode ExecuteImpl record+submit + self-registration (auto-sync P4 M3)"`

---

### Task 7: Demo graph — infra + compute pipelines + `PassGroupNode` wiring

**Files:**
- Create: `application/main/source/graph/BuildAutoSyncDemoGraph.cpp`
- Create shaders: `autosync_fill.comp`, `autosync_post.comp` (compute), `autosync_fullscreen.vert`, `autosync_present.frag`
- Modify: the app graph header that declares `BuildInstancingDemoGraph` (add `BuildAutoSyncDemoGraph` sibling decl)

- [ ] **Step 1: Author the shaders.**
  - `autosync_fill.comp`: writes an SSBO `outBuf[idx]` with a gradient/pattern (e.g. `vec4(uv, 0, 1)` packed) for each pixel of the swapchain extent (1 invocation per pixel via `gl_GlobalInvocationID`). Declares the SSBO `writeonly`.
  - `autosync_post.comp`: reads+writes the same SSBO (e.g. a cheap blur/scale) — establishes the `compute→compute` RAW hazard. `readwrite` SSBO.
  - `autosync_fullscreen.vert`: standard 3-vertex fullscreen triangle from `gl_VertexIndex` (no vertex buffer).
  - `autosync_present.frag`: reads the SSBO at `ivec2(gl_FragCoord.xy)` → emits the color. Declares the SSBO `readonly` (this is the `FragmentStorageRead`).

- [ ] **Step 2: Build the demo graph** — clone `BuildInstancingDemoGraph.cpp`'s self-contained infra (instance / physical+logical device / window / swapchain / command pool / `FrameSyncNode`). Create: 2 `ComputePipelineNode`s (fill, post) + the SSBO resource; a `RenderPassNode`(`initial=General`→`final=PresentSrc`, LOAD) + `FramebufferNode` over the swapchain views + a `GraphicsPipelineNode`(fullscreen.vert + present.frag); a `PassGroupNode`; a `PresentNode`. Connect FrameSync/swapchain slots to the `PassGroupNode` (copy the composite wiring `BuildRenderGraph.cpp:1097-1128`).

- [ ] **Step 3: Assemble the pass list** — after the pipelines/render pass/framebuffers exist, call on the `PassGroupNode`:
  - `AddComputePass`: fill.comp pipeline/layout/sets; `workGroupCount` = ceil(extent / localSize); `accesses = {{ssbo, ComputeStorageWrite, false}}`.
  - `AddComputePass`: post.comp; `accesses = {{ssbo, ComputeStorageReadWrite, false}}`.
  - `AddRenderPass`: graphics pipeline/layout/sets, `renderPass`, `framebuffers`, `renderArea=extent`, `vertexCount=3`; `accesses = {{ssbo, FragmentStorageRead, false}}`.
  - (`ssbo` is the same `const Resource*` for all three — that pointer identity is what drives the baked barriers.)

- [ ] **Step 4: Build green** (`_ninja_preset_build.bat`).

- [ ] **Step 5: Commit** — `git commit -m "feat(app): auto-sync demo graph + shaders (compute->compute->render) (auto-sync P4 M4)"`

---

### Task 8: Wire the `VIXEN_AUTOSYNC_DEMO` env dispatch

**Files:**
- Modify: `application/main/source/graph/BuildRenderGraph.cpp` (`~:87-106`)

- [ ] **Step 1: Add the env check** — mirror the `VIXEN_INSTANCING_DEMO`/`VIXEN_UI_DEMO` blocks: if `getenv("VIXEN_AUTOSYNC_DEMO")` set, `return BuildAutoSyncDemoGraph(...)` early.

- [ ] **Step 2: Build green** (`_ninja_preset_build.bat`).

- [ ] **Step 3: Construct-only smoke** — log the demo graph's node + connection counts at build; confirm `Compile()` returns without throw when the env var is set (a headless construct check, no present loop required for this gate).

- [ ] **Step 4: Commit** — `git commit -m "feat(app): VIXEN_AUTOSYNC_DEMO env dispatch (auto-sync P4 M4)"`

---

### Task 9: Live syncval + visual gate (HANDS-ON — user-driven)

**Files:** none (verification)

- [ ] **Step 1: Build with validation** — `cmd.exe /c _ninja_preset_build.bat` (ensure `VIXEN_VULKAN_VALIDATION` is honored; it's a compile/runtime gate from P3).

- [ ] **Step 2: Run the demo under syncval** — `cmd.exe /c "set VIXEN_AUTOSYNC_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"` (WSL bash does NOT pass env to the Windows .exe — must use `cmd.exe /c "set …&& …"`). `taskkill` to reap when done.

- [ ] **Step 3: Confirm the gate** — (a) the fullscreen pass displays the compute-generated pattern on screen (visual proof the `compute→compute→render` data dependency is synchronized); (b) **0 synchronization-validation errors** in the log. This is the P4 exit gate.

- [ ] **Step 4: Record the result** in the Progress Log + update memory `auto-sync-framegraph-epic.md`.

---

## Out of scope (P5/later)
- Baked **image** barriers / real swapchain `Resource*` identity (P5). P4's `isImage` flag is plumbed but unused.
- Tier-2 timeline semaphores between submit groups; composite migration off `leaveImageInGeneral` (P5).
- Deleting the dead `MultiDispatchNode` (separate cleanup).
- Merging the demo into the live graph (it stays env-gated, like `VIXEN_INSTANCING_DEMO`).

## Self-Review (done at authoring)
- **Spec coverage:** P4 row ("generalized node, trailing render pass, intra-group schedule, reusable pass-recording core, demo+syncval gate") → Tasks 5–6 (node) / Task 4 (trailing render) / Tasks 2–3 (intra-group schedule + recorder core) / Tasks 7–9 (demo+gate). Open-question (a) pass-descriptor field → `PassResourceAccess` (Task 1). Open-question (b) baggage-free recorder → `PassRecorder` free functions (Tasks 3–4), zero `TaskQueue`/budget/capacity. ✓
- **Placeholder scan:** new pure logic shown in full (Tasks 1,2,3,5); GPU-recording + node-wiring use precise file:line lifts (the milestone-pipeline implementers read source) — consistent with the P1–P3 plan format. ✓
- **Type consistency:** `AccessKind`/`ResolveAccess`/`AccessReads` (BarrierTypes.h), `ResourceTimeline`/`ResourceAccessPoint`/`GroupBarrier`/`SubmitGroup`/`FrameSyncSchedule` (FrameSyncSchedule.h), `BuildScheduleFromTimelines(timelines, groupCount)` (FrameSyncScheduler.h), `PushConstantData` (DispatchPass.h) — all used exactly as defined on `main`. `PassResourceAccess`/`ComputePassStep`/`RenderPassStep`/`PassStep`/`StepAccesses` defined in Task 1, used consistently in Tasks 2–7. ✓

## Progress Log
- Milestone 1 (Tasks 1–2): DONE · commits `aaaceb8c`, `d96c2e61` · Opus validator APPROVED (all 6 checks; re-derived all 3 tests vs the real `BuildScheduleFromTimelines` contract, ran the exe = 3 passed, clean tree, correct scope, fresh binary) · 2026-06-21
- Milestone 2 (Tasks 3–4): DONE · commits `e2e021e4` (+ empty marker `41db7b1a`) · Opus validator APPROVED (7 checks; forced a clean recompile of `PassRecorder.cpp` → 77/77 zero errors, render-pass arm verified genuinely present, baggage-free, balanced begin/end render pass, global `VkMemoryBarrier2` mirrors the proven `ComputeDispatchNode` sync2 precedent) · 2026-06-21
