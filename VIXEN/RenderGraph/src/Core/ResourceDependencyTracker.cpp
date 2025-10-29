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
        for (size_t arrayIndex = 0; arrayIndex < slotArray.size(); ++arrayIndex) {
            Resource* resource = slotArray[arrayIndex];
            if (!resource) continue;

            // Only consider this input for compile-time dependencies if the
            // consumer marked it as used during the last Compile() call. This
            // allows ExecuteOnly/CleanupOnly accesses to be ignored for
            // compile-order dependency construction.
            if (!consumer->IsInputUsedInCompile(static_cast<uint32_t>(slotIndex), static_cast<uint32_t>(arrayIndex))) {
                continue;
            }

            NodeInstance* producer = GetProducer(resource);
            if (!producer) continue;

            // Only add unique producers
            if (std::find(dependencies.begin(), dependencies.end(), producer) == dependencies.end()) {
                dependencies.push_back(producer);
            }
        }
    }

    return dependencies;
}

std::vector<NodeHandle> ResourceDependencyTracker::BuildCleanupDependencies(
    NodeInstance* consumer) const
{
    std::vector<NodeHandle> cleanupHandles;
    const auto& inputs = consumer->GetInputs();

    // For cleanup we want to depend on any producer that provides a resource
    // to this consumer, regardless of whether the consumer used it during
    // Compile(). Iterate all input slots and collect unique producers.
    for (size_t slotIndex = 0; slotIndex < inputs.size(); ++slotIndex) {
        const auto& slotArray = inputs[slotIndex];
        for (Resource* resource : slotArray) {
            if (!resource) continue;
            NodeInstance* producer = GetProducer(resource);
            if (!producer) continue;
            NodeHandle h = producer->GetHandle();
            if (std::find(cleanupHandles.begin(), cleanupHandles.end(), h) == cleanupHandles.end()) {
                cleanupHandles.push_back(h);
            }
        }
    }

    return cleanupHandles;
}

void ResourceDependencyTracker::Clear() {
    resourceToProducer.clear();
    producerToResources.clear();
}

} // namespace Vixen::RenderGraph
