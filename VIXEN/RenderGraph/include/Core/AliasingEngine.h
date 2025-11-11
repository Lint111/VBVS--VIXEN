#pragma once

#include "Data/Core/ResourceVariant.h"
#include "Data/Core/ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <map>
#include <cstdint>

namespace Vixen::RenderGraph {

// Forward declaration
class ResourceLifetimeAnalyzer;

/**
 * @brief Alias candidate for memory reuse
 */
struct AliasCandidate {
    Resource* resource = nullptr;
    size_t bytes = 0;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    VkMemoryRequirements memoryRequirements = {};
    uint64_t releaseFrame = 0;  // Frame when released

    // For sorting by size (best-fit)
    bool operator<(const AliasCandidate& other) const {
        return bytes < other.bytes;
    }
};

/**
 * @brief Statistics for aliasing performance
 */
struct AliasingStats {
    uint64_t totalAliasAttempts = 0;
    uint64_t successfulAliases = 0;
    uint64_t failedAliases = 0;
    size_t totalBytesSaved = 0;
    size_t totalBytesAllocated = 0;

    float GetSuccessRate() const {
        return totalAliasAttempts > 0
            ? static_cast<float>(successfulAliases) / totalAliasAttempts
            : 0.0f;
    }

    float GetSavingsPercentage() const {
        return totalBytesAllocated > 0
            ? 100.0f * totalBytesSaved / totalBytesAllocated
            : 0.0f;
    }
};

/**
 * @brief Engine for automatic memory aliasing based on resource lifetimes
 *
 * The AliasingEngine tracks resource lifetimes and reuses memory for resources
 * with non-overlapping lifetimes, achieving 50-80% VRAM savings.
 *
 * Key Features:
 * - Best-fit algorithm for optimal memory reuse
 * - Integration with ResourceLifetimeAnalyzer for overlap detection
 * - Memory requirements compatibility checking (size, alignment, memory type)
 * - Comprehensive statistics tracking
 * - Automatic cleanup of old released resources
 *
 * Algorithm:
 * 1. Resources register themselves with memory requirements and lifetime info
 * 2. When a resource is released, it becomes available for aliasing
 * 3. New allocations query available resources sorted by size
 * 4. Best-fit candidate is selected (smallest resource that satisfies requirements)
 * 5. Lifetime overlap is checked via ResourceLifetimeAnalyzer
 * 6. Compatible resources alias the same memory
 *
 * Usage:
 * @code
 * AliasingEngine engine;
 * engine.SetLifetimeAnalyzer(&lifetimeAnalyzer);
 * engine.SetMinimumAliasingSize(1 * 1024 * 1024);  // 1 MB
 *
 * // During allocation
 * Resource* existingResource = engine.FindAlias(memReqs, lifetime, minBytes);
 * if (existingResource) {
 *     // Reuse existing resource's memory
 *     aliasedResource = existingResource;
 * } else {
 *     // Allocate new memory
 *     newResource = AllocateNewResource();
 *     engine.RegisterForAliasing(newResource, memReqs, lifetime);
 * }
 *
 * // When resource is no longer needed
 * engine.MarkReleased(resource, currentFrame);
 *
 * // Periodic cleanup (e.g., once per frame)
 * engine.ClearReleasedResources(currentFrame - 2);
 *
 * // Get statistics
 * auto stats = engine.GetStats();
 * LOG_INFO("VRAM savings: " + std::to_string(stats.GetSavingsPercentage()) + "%");
 * @endcode
 */
class AliasingEngine {
public:
    AliasingEngine();
    ~AliasingEngine();

    // === Aliasing Operations ===

    /**
     * @brief Find an existing resource that can be aliased
     *
     * Searches for a suitable resource using best-fit algorithm:
     * 1. Filters by minimum size threshold
     * 2. Finds smallest resource that satisfies memory requirements
     * 3. Verifies memory compatibility (alignment, memory type bits)
     * 4. Checks lifetime non-overlap via ResourceLifetimeAnalyzer
     *
     * @param requirements Memory requirements for new allocation
     * @param lifetime Lifetime of the new resource
     * @param minBytes Minimum size threshold for aliasing
     * @return Resource to alias, or nullptr if no suitable candidate
     */
    Resource* FindAlias(
        const VkMemoryRequirements& requirements,
        ResourceLifetime lifetime,
        size_t minBytes
    );

