// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file VirtualResourceAccessTracker.h
 * @brief Per-task resource access tracking for task-level parallelism
 *
 * Sprint 6.5: Task-Level Parallelism Architecture
 * Design Element: #38 Timeline Capacity Tracker (Virtual Task Extension)
 *
 * VirtualResourceAccessTracker extends ResourceAccessTracker to track
 * resource access at (node, taskIndex) granularity. This enables the
 * TBBVirtualTaskExecutor to parallelize individual bundles across nodes.
 *
 * Key Differences from ResourceAccessTracker:
 * - Tracks VirtualTaskId (node + taskIndex) instead of just NodeInstance*
 * - Iterates through bundles to extract per-task resources
 * - Conflict detection at task granularity
 *
 * Conflict Rules (same as ResourceAccessTracker):
 * - Writer + Writer on same resource = CONFLICT
 * - Writer + Reader on same resource = CONFLICT
 * - Reader + Reader on same resource = OK
 *
 * Usage:
 * @code
 * VirtualResourceAccessTracker tracker;
 * tracker.BuildFromTopology(graphTopology);
 *
 * VirtualTaskId taskA{nodeA, 0};
 * VirtualTaskId taskB{nodeB, 1};
 *
 * if (!tracker.HasConflict(taskA, taskB)) {
 *     // Safe to execute concurrently
 * }
 * @endcode
 *
 * @see VirtualTask for task identification
 * @see TaskDependencyGraph for dependency resolution
 * @see TBBVirtualTaskExecutor for parallel execution
 */

#include "VirtualTask.h"
#include "ResourceAccessTracker.h"  // For ResourceAccessType enum
#include "GraphTopology.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vixen::RenderGraph {

// Forward declarations
class Resource;

/**
 * @brief Access record for a virtual task's access to a resource
 */
struct VirtualResourceAccess {
    VirtualTaskId task;                                 ///< Task accessing the resource
    ResourceAccessType accessType = ResourceAccessType::Read;
    uint32_t slotIndex = 0;                             ///< Input or output slot index
    bool isOutput = false;                              ///< True if output, false if input
};

/**
 * @brief Per-resource access tracking at task granularity
 */
struct VirtualResourceAccessInfo {
    Resource* resource = nullptr;
    std::vector<VirtualResourceAccess> accesses;

    /// Get all tasks that write to this resource
    [[nodiscard]] std::vector<VirtualTaskId> GetWriters() const;

    /// Get all tasks that read from this resource
    [[nodiscard]] std::vector<VirtualTaskId> GetReaders() const;

    /// Check if resource has any writers
    [[nodiscard]] bool HasWriter() const;

    /// Check if resource has multiple writers (definite conflict)
    [[nodiscard]] bool HasMultipleWriters() const;

    /// Get writer count
    [[nodiscard]] size_t GetWriterCount() const;

    /// Get reader count
    [[nodiscard]] size_t GetReaderCount() const;
};

/**
 * @brief Per-task resource access tracker for fine-grained conflict detection
 *
 * Tracks which VirtualTasks (node + taskIndex pairs) access which resources,
 * enabling task-level parallel scheduling.
 *
 * Thread Safety: NOT thread-safe. Build once, query from single thread.
 */
class VirtualResourceAccessTracker {
public:
    VirtualResourceAccessTracker() = default;
    ~VirtualResourceAccessTracker() = default;

    // Non-copyable, movable
    VirtualResourceAccessTracker(const VirtualResourceAccessTracker&) = delete;
    VirtualResourceAccessTracker& operator=(const VirtualResourceAccessTracker&) = delete;
    VirtualResourceAccessTracker(VirtualResourceAccessTracker&&) noexcept = default;
    VirtualResourceAccessTracker& operator=(VirtualResourceAccessTracker&&) noexcept = default;

    // =========================================================================
    // Building
    // =========================================================================

    /**
     * @brief Build access tracking from graph topology
     *
     * Scans all nodes and their bundles to record per-task resource accesses.
     *
     * @param topology Graph topology containing nodes to analyze
     */
    void BuildFromTopology(const GraphTopology& topology);

    /**
     * @brief Add a single node's per-task accesses to tracking
     *
     * Iterates through node's bundles and creates VirtualTaskIds for each.
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
     * @brief Check if two virtual tasks have conflicting resource access
     *
     * Returns true if taskA and taskB access any common resource where
     * at least one of them writes.
     *
     * @param taskA First task
     * @param taskB Second task
     * @return true if conflict exists, false if safe to parallelize
     */
    [[nodiscard]] bool HasConflict(const VirtualTaskId& taskA, const VirtualTaskId& taskB) const;

