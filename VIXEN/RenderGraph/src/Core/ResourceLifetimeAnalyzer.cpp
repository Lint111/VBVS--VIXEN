#include "../../include/Core/ResourceLifetimeAnalyzer.h"
#include "../../include/Core/Logging.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph {

// ============================================================================
// TIMELINE COMPUTATION
// ============================================================================

void ResourceLifetimeAnalyzer::ComputeTimelines(
    const std::vector<NodeInstance*>& executionOrder,
    const std::vector<GraphEdge>& edges
) {
    // Store execution order for later queries
    executionOrder_ = executionOrder;
    timelines_.clear();

    if (executionOrder.empty()) {
        LOG_WARNING("ResourceLifetimeAnalyzer: Empty execution order provided");
        return;
    }

    // Step 1: Create execution index map
    // Maps NodeInstance* → position in execution order
    std::unordered_map<NodeInstance*, uint32_t> nodeToIndex;
    for (uint32_t i = 0; i < executionOrder.size(); ++i) {
        nodeToIndex[executionOrder[i]] = i;
    }

    // Step 2: For each edge, track resource lifetime
    // Each edge represents: source node produces resource → target node consumes it
    for (const auto& edge : edges) {
        if (!edge.source || !edge.target) {
            LOG_WARNING("ResourceLifetimeAnalyzer: Null node in edge");
            continue;
        }

        // Get resource being passed from source to target
        // Note: This assumes NodeInstance has GetOutputResource() method
        // In the actual implementation, this might need adjustment based on
        // how NodeInstance exposes its output resources
        auto* resource = edge.source->GetOutputResource(edge.sourceOutputIndex);

        if (!resource) {
            // No resource registered at this output slot (might be optional)
            continue;
        }

        // Create or update timeline for this resource
        auto& timeline = timelines_[resource];

        // First time seeing this resource - initialize
        if (timeline.resource == nullptr) {
            timeline.resource = resource;
            timeline.producer = edge.source;
            timeline.birthIndex = nodeToIndex[edge.source];
            timeline.deathIndex = nodeToIndex[edge.source]; // Will be updated
        }

        // Add consumer
        if (std::find(timeline.consumers.begin(), timeline.consumers.end(), edge.target)
            == timeline.consumers.end()) {
            timeline.consumers.push_back(edge.target);
        }

        // Update death index to latest consumer
        uint32_t consumerIndex = nodeToIndex[edge.target];
        timeline.deathIndex = std::max(timeline.deathIndex, consumerIndex);
    }

    // Step 3: Determine lifetime scopes and update resource metadata
    for (auto& [resource, timeline] : timelines_) {
        timeline.scope = DetermineScope(timeline.birthIndex, timeline.deathIndex);

        // Set metadata on the resource for tracking
        std::string groupID;
        switch (timeline.scope) {
            case LifetimeScope::Transient:
                groupID = "transient";
                break;
            case LifetimeScope::Subpass:
                groupID = "subpass_scoped";
                break;
            case LifetimeScope::Pass:
                groupID = "pass_scoped";
                break;
            case LifetimeScope::Frame:
                groupID = "frame_scoped";
                break;
            case LifetimeScope::Persistent:
                groupID = "persistent";
                break;
        }

        // Update resource metadata (if supported)
        // This allows resources to self-report their aliasing group
        try {
            resource->SetMetadata("aliasing_group", groupID);
            resource->SetMetadata("birth_index", static_cast<uint64_t>(timeline.birthIndex));
            resource->SetMetadata("death_index", static_cast<uint64_t>(timeline.deathIndex));
        } catch (...) {
            // Metadata might not be supported by all resource types
            // Silently continue
        }
    }

    LOG_DEBUG("ResourceLifetimeAnalyzer: Computed " +
              std::to_string(timelines_.size()) + " resource timelines");
}

void ResourceLifetimeAnalyzer::Clear() {
    timelines_.clear();
    executionOrder_.clear();
}

// ============================================================================
// TIMELINE QUERIES
// ============================================================================

