# Auto-Sync FrameGraph — Implementation Plan (Phase P2: Pure FrameSyncScheduler)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the pure, CPU-unit-testable `FrameSyncScheduler` that, from the graph's execution order + the `ResourceAccessTracker`, bakes a structured `FrameSyncSchedule` — per-submit-group entry barriers (layout/memory) + inter-group timeline-semaphore edges — with **no GPU objects and no Execute-time consumption** (that lands in P3).

**Architecture:** A pure core (`BuildScheduleFromTimelines`) operates on explicit per-resource *access timelines* (sequences of `{groupId, AccessKind}`) — trivially synthesizable in tests — and emits the schedule via a running per-resource layout simulation + RAW/WAR/WAW hazard classification. A thin adapter (`FrameSyncScheduler::Build`) extracts those timelines from `(executionOrder, ResourceAccessTracker)`, assigns submit groups (1 node = 1 group; pass-group nodes collapse later), assigns frame-relative timeline offsets, and tags swapchain-adjacent groups. Hooked into `RenderGraph::Compile` beside the access tracker; stored as a member with an accessor. Loop-agnostic at compile (loop membership is Execute-time; multi-loop cadence is Tier-3, deferred).

**Tech Stack:** C++23, Vulkan 1.3 (`VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2`/`VkMemoryBarrier2`), GoogleTest, CMake + Ninja (`vixen-ninja`).

**Spec:** [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]. **Predecessor:** P1 (landed `AccessKind`/`AccessInfo`/`ResolveAccess` in `Core/BarrierTypes.h`; `ResourceAccess.kind`). **Branch:** `feat/auto-sync-framegraph`.

> Saved to the vault (`01-Architecture/`) per project convention (not the skill default `docs/superpowers/plans/`).

---

## Plan series position

**Plan 2 of the P1–P6 series.** P1 (declarative foundation) is **done + merged-ready on the branch**. This plan delivers the **pure scheduler core + adapter** (CPU-only, fully unit-tested). It produces working, testable software on its own (feed it a graph → get a validated schedule object). **Execute-time consumption** (nodes replaying barriers, `FrameSyncNode` timeline semaphore, the multi-submit wiring) is **P3+**, authored against the schedule types this plan lands.

### Design adjustments from the P2 grounding sweep (2026-06-21)
1. **Loop-agnostic compile.** Loop membership is populated at Execute (`SetLoopInput`/`connectedLoops`, `RenderGraph.cpp:564-572`), not compile. So the compile scheduler does NOT group by loop; it computes hazard-based sync over `GetExecutionOrder()`. Cross-loop cadence sync (Tier-3) is layered later at Execute. `SubmitGroup` still carries an optional `loopId` (default `0`) for forward-compat.
2. **`AccessKind` is supplied, not derived.** P1 left `ResourceAccess.kind == None` (real population is a later concern). The pure core therefore consumes whatever `AccessKind` it's given; unit tests set it explicitly. The adapter passes `ResourceAccess.kind` through; until nodes declare usage, real-graph kinds are `None` (the schedule degrades to execution-order edges with `UNDEFINED` layouts — harmless, since P3 is what first consumes it). Wiring real per-slot `AccessKind` is tracked as a P3 prerequisite, NOT this plan.
3. **No `GetAllResources()` on the tracker.** Iterate resources by walking `executionOrder` → `accessTracker.GetNodeResources(node)` and de-duping.

---

## Milestone Map

Segmentation for the post-brainstorm-context-manager pipeline (confirmed 2026-06-21). A resume reuses
this grouping verbatim — do not re-segment. Builds run FOREGROUND with `timeout: 600000`.

- [x] **Milestone 1 — Task 1:** `FrameSyncSchedule` data types. Implementer: **Haiku**. ✅
- [ ] **Milestone 2 — Task 2:** pure scheduling core (`BuildScheduleFromTimelines`). Implementer: **Sonnet**.
- [ ] **Milestone 3 — Task 3:** adapter (`FrameSyncScheduler::Build`) + `RenderGraph` Compile hook. Implementer: **Sonnet**.

