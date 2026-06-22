// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Core/FrameSyncScheduler.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include <unordered_map>
#include <algorithm>
#include <cassert>

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

            // Only DECLARED GPU hazards participate in the timeline. An access is a real
            // GPU memory hazard iff it carries an explicit AccessKind (declared via an
            // INPUT_SLOT_SYNC / sync output slot). Plain accessKind=None accesses are NOT
            // GPU hazards — they are either handle/config passthroughs (device, command
            // pool, semaphores, the buffer-handle passthrough) OR CPU-side metadata reads
            // of an image handle (e.g. StorageBufferNode/DescriptorSetNode read the
            // swapchain IRenderTarget* only for extent/image-count at compile time). Baking
            // edges for these manufactured spurious write→read / WAR edges whose endpoint
            // group is a NON-submitting data node (which never signals its timeline value),
            // so a consumer waiting such an edge hangs forever — surfacing downstream as
            // VUID-vkQueuePresentKHR-pWaitSemaphores-03268. The swapchain image hazard is
            // itself expressed via a declared AccessKind (ComputeStorageWrite today; UI's
            // ColorAttachment kind arrives in M3), so the real swapchain edge survives this
            // gate; the untyped swapchain *metadata* reads are correctly excluded. We still
            // flag the swapchain timeline isImage so its barriers resolve image layouts.
            // (The acquire/present tagging below scans GetNodeResources independently, so it
            // is unaffected by which accesses enter the timeline.)
            const bool isSwapchain = (res == swapchainResource);
            bool hasDeclaredAccessHere = false;
            for (const ResourceAccess& a : info->accesses) {
                if (a.node == node && a.kind != AccessKind::None) { hasDeclaredAccessHere = true; break; }
            }
            if (!hasDeclaredAccessHere) continue;

            ResourceTimeline& tl = byResource[res];
            tl.resource = res;
            if (isSwapchain) tl.isImage = true;
            for (const ResourceAccess& a : info->accesses) {
                if (a.node != node) continue;
                if (a.kind == AccessKind::None) continue;  // only declared GPU hazards
                tl.accesses.push_back({g, a.kind});
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

        // WSI TIMELINE LAW (schedule-build-time invariant):
        // The swapchain acquire (imageAvailable) and present (renderComplete) synchronisation
        // points are BINARY semaphore boundaries — they must NEVER be expressed as timeline
        // SyncEdges on the swapchain resource.  Binary and timeline semaphores are distinct
        // objects with distinct signalling semantics; a timeline edge on the acquire/present
        // boundary would bypass the WSI contract and produce VUID errors.
        //
        // The acquire group's binary wait (swapchainAcquireWait) is independent of timeline
        // waitEdges: an edge from a "pre-acquire" group on the swapchain resource would mean
        // a non-WSI group signals the acquire semaphore value — which never happens.
        // The present group's binary signal (swapchainPresentSignal) is independent of
        // timeline signalEdges on the swapchain resource for the same reason.
        //
        // This assert fires at schedule-build time (cheap, not a hot-path check) if the
        // edge-baking logic ever incorrectly creates a timeline edge where the ONLY shared
        // resource is the swapchain AND one endpoint is the acquire or present group.
        if (first >= 0 && last >= 0) {
            const uint32_t acquireGroupId = static_cast<uint32_t>(first);
            const uint32_t presentGroupId = static_cast<uint32_t>(last);
            for (const SyncEdge& e : schedule_.edges) {
                if (e.resource != swapchainResource) continue;
                // A timeline edge whose TO-group is the acquire group would mean a pre-acquire
                // node must signal a timeline value before the acquire group can proceed —
                // but the acquire group's GPU work is already gated by the BINARY imageAvailable.
                // A timeline edge whose FROM-group is the present group would mean the present
                // group is expected to signal a timeline value AFTER present — meaningless since
                // present owns the binary renderComplete signal, not a timeline signal.
                (void)acquireGroupId; (void)presentGroupId;
                assert(e.toGroup != acquireGroupId
                    && "WSI law violated: timeline SyncEdge targets the swapchain-acquire group; "
                       "imageAvailable is a binary boundary — never a timeline wait.");
                assert(e.fromGroup != presentGroupId
                    && "WSI law violated: timeline SyncEdge originates from the swapchain-present group; "
                       "renderComplete is a binary boundary — never a timeline signal.");
            }
        }
    }
    return schedule_.valid;
}

} // namespace Vixen::RenderGraph