    /**
     * @brief Get all tasks that conflict with a given task
     *
     * @param task Task to check
     * @return Set of conflicting tasks
     */
    [[nodiscard]] std::unordered_set<VirtualTaskId, VirtualTaskIdHash>
        GetConflictingTasks(const VirtualTaskId& task) const;

    /**
     * @brief Get shared resources between two tasks
     *
     * @param taskA First task
     * @param taskB Second task
     * @return Resources accessed by both tasks
     */
    [[nodiscard]] std::vector<Resource*> GetSharedResources(
        const VirtualTaskId& taskA,
        const VirtualTaskId& taskB
    ) const;

    /**
     * @brief Check if two tasks from the same node have conflicts
     *
     * Useful for determining intra-node parallelism potential.
     *
     * @param node Node to check
     * @param taskIndexA First task index
     * @param taskIndexB Second task index
     * @return true if conflict exists
     */
    [[nodiscard]] bool HasIntraNodeConflict(
        NodeInstance* node,
        uint32_t taskIndexA,
        uint32_t taskIndexB
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
    [[nodiscard]] const VirtualResourceAccessInfo* GetAccessInfo(Resource* resource) const;

    /**
     * @brief Get all resources accessed by a virtual task
     *
     * @param task Task to query
     * @return All resources the task accesses
     */
    [[nodiscard]] std::vector<Resource*> GetTaskResources(const VirtualTaskId& task) const;

    /**
     * @brief Get resources a virtual task writes to
     *
     * @param task Task to query
     * @return Resources the task writes
     */
    [[nodiscard]] std::vector<Resource*> GetTaskWrites(const VirtualTaskId& task) const;

    /**
     * @brief Get resources a virtual task reads from
     *
     * @param task Task to query
     * @return Resources the task reads
     */
    [[nodiscard]] std::vector<Resource*> GetTaskReads(const VirtualTaskId& task) const;

    /**
     * @brief Check if a task writes to any resource
     *
     * @param task Task to check
     * @return true if task has any write access
     */
    [[nodiscard]] bool IsWriter(const VirtualTaskId& task) const;

    /**
     * @brief Get all virtual tasks for a node
     *
     * @param node Node to query
     * @return All VirtualTaskIds associated with this node
     */
    [[nodiscard]] std::vector<VirtualTaskId> GetNodeTasks(NodeInstance* node) const;

    /**
     * @brief Get task count for a node
     *
     * @param node Node to query
     * @return Number of tasks (bundles) for this node
     */
    [[nodiscard]] uint32_t GetNodeTaskCount(NodeInstance* node) const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get total number of tracked resources
     */
    [[nodiscard]] size_t GetResourceCount() const { return resourceAccesses_.size(); }

    /**
     * @brief Get total number of tracked virtual tasks
     */
    [[nodiscard]] size_t GetTaskCount() const { return taskResources_.size(); }

    /**
     * @brief Get total number of tracked nodes
     */
    [[nodiscard]] size_t GetNodeCount() const { return nodeTasks_.size(); }

    /**
     * @brief Get count of resources with write conflicts
     */
    [[nodiscard]] size_t GetConflictingResourceCount() const;

    /**
     * @brief Get the maximum number of writers to any single resource
     */
    [[nodiscard]] size_t GetMaxWritersPerResource() const;

    /**
     * @brief Get potential parallelism factor
     *
     * Estimates how many tasks could theoretically run in parallel
     * based on resource conflicts.
     *
     * @return Value between 0.0 (fully sequential) and 1.0 (fully parallel)
     */
    [[nodiscard]] float GetParallelismPotential() const;

private:
    /// Resource -> per-task access info
    std::unordered_map<Resource*, VirtualResourceAccessInfo> resourceAccesses_;

    /// VirtualTaskId -> accessed resources
    std::unordered_map<VirtualTaskId, std::vector<Resource*>, VirtualTaskIdHash> taskResources_;

    /// VirtualTaskId -> written resources
    std::unordered_map<VirtualTaskId, std::unordered_set<Resource*>, VirtualTaskIdHash> taskWrites_;

    /// VirtualTaskId -> read resources
    std::unordered_map<VirtualTaskId, std::unordered_set<Resource*>, VirtualTaskIdHash> taskReads_;

    /// Node -> its VirtualTaskIds (for efficient node queries)
    std::unordered_map<NodeInstance*, std::vector<VirtualTaskId>> nodeTasks_;

    /**
     * @brief Record an access to a resource
     */
    void RecordAccess(
        Resource* resource,
        const VirtualTaskId& task,
        ResourceAccessType accessType,
        uint32_t slotIndex,
        bool isOutput
    );
};

} // namespace Vixen::RenderGraph