Opus validates each milestone; the controller runs the `vixen-ninja` build gate between milestones.

### Progress Log
- **Milestone 1 (Task 1): DONE** · `FrameSyncSchedule.h` (6 structs: `ResourceAccessPoint`/`ResourceTimeline`/`GroupBarrier`/`SyncEdge`/`SubmitGroup`/`FrameSyncSchedule`) + `test_frame_sync_scheduler` (2/2) · commit `9a4fce35` · Opus validator **APPROVED** (type/field-name fidelity confirmed, no collisions) · full build green · 2026-06-21

---

## File structure (P2)

- **Create** `libraries/RenderGraph/include/Core/FrameSyncSchedule.h` — the schedule data types (`ResourceAccessPoint`, `ResourceTimeline`, `GroupBarrier`, `SyncEdge`, `SubmitGroup`, `FrameSyncSchedule`). Pure data, depends on `<vulkan/vulkan.h>` + `Core/BarrierTypes.h`.
- **Create** `libraries/RenderGraph/include/Core/FrameSyncScheduler.h` + `src/Core/FrameSyncScheduler.cpp` — the `BuildScheduleFromTimelines` pure core + the `FrameSyncScheduler::Build` adapter + `Recompute`/`Clear`.
- **Create** `libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp` — core tests (synthetic timelines) + adapter tests (MockNodeType/MockResource).
- **Modify** `libraries/RenderGraph/tests/CMakeLists.txt` — register `test_frame_sync_scheduler`.
- **Modify** `libraries/RenderGraph/include/Core/RenderGraph.h` + `src/Core/RenderGraph.cpp` — add `FrameSyncScheduler frameSyncScheduler_;` member, `Build(...)` call at the Compile hook (`:539`), and `GetFrameSyncSchedule()` accessor.

**Build:** `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c _ninja_preset_build.bat` — **run FOREGROUND with `timeout: 600000`** (full build takes minutes; backgrounding-and-waiting stalls — see `~/.claude/friction.md`).
**Run a test:** `cmd.exe /c "build-ninja\libraries\RenderGraph\tests\test_frame_sync_scheduler.exe --gtest_brief=1"`.

---

## Task 1: Schedule data types

**Files:** Create `libraries/RenderGraph/include/Core/FrameSyncSchedule.h`; Test `tests/test_frame_sync_scheduler.cpp`; Modify `tests/CMakeLists.txt`.

- [ ] **Step 1: Write the failing test** (`tests/test_frame_sync_scheduler.cpp`)

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/FrameSyncSchedule.h"
using namespace Vixen::RenderGraph;

TEST(FrameSyncScheduleTypes, DefaultScheduleIsEmptyAndInvalid) {
    FrameSyncSchedule s;
    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.groups.empty());
    EXPECT_TRUE(s.edges.empty());
    EXPECT_EQ(s.timelineValuesPerFrame, 0u);
}

