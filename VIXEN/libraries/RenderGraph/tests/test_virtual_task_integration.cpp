// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_virtual_task_integration.cpp
 * @brief Integration tests for virtual task parallelism
 *
 * Sprint 6.5 Phase 6: End-to-end verification of task-level parallelism
 * Tests the complete flow from NodeInstance opt-in through execution.
 */

#include <gtest/gtest.h>
#include "Core/VirtualTask.h"
#include "Core/VirtualResourceAccessTracker.h"
#include "Core/TaskDependencyGraph.h"
#include "Core/TBBVirtualTaskExecutor.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <memory>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>

using namespace Vixen::RenderGraph;

// ============================================================================
// Test Node Types - Demonstrate Opt-In Pattern
// ============================================================================

/**
 * @brief Mock NodeType for creating test instances
 */
class TestNodeType : public NodeType {
public:
    explicit TestNodeType(const std::string& name)
        : NodeType(name) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Node that opts-in to task parallelism
 *
 * This demonstrates the pattern for nodes to support virtual task parallelism:
 * 1. Override SupportsTaskParallelism() to return true
 * 2. Override GetTaskParallelismMode() to specify parallelism behavior
 * 3. Override CreateVirtualTask() to provide per-task execution
 * 4. Optionally override EstimateTaskCost() for budget-aware scheduling
 */
class ParallelTestNode : public NodeInstance {
public:
    ParallelTestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    // =========================================================================
    // Opt-In API Implementation
    // =========================================================================

    bool SupportsTaskParallelism() const override {
        return true;
    }

    TaskParallelismMode GetTaskParallelismMode() const override {
        return TaskParallelismMode::Parallel;
    }

    std::function<void()> CreateVirtualTask(
        uint32_t taskIndex,
        NodeLifecyclePhase phase
    ) override {
        // Only provide tasks for Execute phase
        if (phase != NodeLifecyclePhase::PreExecute &&
            phase != NodeLifecyclePhase::PostExecute) {
            // For other phases, use default (task 0 runs whole phase)
            if (taskIndex == 0) {
                return NodeInstance::CreateVirtualTask(taskIndex, phase);
            }
            return {};
        }

        // Return a task that increments counter and records execution
        return [this, taskIndex]() {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            ++executionCount_;
            executedTasks_.fetch_or(1u << taskIndex);
        };
    }

    uint64_t EstimateTaskCost(uint32_t /*taskIndex*/) const override {
        return 100000;  // 100 microseconds
    }

    // =========================================================================
    // Test Helpers
    // =========================================================================

    int GetExecutionCount() const { return executionCount_.load(); }
    uint32_t GetExecutedTasksMask() const { return executedTasks_.load(); }
    void ResetCounters() {
        executionCount_ = 0;
        executedTasks_ = 0;
    }

private:
    std::atomic<int> executionCount_{0};
    std::atomic<uint32_t> executedTasks_{0};
};

/**
 * @brief Node that does NOT opt-in (backward compatibility test)
 */
class SequentialTestNode : public NodeInstance {
public:
    SequentialTestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    // Does NOT override SupportsTaskParallelism - defaults to false

    int GetExecutionCount() const { return executionCount_.load(); }
    void ResetCounters() { executionCount_ = 0; }

private:
    std::atomic<int> executionCount_{0};
};

// NodeType factory implementation
std::unique_ptr<NodeInstance> TestNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<NodeInstance>(instanceName, const_cast<TestNodeType*>(this));
}

/**
 * @brief Mock Resource for testing
 */
class TestResource : public Resource {
public:
    explicit TestResource(const std::string& name) : name_(name) {}
    const std::string& GetName() const { return name_; }
private:
    std::string name_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class VirtualTaskIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeType_ = std::make_unique<TestNodeType>("TestType");
        resA_ = std::make_unique<TestResource>("ResourceA");
        resB_ = std::make_unique<TestResource>("ResourceB");
        resC_ = std::make_unique<TestResource>("ResourceC");
    }

    // Helper to create a parallel-enabled node
    std::unique_ptr<ParallelTestNode> CreateParallelNode(const std::string& name) {
        return std::make_unique<ParallelTestNode>(name, nodeType_.get());
    }

    // Helper to create a sequential node
    std::unique_ptr<SequentialTestNode> CreateSequentialNode(const std::string& name) {
        return std::make_unique<SequentialTestNode>(name, nodeType_.get());
    }

    std::unique_ptr<TestNodeType> nodeType_;
    std::unique_ptr<TestResource> resA_, resB_, resC_;
};

