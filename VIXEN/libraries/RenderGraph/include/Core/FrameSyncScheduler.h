// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/FrameSyncSchedule.h"
#include <vector>

namespace Vixen::RenderGraph {

class GraphTopology;
class ResourceAccessTracker;
class NodeInstance;

[[nodiscard]] FrameSyncSchedule BuildScheduleFromTimelines(
    const std::vector<ResourceTimeline>& timelines, uint32_t groupCount);

class FrameSyncScheduler {
public:
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
