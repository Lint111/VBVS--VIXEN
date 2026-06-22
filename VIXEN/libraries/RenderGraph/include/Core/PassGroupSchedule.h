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
