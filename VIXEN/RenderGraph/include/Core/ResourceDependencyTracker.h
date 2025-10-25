#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
class Resource;

/**
 * @brief Tracks which NodeInstance provides which Resource
 * 
 * Enables dynamic dependency resolution:
 * - Given a Resource*, find the NodeInstance that produced it
 * - Build cleanup dependency chains automatically from input slots
 * - Support multiple instances of same NodeType
 */
class ResourceDependencyTracker {
public:
    /**
     * @brief Register that a NodeInstance produces a Resource at an output slot
     * @param resource The Resource being produced
     * @param producer The NodeInstance that creates this resource
     * @param outputSlotIndex Which output slot produces this resource
     */
    void RegisterResourceProducer(
        Resource* resource,
        NodeInstance* producer,
        uint32_t outputSlotIndex
    );

    /**
     * @brief Get the NodeInstance that produces a given Resource
     * @param resource The resource to query
     * @return Pointer to producer NodeInstance, or nullptr if not found
     */
    NodeInstance* GetProducer(Resource* resource) const;

    /**
     * @brief Get all NodeInstances that this node depends on via its input slots
     * @param consumer The NodeInstance whose dependencies we want to find
     * @return Vector of producer NodeInstances this consumer depends on
     */
    std::vector<NodeInstance*> GetDependenciesForNode(NodeInstance* consumer) const;

    /**
     * @brief Build cleanup dependency names for a NodeInstance
     * 
     * Looks at all input slots, finds producer instances, returns their cleanup names.
     * Used when registering with CleanupStack.
     * 
     * @param consumer The NodeInstance that consumes resources
     * @return Vector of cleanup names this node depends on (must cleanup after these)
     */
    std::vector<std::string> BuildCleanupDependencies(NodeInstance* consumer) const;

    /**
     * @brief Clear all tracked dependencies
     */
    void Clear();

    /**
     * @brief Get number of tracked resource producers
     */
    size_t GetTrackedResourceCount() const { return resourceToProducer.size(); }

private:
    // Map: Resource pointer → NodeInstance that produces it
    std::unordered_map<Resource*, NodeInstance*> resourceToProducer;

    // Map: NodeInstance → Resources it produces (for bidirectional lookup)
    std::unordered_map<NodeInstance*, std::vector<Resource*>> producerToResources;
};

} // namespace Vixen::RenderGraph