// ============================================================================
// Opt-In Pattern Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, OptIn_ParallelNodeReturnsTrue) {
    auto node = CreateParallelNode("ParallelNode");
    EXPECT_TRUE(node->SupportsTaskParallelism());
    EXPECT_EQ(node->GetTaskParallelismMode(), NodeInstance::TaskParallelismMode::Parallel);
}

TEST_F(VirtualTaskIntegrationTest, OptIn_SequentialNodeReturnsFalse) {
    auto node = CreateSequentialNode("SequentialNode");
    EXPECT_FALSE(node->SupportsTaskParallelism());
}

TEST_F(VirtualTaskIntegrationTest, OptIn_DefaultNodeReturnsFalse) {
    auto node = nodeType_->CreateInstance("DefaultNode");
    EXPECT_FALSE(node->SupportsTaskParallelism());
}

// ============================================================================
// CreateVirtualTask Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, CreateVirtualTask_ParallelNodeProvidesTask) {
    auto node = CreateParallelNode("TestNode");

    auto task = node->CreateVirtualTask(0, NodeLifecyclePhase::PreExecute);
    ASSERT_TRUE(task);  // Should return valid function

    task();  // Execute the task

    EXPECT_EQ(node->GetExecutionCount(), 1);
    EXPECT_EQ(node->GetExecutedTasksMask(), 1u);  // Bit 0 set
}

TEST_F(VirtualTaskIntegrationTest, CreateVirtualTask_MultipleTasks) {
    auto node = CreateParallelNode("TestNode");

    // Create 3 tasks
    auto task0 = node->CreateVirtualTask(0, NodeLifecyclePhase::PreExecute);
    auto task1 = node->CreateVirtualTask(1, NodeLifecyclePhase::PreExecute);
    auto task2 = node->CreateVirtualTask(2, NodeLifecyclePhase::PreExecute);

    ASSERT_TRUE(task0);
    ASSERT_TRUE(task1);
    ASSERT_TRUE(task2);

    // Execute all
    task0();
    task1();
    task2();

    EXPECT_EQ(node->GetExecutionCount(), 3);
    EXPECT_EQ(node->GetExecutedTasksMask(), 0b111u);  // All 3 bits set
}

TEST_F(VirtualTaskIntegrationTest, CreateVirtualTask_EstimateCost) {
    auto node = CreateParallelNode("TestNode");
    EXPECT_EQ(node->EstimateTaskCost(0), 100000);  // 100 microseconds
}

// ============================================================================
// End-to-End Pipeline Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, Pipeline_TrackerBuildsTasks) {
    auto nodeA = CreateParallelNode("NodeA");
    auto nodeB = CreateParallelNode("NodeB");

    // Set up bundles with resources
    nodeA->SetOutput(0, 0, resA_.get());
    nodeA->SetOutput(0, 1, resB_.get());  // 2 bundles

    nodeB->SetInput(0, 0, resA_.get());
    nodeB->SetInput(0, 1, resC_.get());  // 2 bundles

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(nodeA.get());
    tracker.AddNode(nodeB.get());

    // Should have 2 tasks per node = 4 total
    EXPECT_EQ(tracker.GetNodeTaskCount(nodeA.get()), 2);
    EXPECT_EQ(tracker.GetNodeTaskCount(nodeB.get()), 2);
    EXPECT_EQ(tracker.GetTaskCount(), 4);
}

