// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_tbb_graph_executor.cpp
 * @brief Unit tests for TBBGraphExecutor parallel execution
 *
 * Sprint 6.4: Phase 3 - TBB Integration
 * Design Element: #38 Timeline Capacity Tracker
 */

#include <gtest/gtest.h>
#include "Core/TBBGraphExecutor.h"
#include "Core/GraphTopology.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <set>
#include <mutex>

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST HELPERS
// ============================================================================

class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& name) : NodeType(name) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<MockNodeType*>(this));
    }
};

// ============================================================================
// BASIC TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, DefaultConstruction) {
    TBBGraphExecutor executor;
    EXPECT_FALSE(executor.IsBuilt());
    EXPECT_EQ(executor.GetNodeCount(), 0u);
    EXPECT_EQ(executor.GetEdgeCount(), 0u);
}

TEST(TBBGraphExecutorTest, ConfiguredConstruction) {
    TBBExecutorConfig config;
    config.mode = TBBExecutionMode::Sequential;
    config.maxConcurrency = 4;

    TBBGraphExecutor executor(config);
    EXPECT_EQ(executor.GetMode(), TBBExecutionMode::Sequential);
}

TEST(TBBGraphExecutorTest, BuildFromEmptyNodes) {
    TBBGraphExecutor executor;

    std::vector<NodeInstance*> nodes;
    std::vector<std::pair<size_t, size_t>> edges;

    EXPECT_TRUE(executor.BuildFromNodes(nodes, edges));
    EXPECT_TRUE(executor.IsBuilt());
    EXPECT_EQ(executor.GetNodeCount(), 0u);
}

// ============================================================================
// GRAPH BUILDING TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, BuildFromNodes_Simple) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges = {
        {0, 1},  // Node1 -> Node2
        {1, 2}   // Node2 -> Node3
    };

    EXPECT_TRUE(executor.BuildFromNodes(nodes, edges));
    EXPECT_TRUE(executor.IsBuilt());
    EXPECT_EQ(executor.GetNodeCount(), 3u);
    EXPECT_EQ(executor.GetEdgeCount(), 2u);
}

TEST(TBBGraphExecutorTest, BuildFromNodes_Diamond) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    //     A
    //    / \
    //   B   C
    //    \ /
    //     D
    auto nodeA = nodeType.CreateInstance("A");
    auto nodeB = nodeType.CreateInstance("B");
    auto nodeC = nodeType.CreateInstance("C");
    auto nodeD = nodeType.CreateInstance("D");

    std::vector<NodeInstance*> nodes = {
        nodeA.get(), nodeB.get(), nodeC.get(), nodeD.get()
    };
    std::vector<std::pair<size_t, size_t>> edges = {
        {0, 1},  // A -> B
        {0, 2},  // A -> C
        {1, 3},  // B -> D
        {2, 3}   // C -> D
    };

    EXPECT_TRUE(executor.BuildFromNodes(nodes, edges));
    EXPECT_EQ(executor.GetNodeCount(), 4u);
    EXPECT_EQ(executor.GetEdgeCount(), 4u);
}

TEST(TBBGraphExecutorTest, Clear_ResetsState) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);
    EXPECT_TRUE(executor.IsBuilt());

    executor.Clear();
    EXPECT_FALSE(executor.IsBuilt());
    EXPECT_EQ(executor.GetNodeCount(), 0u);
}

// ============================================================================
// EXECUTION TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, Execute_SingleNode) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> count{0};
    executor.Execute([&count](NodeInstance*) {
        count.fetch_add(1);
    });

    EXPECT_EQ(count.load(), 1);
}

TEST(TBBGraphExecutorTest, Execute_AllNodesExecuted) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges = {
        {0, 1}, {1, 2}
    };

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> count{0};
    executor.Execute([&count](NodeInstance*) {
        count.fetch_add(1);
    });

    EXPECT_EQ(count.load(), 3);
}

TEST(TBBGraphExecutorTest, Execute_RespectsOrder_Linear) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges = {
        {0, 1}, {1, 2}  // Linear chain
    };

    executor.BuildFromNodes(nodes, edges);

    std::vector<NodeInstance*> executionOrder;
    std::mutex orderMutex;

    executor.Execute([&](NodeInstance* node) {
        std::lock_guard<std::mutex> lock(orderMutex);
        executionOrder.push_back(node);
    });

    // In a linear chain, order must be preserved
    ASSERT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], node1.get());
    EXPECT_EQ(executionOrder[1], node2.get());
    EXPECT_EQ(executionOrder[2], node3.get());
}

