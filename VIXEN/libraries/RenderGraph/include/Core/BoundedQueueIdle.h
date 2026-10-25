#pragma once

// R516 (gpuharden): a vkQueueWaitIdle replacement bounded under the platform watchdog.
//
// The queue owner already assigns a timeline value to every submission. Teardown waits on that
// owner timeline instead of publishing an extra empty submit on the physical queue.

#include "Core/GpuFault.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph { class RenderGraph; }

namespace Vixen::RenderGraph {

class BoundedQueueIdle {
public:
    BoundedQueueIdle() = default;
    BoundedQueueIdle(const BoundedQueueIdle&) = delete;
    BoundedQueueIdle& operator=(const BoundedQueueIdle&) = delete;

    /// Wait until the queue-owner timeline reaches its newest value, or the bound expires.
    [[nodiscard]] GpuWaitResult Wait(RenderGraph& graph,
                                     Vixen::Vulkan::Resources::VulkanDevice& dev,
                                     uint64_t boundNs = GpuWaitBoundNs());
};

}  // namespace Vixen::RenderGraph