TEST(FrameSyncScheduleTypes, GroupBarrierDistinguishesImageVsBuffer) {
    GroupBarrier b{};
    b.isImage = true;
    b.src = ResolveAccess(AccessKind::ComputeStorageWrite);
    b.dst = ResolveAccess(AccessKind::FragmentSampledRead);
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
```

- [ ] **Step 2: Register the test target** in `tests/CMakeLists.txt` (mirror the `test_barrier_types` block added in P1):

```cmake
add_executable(test_frame_sync_scheduler test_frame_sync_scheduler.cpp)
target_link_libraries(test_frame_sync_scheduler PRIVATE GTest::gtest_main RenderGraph)
set_target_properties(test_frame_sync_scheduler PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_frame_sync_scheduler)
message(STATUS "✓ test_frame_sync_scheduler configured (auto-sync P2)")
```

- [ ] **Step 3: Build FOREGROUND (`timeout: 600000`) — verify FAIL** (`Core/FrameSyncSchedule.h` missing).

- [ ] **Step 4: Implement** `libraries/RenderGraph/include/Core/FrameSyncSchedule.h`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "Core/BarrierTypes.h"

namespace Vixen::RenderGraph {

class Resource;       // logical resource identity (pointer key)
class NodeInstance;   // owning node of a submit group

/// One access to a resource at a known position in execution order.
struct ResourceAccessPoint {
    uint32_t   groupId = 0;                 ///< submit-group ordinal (execution order)
    AccessKind kind    = AccessKind::None;  ///< declarative sync semantics of this access
};

/// The ordered access history of a single resource across the frame.
struct ResourceTimeline {
    const Resource* resource = nullptr;
    bool isImage = false;                       ///< image ⇒ layout transitions apply
    std::vector<ResourceAccessPoint> accesses;  ///< sorted ascending by groupId
};

/// A barrier a consumer group records at its start to make a producer's
/// writes visible / transition an image layout. Buffers: layout fields ignored.
struct GroupBarrier {
    const Resource* resource = nullptr;
    AccessInfo src{};     ///< producer's resolved {stage, access, layout}
    AccessInfo dst{};     ///< consumer's resolved {stage, access, layout}
    bool isImage = false;
};

/// A cross-group ordering dependency, realized at Execute as a timeline
/// semaphore signal (producer) + wait (consumer). `timelineOffset` is the
/// frame-relative value; Execute adds a per-frame base.
struct SyncEdge {
    uint32_t fromGroup = 0;
    uint32_t toGroup   = 0;
    const Resource* resource = nullptr;
    uint64_t timelineOffset = 0;
};

/// One submit unit (1 node today; pass-group nodes collapse later).
struct SubmitGroup {
    uint32_t groupId = 0;
    NodeInstance* node = nullptr;
    uint32_t loopId = 0;                        ///< forward-compat; 0 = default loop (compile is loop-agnostic)
    std::vector<GroupBarrier> entryBarriers;    ///< recorded at this group's command-buffer start
    std::vector<uint32_t> waitEdges;            ///< indices into FrameSyncSchedule::edges (incoming)
    std::vector<uint32_t> signalEdges;          ///< indices into FrameSyncSchedule::edges (outgoing)
    bool swapchainAcquireWait = false;          ///< first group consuming the swapchain image (binary WSI wait)
    bool swapchainPresentSignal = false;        ///< last group before present (binary WSI signal)
};

/// The full baked schedule. Produced at Compile, replayed at Execute (P3+).
struct FrameSyncSchedule {
    std::vector<SubmitGroup> groups;   ///< indexed by groupId (execution order)
    std::vector<SyncEdge> edges;       ///< all inter-group edges
    uint64_t timelineValuesPerFrame = 0; ///< stride for the per-frame timeline base
    bool valid = false;

    void Clear() { groups.clear(); edges.clear(); timelineValuesPerFrame = 0; valid = false; }
};

} // namespace Vixen::RenderGraph
```

- [ ] **Step 5: Build FOREGROUND — run, verify PASS** (`test_frame_sync_scheduler` 2 tests).

- [ ] **Step 6: Commit**

```bash
git add VIXEN/libraries/RenderGraph/include/Core/FrameSyncSchedule.h \
        VIXEN/libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp \
        VIXEN/libraries/RenderGraph/tests/CMakeLists.txt
git commit -m "feat(rendergraph): FrameSyncSchedule data types (auto-sync P2)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: The pure scheduling core (`BuildScheduleFromTimelines`)

This is the algorithmic heart — pure, no graph/Vulkan-object dependencies, exhaustively unit-testable on synthetic timelines.

**Files:** Create `libraries/RenderGraph/include/Core/FrameSyncScheduler.h` + `src/Core/FrameSyncScheduler.cpp`; Test `tests/test_frame_sync_scheduler.cpp` (extend).

- [ ] **Step 1: Write failing tests** (append to `tests/test_frame_sync_scheduler.cpp`)

```cpp
#include "Core/FrameSyncScheduler.h"

// Helpers: a fake resource identity is just a distinct address.
static const Resource* R(int i) { return reinterpret_cast<const Resource*>(0x1000 + i); }

// compute(group0) writes image res; render(group1) samples it ⇒ one edge + a
// layout-transition entry barrier on the consumer (GENERAL → READ_ONLY_OPTIMAL).
TEST(FrameSyncCore, ComputeWriteThenFragmentRead_ImageHandoff) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), /*isImage=*/true,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, /*groupCount=*/2);

    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.edges.size(), 1u);
    EXPECT_EQ(s.edges[0].fromGroup, 0u);
    EXPECT_EQ(s.edges[0].toGroup, 1u);
    ASSERT_EQ(s.groups.size(), 2u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    const GroupBarrier& b = s.groups[1].entryBarriers[0];
    EXPECT_TRUE(b.isImage);
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // consumer waits the producer's signal
    ASSERT_EQ(s.groups[1].waitEdges.size(), 1u);
    ASSERT_EQ(s.groups[0].signalEdges.size(), 1u);
}