    /**
     * @brief Register a resource for potential aliasing
     *
     * Tracks the resource so it can be reused by future allocations
     * once it's marked as released.
     *
     * @param resource Resource to track
     * @param requirements Memory requirements
     * @param lifetime Resource lifetime
     */
    void RegisterForAliasing(
        Resource* resource,
        const VkMemoryRequirements& requirements,
        ResourceLifetime lifetime
    );

    /**
     * @brief Mark resource as released and available for aliasing
     *
     * Moves the resource into the available pool where it can be
     * discovered by FindAlias().
     *
     * @param resource Resource being released
     * @param frameNumber Frame when released (for cleanup tracking)
     */
    void MarkReleased(Resource* resource, uint64_t frameNumber);

    // === Lifetime Analyzer Integration ===

    /**
     * @brief Set the lifetime analyzer for overlap detection
     *
     * The analyzer is used to verify that two resources don't have
     * overlapping lifetimes before aliasing them together.
     *
     * @param analyzer Non-owning pointer to lifetime analyzer
     */
    void SetLifetimeAnalyzer(ResourceLifetimeAnalyzer* analyzer);

    // === Statistics ===

    /**
     * @brief Get current aliasing statistics
     */
    AliasingStats GetStats() const { return stats_; }

    /**
     * @brief Reset all statistics counters
     */
    void ResetStats();

    // === Configuration ===

    /**
     * @brief Set minimum resource size for aliasing consideration
     *
     * Small resources are not worth the overhead of aliasing.
     * Default is 1 MB.
     *
     * @param bytes Minimum size in bytes
     */
    void SetMinimumAliasingSize(size_t bytes) { minimumAliasingSize_ = bytes; }

    /**
     * @brief Get current minimum aliasing size threshold
     */
    size_t GetMinimumAliasingSize() const { return minimumAliasingSize_; }

    // === Cleanup ===

    /**
     * @brief Remove released resources older than specified frame
     *
     * Cleans up resources that were released before the specified frame.
     * This prevents unbounded memory growth from accumulating old resources.
     *
     * Typically called once per frame:
     * @code
     * engine.ClearReleasedResources(currentFrame - framesInFlight);
     * @endcode
     *
     * @param olderThanFrame Frame threshold (exclusive)
     */
    void ClearReleasedResources(uint64_t olderThanFrame);

private:
    // Helper methods

    /**
     * @brief Check if memory requirements are compatible
     *
     * Verifies:
     * - Available size >= required size
     * - Available alignment satisfies required alignment
     * - Memory type bits are compatible
     *
     * @param required Requirements of resource requesting memory
     * @param available Requirements of existing resource
     * @return true if compatible, false otherwise
     */
    bool AreMemoryRequirementsCompatible(
        const VkMemoryRequirements& required,
        const VkMemoryRequirements& available
    ) const;

    /**
     * @brief Check if two resource lifetimes are non-overlapping
     *
     * Uses the ResourceLifetimeAnalyzer to determine if resources
     * have overlapping execution windows. Only non-overlapping
     * resources can safely share memory.
     *
     * @param resource1 First resource
     * @param resource2 Second resource
     * @return true if lifetimes don't overlap, false otherwise
     */
    bool AreLifetimesNonOverlapping(
        Resource* resource1,
        Resource* resource2
    ) const;

    // Available resources for aliasing (sorted by size for best-fit)
    // Key: size in bytes, Value: candidate details
    std::multimap<size_t, AliasCandidate> availableResources_;

    // Active resources registered but not yet released
    // Map: Resource pointer -> Candidate info
    std::unordered_map<Resource*, AliasCandidate> activeResources_;

    // Alias relationships: original resource -> resources aliasing it
    std::unordered_map<Resource*, std::vector<Resource*>> aliasMap_;

    // Lifetime analyzer (non-owning)
    ResourceLifetimeAnalyzer* lifetimeAnalyzer_ = nullptr;

    // Configuration
    size_t minimumAliasingSize_ = 1 * 1024 * 1024;  // 1 MB default

    // Statistics
    AliasingStats stats_;
};

} // namespace Vixen::RenderGraph
