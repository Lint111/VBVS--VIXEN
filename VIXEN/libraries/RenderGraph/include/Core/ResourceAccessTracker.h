// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file ResourceAccessTracker.h
 * @brief Resource access pattern tracking for parallel execution conflict detection
 *
 * Sprint 6.4: Phase 0 - WaveScheduler Foundation
 * Design Element: #38 Timeline Capacity Tracker (Wave Scheduling extension)
 *
 * ResourceAccessTracker monitors which nodes read/write which resources,
 * enabling the WaveScheduler to compute safe parallel execution waves.
 *
 * Conflict Rules:
 * - Writer + Writer on same resource = CONFLICT (data race)
 * - Writer + Reader on same resource = CONFLICT (read-after-write hazard)
 * - Reader + Reader on same resource = OK (parallel reads safe)
 *
 * Usage:
 * @code
 * ResourceAccessTracker tracker;
 * tracker.BuildFromTopology(graphTopology);
 *
 * // Check if two nodes can safely execute in parallel
 * if (!tracker.HasConflict(nodeA, nodeB)) {
 *     // Safe to execute concurrently
 * }
 * @endcode
 *
 * @see WaveScheduler for wave computation using this tracker
 * @see GraphTopology for graph structure
 */

#include "NodeInstance.h"
#include "GraphTopology.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vixen::RenderGraph {

// Forward declarations
class Resource;

/**
 * @brief Access type for a resource
 */
enum class ResourceAccessType : uint8_t {
    Read,       ///< Node only reads the resource
    Write,      ///< Node writes to the resource
    ReadWrite   ///< Node both reads and writes
};

/**
 * @brief Access record for a single node's access to a resource
 */
struct ResourceAccess {
    NodeInstance* node = nullptr;
    ResourceAccessType accessType = ResourceAccessType::Read;
    uint32_t slotIndex = 0;     ///< Input or output slot index
    bool isOutput = false;      ///< True if output slot, false if input
};

/**
 * @brief Per-resource access tracking
 *
 * Tracks all nodes that access a particular resource and their access patterns.
 */
struct ResourceAccessInfo {
    Resource* resource = nullptr;
    std::vector<ResourceAccess> accesses;

    /// Get all nodes that write to this resource
    [[nodiscard]] std::vector<NodeInstance*> GetWriters() const;

    /// Get all nodes that read from this resource
    [[nodiscard]] std::vector<NodeInstance*> GetReaders() const;

    /// Check if resource has any writers
    [[nodiscard]] bool HasWriter() const;

    /// Check if resource has multiple writers (definite conflict)
    [[nodiscard]] bool HasMultipleWriters() const;
};

/**
 * @brief Resource access pattern tracker for conflict detection
 *
 * Builds a map of resources to accessing nodes, enabling efficient
 * conflict detection for parallel scheduling.
 *
 * Thread Safety: NOT thread-safe. Build once, query from single thread.
 */
class ResourceAccessTracker {
public:
    ResourceAccessTracker() = default;
    ~ResourceAccessTracker() = default;

    // Non-copyable, movable
    ResourceAccessTracker(const ResourceAccessTracker&) = delete;
    ResourceAccessTracker& operator=(const ResourceAccessTracker&) = delete;
    ResourceAccessTracker(ResourceAccessTracker&&) noexcept = default;
    ResourceAccessTracker& operator=(ResourceAccessTracker&&) noexcept = default;

    // =========================================================================
    // Building
    // =========================================================================

    /**
     * @brief Build access tracking from graph topology
     *
     * Scans all nodes in the topology and records their resource accesses
     * based on input/output bundles.
     *
     * @param topology Graph topology containing nodes to analyze
     */
    void BuildFromTopology(const GraphTopology& topology);

    /**
     * @brief Add a single node's accesses to tracking
     *
     * @param node Node to analyze
     */
    void AddNode(NodeInstance* node);

    /**
     * @brief Clear all tracking data
     */
    void Clear();

    // =========================================================================
    // Conflict Detection
    // =========================================================================

    /**
     * @brief Check if two nodes have conflicting resource access
     *
     * Returns true if nodeA and nodeB access any common resource where
     * at least one of them writes.
     *
     * @param nodeA First node
     * @param nodeB Second node
     * @return true if conflict exists, false if safe to parallelize
     */
    [[nodiscard]] bool HasConflict(NodeInstance* nodeA, NodeInstance* nodeB) const;

    /**
     * @brief Get all nodes that conflict with a given node
     *
     * @param node Node to check
     * @return Set of conflicting nodes
     */
    [[nodiscard]] std::unordered_set<NodeInstance*> GetConflictingNodes(NodeInstance* node) const;

    /**
     * @brief Get shared resources between two nodes
     *
     * @param nodeA First node
     * @param nodeB Second node
     * @return Resources accessed by both nodes
     */
    [[nodiscard]] std::vector<Resource*> GetSharedResources(
        NodeInstance* nodeA,
        NodeInstance* nodeB
    ) const;

    // =========================================================================
    // Queries
    // =========================================================================

    /**
     * @brief Get access info for a specific resource
     *
     * @param resource Resource to query
     * @return Pointer to access info, or nullptr if not tracked
     */
    [[nodiscard]] const ResourceAccessInfo* GetAccessInfo(Resource* resource) const;

    /**
     * @brief Get all resources accessed by a node
     *
     * @param node Node to query
     * @return All resources the node accesses
     */
    [[nodiscard]] std::vector<Resource*> GetNodeResources(NodeInstance* node) const;

    /**
     * @brief Get resources a node writes to
     *
     * @param node Node to query
     * @return Resources the node writes
     */
    [[nodiscard]] std::vector<Resource*> GetNodeWrites(NodeInstance* node) const;

    /**
     * @brief Get resources a node reads from
     *
     * @param node Node to query
     * @return Resources the node reads
     */
    [[nodiscard]] std::vector<Resource*> GetNodeReads(NodeInstance* node) const;

    /**
     * @brief Check if a node writes to any resource
     *
     * @param node Node to check
     * @return true if node has any write access
     */
    [[nodiscard]] bool IsWriter(NodeInstance* node) const;

    /**
     * @brief Get total number of tracked resources
     */
    [[nodiscard]] size_t GetResourceCount() const { return resourceAccesses_.size(); }

    /**
     * @brief Get total number of tracked nodes
     */
    [[nodiscard]] size_t GetNodeCount() const { return nodeResources_.size(); }

    // =========================================================================
    // Statistics (for debugging/optimization)
    // =========================================================================

    /**
     * @brief Get count of resources with write conflicts
     */
    [[nodiscard]] size_t GetConflictingResourceCount() const;

    /**
     * @brief Get the maximum number of writers to any single resource
     */
    [[nodiscard]] size_t GetMaxWritersPerResource() const;

private:
    /// Resource -> access info mapping
    std::unordered_map<Resource*, ResourceAccessInfo> resourceAccesses_;

    /// Node -> accessed resources mapping (for efficient node queries)
    std::unordered_map<NodeInstance*, std::vector<Resource*>> nodeResources_;

    /// Node -> written resources mapping (for efficient conflict checks)
    std::unordered_map<NodeInstance*, std::unordered_set<Resource*>> nodeWrites_;

    /// Node -> read resources mapping
    std::unordered_map<NodeInstance*, std::unordered_set<Resource*>> nodeReads_;

    /**
     * @brief Record an access to a resource
     */
    void RecordAccess(
        Resource* resource,
        NodeInstance* node,
        ResourceAccessType accessType,
        uint32_t slotIndex,
        bool isOutput
    );
};

} // namespace Vixen::RenderGraph