// Two consecutive reads of the same image in the same layout ⇒ NO sync.
TEST(FrameSyncCore, ReadAfterReadSameLayout_NoSync) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), true,
        {{0, AccessKind::FragmentSampledRead}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, 2);
    EXPECT_TRUE(s.edges.empty());
    EXPECT_TRUE(s.groups[1].entryBarriers.empty());
}

// Buffer RAW (compute write → compute read) ⇒ edge + memory barrier (no layout).
TEST(FrameSyncCore, BufferRAW_EdgeNoLayout) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), /*isImage=*/false,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::ComputeStorageRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, 2);
    ASSERT_EQ(s.edges.size(), 1u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_FALSE(s.groups[1].entryBarriers[0].isImage);
}

// Fan-in: groups 0 and 1 both write buffer res; group 2 reads ⇒ 2 incoming edges.
TEST(FrameSyncCore, FanIn_TwoProducersOneConsumer) {
    std::vector<ResourceTimeline> tl = {
        {R(0), false, {{0, AccessKind::ComputeStorageWrite}, {2, AccessKind::ComputeStorageRead}}},
        {R(1), false, {{1, AccessKind::ComputeStorageWrite}, {2, AccessKind::ComputeStorageRead}}},
    };
    FrameSyncSchedule s = BuildScheduleFromTimelines(tl, 3);
    EXPECT_EQ(s.edges.size(), 2u);
    EXPECT_EQ(s.groups[2].waitEdges.size(), 2u);
}

