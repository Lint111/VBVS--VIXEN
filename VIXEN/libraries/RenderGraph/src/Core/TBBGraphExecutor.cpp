// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/TBBGraphExecutor.h"
#include "Core/GraphTopology.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"

#include <oneapi/tbb/flow_graph.h>
#include <oneapi/tbb/global_control.h>
#include <mutex>
#include <unordered_map>

namespace Vixen::RenderGraph {

// Note: Cannot use "namespace tbb = oneapi::tbb" as it conflicts with TBB's internal namespace injection

/**
 * @brief Pimpl implementation hiding TBB types
 *
 * Uses TBB flow_graph with continue_node for dependency-based execution.
 * Each NodeInstance becomes a continue_node that executes when all
 * predecessors complete.
 */
struct TBBGraphExecutor::Impl {
    // The TBB flow graph
    std::unique_ptr<oneapi::tbb::flow::graph> graph;

    // Start node broadcasts to all root nodes
    std::unique_ptr<oneapi::tbb::flow::broadcast_node<oneapi::tbb::flow::continue_msg>> startNode;

    // Continue nodes for each render graph node
    // continue_node waits for all predecessors before executing
    std::vector<std::unique_ptr<oneapi::tbb::flow::continue_node<oneapi::tbb::flow::continue_msg>>> continueNodes;

    // Map from NodeInstance* to continue node index
    std::unordered_map<NodeInstance*, size_t> nodeToIndex;

    // Stored nodes for execution
    std::vector<NodeInstance*> nodes;

    // Stored edges (from -> to indices)
    std::vector<std::pair<size_t, size_t>> edges;

    // Global control for limiting concurrency
    std::unique_ptr<oneapi::tbb::global_control> concurrencyControl;

    // Cancellation flag
    std::atomic<bool> cancelled{false};

    // Exception storage
    std::mutex exceptionMutex;
    std::vector<std::exception_ptr> exceptions;

    // Current executor function (set during Execute)
    std::function<void(NodeInstance*)> currentExecutor;

    Impl() = default;

    void Reset() {
        continueNodes.clear();
        startNode.reset();
        graph.reset();
        nodeToIndex.clear();
        nodes.clear();
        edges.clear();
        cancelled.store(false);
        exceptions.clear();
    }
};

TBBGraphExecutor::TBBGraphExecutor(const TBBExecutorConfig& config)
    : impl_(std::make_unique<Impl>())
    , config_(config)
{
    // Set up concurrency control if limited
    if (config_.maxConcurrency > 0) {
        impl_->concurrencyControl = std::make_unique<oneapi::tbb::global_control>(
            oneapi::tbb::global_control::max_allowed_parallelism,
            config_.maxConcurrency
        );
    }
}

TBBGraphExecutor::~TBBGraphExecutor() {
    Wait();
}

TBBGraphExecutor::TBBGraphExecutor(TBBGraphExecutor&& other) noexcept
    : impl_(std::move(other.impl_))
    , config_(other.config_)
    , graphBuilt_(other.graphBuilt_)
    , nodeCount_(other.nodeCount_)
    , edgeCount_(other.edgeCount_)
    , executionsCompleted_(other.executionsCompleted_.load())
    , exceptionsThrown_(other.exceptionsThrown_.load())
    , executeCount_(other.executeCount_.load())
    , totalExecutionMs_(other.totalExecutionMs_.load())
    , lastExecutionMs_(other.lastExecutionMs_)
{
    // Re-create impl for moved-from object so it remains valid
    other.impl_ = std::make_unique<Impl>();
    other.graphBuilt_ = false;
    other.nodeCount_ = 0;
    other.edgeCount_ = 0;
}

TBBGraphExecutor& TBBGraphExecutor::operator=(TBBGraphExecutor&& other) noexcept {
    if (this != &other) {
        Wait();
        impl_ = std::move(other.impl_);
        config_ = other.config_;
        graphBuilt_ = other.graphBuilt_;
        nodeCount_ = other.nodeCount_;
        edgeCount_ = other.edgeCount_;
        executionsCompleted_.store(other.executionsCompleted_.load());
        exceptionsThrown_.store(other.exceptionsThrown_.load());
        executeCount_.store(other.executeCount_.load());
        totalExecutionMs_.store(other.totalExecutionMs_.load());
        lastExecutionMs_ = other.lastExecutionMs_;

        // Re-create impl for moved-from object so it remains valid
        other.impl_ = std::make_unique<Impl>();
        other.graphBuilt_ = false;
        other.nodeCount_ = 0;
        other.edgeCount_ = 0;
    }
    return *this;
}

bool TBBGraphExecutor::BuildFromTopology(
    const GraphTopology& topology,
    const ResourceAccessTracker& accessTracker
) {
    // Get execution order from topology (topological sort)
    auto executionOrder = topology.TopologicalSort();
    if (executionOrder.empty()) {
        return true;  // Empty graph is valid
    }

    // Build node list
    std::vector<NodeInstance*> nodes;
    nodes.reserve(executionOrder.size());
    for (NodeInstance* node : executionOrder) {
        if (node) {
            nodes.push_back(node);
        }
    }

    // Build edge list from topology dependencies
    std::vector<std::pair<size_t, size_t>> edges;

    // Create index map
    std::unordered_map<NodeInstance*, size_t> nodeIndex;
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodeIndex[nodes[i]] = i;
    }

