// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Core/FrameSyncScheduler.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include <unordered_map>
#include <algorithm>

namespace Vixen::RenderGraph {

namespace {
bool NeedsSync(AccessKind prev, AccessKind cur, bool isImage) {
    const bool hazard = AccessWrites(prev) || AccessWrites(cur);
    if (hazard) return true;
    if (isImage) return ResolveAccess(prev).layout != ResolveAccess(cur).layout;
    return false;
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
            if (prev.groupId == cur.groupId) continue;
            if (!NeedsSync(prev.kind, cur.kind, tl.isImage)) continue;

            SyncEdge edge;
            edge.fromGroup = prev.groupId;
            edge.toGroup   = cur.groupId;
            edge.resource  = tl.resource;
            edge.timelineOffset = prev.groupId;
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

namespace {
AccessKind ProvisionalKind(ResourceAccessType t, bool /*isImage*/) {
    switch (t) {
    case ResourceAccessType::Write:     return AccessKind::ComputeStorageWrite;
    case ResourceAccessType::ReadWrite: return AccessKind::ComputeStorageReadWrite;
    case ResourceAccessType::Read:
    default:                            return AccessKind::ComputeStorageRead;
    }
}
} // anonymous namespace

bool FrameSyncScheduler::Build(const std::vector<NodeInstance*>& executionOrder,
                               const ResourceAccessTracker& tracker,
                               const Resource* swapchainResource) {
    schedule_.Clear();
    const uint32_t groupCount = static_cast<uint32_t>(executionOrder.size());

    std::unordered_map<Resource*, ResourceTimeline> byResource;
    for (uint32_t g = 0; g < groupCount; ++g) {
        NodeInstance* node = executionOrder[g];
        for (Resource* res : tracker.GetNodeResources(node)) {
            const ResourceAccessInfo* info = tracker.GetAccessInfo(res);
            if (!info) continue;
            ResourceTimeline& tl = byResource[res];
            tl.resource = res;
            if (res == swapchainResource) tl.isImage = true;
            for (const ResourceAccess& a : info->accesses) {
                if (a.node != node) continue;
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

    if (swapchainResource) {
        int first = -1, last = -1;
        for (uint32_t g = 0; g < groupCount; ++g) {
            const auto res = tracker.GetNodeResources(executionOrder[g]);
            if (std::find(res.begin(), res.end(),
                          const_cast<Resource*>(swapchainResource)) != res.end()) {
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