// Timeline offsets are monotonic per group ordinal; stride == groupCount.
TEST(FrameSyncCore, TimelineOffsetsAreGroupOrdinals) {
    std::vector<ResourceTimeline> tl = {
        {R(0), false, {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::ComputeStorageRead}}},
    };
    FrameSyncSchedule s = BuildScheduleFromTimelines(tl, 2);
    EXPECT_EQ(s.timelineValuesPerFrame, 2u);
    ASSERT_EQ(s.edges.size(), 1u);
    EXPECT_EQ(s.edges[0].timelineOffset, 0u); // producer is group 0 ⇒ signals offset 0
}
```

- [ ] **Step 2: Build FOREGROUND — verify FAIL** (`BuildScheduleFromTimelines` / header missing).

- [ ] **Step 3: Implement the core.** Create `libraries/RenderGraph/include/Core/FrameSyncScheduler.h`:

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/FrameSyncSchedule.h"
#include <vector>

namespace Vixen::RenderGraph {

class GraphTopology;        // (adapter, Task 3)
class ResourceAccessTracker;
class NodeInstance;

/// PURE core: given per-resource access timelines (sorted by groupId) and the
/// total group count, produce the baked schedule. No graph/Vulkan-object deps.
[[nodiscard]] FrameSyncSchedule BuildScheduleFromTimelines(
    const std::vector<ResourceTimeline>& timelines, uint32_t groupCount);

/// Compile-time scheduler. Extracts timelines from the access tracker + execution
/// order, then calls the pure core. Loop-agnostic (compile); GPU-object-free.
class FrameSyncScheduler {
public:
    /// @param executionOrder topo-sorted nodes (RenderGraph::GetExecutionOrder()).
    /// @param tracker the built ResourceAccessTracker.
    /// @param swapchainResource the resource backing the swapchain image, or nullptr.
    bool Build(const std::vector<NodeInstance*>& executionOrder,
               const ResourceAccessTracker& tracker,
               const Resource* swapchainResource = nullptr);

    void Clear() { schedule_.Clear(); }
    [[nodiscard]] const FrameSyncSchedule& GetSchedule() const { return schedule_; }
    [[nodiscard]] bool IsBuilt() const { return schedule_.valid; }

private:
    FrameSyncSchedule schedule_;
};

} // namespace Vixen::RenderGraph
```

Create `libraries/RenderGraph/src/Core/FrameSyncScheduler.cpp` (core first; adapter in Task 3):

```cpp
// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Core/FrameSyncScheduler.h"

namespace Vixen::RenderGraph {

namespace {
// A sync is required between a previous access and the current one when a write
// is involved (RAW/WAR/WAW) or — for images — the required layout changes.
bool NeedsSync(AccessKind prev, AccessKind cur, bool isImage) {
    const bool hazard = AccessWrites(prev) || AccessWrites(cur);
    if (hazard) return true;
    if (isImage) {
        return ResolveAccess(prev).layout != ResolveAccess(cur).layout;
    }
    return false; // read-after-read on a buffer: no sync
}
} // namespace

FrameSyncSchedule BuildScheduleFromTimelines(
    const std::vector<ResourceTimeline>& timelines, uint32_t groupCount) {
    FrameSyncSchedule s;
    s.groups.resize(groupCount);
    for (uint32_t g = 0; g < groupCount; ++g) s.groups[g].groupId = g;

    for (const ResourceTimeline& tl : timelines) {
        for (size_t i = 1; i < tl.accesses.size(); ++i) {
            const ResourceAccessPoint& prev = tl.accesses[i - 1];
            const ResourceAccessPoint& cur  = tl.accesses[i];
            if (prev.groupId == cur.groupId) continue;            // intra-group: handled by pass-group node (later)
            if (!NeedsSync(prev.kind, cur.kind, tl.isImage)) continue;

            // Inter-group edge (timeline) + entry barrier on the consumer.
            SyncEdge edge;
            edge.fromGroup = prev.groupId;
            edge.toGroup   = cur.groupId;
            edge.resource  = tl.resource;
            edge.timelineOffset = prev.groupId; // producer signals its ordinal
            const uint32_t edgeIdx = static_cast<uint32_t>(s.edges.size());
            s.edges.push_back(edge);
            s.groups[prev.groupId].signalEdges.push_back(edgeIdx);
            s.groups[cur.groupId].waitEdges.push_back(edgeIdx);

            GroupBarrier b;
            b.resource = tl.resource;
            b.src = ResolveAccess(prev.kind);
            b.dst = ResolveAccess(cur.kind);
            b.isImage = tl.isImage;
            s.groups[cur.groupId].entryBarriers.push_back(b);
        }
    }

    s.timelineValuesPerFrame = groupCount;
    s.valid = true;
    return s;
}

} // namespace Vixen::RenderGraph
```

- [ ] **Step 4: Register the .cpp** — add `src/Core/FrameSyncScheduler.cpp` to the RenderGraph library sources in `libraries/RenderGraph/CMakeLists.txt` (find the list that includes `src/Core/ResourceAccessTracker.cpp` and add the new file beside it). Verify by reading the surrounding lines.

