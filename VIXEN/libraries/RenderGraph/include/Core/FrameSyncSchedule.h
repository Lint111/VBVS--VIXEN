// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "Core/BarrierTypes.h"

namespace Vixen::RenderGraph {

class Resource;
class NodeInstance;

struct ResourceAccessPoint {
    uint32_t   groupId = 0;
    AccessKind kind    = AccessKind::None;
};

struct ResourceTimeline {
    const Resource* resource = nullptr;
    bool isImage = false;
    std::vector<ResourceAccessPoint> accesses;  // sorted ascending by groupId
};

struct GroupBarrier {
    const Resource* resource = nullptr;
    AccessInfo src{};
    AccessInfo dst{};
    bool isImage = false;
};

struct SyncEdge {
    uint32_t fromGroup = 0;
    uint32_t toGroup   = 0;
    const Resource* resource = nullptr;
    uint64_t timelineOffset = 0;
};

struct SubmitGroup {
    uint32_t groupId = 0;
    NodeInstance* node = nullptr;
    uint32_t loopId = 0;
    std::vector<GroupBarrier> entryBarriers;
    std::vector<uint32_t> waitEdges;    // indices into FrameSyncSchedule::edges
    std::vector<uint32_t> signalEdges;  // indices into FrameSyncSchedule::edges
    bool swapchainAcquireWait = false;
    bool swapchainPresentSignal = false;
};

struct FrameSyncSchedule {
    std::vector<SubmitGroup> groups;
    std::vector<SyncEdge> edges;
    uint64_t timelineValuesPerFrame = 0;
    bool valid = false;

    void Clear() { groups.clear(); edges.clear(); timelineValuesPerFrame = 0; valid = false; }
};

} // namespace Vixen::RenderGraph
