#pragma once
#include "IRenderTarget.h"
#include <stdexcept>
#include <vector>

namespace Vixen::RenderGraph {

struct TargetSemaphoreHandoffs {
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore present = VK_NULL_HANDLE;
};

// Roles decide which submit owns each handoff; the target decides whether WSI exists at all.
// Validate required window handoffs before any fence reset, command recording or array access.
inline TargetSemaphoreHandoffs ResolveTargetSemaphoreHandoffs(
    const Vixen::Vulkan::Resources::IRenderTarget* target,
    const std::vector<VkSemaphore>& acquire, const std::vector<VkSemaphore>& present,
    uint32_t frameIndex, uint32_t imageIndex, bool consumesAcquire, bool signalsPresent) {
    if (!target) {
        if (consumesAcquire || signalsPresent) throw std::runtime_error("WSI role has no presentation target");
        return {};
    }
    if (!target->UsesWsiSynchronization()) return {};
    TargetSemaphoreHandoffs result;
    if (consumesAcquire) {
        if (frameIndex >= acquire.size() || acquire[frameIndex] == VK_NULL_HANDLE)
            throw std::runtime_error("Window target is missing its frame-indexed acquire semaphore");
        result.acquire = acquire[frameIndex];
    }
    if (signalsPresent) {
        if (imageIndex >= present.size() || present[imageIndex] == VK_NULL_HANDLE)
            throw std::runtime_error("Window target is missing its image-indexed present semaphore");
        result.present = present[imageIndex];
    }
    return result;
}

} // namespace Vixen::RenderGraph
