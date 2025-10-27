#include "Core/ResourceDependencyTracker.h"
#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"
#include <algorithm>

namespace Vixen::RenderGraph {

void ResourceDependencyTracker::RegisterResourceProducer(
    Resource* resource,
    NodeInstance* producer,
    uint32_t outputSlotIndex)
{
    if (!resource || !producer) {
        return;
    }

    // Register resource → producer mapping
    resourceToProducer[resource] = producer;

    // Register producer → resources mapping (for cleanup)
    producerToResources[producer].push_back(resource);
}

NodeInstance* ResourceDependencyTracker::GetProducer(Resource* resource) const {
    auto it = resourceToProducer.find(resource);
    return (it != resourceToProducer.end()) ? it->second : nullptr;
}

std::vector<NodeInstance*> ResourceDependencyTracker::GetDependenciesForNode(
    NodeInstance* consumer) const
{
    if (!consumer) {
        return {};
    }

    std::vector<NodeInstance*> dependencies;
    const auto& inputs = consumer->GetInputs();

    // Iterate through all input slots
    for (size_t slotIndex = 0; slotIndex < inputs.size(); ++slotIndex) {
        const auto& slotArray = inputs[slotIndex];

        // Iterate through all resources in this slot (handles arrays)
        for (Resource* resource : slotArray) {
            if (resource) {
                NodeInstance* producer = GetProducer(resource);
                if (producer) {
                    // Only add unique producers
                    if (std::find(dependencies.begin(), dependencies.end(), producer) == dependencies.end()) {
                        dependencies.push_back(producer);
                    }
                }
            }
        }
    }

    return dependencies;
}

std::vector<NodeHandle> ResourceDependencyTracker::BuildCleanupDependencies(
    NodeInstance* consumer) const
{
    std::vector<NodeInstance*> producers = GetDependenciesForNode(consumer);
    std::vector<NodeHandle> cleanupHandles;
    cleanupHandles.reserve(producers.size());

    for (NodeInstance* producer : producers) {
        cleanupHandles.push_back(producer->GetHandle());
    }

    return cleanupHandles;
}

void ResourceDependencyTracker::Clear() {
    resourceToProducer.clear();
    producerToResources.clear();
}

} // namespace Vixen::RenderGraph