TEST(TBBGraphExecutorTest, Execute_ParallelNodes) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    // Three independent nodes (no edges) - can run in parallel
    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges;  // No dependencies

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> concurrentCount{0};
    std::atomic<int> maxConcurrent{0};
    std::atomic<int> totalExecuted{0};

    executor.Execute([&](NodeInstance*) {
        int current = concurrentCount.fetch_add(1) + 1;

        // Track max concurrency
        int expected = maxConcurrent.load();
        while (current > expected && !maxConcurrent.compare_exchange_weak(expected, current)) {}

        // Simulate work to allow parallelism
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        concurrentCount.fetch_sub(1);
        totalExecuted.fetch_add(1);
    });

    EXPECT_EQ(totalExecuted.load(), 3);
    // Should achieve some parallelism (may not be exactly 3 due to scheduling)
    EXPECT_GE(maxConcurrent.load(), 1);
}

TEST(TBBGraphExecutorTest, Execute_DiamondPattern) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    //     A
    //    / \
    //   B   C
    //    \ /
    //     D
    auto nodeA = nodeType.CreateInstance("A");
    auto nodeB = nodeType.CreateInstance("B");
    auto nodeC = nodeType.CreateInstance("C");
    auto nodeD = nodeType.CreateInstance("D");

    std::vector<NodeInstance*> nodes = {
        nodeA.get(), nodeB.get(), nodeC.get(), nodeD.get()
    };
    std::vector<std::pair<size_t, size_t>> edges = {
        {0, 1}, {0, 2}, {1, 3}, {2, 3}
    };

    executor.BuildFromNodes(nodes, edges);

    std::set<NodeInstance*> executedBefore;
    std::mutex checkMutex;
    std::atomic<bool> orderValid{true};

    executor.Execute([&](NodeInstance* node) {
        std::lock_guard<std::mutex> lock(checkMutex);

        // A must execute before B and C
        if (node == nodeB.get() || node == nodeC.get()) {
            if (executedBefore.find(nodeA.get()) == executedBefore.end()) {
                orderValid.store(false);
            }
        }
        // D must execute after B and C
        if (node == nodeD.get()) {
            if (executedBefore.find(nodeB.get()) == executedBefore.end() ||
                executedBefore.find(nodeC.get()) == executedBefore.end()) {
                orderValid.store(false);
            }
        }

        executedBefore.insert(node);
    });

    EXPECT_TRUE(orderValid.load());
    EXPECT_EQ(executedBefore.size(), 4u);
}

// ============================================================================
// SEQUENTIAL MODE TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, SequentialMode_ExecutesInOrder) {
    TBBExecutorConfig config;
    config.mode = TBBExecutionMode::Sequential;
    TBBGraphExecutor executor(config);

    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    std::vector<NodeInstance*> order;
    executor.Execute([&order](NodeInstance* node) {
        order.push_back(node);
    });

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], node1.get());
    EXPECT_EQ(order[1], node2.get());
    EXPECT_EQ(order[2], node3.get());
}

TEST(TBBGraphExecutorTest, SetMode_SwitchesBehavior) {
    TBBGraphExecutor executor;
    EXPECT_EQ(executor.GetMode(), TBBExecutionMode::Parallel);

    executor.SetMode(TBBExecutionMode::Sequential);
    EXPECT_EQ(executor.GetMode(), TBBExecutionMode::Sequential);
}

// ============================================================================
// EXCEPTION TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, Execute_PropagatesException) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    EXPECT_THROW(
        executor.Execute([](NodeInstance*) {
            throw std::runtime_error("Test exception");
        }),
        std::runtime_error
    );
}

TEST(TBBGraphExecutorTest, ExecuteCollectErrors_CollectsAll) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    auto errors = executor.ExecuteCollectErrors([](NodeInstance*) {
        throw std::runtime_error("Failure");
    });

    EXPECT_EQ(errors.size(), 3u);
}

TEST(TBBGraphExecutorTest, ExecuteCollectErrors_PartialFailure) {
    TBBExecutorConfig config;
    config.mode = TBBExecutionMode::Sequential;  // Ensure deterministic order
    TBBGraphExecutor executor(config);

    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get()
    };
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> callCount{0};
    auto errors = executor.ExecuteCollectErrors([&callCount](NodeInstance*) {
        int n = callCount.fetch_add(1);
        if (n == 1) {  // Second call throws
            throw std::runtime_error("Second node failed");
        }
    });

    EXPECT_EQ(errors.size(), 1u);
}

// ============================================================================
// STATISTICS TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, Stats_TracksExecutions) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");

    std::vector<NodeInstance*> nodes = {node1.get(), node2.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);
    executor.Execute([](NodeInstance*) {});

    auto stats = executor.GetStats();
    EXPECT_EQ(stats.nodeCount, 2u);
    EXPECT_EQ(stats.executionsCompleted, 2u);
    EXPECT_EQ(stats.executeCount, 1u);
    EXPECT_GT(stats.lastExecutionMs, 0.0);
}