const ResourceTimeline* ResourceLifetimeAnalyzer::GetTimeline(
    ResourceManagement::UnifiedRM_Base* resource
) const {
    auto it = timelines_.find(resource);
    if (it != timelines_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// ALIASING ANALYSIS
// ============================================================================

std::vector<ResourceManagement::UnifiedRM_Base*>
ResourceLifetimeAnalyzer::FindAliasingCandidates(
    ResourceManagement::UnifiedRM_Base* resource
) const {
    std::vector<ResourceManagement::UnifiedRM_Base*> candidates;

    auto it = timelines_.find(resource);
    if (it == timelines_.end()) {
        return candidates; // Resource not tracked
    }

    const auto& timeline = it->second;

    // Find all resources with non-overlapping lifetimes
    for (const auto& [otherResource, otherTimeline] : timelines_) {
        if (otherResource == resource) continue;

        // Check if lifetimes don't overlap
        if (!timeline.overlaps(otherTimeline)) {
            // Check if memory locations are compatible
            if (resource->GetMemoryLocation() == otherResource->GetMemoryLocation()) {
                candidates.push_back(otherResource);
            }
        }
    }

    return candidates;
}

std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>>
ResourceLifetimeAnalyzer::ComputeAliasingGroups() const {
    std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>> groups;

    // Group resources by scope first (only alias within same scope)
    std::unordered_map<LifetimeScope, std::vector<ResourceManagement::UnifiedRM_Base*>>
        scopeGroups;

    for (const auto& [resource, timeline] : timelines_) {
        // Only consider device memory resources for aliasing
        if (resource->GetMemoryLocation() ==
            ResourceManagement::MemoryLocation::DeviceLocal) {
            scopeGroups[timeline.scope].push_back(resource);
        }
    }

    // Within each scope, apply interval scheduling
    for (const auto& [scope, resources] : scopeGroups) {
        if (resources.empty()) continue;

        auto aliasingGroups = ComputeIntervalScheduling(resources);
        groups.insert(groups.end(), aliasingGroups.begin(), aliasingGroups.end());
    }

    // Filter out groups with single resources (no aliasing benefit)
    groups.erase(
        std::remove_if(groups.begin(), groups.end(),
            [](const auto& group) { return group.size() < 2; }),
        groups.end()
    );

    return groups;
}

float ResourceLifetimeAnalyzer::ComputeAliasingEfficiency() const {
    auto groups = ComputeAliasingGroups();

    size_t totalMemoryWithAliasing = 0;
    size_t totalMemoryWithoutAliasing = 0;

    for (const auto& group : groups) {
        // With aliasing: use max size in group
        size_t maxSize = 0;
        for (auto* resource : group) {
            size_t size = resource->GetAllocatedBytes();
            maxSize = std::max(maxSize, size);
            totalMemoryWithoutAliasing += size;
        }
        totalMemoryWithAliasing += maxSize;
    }

    if (totalMemoryWithoutAliasing == 0) {
        return 0.0f;
    }

    float savings = 100.0f * (1.0f -
        static_cast<float>(totalMemoryWithAliasing) /
        static_cast<float>(totalMemoryWithoutAliasing));

    return savings;
}

// ============================================================================
// VALIDATION & DEBUGGING
// ============================================================================

bool ResourceLifetimeAnalyzer::ValidateTimelines(std::string& errorMessage) const {
    std::ostringstream errors;
    bool valid = true;

    for (const auto& [resource, timeline] : timelines_) {
        // Check 1: Resource pointer valid
        if (timeline.resource == nullptr) {
            errors << "Timeline has null resource pointer\n";
            valid = false;
        }

        // Check 2: Producer exists
        if (timeline.producer == nullptr) {
            errors << "Timeline for resource has null producer\n";
            valid = false;
        }

        // Check 3: Birth before death
        if (timeline.birthIndex > timeline.deathIndex) {
            errors << "Timeline has birth (" << timeline.birthIndex
                   << ") after death (" << timeline.deathIndex << ")\n";
            valid = false;
        }

        // Check 4: Indices within execution order bounds
        if (timeline.birthIndex >= executionOrder_.size() ||
            timeline.deathIndex >= executionOrder_.size()) {
            errors << "Timeline indices out of bounds (order size: "
                   << executionOrder_.size() << ")\n";
            valid = false;
        }

        // Check 5: At least one consumer exists
        if (timeline.consumers.empty()) {
            errors << "Timeline has no consumers (unused resource?)\n";
            // This is a warning, not an error
        }
    }

    errorMessage = errors.str();
    return valid;
}

void ResourceLifetimeAnalyzer::PrintTimelines() const {
    LOG_INFO("=== Resource Timelines ===");
    LOG_INFO("Total tracked resources: " + std::to_string(timelines_.size()));
    LOG_INFO("");

    // Sort by birth index for readable output
    std::vector<std::pair<ResourceManagement::UnifiedRM_Base*, ResourceTimeline>>
        sortedTimelines(timelines_.begin(), timelines_.end());

    std::sort(sortedTimelines.begin(), sortedTimelines.end(),
        [](const auto& a, const auto& b) {
            return a.second.birthIndex < b.second.birthIndex;
        });

    for (const auto& [resource, timeline] : sortedTimelines) {
        std::string scopeName;
        switch (timeline.scope) {
            case LifetimeScope::Transient: scopeName = "Transient"; break;
            case LifetimeScope::Subpass: scopeName = "Subpass"; break;
            case LifetimeScope::Pass: scopeName = "Pass"; break;
            case LifetimeScope::Frame: scopeName = "Frame"; break;
            case LifetimeScope::Persistent: scopeName = "Persistent"; break;
        }

        LOG_INFO("Resource: " + GetResourceDebugName(resource));
        LOG_INFO("  Birth: " + std::to_string(timeline.birthIndex) +
                 " | Death: " + std::to_string(timeline.deathIndex) +
                 " | Length: " + std::to_string(timeline.lifetimeLength()));
        LOG_INFO("  Scope: " + scopeName);
        LOG_INFO("  Producer: " + (timeline.producer ?
                 timeline.producer->GetInstanceName() : "null"));
        LOG_INFO("  Consumers: " + std::to_string(timeline.consumers.size()));
        LOG_INFO("");
    }
}

void ResourceLifetimeAnalyzer::PrintAliasingReport() const {
    auto groups = ComputeAliasingGroups();

    LOG_INFO("=== Automatic Aliasing Report ===");
    LOG_INFO("Aliasing Pools: " + std::to_string(groups.size()));

    size_t totalAliased = 0;
    size_t totalMemory = 0;
    size_t memoryIfNoAliasing = 0;

    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        totalAliased += group.size();

        // Find max size in group
        size_t maxSize = 0;
        for (auto* resource : group) {
            size_t size = resource->GetAllocatedBytes();
            maxSize = std::max(maxSize, size);
            memoryIfNoAliasing += size;
        }
        totalMemory += maxSize;

        LOG_INFO("");
        LOG_INFO("Pool " + std::to_string(i) + ":");
        LOG_INFO("  Resources: " + std::to_string(group.size()));
        LOG_INFO("  Shared Size: " + FormatBytes(maxSize));
        LOG_INFO("  Without Aliasing: " + FormatBytes(memoryIfNoAliasing));

        // List resources in this pool
        for (auto* resource : group) {
            const auto* timeline = GetTimeline(resource);
            LOG_INFO("    - " + GetResourceDebugName(resource) +
                     " [" + std::to_string(timeline->birthIndex) +
                     ", " + std::to_string(timeline->deathIndex) + "]");
        }
    }

    if (memoryIfNoAliasing > 0) {
        float savings = 100.0f * (1.0f -
            static_cast<float>(totalMemory) / static_cast<float>(memoryIfNoAliasing));

        LOG_INFO("");
        LOG_INFO("=== Summary ===");
        LOG_INFO("Aliased Resources: " + std::to_string(totalAliased));
        LOG_INFO("Memory Allocated: " + FormatBytes(totalMemory));
        LOG_INFO("Memory If No Aliasing: " + FormatBytes(memoryIfNoAliasing));
        LOG_INFO("Savings: " + std::to_string(static_cast<int>(savings)) + "%");
    }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

uint32_t ResourceLifetimeAnalyzer::FindLastConsumerIndex(
    const std::vector<NodeInstance*>& consumers,
    const std::unordered_map<NodeInstance*, uint32_t>& nodeToIndex
) const {
    uint32_t maxIndex = 0;

    for (auto* consumer : consumers) {
        auto it = nodeToIndex.find(consumer);
        if (it != nodeToIndex.end()) {
            maxIndex = std::max(maxIndex, it->second);
        }
    }

    return maxIndex;
}

LifetimeScope ResourceLifetimeAnalyzer::DetermineScope(
    uint32_t birthIndex,
    uint32_t deathIndex
) const {
    size_t length = deathIndex - birthIndex;

    // Classify based on lifetime length
    // These thresholds are heuristic and can be tuned
    if (length <= 4) {
        return LifetimeScope::Transient;
    } else if (length <= 10) {
        return LifetimeScope::Subpass;
    } else if (length <= 20) {
        return LifetimeScope::Pass;
    } else if (length < executionOrder_.size()) {
        return LifetimeScope::Frame;
    } else {
        return LifetimeScope::Persistent;
    }
}

std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>>
ResourceLifetimeAnalyzer::ComputeIntervalScheduling(
    const std::vector<ResourceManagement::UnifiedRM_Base*>& resources
) const {
    std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>> groups;

    // Greedy interval scheduling algorithm
    // For each resource, try to fit into existing group that doesn't overlap
    for (auto* resource : resources) {
        auto it = timelines_.find(resource);
        if (it == timelines_.end()) continue;

        const auto& timeline = it->second;

        // Try to fit into existing group
        bool fitted = false;
        for (auto& group : groups) {
            // Check if this resource overlaps with any in the group
            bool overlaps = false;
            for (auto* existing : group) {
                auto existingIt = timelines_.find(existing);
                if (existingIt != timelines_.end() &&
                    timeline.overlaps(existingIt->second)) {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps) {
                // No overlap - can alias with this group!
                group.push_back(resource);
                fitted = true;
                break;
            }
        }

        if (!fitted) {
            // Couldn't fit into any existing group - create new one
            groups.push_back({resource});
        }
    }

    return groups;
}

std::string ResourceLifetimeAnalyzer::GetResourceDebugName(
    ResourceManagement::UnifiedRM_Base* resource
) const {
    if (!resource) return "null";

    std::string name = resource->GetDebugName();
    if (name.empty()) {
        // Fallback to unique ID
        std::ostringstream oss;
        oss << "Resource@0x" << std::hex << resource->GetUniqueID();
        return oss.str();
    }

    return name;
}

std::string ResourceLifetimeAnalyzer::FormatBytes(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

} // namespace Vixen::RenderGraph