    // Add dependency edges
    for (size_t toIdx = 0; toIdx < nodes.size(); ++toIdx) {
        NodeInstance* toNode = nodes[toIdx];
        auto dependencies = topology.GetDirectDependencies(toNode);

        for (NodeInstance* fromNode : dependencies) {
            auto fromIt = nodeIndex.find(fromNode);
            if (fromIt != nodeIndex.end()) {
                edges.emplace_back(fromIt->second, toIdx);
            }
        }
    }

    // Add conflict edges from ResourceAccessTracker
    // Nodes with resource conflicts must not execute concurrently
    for (size_t i = 0; i < nodes.size(); ++i) {
        for (size_t j = i + 1; j < nodes.size(); ++j) {
            if (accessTracker.HasConflict(nodes[i], nodes[j])) {
                // Add edge from earlier to later (in topological order)
                // This ensures they don't run concurrently
                edges.emplace_back(i, j);
            }
        }
    }

    return BuildFromNodes(nodes, edges);
}

bool TBBGraphExecutor::BuildFromNodes(
    const std::vector<NodeInstance*>& nodes,
    const std::vector<std::pair<size_t, size_t>>& edges
) {
    Clear();

    if (nodes.empty()) {
        graphBuilt_ = true;
        return true;
    }

    impl_->nodes = nodes;
    impl_->edges = edges;
    nodeCount_ = nodes.size();
    edgeCount_ = edges.size();

    // Build index map
    for (size_t i = 0; i < nodes.size(); ++i) {
        impl_->nodeToIndex[nodes[i]] = i;
    }

    // Graph will be built lazily during Execute()
    graphBuilt_ = true;
    return true;
}

void TBBGraphExecutor::RebuildGraph() {
    // Create new TBB graph
    impl_->graph = std::make_unique<oneapi::tbb::flow::graph>();
    impl_->continueNodes.clear();

    // Create start node (broadcasts to all root nodes)
    impl_->startNode = std::make_unique<oneapi::tbb::flow::broadcast_node<oneapi::tbb::flow::continue_msg>>(
        *impl_->graph
    );

    // Determine which nodes have incoming edges (not roots)
    std::vector<bool> hasIncoming(impl_->nodes.size(), false);
    for (const auto& [from, to] : impl_->edges) {
        if (to < hasIncoming.size()) {
            hasIncoming[to] = true;
        }
    }

    // Create continue_node for each render graph node
    // continue_node waits for ALL predecessors to send continue_msg before executing
    impl_->continueNodes.reserve(impl_->nodes.size());

    for (size_t i = 0; i < impl_->nodes.size(); ++i) {
        NodeInstance* node = impl_->nodes[i];

        // Create continue_node with body that executes the render graph node
        auto continueNode = std::make_unique<oneapi::tbb::flow::continue_node<oneapi::tbb::flow::continue_msg>>(
            *impl_->graph,
            [this, node](oneapi::tbb::flow::continue_msg) -> oneapi::tbb::flow::continue_msg {
                // Check cancellation
                if (impl_->cancelled.load()) {
                    return oneapi::tbb::flow::continue_msg();
                }

                // Execute the node
                if (config_.captureExceptions) {
                    try {
                        if (impl_->currentExecutor) {
                            impl_->currentExecutor(node);
                        }
                        executionsCompleted_.fetch_add(1);
                    } catch (...) {
                        exceptionsThrown_.fetch_add(1);
                        std::lock_guard<std::mutex> lock(impl_->exceptionMutex);
                        impl_->exceptions.push_back(std::current_exception());
                    }
                } else {
                    if (impl_->currentExecutor) {
                        impl_->currentExecutor(node);
                    }
                    executionsCompleted_.fetch_add(1);
                }

                return oneapi::tbb::flow::continue_msg();
            }
        );

        impl_->continueNodes.push_back(std::move(continueNode));
    }

    // Connect start node to root nodes (nodes with no incoming edges)
    for (size_t i = 0; i < impl_->nodes.size(); ++i) {
        if (!hasIncoming[i]) {
            oneapi::tbb::flow::make_edge(*impl_->startNode, *impl_->continueNodes[i]);
        }
    }

    // Connect dependency edges
    for (const auto& [from, to] : impl_->edges) {
        if (from < impl_->continueNodes.size() && to < impl_->continueNodes.size()) {
            oneapi::tbb::flow::make_edge(*impl_->continueNodes[from], *impl_->continueNodes[to]);
        }
    }
}

void TBBGraphExecutor::Clear() {
    Wait();
    impl_->Reset();
    graphBuilt_ = false;
    nodeCount_ = 0;
    edgeCount_ = 0;
}