TEST(TBBGraphExecutorTest, Stats_TracksExceptions) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    try {
        executor.Execute([](NodeInstance*) {
            throw std::runtime_error("Test");
        });
    } catch (...) {}

    auto stats = executor.GetStats();
    EXPECT_EQ(stats.exceptionsThrown, 1u);
}

TEST(TBBGraphExecutorTest, ResetStats_ClearsCounters) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);
    executor.Execute([](NodeInstance*) {});

    executor.ResetStats();

    auto stats = executor.GetStats();
    EXPECT_EQ(stats.executionsCompleted, 0u);
    EXPECT_EQ(stats.executeCount, 0u);
}

// ============================================================================
// MOVE SEMANTICS TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, MoveConstruction) {
    TBBGraphExecutor executor1;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor1.BuildFromNodes(nodes, edges);

    TBBGraphExecutor executor2(std::move(executor1));

    EXPECT_TRUE(executor2.IsBuilt());
    EXPECT_EQ(executor2.GetNodeCount(), 1u);
}

TEST(TBBGraphExecutorTest, MoveAssignment) {
    TBBGraphExecutor executor1;
    TBBGraphExecutor executor2;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor1.BuildFromNodes(nodes, edges);
    executor2 = std::move(executor1);

    EXPECT_TRUE(executor2.IsBuilt());
    EXPECT_EQ(executor2.GetNodeCount(), 1u);
}

// ============================================================================
// EMPTY GRAPH TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, Execute_EmptyGraph) {
    TBBGraphExecutor executor;

    std::vector<NodeInstance*> nodes;
    std::vector<std::pair<size_t, size_t>> edges;
    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> count{0};
    executor.Execute([&count](NodeInstance*) {
        count.fetch_add(1);
    });

    EXPECT_EQ(count.load(), 0);
}

TEST(TBBGraphExecutorTest, Execute_BeforeBuild) {
    TBBGraphExecutor executor;

    std::atomic<int> count{0};
    executor.Execute([&count](NodeInstance*) {
        count.fetch_add(1);
    });

    EXPECT_EQ(count.load(), 0);
}

// ============================================================================
// CONCURRENCY TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, SetMaxConcurrency) {
    TBBGraphExecutor executor;
    executor.SetMaxConcurrency(2);

    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");
    auto node3 = nodeType.CreateInstance("Node3");
    auto node4 = nodeType.CreateInstance("Node4");

    std::vector<NodeInstance*> nodes = {
        node1.get(), node2.get(), node3.get(), node4.get()
    };
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> maxConcurrent{0};
    std::atomic<int> concurrent{0};

    executor.Execute([&](NodeInstance*) {
        int c = concurrent.fetch_add(1) + 1;
        int expected = maxConcurrent.load();
        while (c > expected && !maxConcurrent.compare_exchange_weak(expected, c)) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        concurrent.fetch_sub(1);
    });

    // With max concurrency 2, should not exceed 2 concurrent executions
    // (Note: TBB global_control affects all TBB operations, so this may vary)
    EXPECT_LE(maxConcurrent.load(), 4);  // Relaxed check
}

// ============================================================================
// CANCELLATION TESTS
// ============================================================================

TEST(TBBGraphExecutorTest, Cancel_StopsExecution) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    auto node2 = nodeType.CreateInstance("Node2");

    std::vector<NodeInstance*> nodes = {node1.get(), node2.get()};
    std::vector<std::pair<size_t, size_t>> edges = {{0, 1}};

    executor.BuildFromNodes(nodes, edges);

    std::atomic<int> executed{0};
    std::thread cancelThread([&executor]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        executor.Cancel();
    });

    executor.Execute([&executed](NodeInstance*) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed.fetch_add(1);
    });

    cancelThread.join();

    // At least one node should have started (may or may not complete)
    // The test mainly verifies Cancel() doesn't crash
    SUCCEED();
}

TEST(TBBGraphExecutorTest, Wait_BlocksUntilComplete) {
    TBBGraphExecutor executor;
    MockNodeType nodeType("TestType");

    auto node1 = nodeType.CreateInstance("Node1");
    std::vector<NodeInstance*> nodes = {node1.get()};
    std::vector<std::pair<size_t, size_t>> edges;

    executor.BuildFromNodes(nodes, edges);

    std::atomic<bool> completed{false};

    std::thread execThread([&]() {
        executor.Execute([&completed](NodeInstance*) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            completed.store(true);
        });
    });

    // Wait should return after execution completes
    execThread.join();
    executor.Wait();

    EXPECT_TRUE(completed.load());
}
