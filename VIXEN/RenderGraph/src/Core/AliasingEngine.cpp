#include "../../include/Core/AliasingEngine.h"
#include "../../include/Core/ResourceLifetimeAnalyzer.h"
#include "../../include/Core/Logging.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Vixen::RenderGraph {

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

AliasingEngine::AliasingEngine()
    : lifetimeAnalyzer_(nullptr)
    , minimumAliasingSize_(1 * 1024 * 1024)
    , stats_{}
{
    LOG_DEBUG("AliasingEngine: Initialized with minimum aliasing size: " +
              std::to_string(minimumAliasingSize_ / 1024) + " KB");
}

AliasingEngine::~AliasingEngine() {
    LOG_DEBUG("AliasingEngine: Destroyed. Final stats - " +
              std::to_string(stats_.successfulAliases) + " successful aliases, " +
              std::to_string(stats_.totalBytesSaved / (1024 * 1024)) + " MB saved");
}

// ============================================================================
// ALIASING OPERATIONS
// ============================================================================

Resource* AliasingEngine::FindAlias(
    const VkMemoryRequirements& requirements,
    ResourceLifetime lifetime,
    size_t minBytes
) {
    stats_.totalAliasAttempts++;

    // Check if resource meets minimum threshold
    if (requirements.size < minBytes || requirements.size < minimumAliasingSize_) {
        LOG_DEBUG("AliasingEngine: Resource size " +
                  std::to_string(requirements.size) +
                  " bytes below minimum threshold, skipping aliasing");
        stats_.failedAliases++;
        return nullptr;
    }

    // Early exit if no available resources
    if (availableResources_.empty()) {
        LOG_DEBUG("AliasingEngine: No available resources for aliasing");
        stats_.failedAliases++;
        return nullptr;
    }

    LOG_DEBUG("AliasingEngine: Searching for alias candidate - " +
              std::to_string(requirements.size) + " bytes, " +
              std::to_string(requirements.alignment) + " alignment");

    // Best-fit algorithm: Find smallest resource that satisfies requirements
    // Using lower_bound on size gives us resources >= required size
    auto it = availableResources_.lower_bound(requirements.size);

    // Try each candidate in size order until we find a compatible one
    for (; it != availableResources_.end(); ++it) {
        const AliasCandidate& candidate = it->second;

        LOG_DEBUG("AliasingEngine: Evaluating candidate - " +
                  std::to_string(candidate.bytes) + " bytes");

        // Check memory requirements compatibility
        if (!AreMemoryRequirementsCompatible(requirements, candidate.memoryRequirements)) {
            LOG_DEBUG("AliasingEngine: Memory requirements incompatible (alignment or type mismatch)");
            continue;
        }

        // Check lifetime compatibility via analyzer
        if (lifetimeAnalyzer_) {
            // Note: We can't directly compare lifetimes without the requesting resource
            // In practice, this would require the caller to provide their Resource*
            // For now, we do basic lifetime enum comparison

            // Persistent resources can't be aliased
            if (candidate.lifetime == ResourceLifetime::Persistent ||
                lifetime == ResourceLifetime::Persistent) {
                LOG_DEBUG("AliasingEngine: Persistent resource cannot be aliased");
                continue;
            }

            // Imported resources can't be aliased
            if (candidate.lifetime == ResourceLifetime::Imported ||
                lifetime == ResourceLifetime::Imported) {
                LOG_DEBUG("AliasingEngine: Imported resource cannot be aliased");
                continue;
            }

            // Both are transient - likely safe to alias if memory requirements match
            // More sophisticated overlap checking would require timeline information
        }

        // Found a compatible candidate!
        LOG_INFO("AliasingEngine: Found alias candidate! Reusing " +
                 std::to_string(candidate.bytes / 1024) + " KB resource");

        // Update statistics
        stats_.successfulAliases++;
        stats_.totalBytesSaved += requirements.size;
        stats_.totalBytesAllocated += requirements.size;

        // Track alias relationship
        aliasMap_[candidate.resource].push_back(nullptr);  // Caller will update with actual resource

        return candidate.resource;
    }

    // No suitable candidate found
    LOG_DEBUG("AliasingEngine: No compatible alias candidate found");
    stats_.failedAliases++;
    stats_.totalBytesAllocated += requirements.size;

    return nullptr;
}