- [ ] **Step 5: Build FOREGROUND — run, verify PASS** (all `FrameSyncCore` + `FrameSyncScheduleTypes` tests).

- [ ] **Step 6: Commit**

```bash
git add VIXEN/libraries/RenderGraph/include/Core/FrameSyncScheduler.h \
        VIXEN/libraries/RenderGraph/src/Core/FrameSyncScheduler.cpp \
        VIXEN/libraries/RenderGraph/CMakeLists.txt \
        VIXEN/libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp
git commit -m "feat(rendergraph): pure FrameSync scheduling core (auto-sync P2)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: The adapter + RenderGraph integration

Wire the pure core to the real graph: extract timelines from `(executionOrder, ResourceAccessTracker)`, assign one submit group per node, tag swapchain-adjacency, store on `RenderGraph`, expose via accessor. **No Execute consumption** (P3).

**Files:** Modify `src/Core/FrameSyncScheduler.cpp` (add `Build`); Modify `include/Core/RenderGraph.h` + `src/Core/RenderGraph.cpp`; Test `tests/test_frame_sync_scheduler.cpp` (extend with MockNodeType).

- [ ] **Step 1: Write the failing adapter test** (append to `tests/test_frame_sync_scheduler.cpp`; reuse the harness from `tests/test_wave_scheduler.cpp` — `MockNodeType`, `MockResource`, `GraphTopology`, `AddOutput`/`AddInput`; copy those helpers/fixture in if not shared)

```cpp
#include "Core/ResourceAccessTracker.h"
#include "Core/GraphTopology.h"
// (reuse MockNodeType / MockResource / AddOutput / AddInput from the wave-scheduler test pattern)

TEST(FrameSyncAdapter, WriterThenReader_ProducesEdge) {
    MockNodeType type("mock");
    auto writer = type.CreateInstance("writer");
    auto reader = type.CreateInstance("reader");
    MockResource res; res.debugName = "img";

    AddOutput(writer.get(), &res);   // writer writes res
    AddInput(reader.get(),  &res);   // reader reads res

    ResourceAccessTracker tracker;
    tracker.AddNode(writer.get());
    tracker.AddNode(reader.get());

    std::vector<NodeInstance*> execOrder = {writer.get(), reader.get()};
    FrameSyncScheduler scheduler;
    ASSERT_TRUE(scheduler.Build(execOrder, tracker));
    const FrameSyncSchedule& s = scheduler.GetSchedule();
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_EQ(s.groups[0].node, writer.get());
    EXPECT_EQ(s.groups[1].node, reader.get());
    EXPECT_EQ(s.edges.size(), 1u);   // RAW write→read across groups
}
```

> NOTE on `AccessKind`: until per-slot usage is declared (a P3 prerequisite), the tracker yields `kind == None`, and `ResolveAccess(None)` gives `UNDEFINED` layout with no read/write bits — so `NeedsSync` sees no hazard and this adapter test would get **0 edges**. To test the adapter's *wiring* deterministically now, the adapter must derive a provisional `AccessKind` from `ResourceAccessType` + image-ness when `kind == None` (write→`ComputeStorageWrite`, read→`ComputeStorageRead`). Implement that fallback in Step 2 so the test above expects 1 edge. Document it as provisional (replaced when real per-slot `AccessKind` lands in P3).

- [ ] **Step 2: Implement `FrameSyncScheduler::Build`** in `src/Core/FrameSyncScheduler.cpp` (append):

```cpp
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include <unordered_map>
#include <algorithm>

