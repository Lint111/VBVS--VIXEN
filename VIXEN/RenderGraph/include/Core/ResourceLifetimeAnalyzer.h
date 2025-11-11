#pragma once

#include "NodeInstance.h"
#include "GraphTopology.h"
#include "../../ResourceManagement/include/ResourceManagement/UnifiedRM_TypeSafe.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>

namespace Vixen::RenderGraph {

/**
 * @brief Lifetime scope classification for resources
 */
enum class LifetimeScope : uint8_t {
    Transient,    ///< Single pass (1-4 nodes)
    Subpass,      ///< Within subpass (5-10 nodes)
    Pass,         ///< Entire render pass (11-20 nodes)
    Frame,        ///< Entire frame (21+ nodes)
    Persistent    ///< Multiple frames (external resources)
};

/**
 * @brief Timeline information for a single resource
 *
 * Tracks when a resource is created (birth), when it's last used (death),
 * and which nodes produce/consume it. This information is automatically
 * derived from GraphTopology execution order.
 */
struct ResourceTimeline {
    ResourceManagement::UnifiedRM_Base* resource = nullptr;
    NodeInstance* producer = nullptr;
    std::vector<NodeInstance*> consumers;

    // Execution indices (from topological sort)
    uint32_t birthIndex = 0;      ///< When produced (execution order index)
    uint32_t deathIndex = 0;      ///< Last use (execution order index)
    uint32_t executionWave = 0;   ///< For parallel execution (future)

    // Lifetime classification
    LifetimeScope scope = LifetimeScope::Transient;

    /**
     * @brief Check if this is a short-lived transient resource
     */
    bool isTransient() const {
        return (deathIndex - birthIndex) < 5;
    }

    /**
     * @brief Check if two resource timelines overlap in execution
     *
     * Non-overlapping resources are candidates for memory aliasing.
     */
    bool overlaps(const ResourceTimeline& other) const {
        // Resources overlap if their lifetime intervals intersect
        // [birth, death] vs [other.birth, other.death]
        return !(deathIndex < other.birthIndex ||
                 other.deathIndex < birthIndex);
    }

    /**
     * @brief Get lifetime length in execution steps
     */
    size_t lifetimeLength() const {
        return deathIndex - birthIndex;
    }

    /**
     * @brief Check if resource is consumed by a specific node
     */
    bool isConsumedBy(NodeInstance* node) const {
        return std::find(consumers.begin(), consumers.end(), node) != consumers.end();
    }
};

/**
 * @brief Analyzes resource lifetimes from graph execution order
 *
 * This class automatically computes when resources are created and destroyed
 * based on the GraphTopology's execution order. This information is used to:
 *
 * 1. Identify resources with non-overlapping lifetimes (aliasing candidates)
 * 2. Classify resources by lifetime scope (Transient/Pass/Frame/Persistent)
 * 3. Create automatic memory aliasing pools for VRAM savings
 *
 * Key Innovation: NO MANUAL CONFIGURATION NEEDED!
 * - Lifetimes derived from topological sort
 * - Automatically updates when graph changes
 * - Always correct (synchronized with graph state)
 *
 * Usage:
 * @code
 * GraphTopology topology;
 * // ... build topology ...
 *
 * auto executionOrder = topology.TopologicalSort();
 * auto edges = topology.GetEdges();
 *
 * ResourceLifetimeAnalyzer analyzer;
 * analyzer.ComputeTimelines(executionOrder, edges);
 *
 * // Find resources that can share memory
 * auto aliasingGroups = analyzer.ComputeAliasingGroups();
 * @endcode
 */
class ResourceLifetimeAnalyzer {
public:
    ResourceLifetimeAnalyzer() = default;
    ~ResourceLifetimeAnalyzer() = default;

    // Non-copyable (contains resource tracking state)
    ResourceLifetimeAnalyzer(const ResourceLifetimeAnalyzer&) = delete;
    ResourceLifetimeAnalyzer& operator=(const ResourceLifetimeAnalyzer&) = delete;

    // Movable
    ResourceLifetimeAnalyzer(ResourceLifetimeAnalyzer&&) noexcept = default;
    ResourceLifetimeAnalyzer& operator=(ResourceLifetimeAnalyzer&&) noexcept = default;

    // ========================================================================
    // TIMELINE COMPUTATION
    // ========================================================================

    /**
     * @brief Compute resource timelines from graph execution order
     *
     * Analyzes the graph to determine:
     * - When each resource is created (producer node execution)
     * - When each resource is last used (last consumer node execution)
     * - Lifetime scope classification
     *
     * This is called automatically when the graph topology changes.
     *
     * @param executionOrder Topologically sorted node execution order
     * @param edges Graph edges (producer->consumer connections)
     */
    void ComputeTimelines(
        const std::vector<NodeInstance*>& executionOrder,
        const std::vector<GraphEdge>& edges
    );

