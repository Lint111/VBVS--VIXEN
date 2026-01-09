// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/ResourceAccessTracker.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource class
#include <algorithm>

namespace Vixen::RenderGraph {

// ============================================================================
// ResourceAccessInfo Implementation
// ============================================================================

std::vector<NodeInstance*> ResourceAccessInfo::GetWriters() const {
    std::vector<NodeInstance*> writers;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Write ||
            access.accessType == ResourceAccessType::ReadWrite) {
            writers.push_back(access.node);
        }
    }
    return writers;
}

std::vector<NodeInstance*> ResourceAccessInfo::GetReaders() const {
    std::vector<NodeInstance*> readers;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Read ||
            access.accessType == ResourceAccessType::ReadWrite) {
            readers.push_back(access.node);
        }
    }
    return readers;
}

bool ResourceAccessInfo::HasWriter() const {
    return std::any_of(accesses.begin(), accesses.end(), [](const ResourceAccess& a) {
        return a.accessType == ResourceAccessType::Write ||
               a.accessType == ResourceAccessType::ReadWrite;
    });
}

bool ResourceAccessInfo::HasMultipleWriters() const {
    size_t writerCount = 0;
    for (const auto& access : accesses) {
        if (access.accessType == ResourceAccessType::Write ||
            access.accessType == ResourceAccessType::ReadWrite) {
            ++writerCount;
            if (writerCount > 1) return true;
        }
    }
    return false;
}

// ============================================================================
// ResourceAccessTracker Implementation
// ============================================================================

void ResourceAccessTracker::BuildFromTopology(const GraphTopology& topology) {
    Clear();

    for (NodeInstance* node : topology.GetNodes()) {
        if (node) {
            AddNode(node);
        }
    }
}

void ResourceAccessTracker::AddNode(NodeInstance* node) {
    if (!node) return;

    const auto& bundles = node->GetBundles();

    // Process each bundle (task/array index)
    for (const auto& bundle : bundles) {
        // Outputs are WRITES
        for (size_t slotIndex = 0; slotIndex < bundle.outputs.size(); ++slotIndex) {
            Resource* resource = bundle.outputs[slotIndex];
            if (resource) {
                RecordAccess(
                    resource,
                    node,
                    ResourceAccessType::Write,
                    static_cast<uint32_t>(slotIndex),
                    true  // isOutput
                );
            }
        }

        // Inputs are READS (conservative default)
        // TODO Phase 1: Check SlotMutability for ReadWrite inputs
        for (size_t slotIndex = 0; slotIndex < bundle.inputs.size(); ++slotIndex) {
            Resource* resource = bundle.inputs[slotIndex];
            if (resource) {
                RecordAccess(
                    resource,
                    node,
                    ResourceAccessType::Read,
                    static_cast<uint32_t>(slotIndex),
                    false  // isOutput
                );
            }
        }
    }
}

void ResourceAccessTracker::Clear() {
    resourceAccesses_.clear();
    nodeResources_.clear();
    nodeWrites_.clear();
    nodeReads_.clear();
}

bool ResourceAccessTracker::HasConflict(NodeInstance* nodeA, NodeInstance* nodeB) const {
    if (!nodeA || !nodeB || nodeA == nodeB) return false;

    // Get writes from both nodes
    auto itWritesA = nodeWrites_.find(nodeA);
    auto itWritesB = nodeWrites_.find(nodeB);
    auto itReadsA = nodeReads_.find(nodeA);
    auto itReadsB = nodeReads_.find(nodeB);

    // Check: A writes to something B writes to (Write-Write conflict)
    if (itWritesA != nodeWrites_.end() && itWritesB != nodeWrites_.end()) {
        for (Resource* resA : itWritesA->second) {
            if (itWritesB->second.count(resA) > 0) {
                return true;  // Both write to same resource
            }
        }
    }

    // Check: A writes to something B reads (Write-Read conflict)
    if (itWritesA != nodeWrites_.end() && itReadsB != nodeReads_.end()) {
        for (Resource* resA : itWritesA->second) {
            if (itReadsB->second.count(resA) > 0) {
                return true;  // A writes what B reads
            }
        }
    }

    // Check: B writes to something A reads (Read-Write conflict)
    if (itWritesB != nodeWrites_.end() && itReadsA != nodeReads_.end()) {
        for (Resource* resB : itWritesB->second) {
            if (itReadsA->second.count(resB) > 0) {
                return true;  // B writes what A reads
            }
        }
    }

    // No conflict: either no shared resources, or both only read shared resources
    return false;
}

