// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file TBBGraphExecutor.h
 * @brief TBB flow_graph based executor for parallel render graph execution
 *
 * Sprint 6.4: Phase 3 - TBB Integration
 * Design Element: #38 Timeline Capacity Tracker (Wave Scheduling extension)
 *
 * TBBGraphExecutor provides parallel execution of render graph nodes using
 * Intel TBB's flow_graph. Unlike the WaveScheduler + ThreadPool approach,
 * TBB flow_graph handles dependencies natively and uses work-stealing for
 * optimal load balancing.
 *
 * Benefits over custom WaveScheduler:
 * - Native dependency handling (no manual wave computation)
 * - Work-stealing prevents deadlock from nested parallelism
 * - Better load balancing for variable-cost nodes
 * - Proven production-quality implementation
 *
 * Usage:
 * @code
 * TBBGraphExecutor executor;
 *
 * // Build graph from topology
 * executor.BuildFromTopology(topology, accessTracker);
 *
 * // Execute all nodes in parallel (respects dependencies)
 * executor.Execute([](NodeInstance* node) {
 *     node->Execute();
 * });
 *
 * // Check stats
 * auto stats = executor.GetStats();
 * @endcode
 *
 * @see GraphTopology for dependency information
 * @see ResourceAccessTracker for conflict detection
 */

#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>

// Note: TBB types are hidden via pimpl in the cpp file
// No forward declarations needed here

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;
class GraphTopology;
class ResourceAccessTracker;

/**
 * @brief Execution statistics from TBB graph executor
 */
struct TBBExecutorStats {
    size_t nodeCount = 0;           ///< Total nodes in graph
    size_t edgeCount = 0;           ///< Total dependency edges
    size_t executionsCompleted = 0; ///< Total node executions
    size_t exceptionsThrown = 0;    ///< Exceptions during execution
    double lastExecutionMs = 0.0;   ///< Last Execute() duration in ms
    double avgExecutionMs = 0.0;    ///< Average Execute() duration
    size_t executeCount = 0;        ///< Number of Execute() calls
};

/**
 * @brief Execution mode for the TBB executor
 */
enum class TBBExecutionMode {
    Parallel,    ///< Full parallel execution (default)
    Sequential,  ///< Sequential execution (for debugging)
    Limited      ///< Limited parallelism (max N concurrent)
};

/**
 * @brief Configuration for TBB executor
 */
struct TBBExecutorConfig {
    TBBExecutionMode mode = TBBExecutionMode::Parallel;
    size_t maxConcurrency = 0;  ///< 0 = unlimited (hardware_concurrency)
    bool captureExceptions = true;
    bool enableProfiling = false;
};

/**
 * @brief TBB flow_graph based executor for render graph nodes
 *
 * Builds a TBB flow_graph that mirrors the render graph's dependency
 * structure. Each node becomes a function_node, and dependencies become
 * edges. TBB handles scheduling automatically with work-stealing.
 */
class TBBGraphExecutor {
public:
    /**
     * @brief Construct executor with optional configuration
     */
    explicit TBBGraphExecutor(const TBBExecutorConfig& config = {});

    /**
     * @brief Destructor - waits for any pending execution
     */
    ~TBBGraphExecutor();

    // Non-copyable, movable
    TBBGraphExecutor(const TBBGraphExecutor&) = delete;
    TBBGraphExecutor& operator=(const TBBGraphExecutor&) = delete;
    TBBGraphExecutor(TBBGraphExecutor&&) noexcept;
    TBBGraphExecutor& operator=(TBBGraphExecutor&&) noexcept;

    // =========================================================================
    // Graph Construction
    // =========================================================================

    /**
     * @brief Build TBB flow graph from render graph topology
     *
     * Creates function_node for each NodeInstance and connects them
     * based on dependencies from GraphTopology.
     *
     * @param topology Dependency information
     * @param accessTracker Resource access patterns (for conflict edges)
     * @return true if graph built successfully
     */
    bool BuildFromTopology(
        const GraphTopology& topology,
        const ResourceAccessTracker& accessTracker
    );

    /**
     * @brief Build graph from explicit node list and edges
     *
     * Lower-level API for custom graph construction.
     *
     * @param nodes Nodes in execution order
     * @param edges Pairs of (from, to) node indices indicating dependencies
     * @return true if graph built successfully
     */
    bool BuildFromNodes(
        const std::vector<NodeInstance*>& nodes,
        const std::vector<std::pair<size_t, size_t>>& edges
    );

    /**
     * @brief Clear the current graph
     */
    void Clear();

    /**
     * @brief Check if graph has been built
     */
    [[nodiscard]] bool IsBuilt() const { return graphBuilt_; }

    // =========================================================================
    // Execution
    // =========================================================================

    /**
     * @brief Execute all nodes in the graph
     *
     * Nodes execute in parallel where dependencies allow.
     * Blocks until all nodes complete.
     *
     * @param executor Function to call for each node
     * @throws std::exception if any node throws and captureExceptions is false
     */
    void Execute(std::function<void(NodeInstance*)> executor);

    /**
     * @brief Execute with exception collection
     *
     * Like Execute but collects all exceptions instead of throwing.
     *
     * @param executor Function to call for each node
     * @return Vector of exception_ptrs (empty if all succeeded)
     */
    std::vector<std::exception_ptr> ExecuteCollectErrors(
        std::function<void(NodeInstance*)> executor
    );

    /**
     * @brief Cancel any pending execution
     *
     * Nodes currently executing will complete, but no new nodes will start.
     */
    void Cancel();

    /**
     * @brief Wait for any pending execution to complete
     */
    void Wait();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set execution mode
     */
    void SetMode(TBBExecutionMode mode);

    /**
     * @brief Get current execution mode
     */
    [[nodiscard]] TBBExecutionMode GetMode() const { return config_.mode; }

    /**
     * @brief Set maximum concurrency (0 = unlimited)
     */
    void SetMaxConcurrency(size_t max);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get execution statistics
     */
    [[nodiscard]] TBBExecutorStats GetStats() const;

    /**
     * @brief Reset statistics
     */
    void ResetStats();

    /**
     * @brief Get node count
     */
    [[nodiscard]] size_t GetNodeCount() const { return nodeCount_; }

    /**
     * @brief Get edge count
     */
    [[nodiscard]] size_t GetEdgeCount() const { return edgeCount_; }

private:
    // Pimpl for TBB types to avoid header dependency
    struct Impl;
    std::unique_ptr<Impl> impl_;

    TBBExecutorConfig config_;
    bool graphBuilt_ = false;
    size_t nodeCount_ = 0;
    size_t edgeCount_ = 0;

    // Statistics
    std::atomic<size_t> executionsCompleted_{0};
    std::atomic<size_t> exceptionsThrown_{0};
    std::atomic<size_t> executeCount_{0};
    std::atomic<double> totalExecutionMs_{0.0};
    double lastExecutionMs_ = 0.0;

    /**
     * @brief Internal graph rebuild
     */
    void RebuildGraph();
};

} // namespace Vixen::RenderGraph
