#include "Core/BoundedQueueIdle.h"
#include "Core/RenderGraph.h"

namespace Vixen::RenderGraph {

GpuWaitResult BoundedQueueIdle::Wait(RenderGraph& graph,
                                     Vixen::Vulkan::Resources::VulkanDevice& device,
                                     uint64_t boundNs) {
    return graph.WaitIssuedWork(&device, boundNs);
}

} // namespace Vixen::RenderGraph