    /**
     * @brief Clear all tracked timelines
     */
    void Clear();

    // ========================================================================
    // TIMELINE QUERIES
    // ========================================================================

    /**
     * @brief Get timeline for a specific resource
     *
     * @param resource Resource to query
     * @return Pointer to timeline, or nullptr if not tracked
     */
    const ResourceTimeline* GetTimeline(
        ResourceManagement::UnifiedRM_Base* resource
    ) const;

    /**
     * @brief Get all tracked timelines
     */
    const std::unordered_map<ResourceManagement::UnifiedRM_Base*, ResourceTimeline>&
    GetAllTimelines() const {
        return timelines_;
    }

    /**
     * @brief Get number of tracked resources
     */
    size_t GetTrackedResourceCount() const {
        return timelines_.size();
    }

    // ========================================================================
    // ALIASING ANALYSIS
    // ========================================================================

    /**
     * @brief Find resources that can alias with a given resource
     *
     * Returns all resources that have non-overlapping lifetimes and
     * similar memory requirements.
     *
     * @param resource Resource to find aliasing candidates for
     * @return Vector of resources that can share memory
     */
    std::vector<ResourceManagement::UnifiedRM_Base*> FindAliasingCandidates(
        ResourceManagement::UnifiedRM_Base* resource
    ) const;

    /**
     * @brief Compute optimal aliasing groups for all resources
     *
     * Uses interval scheduling algorithm to group resources with
     * non-overlapping lifetimes into memory aliasing pools.
     *
     * Each group represents resources that can share the same GPU memory.
     *
     * @return Vector of aliasing groups (each group = vector of resources)
     */
    std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>>
    ComputeAliasingGroups() const;

    /**
     * @brief Calculate potential memory savings from aliasing
     *
     * Compares memory usage with aliasing vs without aliasing.
     *
     * @return Percentage of memory saved (0.0 - 100.0)
     */
    float ComputeAliasingEfficiency() const;

    // ========================================================================
    // VALIDATION & DEBUGGING
    // ========================================================================

    /**
     * @brief Validate computed timelines
     *
     * Checks for:
     * - Invalid birth/death indices
     * - Missing producers/consumers
     * - Timeline inconsistencies
     *
     * @param errorMessage Output parameter for error description
     * @return true if valid, false if errors detected
     */
    bool ValidateTimelines(std::string& errorMessage) const;

    /**
     * @brief Print timeline information for debugging
     */
    void PrintTimelines() const;

    /**
     * @brief Print aliasing groups and savings estimate
     */
    void PrintAliasingReport() const;

private:
    // ========================================================================
    // INTERNAL STATE
    // ========================================================================

    /// Map: Resource → Timeline
    std::unordered_map<ResourceManagement::UnifiedRM_Base*, ResourceTimeline> timelines_;

    /// Cached execution order (for index lookups)
    std::vector<NodeInstance*> executionOrder_;

    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    /**
     * @brief Find the last consumer of a resource in execution order
     *
     * @param consumers All consumers of the resource
     * @param nodeToIndex Map from node to execution order index
     * @return Index of last consumer in execution order
     */
    uint32_t FindLastConsumerIndex(
        const std::vector<NodeInstance*>& consumers,
        const std::unordered_map<NodeInstance*, uint32_t>& nodeToIndex
    ) const;

    /**
     * @brief Determine lifetime scope based on execution span
     *
     * @param birthIndex When resource is created
     * @param deathIndex When resource is last used
     * @return Classified lifetime scope
     */
    LifetimeScope DetermineScope(uint32_t birthIndex, uint32_t deathIndex) const;

    /**
     * @brief Apply interval scheduling algorithm to find aliasing groups
     *
     * Classic greedy algorithm for interval scheduling:
     * - Sort resources by birth time
     * - For each resource, try to fit into existing group
     * - If overlaps with any in group, create new group
     *
     * @param resources Resources to group
     * @return Vector of aliasing groups
     */
    std::vector<std::vector<ResourceManagement::UnifiedRM_Base*>>
    ComputeIntervalScheduling(
        const std::vector<ResourceManagement::UnifiedRM_Base*>& resources
    ) const;

    /**
     * @brief Get debug name for a resource (type + ID)
     */
    std::string GetResourceDebugName(
        ResourceManagement::UnifiedRM_Base* resource
    ) const;

    /**
     * @brief Format bytes to human-readable string (KB, MB, GB)
     */
    std::string FormatBytes(size_t bytes) const;
};

} // namespace Vixen::RenderGraph
