// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file WaveScheduler.h
 * @brief Parallel execution wave computation for render graphs
 *
 * Sprint 6.4: Phase 1 - WaveScheduler Core Algorithm
 * Design Element: #38 Timeline Capacity Tracker (Wave Scheduling extension)
 *
 * WaveScheduler computes "execution waves" - groups of nodes that can safely
 * execute in parallel. A wave contains nodes that:
 * 1. Have all dependencies in earlier waves
 * 2. Have no resource conflicts with other nodes in the same wave
 *
 * Algorithm complexity: O(N * E) where N = nodes, E = edges
 *
 * Usage:
 * @code
 * WaveScheduler scheduler;
 * scheduler.ComputeWaves(topology, accessTracker);
 *
 * for (const auto& wave : scheduler.GetWaves()) {
 *     // Execute all nodes in wave.nodes concurrently
 *     for (NodeInstance* node : wave.nodes) {
 *         threadPool.Submit([node] { node->Execute(); });
 *     }
 *     // Barrier: wait for all wave nodes to complete
 * }
 * @endcode
 *
 * @see ResourceAccessTracker for conflict detection
 * @see GraphTopology for dependency analysis
 */

#include "NodeInstance.h"
#include "GraphTopology.h"
#include "ResourceAccessTracker.h"
#include <vector>
#include <unordered_map>

namespace Vixen::RenderGraph {

/**
 * @brief A group of nodes that can execute concurrently
 *
 * All nodes in a wave:
 * - Have no dependencies on each other
 * - Have no resource conflicts with each other
 * - Can safely execute in parallel
 */
struct ExecutionWave {
    uint32_t waveIndex = 0;                  ///< Wave number (0 = first wave)
    std::vector<NodeInstance*> nodes;        ///< Nodes in this wave

    /// Check if wave is empty
    [[nodiscard]] bool IsEmpty() const { return nodes.empty(); }

    /// Get number of nodes in wave
    [[nodiscard]] size_t Size() const { return nodes.size(); }
};

/**
 * @brief Statistics about wave computation
 */
struct WaveSchedulerStats {
    size_t totalNodes = 0;           ///< Total nodes scheduled
    size_t waveCount = 0;            ///< Number of waves
    size_t maxWaveSize = 0;          ///< Largest wave (max parallelism)
    size_t minWaveSize = 0;          ///< Smallest wave
    float avgWaveSize = 0.0f;        ///< Average wave size
    float parallelismFactor = 0.0f;  ///< avgWaveSize / waveCount (higher = more parallel)
    size_t conflictCount = 0;        ///< Number of conflict-induced wave splits
};

/**
 * @brief Computes parallel execution waves for render graph nodes
 *
 * Given a graph topology and resource access patterns, WaveScheduler
 * partitions nodes into waves that can safely execute concurrently.
 *
 * Thread Safety: NOT thread-safe. ComputeWaves must complete before
 * accessing results. Use from single thread or with external synchronization.
 */
class WaveScheduler {
public:
    WaveScheduler() = default;
    ~WaveScheduler() = default;

    // Non-copyable, movable
    WaveScheduler(const WaveScheduler&) = delete;
    WaveScheduler& operator=(const WaveScheduler&) = delete;
    WaveScheduler(WaveScheduler&&) noexcept = default;
    WaveScheduler& operator=(WaveScheduler&&) noexcept = default;

    // =========================================================================
    // Wave Computation
    // =========================================================================

    /**
     * @brief Compute execution waves from graph topology and access patterns
     *
     * Algorithm:
     * 1. Get topological order from GraphTopology
     * 2. For each node in topological order:
     *    a. Find earliest wave where all dependencies are in earlier waves
     *    b. Check for resource conflicts with nodes in that wave
     *    c. If conflict, try next wave; repeat until no conflict
     *    d. Assign node to wave
     *
     * @param topology Graph structure with dependencies
     * @param accessTracker Resource access patterns for conflict detection
     * @return true if waves computed successfully, false if graph has issues
     */
    bool ComputeWaves(const GraphTopology& topology, const ResourceAccessTracker& accessTracker);

    /**
     * @brief Recompute waves (convenience method)
     *
     * Call when graph structure or access patterns change.
     * Clears existing waves and recomputes.
     */
    void Recompute();

    /**
     * @brief Clear all computed waves
     */
    void Clear();

    // =========================================================================
    // Results Access
    // =========================================================================

    /**
     * @brief Get all computed waves
     *
     * Waves are ordered: wave[0] executes first, wave[N-1] executes last.
     * Within each wave, nodes can execute in any order (parallel).
     */
    [[nodiscard]] const std::vector<ExecutionWave>& GetWaves() const { return waves_; }

    /**
     * @brief Get number of waves
     */
    [[nodiscard]] size_t GetWaveCount() const { return waves_.size(); }

    /**
     * @brief Get total nodes scheduled
     */
    [[nodiscard]] size_t GetTotalNodes() const { return totalNodes_; }

    /**
     * @brief Get wave index for a specific node
     *
     * @param node Node to query
     * @return Wave index, or UINT32_MAX if node not found
     */
    [[nodiscard]] uint32_t GetNodeWave(NodeInstance* node) const;

    /**
     * @brief Check if waves have been computed
     */
    [[nodiscard]] bool IsComputed() const { return computed_; }

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get wave computation statistics
     */
    [[nodiscard]] WaveSchedulerStats GetStats() const;

    /**
     * @brief Get parallelism factor (average nodes per wave)
     *
     * Higher values indicate more parallel execution potential.
     * Value of 1.0 means fully sequential.
     */
    [[nodiscard]] float GetParallelismFactor() const;

    /**
     * @brief Get theoretical speedup vs sequential execution
     *
     * Returns totalNodes / waveCount (ideal speedup assuming
     * all nodes take equal time and no thread overhead).
     */
    [[nodiscard]] float GetTheoreticalSpeedup() const;

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate computed waves are correct
     *
     * Checks:
     * - All nodes from topology are scheduled
     * - No dependency violations (dependent in same or later wave)
     * - No resource conflicts within waves
     *
     * @param topology Original graph topology
     * @param accessTracker Original access tracker
     * @return true if valid, false with error message otherwise
     */
    [[nodiscard]] bool Validate(
        const GraphTopology& topology,
        const ResourceAccessTracker& accessTracker,
        std::string& errorMessage
    ) const;

private:
    std::vector<ExecutionWave> waves_;
    std::unordered_map<NodeInstance*, uint32_t> nodeToWave_;
    size_t totalNodes_ = 0;
    size_t conflictCount_ = 0;
    bool computed_ = false;

    // Cached references for Recompute()
    const GraphTopology* cachedTopology_ = nullptr;
    const ResourceAccessTracker* cachedAccessTracker_ = nullptr;

    /**
     * @brief Find earliest wave for a node based on dependencies
     *
     * @param node Node to place
     * @return Minimum wave index (dependencies must be in earlier waves)
     */
    uint32_t FindEarliestWaveByDependencies(NodeInstance* node) const;

    /**
     * @brief Check if node conflicts with any node in a wave
     *
     * @param node Node to check
     * @param waveIndex Wave to check against
     * @param accessTracker Resource access tracker
     * @return true if conflict exists
     */
    bool HasConflictInWave(
        NodeInstance* node,
        uint32_t waveIndex,
        const ResourceAccessTracker& accessTracker
    ) const;

    /**
     * @brief Ensure wave exists at index, creating empty waves if needed
     */
    void EnsureWaveExists(uint32_t waveIndex);
};

} // namespace Vixen::RenderGraph