void TBBGraphExecutor::Execute(std::function<void(NodeInstance*)> executor) {
    if (!graphBuilt_ || impl_->nodes.empty()) {
        return;
    }

    // Sequential mode - just iterate
    if (config_.mode == TBBExecutionMode::Sequential) {
        auto start = std::chrono::high_resolution_clock::now();

        for (NodeInstance* node : impl_->nodes) {
            executor(node);
            executionsCompleted_.fetch_add(1);
        }

        auto end = std::chrono::high_resolution_clock::now();
        lastExecutionMs_ = std::chrono::duration<double, std::milli>(end - start).count();
        totalExecutionMs_.store(totalExecutionMs_.load() + lastExecutionMs_);
        executeCount_.fetch_add(1);
        return;
    }

    // Rebuild TBB graph (nodes and edges stored)
    RebuildGraph();

    impl_->currentExecutor = std::move(executor);
    impl_->cancelled.store(false);
    impl_->exceptions.clear();

    auto start = std::chrono::high_resolution_clock::now();

    // Trigger execution by sending message to start node
    impl_->startNode->try_put(oneapi::tbb::flow::continue_msg());

    // Wait for all nodes to complete
    impl_->graph->wait_for_all();

    auto end = std::chrono::high_resolution_clock::now();
    lastExecutionMs_ = std::chrono::duration<double, std::milli>(end - start).count();
    totalExecutionMs_.store(totalExecutionMs_.load() + lastExecutionMs_);
    executeCount_.fetch_add(1);

    impl_->currentExecutor = nullptr;

    // Rethrow first exception if any
    if (!impl_->exceptions.empty()) {
        std::rethrow_exception(impl_->exceptions.front());
    }
}

std::vector<std::exception_ptr> TBBGraphExecutor::ExecuteCollectErrors(
    std::function<void(NodeInstance*)> executor
) {
    if (!graphBuilt_ || impl_->nodes.empty()) {
        return {};
    }

    // Sequential mode
    if (config_.mode == TBBExecutionMode::Sequential) {
        std::vector<std::exception_ptr> errors;
        auto start = std::chrono::high_resolution_clock::now();

        for (NodeInstance* node : impl_->nodes) {
            try {
                executor(node);
                executionsCompleted_.fetch_add(1);
            } catch (...) {
                exceptionsThrown_.fetch_add(1);
                errors.push_back(std::current_exception());
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        lastExecutionMs_ = std::chrono::duration<double, std::milli>(end - start).count();
        totalExecutionMs_.store(totalExecutionMs_.load() + lastExecutionMs_);
        executeCount_.fetch_add(1);
        return errors;
    }

    // Rebuild and execute
    RebuildGraph();

    impl_->currentExecutor = std::move(executor);
    impl_->cancelled.store(false);
    impl_->exceptions.clear();

    auto start = std::chrono::high_resolution_clock::now();

    impl_->startNode->try_put(oneapi::tbb::flow::continue_msg());
    impl_->graph->wait_for_all();

    auto end = std::chrono::high_resolution_clock::now();
    lastExecutionMs_ = std::chrono::duration<double, std::milli>(end - start).count();
    totalExecutionMs_.store(totalExecutionMs_.load() + lastExecutionMs_);
    executeCount_.fetch_add(1);

    impl_->currentExecutor = nullptr;

    return std::move(impl_->exceptions);
}

void TBBGraphExecutor::Cancel() {
    impl_->cancelled.store(true);
    if (impl_->graph) {
        impl_->graph->cancel();
    }
}

void TBBGraphExecutor::Wait() {
    if (impl_->graph) {
        impl_->graph->wait_for_all();
    }
}

void TBBGraphExecutor::SetMode(TBBExecutionMode mode) {
    config_.mode = mode;
}

void TBBGraphExecutor::SetMaxConcurrency(size_t max) {
    config_.maxConcurrency = max;
    if (max > 0) {
        impl_->concurrencyControl = std::make_unique<oneapi::tbb::global_control>(
            oneapi::tbb::global_control::max_allowed_parallelism,
            max
        );
    } else {
        impl_->concurrencyControl.reset();
    }
}

TBBExecutorStats TBBGraphExecutor::GetStats() const {
    TBBExecutorStats stats;
    stats.nodeCount = nodeCount_;
    stats.edgeCount = edgeCount_;
    stats.executionsCompleted = executionsCompleted_.load();
    stats.exceptionsThrown = exceptionsThrown_.load();
    stats.lastExecutionMs = lastExecutionMs_;
    stats.executeCount = executeCount_.load();

    if (stats.executeCount > 0) {
        stats.avgExecutionMs = totalExecutionMs_.load() / stats.executeCount;
    }

    return stats;
}

void TBBGraphExecutor::ResetStats() {
    executionsCompleted_.store(0);
    exceptionsThrown_.store(0);
    executeCount_.store(0);
    totalExecutionMs_.store(0.0);
    lastExecutionMs_ = 0.0;
}

} // namespace Vixen::RenderGraph