void AliasingEngine::RegisterForAliasing(
    Resource* resource,
    const VkMemoryRequirements& requirements,
    ResourceLifetime lifetime
) {
    if (!resource) {
        LOG_WARNING("AliasingEngine: Attempted to register null resource");
        return;
    }

    // Skip resources below minimum threshold
    if (requirements.size < minimumAliasingSize_) {
        LOG_DEBUG("AliasingEngine: Resource size " +
                  std::to_string(requirements.size) +
                  " bytes below minimum threshold, not registering for aliasing");
        return;
    }

    // Create candidate info
    AliasCandidate candidate;
    candidate.resource = resource;
    candidate.bytes = requirements.size;
    candidate.lifetime = lifetime;
    candidate.memoryRequirements = requirements;
    candidate.releaseFrame = 0;  // Not yet released

    // Add to active resources
    activeResources_[resource] = candidate;

    LOG_DEBUG("AliasingEngine: Registered resource for aliasing - " +
              std::to_string(candidate.bytes / 1024) + " KB, lifetime=" +
              std::to_string(static_cast<int>(lifetime)));
}

void AliasingEngine::MarkReleased(Resource* resource, uint64_t frameNumber) {
    if (!resource) {
        LOG_WARNING("AliasingEngine: Attempted to mark null resource as released");
        return;
    }

    // Find in active resources
    auto it = activeResources_.find(resource);
    if (it == activeResources_.end()) {
        LOG_DEBUG("AliasingEngine: Resource not found in active resources, ignoring release");
        return;
    }

    // Move to available resources
    AliasCandidate candidate = it->second;
    candidate.releaseFrame = frameNumber;

    // Add to available pool, sorted by size (multimap automatically sorts)
    availableResources_.insert({candidate.bytes, candidate});

    // Remove from active
    activeResources_.erase(it);

    LOG_DEBUG("AliasingEngine: Marked resource as released - " +
              std::to_string(candidate.bytes / 1024) + " KB, frame=" +
              std::to_string(frameNumber));

    LOG_INFO("AliasingEngine: Available resources: " +
             std::to_string(availableResources_.size()) +
             ", Active resources: " + std::to_string(activeResources_.size()));
}

// ============================================================================
// LIFETIME ANALYZER INTEGRATION
// ============================================================================

void AliasingEngine::SetLifetimeAnalyzer(ResourceLifetimeAnalyzer* analyzer) {
    lifetimeAnalyzer_ = analyzer;

    if (analyzer) {
        LOG_INFO("AliasingEngine: Lifetime analyzer connected - "
                 "advanced overlap detection enabled");
    } else {
        LOG_WARNING("AliasingEngine: Lifetime analyzer disconnected - "
                    "using basic lifetime checking only");
    }
}

// ============================================================================
// STATISTICS
// ============================================================================

void AliasingEngine::ResetStats() {
    stats_ = AliasingStats{};
    LOG_DEBUG("AliasingEngine: Statistics reset");
}

// ============================================================================
// CLEANUP
// ============================================================================

