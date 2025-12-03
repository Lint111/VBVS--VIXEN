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
    const auto& bundles = consumer->GetBundles();

    // Phase F: Iterate through bundles (bundle-first organization)
    for (size_t bundleIndex = 0; bundleIndex < bundles.size(); ++bundleIndex) {
        const auto& bundle = bundles[bundleIndex];

        // Iterate through all input slots in this bundle
        for (size_t slotIndex = 0; slotIndex < bundle.inputs.size(); ++slotIndex) {
            Resource* resource = bundle.inputs[slotIndex];
            if (!resource) continue;

            // Only consider this input for compile-time dependencies if the
            // consumer marked it as used during the last Compile() call. This
            // allows Execute/CleanupOnly accesses to be ignored for
            // compile-order dependency construction.
            if (!consumer->IsInputUsedInCompile(static_cast<uint32_t>(slotIndex), static_cast<uint32_t>(bundleIndex))) {
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
    const auto& bundles = consumer->GetBundles();

    // Phase F: For cleanup we want to depend on any producer that provides a resource
    // to this consumer, regardless of whether the consumer used it during
    // Compile(). Iterate all bundles and input slots and collect unique producers.
    for (const auto& bundle : bundles) {
        for (Resource* resource : bundle.inputs) {
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