std::unordered_set<NodeInstance*> ResourceAccessTracker::GetConflictingNodes(NodeInstance* node) const {
    std::unordered_set<NodeInstance*> conflicting;
    if (!node) return conflicting;

    // Get all resources this node accesses
    auto itResources = nodeResources_.find(node);
    if (itResources == nodeResources_.end()) return conflicting;

    // Check each resource for conflicts
    for (Resource* resource : itResources->second) {
        auto itAccess = resourceAccesses_.find(resource);
        if (itAccess == resourceAccesses_.end()) continue;

        const auto& info = itAccess->second;

        // If this node writes to the resource, all other accessors conflict
        auto itWrites = nodeWrites_.find(node);
        bool nodeWritesToResource = itWrites != nodeWrites_.end() &&
                                    itWrites->second.count(resource) > 0;

        for (const auto& access : info.accesses) {
            if (access.node == node) continue;

            if (nodeWritesToResource) {
                // Node writes → conflict with all other accessors
                conflicting.insert(access.node);
            } else if (access.accessType == ResourceAccessType::Write ||
                       access.accessType == ResourceAccessType::ReadWrite) {
                // Other node writes → conflict
                conflicting.insert(access.node);
            }
            // Both read → no conflict
        }
    }

    return conflicting;
}

std::vector<Resource*> ResourceAccessTracker::GetSharedResources(
    NodeInstance* nodeA,
    NodeInstance* nodeB
) const {
    std::vector<Resource*> shared;
    if (!nodeA || !nodeB) return shared;

    auto itA = nodeResources_.find(nodeA);
    auto itB = nodeResources_.find(nodeB);
    if (itA == nodeResources_.end() || itB == nodeResources_.end()) return shared;

    // Convert B's resources to set for O(1) lookup
    std::unordered_set<Resource*> resourcesB(itB->second.begin(), itB->second.end());

    for (Resource* res : itA->second) {
        if (resourcesB.count(res) > 0) {
            shared.push_back(res);
        }
    }

    return shared;
}

const ResourceAccessInfo* ResourceAccessTracker::GetAccessInfo(Resource* resource) const {
    auto it = resourceAccesses_.find(resource);
    return it != resourceAccesses_.end() ? &it->second : nullptr;
}

std::vector<Resource*> ResourceAccessTracker::GetNodeResources(NodeInstance* node) const {
    auto it = nodeResources_.find(node);
    return it != nodeResources_.end() ? it->second : std::vector<Resource*>{};
}

std::vector<Resource*> ResourceAccessTracker::GetNodeWrites(NodeInstance* node) const {
    auto it = nodeWrites_.find(node);
    if (it == nodeWrites_.end()) return {};
    return std::vector<Resource*>(it->second.begin(), it->second.end());
}

std::vector<Resource*> ResourceAccessTracker::GetNodeReads(NodeInstance* node) const {
    auto it = nodeReads_.find(node);
    if (it == nodeReads_.end()) return {};
    return std::vector<Resource*>(it->second.begin(), it->second.end());
}

bool ResourceAccessTracker::IsWriter(NodeInstance* node) const {
    auto it = nodeWrites_.find(node);
    return it != nodeWrites_.end() && !it->second.empty();
}

size_t ResourceAccessTracker::GetConflictingResourceCount() const {
    size_t count = 0;
    for (const auto& [resource, info] : resourceAccesses_) {
        if (info.HasWriter() && info.accesses.size() > 1) {
            // Resource has a writer and multiple accessors = potential conflict
            ++count;
        }
    }
    return count;
}

size_t ResourceAccessTracker::GetMaxWritersPerResource() const {
    size_t maxWriters = 0;
    for (const auto& [resource, info] : resourceAccesses_) {
        size_t writers = info.GetWriters().size();
        maxWriters = std::max(maxWriters, writers);
    }
    return maxWriters;
}

void ResourceAccessTracker::RecordAccess(
    Resource* resource,
    NodeInstance* node,
    ResourceAccessType accessType,
    uint32_t slotIndex,
    bool isOutput
) {
    if (!resource || !node) return;

    // Record in resource -> accesses map
    auto& info = resourceAccesses_[resource];
    info.resource = resource;

    // Check if we already have an access record for this node+slot
    // (can happen with ReadWrite where we might record twice)
    auto existingIt = std::find_if(info.accesses.begin(), info.accesses.end(),
        [node, slotIndex, isOutput](const ResourceAccess& a) {
            return a.node == node && a.slotIndex == slotIndex && a.isOutput == isOutput;
        });

    if (existingIt == info.accesses.end()) {
        info.accesses.push_back({node, accessType, slotIndex, isOutput});
    } else {
        // Upgrade to ReadWrite if needed
        if (accessType != existingIt->accessType) {
            existingIt->accessType = ResourceAccessType::ReadWrite;
        }
    }

    // Record in node -> resources map
    auto& resources = nodeResources_[node];
    if (std::find(resources.begin(), resources.end(), resource) == resources.end()) {
        resources.push_back(resource);
    }

    // Record in node -> writes/reads maps
    if (accessType == ResourceAccessType::Write || accessType == ResourceAccessType::ReadWrite) {
        nodeWrites_[node].insert(resource);
    }
    if (accessType == ResourceAccessType::Read || accessType == ResourceAccessType::ReadWrite) {
        nodeReads_[node].insert(resource);
    }
}

} // namespace Vixen::RenderGraph
