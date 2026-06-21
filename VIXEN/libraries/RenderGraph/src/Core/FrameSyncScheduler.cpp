// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Core/FrameSyncScheduler.h"

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

} // namespace Vixen::RenderGraph
