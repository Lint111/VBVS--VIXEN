#pragma once

#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief Capability interface for nodes that own a command pool and can
 *        pre-allocate command buffers ahead of the first frame.
 *
 * The graph core (RenderGraph::PreAllocateResources) aggregates command-buffer
 * requirements across all nodes via NodeInstance::GetPreAllocationRequirements(),
 * then asks a command-pool-owning node to reserve them up-front — eliminating the
 * per-frame allocation cost. This interface lets the core do that WITHOUT depending
 * on the concrete CommandPoolNode type: the core dynamic_casts each instance to this
 * capability and calls it, exactly as it does for IGraphCompilable (Core/IGraphCompilable.h).
 *
 * This keeps the graph engine (Core/) free of #includes of concrete leaf nodes
 * (Nodes/), preserving a one-directional Core → Nodes layering (AR#3/#4).
 */
class ICommandBufferPreallocator {
public:
    virtual ~ICommandBufferPreallocator() = default;

    /**
     * @brief Pre-allocate command buffers in this node's pool.
     * @param primaryCount   Number of primary command buffers to reserve.
     * @param secondaryCount Number of secondary command buffers to reserve.
     *
     * No default argument here: defaults on virtual methods bind statically and
     * would be a footgun across the interface boundary. Callers pass both counts
     * explicitly; the concrete node may still offer a defaulted convenience overload.
     */
    virtual void PreAllocateCommandBuffers(uint32_t primaryCount, uint32_t secondaryCount) = 0;
};

} // namespace Vixen::RenderGraph
