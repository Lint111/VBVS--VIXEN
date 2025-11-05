#pragma once

#include "Data/Core/ResourceVariant.h"
#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief Narrow interface for graph wiring operations
 *
 * Provides controlled access to node connection methods. This interface
 * replaces the `friend class RenderGraph` declaration, improving encapsulation
 * by exposing only the methods RenderGraph needs for graph construction.
 *
 * **Design Pattern**: Interface Segregation Principle
 * - RenderGraph only sees graph wiring methods (GetInput/SetInput/GetOutput/SetOutput)
 * - Hides all other NodeInstance internals (state, lifecycle, execution)
 * - Enforces single responsibility (graph wiring separate from execution)
 *
 * **Thread Safety**: NOT thread-safe
 * - All wiring operations must occur on the same thread
 * - Wiring must complete before any graph execution begins
 * - Do not call wiring methods during Execute()
 */
class INodeWiring {
public:
    virtual ~INodeWiring() = default;

    /**
     * @brief Get input resource at slot/array index
     *
     * Used by RenderGraph during validation to check if required inputs are connected.
     *
     * @param slotIndex Slot index (0-based, matches input schema order)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @return Resource pointer, or nullptr if not connected
     *
     * **Thread Safety**: NOT thread-safe - call only during graph construction
     */
    virtual Resource* GetInput(uint32_t slotIndex, uint32_t arrayIndex = 0) const = 0;

    /**
     * @brief Get output resource at slot/array index
     *
     * Used by RenderGraph during ConnectNodes() to wire node connections.
     *
     * @param slotIndex Slot index (0-based, matches output schema order)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @return Resource pointer, or nullptr if not yet created
     *
     * **Thread Safety**: NOT thread-safe - call only during graph construction
     */
    virtual Resource* GetOutput(uint32_t slotIndex, uint32_t arrayIndex = 0) const = 0;

    /**
     * @brief Set input resource at slot/array index
     *
     * Used by RenderGraph during ConnectNodes() to establish input connections.
     * This is the PRIMARY method for wiring node inputs in the graph.
     *
     * @param slotIndex Slot index (0-based, matches input schema order)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @param resource Resource pointer (lifetime managed by graph)
     *
     * **Preconditions**:
     * - slotIndex must be valid for node's input schema
     * - arrayIndex must be < array size for the slot
     * - resource must not be nullptr
     *
     * **Thread Safety**: NOT thread-safe - call only during graph construction
     */
    virtual void SetInput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) = 0;

    /**
     * @brief Set output resource at slot/array index
     *
     * Used by RenderGraph during ConnectNodes() to allocate output resources.
     * Typically called when an output hasn't been created yet.
     *
     * @param slotIndex Slot index (0-based, matches output schema order)
     * @param arrayIndex Array element index (0 for non-array slots)
     * @param resource Resource pointer (lifetime managed by graph)
     *
     * **Preconditions**:
     * - slotIndex must be valid for node's output schema
     * - arrayIndex must be < array size for the slot
     * - resource must not be nullptr
     *
     * **Thread Safety**: NOT thread-safe - call only during graph construction
     */
    virtual void SetOutput(uint32_t slotIndex, uint32_t arrayIndex, Resource* resource) = 0;
};

} // namespace Vixen::RenderGraph