namespace Vixen::RenderGraph {

namespace {
// Provisional AccessKind until nodes declare per-slot usage (P3). Derives a
// safe compute-storage kind from the coarse read/write + image-ness so hazard
// detection is meaningful before real usage is wired.
AccessKind ProvisionalKind(ResourceAccessType t, bool /*isImage*/) {
    switch (t) {
    case ResourceAccessType::Write:     return AccessKind::ComputeStorageWrite;
    case ResourceAccessType::ReadWrite: return AccessKind::ComputeStorageReadWrite;
    case ResourceAccessType::Read:
    default:                            return AccessKind::ComputeStorageRead;
    }
}
} // namespace

bool FrameSyncScheduler::Build(const std::vector<NodeInstance*>& executionOrder,
                               const ResourceAccessTracker& tracker,
                               const Resource* swapchainResource) {
    schedule_.Clear();
    const uint32_t groupCount = static_cast<uint32_t>(executionOrder.size());

    // node -> group ordinal (1 node = 1 group for now)
    std::unordered_map<NodeInstance*, uint32_t> nodeToGroup;
    nodeToGroup.reserve(groupCount);
    for (uint32_t g = 0; g < groupCount; ++g) nodeToGroup[executionOrder[g]] = g;

    // Collect unique resources (no GetAllResources on the tracker) and, per
    // resource, the access points sorted by group ordinal.
    std::unordered_map<const Resource*, ResourceTimeline> byResource;
    for (uint32_t g = 0; g < groupCount; ++g) {
        NodeInstance* node = executionOrder[g];
        for (Resource* res : tracker.GetNodeResources(node)) {
            const ResourceAccessInfo* info = tracker.GetAccessInfo(res);
            if (!info) continue;
            ResourceTimeline& tl = byResource[res];
            tl.resource = res;
            // image-ness: leave false unless caller flags the swapchain image.
            // (Real per-resource image-ness arrives with per-slot usage in P3.)
            if (res == swapchainResource) tl.isImage = true;
            for (const ResourceAccess& a : info->accesses) {
                if (a.node != node) continue; // this group's access only
                AccessKind kind = (a.kind != AccessKind::None)
                    ? a.kind : ProvisionalKind(a.accessType, tl.isImage);
                tl.accesses.push_back({g, kind});
            }
        }
    }

    std::vector<ResourceTimeline> timelines;
    timelines.reserve(byResource.size());
    for (auto& [res, tl] : byResource) {
        std::sort(tl.accesses.begin(), tl.accesses.end(),
                  [](const ResourceAccessPoint& x, const ResourceAccessPoint& y) {
                      return x.groupId < y.groupId;
                  });
        timelines.push_back(std::move(tl));
    }

    schedule_ = BuildScheduleFromTimelines(timelines, groupCount);
    for (uint32_t g = 0; g < groupCount; ++g) schedule_.groups[g].node = executionOrder[g];

    // Swapchain-adjacency (binary WSI): first group touching the swapchain image
    // waits acquire; the last touching it signals present.
    if (swapchainResource) {
        int first = -1, last = -1;
        for (uint32_t g = 0; g < groupCount; ++g) {
            const auto& res = tracker.GetNodeResources(executionOrder[g]);
            if (std::find(res.begin(), res.end(), swapchainResource) != res.end()) {
                if (first < 0) first = static_cast<int>(g);
                last = static_cast<int>(g);
            }
        }
        if (first >= 0) schedule_.groups[first].swapchainAcquireWait = true;
        if (last  >= 0) schedule_.groups[last].swapchainPresentSignal = true;
    }
    return schedule_.valid;
}

} // namespace Vixen::RenderGraph
```

- [ ] **Step 3: Build FOREGROUND — run, verify the adapter test PASSES** (1 edge, groups wired to nodes).

- [ ] **Step 4: Hook into `RenderGraph`.** In `include/Core/RenderGraph.h`: add `#include "Core/FrameSyncScheduler.h"`, declare `FrameSyncScheduler frameSyncScheduler_;` beside `resourceAccessTracker_` (~`:941`), and add the accessor:

```cpp
[[nodiscard]] const FrameSyncSchedule& GetFrameSyncSchedule() const {
    return frameSyncScheduler_.GetSchedule();
}
```