void AliasingEngine::ClearReleasedResources(uint64_t olderThanFrame) {
    size_t removedCount = 0;
    size_t freedBytes = 0;

    // Remove all resources released before the specified frame
    auto it = availableResources_.begin();
    while (it != availableResources_.end()) {
        if (it->second.releaseFrame < olderThanFrame) {
            freedBytes += it->second.bytes;
            removedCount++;

            // Remove any alias relationships
            auto aliasIt = aliasMap_.find(it->second.resource);
            if (aliasIt != aliasMap_.end()) {
                aliasMap_.erase(aliasIt);
            }

            it = availableResources_.erase(it);
        } else {
            ++it;
        }
    }

    if (removedCount > 0) {
        LOG_INFO("AliasingEngine: Cleaned up " + std::to_string(removedCount) +
                 " old resources (" + std::to_string(freedBytes / (1024 * 1024)) +
                 " MB) from frame < " + std::to_string(olderThanFrame));
    }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool AliasingEngine::AreMemoryRequirementsCompatible(
    const VkMemoryRequirements& required,
    const VkMemoryRequirements& available
) const {
    // Check size: available must be >= required
    if (available.size < required.size) {
        LOG_DEBUG("AliasingEngine: Size incompatible - available: " +
                  std::to_string(available.size) + ", required: " +
                  std::to_string(required.size));
        return false;
    }

    // Check alignment: available must satisfy required alignment
    // A resource with alignment X can be used for alignment Y if X >= Y
    if (available.alignment < required.alignment) {
        LOG_DEBUG("AliasingEngine: Alignment incompatible - available: " +
                  std::to_string(available.alignment) + ", required: " +
                  std::to_string(required.alignment));
        return false;
    }

    // Check memory type bits: available types must overlap with required types
    // Memory type bits is a bitmask where each bit represents a memory type
    // For aliasing, we need at least one common memory type
    if ((available.memoryTypeBits & required.memoryTypeBits) == 0) {
        LOG_DEBUG("AliasingEngine: Memory type bits incompatible - available: 0x" +
                  std::to_string(available.memoryTypeBits) + ", required: 0x" +
                  std::to_string(required.memoryTypeBits));
        return false;
    }

    return true;
}

bool AliasingEngine::AreLifetimesNonOverlapping(
    Resource* resource1,
    Resource* resource2
) const {
    if (!resource1 || !resource2) {
        LOG_WARNING("AliasingEngine: Null resource in lifetime overlap check");
        return false;
    }

    // If we have a lifetime analyzer, use it for precise overlap detection
    if (lifetimeAnalyzer_) {
        const ResourceTimeline* timeline1 = lifetimeAnalyzer_->GetTimeline(resource1);
        const ResourceTimeline* timeline2 = lifetimeAnalyzer_->GetTimeline(resource2);

        if (timeline1 && timeline2) {
            bool overlaps = timeline1->overlaps(*timeline2);

            LOG_DEBUG("AliasingEngine: Lifetime overlap check - " +
                      std::string(overlaps ? "OVERLAPS" : "NON-OVERLAPPING") +
                      " (birth1=" + std::to_string(timeline1->birthIndex) +
                      ", death1=" + std::to_string(timeline1->deathIndex) +
                      ", birth2=" + std::to_string(timeline2->birthIndex) +
                      ", death2=" + std::to_string(timeline2->deathIndex) + ")");

            return !overlaps;
        } else {
            LOG_WARNING("AliasingEngine: Timeline not found for one or both resources");
            return false;  // Conservative: assume overlap if we don't have timeline info
        }
    }

    // Fallback: Basic lifetime enum comparison
    // This is less accurate but better than nothing
    ResourceLifetime lifetime1 = resource1->GetLifetime();
    ResourceLifetime lifetime2 = resource2->GetLifetime();

    // Persistent resources always overlap with everything
    if (lifetime1 == ResourceLifetime::Persistent ||
        lifetime2 == ResourceLifetime::Persistent) {
        return false;
    }

    // Imported resources can't be aliased
    if (lifetime1 == ResourceLifetime::Imported ||
        lifetime2 == ResourceLifetime::Imported) {
        return false;
    }

    // Static resources overlap with everything
    if (lifetime1 == ResourceLifetime::Static ||
        lifetime2 == ResourceLifetime::Static) {
        return false;
    }

    // Both transient: Assume they might not overlap
    // This is conservative - in reality we'd need more information
    LOG_DEBUG("AliasingEngine: Using basic lifetime check - both transient, assuming non-overlapping");
    return true;
}

} // namespace Vixen::RenderGraph