TEST_F(VirtualTaskIntegrationTest, Pipeline_DependencyGraphBuilds) {
    auto nodeA = CreateParallelNode("NodeA");
    auto nodeB = CreateParallelNode("NodeB");

    // A:0 writes resA, B:0 reads resA -> dependency
    nodeA->SetOutput(0, 0, resA_.get());
    nodeB->SetInput(0, 0, resA_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(nodeA.get());
    tracker.AddNode(nodeB.get());

    std::vector<NodeInstance*> order = {nodeA.get(), nodeB.get()};

    TaskDependencyGraph depGraph;
    depGraph.Build(tracker, order);

    VirtualTaskId taskA{nodeA.get(), 0};
    VirtualTaskId taskB{nodeB.get(), 0};

    EXPECT_TRUE(depGraph.HasDependency(taskA, taskB));  // A:0 -> B:0
    EXPECT_FALSE(depGraph.CanParallelize(taskA, taskB));
}

TEST_F(VirtualTaskIntegrationTest, Pipeline_ExecutorExecutesTasks) {
    auto nodeA = CreateParallelNode("NodeA");
    auto nodeB = CreateParallelNode("NodeB");

    // Independent resources -> can parallelize
    nodeA->SetOutput(0, 0, resA_.get());
    nodeB->SetOutput(0, 0, resB_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(nodeA.get());
    tracker.AddNode(nodeB.get());

    std::vector<NodeInstance*> order = {nodeA.get(), nodeB.get()};

    TBBVirtualTaskExecutor executor;
    executor.Build(tracker, order);

    EXPECT_TRUE(executor.IsBuilt());
    EXPECT_EQ(executor.GetStats().totalNodes, 2);
    EXPECT_EQ(executor.GetStats().optedInNodes, 2);

    // Execute
    bool success = executor.ExecutePhase(VirtualTaskPhase::Execute);
    EXPECT_TRUE(success);
    EXPECT_FALSE(executor.HasErrors());
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, BackwardCompat_NonOptedNodeSkipped) {
    auto seqNode = CreateSequentialNode("SequentialNode");
    auto parNode = CreateParallelNode("ParallelNode");

    seqNode->SetOutput(0, 0, resA_.get());
    parNode->SetOutput(0, 0, resB_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(seqNode.get());
    tracker.AddNode(parNode.get());

    std::vector<NodeInstance*> order = {seqNode.get(), parNode.get()};

    TBBVirtualTaskExecutor executor;
    executor.Build(tracker, order);

    // Only parallel node counts as opted-in
    EXPECT_EQ(executor.GetStats().optedInNodes, 1);
    EXPECT_EQ(executor.GetStats().totalNodes, 2);
}

TEST_F(VirtualTaskIntegrationTest, BackwardCompat_MixedNodeGraph) {
    auto seqNode = CreateSequentialNode("SequentialNode");
    auto parNode1 = CreateParallelNode("ParallelNode1");
    auto parNode2 = CreateParallelNode("ParallelNode2");

    // Chain: seq -> par1 -> par2
    seqNode->SetOutput(0, 0, resA_.get());
    parNode1->SetInput(0, 0, resA_.get());
    parNode1->SetOutput(0, 0, resB_.get());
    parNode2->SetInput(0, 0, resB_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(seqNode.get());
    tracker.AddNode(parNode1.get());
    tracker.AddNode(parNode2.get());

    std::vector<NodeInstance*> order = {seqNode.get(), parNode1.get(), parNode2.get()};

    TBBVirtualTaskExecutor executor;
    executor.Build(tracker, order);

    EXPECT_EQ(executor.GetStats().optedInNodes, 2);  // 2 parallel nodes

    bool success = executor.ExecutePhase(VirtualTaskPhase::Execute);
    EXPECT_TRUE(success);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, Stats_ParallelismMetrics) {
    auto nodeA = CreateParallelNode("NodeA");
    auto nodeB = CreateParallelNode("NodeB");
    auto nodeC = CreateParallelNode("NodeC");

    // Independent resources -> all can parallelize
    nodeA->SetOutput(0, 0, resA_.get());
    nodeB->SetOutput(0, 0, resB_.get());
    nodeC->SetOutput(0, 0, resC_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(nodeA.get());
    tracker.AddNode(nodeB.get());
    tracker.AddNode(nodeC.get());

    std::vector<NodeInstance*> order = {nodeA.get(), nodeB.get(), nodeC.get()};

    TBBVirtualTaskExecutor executor;
    executor.Build(tracker, order);

    const auto& stats = executor.GetStats();
    EXPECT_EQ(stats.totalNodes, 3);
    EXPECT_EQ(stats.optedInNodes, 3);
    EXPECT_GT(stats.buildTimeMs, 0.0);
    EXPECT_GE(stats.maxParallelLevel, 1);
}

TEST_F(VirtualTaskIntegrationTest, Stats_ParallelismRatio) {
    auto nodeA = CreateParallelNode("NodeA");
    auto nodeB = CreateSequentialNode("NodeB");

    nodeA->SetOutput(0, 0, resA_.get());
    nodeB->SetOutput(0, 0, resB_.get());

    VirtualResourceAccessTracker tracker;
    tracker.AddNode(nodeA.get());
    tracker.AddNode(nodeB.get());

    std::vector<NodeInstance*> order = {nodeA.get(), nodeB.get()};

    TBBVirtualTaskExecutor executor;
    executor.Build(tracker, order);

    const auto& stats = executor.GetStats();
    EXPECT_FLOAT_EQ(stats.GetOptInRatio(), 0.5f);  // 1 of 2 nodes opted in
}