In `src/Core/RenderGraph.cpp`, immediately after `resourceAccessTracker_.BuildFromTopology(topology);` (~`:534`) and its log line, add:

```cpp
// Auto-sync: bake the frame sync schedule from the access model (P2).
frameSyncScheduler_.Build(executionOrder, resourceAccessTracker_, /*swapchainResource=*/nullptr);
GRAPH_LOG_INFO("[RenderGraph::Compile] FrameSyncSchedule built: " +
    std::to_string(GetFrameSyncSchedule().groups.size()) + " groups, " +
    std::to_string(GetFrameSyncSchedule().edges.size()) + " edges");
```

(Pass `nullptr` for the swapchain resource for now; resolving the real swapchain `Resource*` is wired in P3 when the schedule is consumed. The build must stay green and the live app unaffected — nothing reads the schedule yet.)

- [ ] **Step 5: Full build FOREGROUND (`timeout: 600000`) — verify green + run the regression net** (`test_frame_sync_scheduler`, `test_resource_access_tracker`, `test_wave_scheduler`, `test_node_self_registration`). Quote summaries.

- [ ] **Step 6: Commit**

```bash
git add VIXEN/libraries/RenderGraph/src/Core/FrameSyncScheduler.cpp \
        VIXEN/libraries/RenderGraph/include/Core/RenderGraph.h \
        VIXEN/libraries/RenderGraph/src/Core/RenderGraph.cpp \
        VIXEN/libraries/RenderGraph/tests/test_frame_sync_scheduler.cpp
git commit -m "feat(rendergraph): FrameSyncScheduler adapter + Compile hook (auto-sync P2)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase P2 exit gate

- [ ] Full `vixen-ninja` build green (FOREGROUND, `timeout: 600000`).
- [ ] `test_frame_sync_scheduler` passes (types + core + adapter); regression net green (`test_resource_access_tracker`, `test_wave_scheduler`, `test_node_self_registration`).
- [ ] **No behavior change**: `RenderGraph::Compile` now logs a schedule but nothing consumes it; the live app path is unchanged (deferred app/syncval verification to P3, which first consumes the schedule).
- [ ] Commits for Tasks 1–3 on `feat/auto-sync-framegraph`.

**Next (P3):** wire real per-slot `AccessKind` (declare usage on the recording nodes' slots), resolve the real swapchain `Resource*` into `Build`, then have `ComputeDispatchNode` replay its group's `entryBarriers` (Tier-1) — the first phase that changes execution, gated by **syncval**.

---

## Self-review (P2 plan vs spec)

- **Spec coverage:** implements design components #3 (`FrameSyncScheduler` + structured `FrameSyncSchedule`) and the compile-side of the "centralized, loop-aware, rebuildable context." `Recompute` is satisfied by re-running `Build` each Compile (the device-loss path already recompiles). Tier-1 barriers + Tier-2 timeline edges are *computed* here; their *Execute replay* is explicitly P3+/P5 (matches the spec's phasing). Loop/Tier-3 deferral is documented and consistent with the spec.
- **Placeholders:** none. The one provisional element (`ProvisionalKind` fallback) is real, named, justified code with a documented replacement trigger (P3 per-slot usage) — not a TODO.
- **Type consistency:** `FrameSyncSchedule`/`SubmitGroup`/`SyncEdge`/`GroupBarrier`/`ResourceTimeline`/`ResourceAccessPoint` are defined in Task 1 and used unchanged in Tasks 2–3; `BuildScheduleFromTimelines(timelines, groupCount)` and `FrameSyncScheduler::Build(executionOrder, tracker, swapchainResource)` signatures match across declaration, definition, tests, and the RenderGraph call.

---

*Created 2026-06-21 by Claude Code (writing-plans). Plan 2 of 6. Spec: [[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]. Predecessor: [[Auto-Sync-FrameGraph-Inc1-Plan-2026-06]] (P1, done).*
