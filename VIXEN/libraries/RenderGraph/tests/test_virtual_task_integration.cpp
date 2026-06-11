// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_virtual_task_integration.cpp
 * @brief Integration tests for virtual task parallelism
 *
 * Sprint 6.5 Phase 6: End-to-end verification of task-level parallelism
 * Tests the unified GetExecutionTasks() API pattern.
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
// Test Node Types - Demonstrate Unified GetExecutionTasks() Pattern
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
 * @brief Node that returns N tasks (parallel execution)
 *
 * This demonstrates the pattern for parallel nodes:
 * Override GetExecutionTasks() to return multiple tasks.
 */
class ParallelTestNode : public NodeInstance {
public:
    ParallelTestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    // =========================================================================
    // Unified API: Return N tasks for parallel execution
    // =========================================================================

    std::vector<VirtualTask> GetExecutionTasks(VirtualTaskPhase phase) override {
        if (phase != VirtualTaskPhase::Execute) {
            return NodeInstance::GetExecutionTasks(phase);
        }

        // Return one task per bundle (parallel execution)
        std::vector<VirtualTask> tasks;
        uint32_t count = GetVirtualTaskCount();

        for (uint32_t i = 0; i < count; ++i) {
            VirtualTask task;
            task.id = {this, i};
            task.execute = [this, i]() {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                ++executionCount_;
                executedTasks_.fetch_or(1u << i);
            };
            // Cost comes from profiles (GetPhaseProfiles)
            task.profiles = GetPhaseProfiles(phase);
            tasks.push_back(std::move(task));
        }

        return tasks;
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

    // Check if this is a parallel node (returns >1 task)
    bool IsParallel() const {
        auto tasks = const_cast<ParallelTestNode*>(this)->GetExecutionTasks(VirtualTaskPhase::Execute);
        return tasks.size() > 1;
    }

private:
    std::atomic<int> executionCount_{0};
    std::atomic<uint32_t> executedTasks_{0};
};

/**
 * @brief Node that returns 1 task (sequential execution)
 *
 * Uses default GetExecutionTasks() - returns single task.
 */
class SequentialTestNode : public NodeInstance {
public:
    SequentialTestNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    // Uses default GetExecutionTasks() - returns 1 task

    int GetExecutionCount() const { return executionCount_.load(); }
    void ResetCounters() { executionCount_ = 0; }

    // Check if this is a parallel node (returns >1 task)
    bool IsParallel() const {
        auto tasks = const_cast<SequentialTestNode*>(this)->GetExecutionTasks(VirtualTaskPhase::Execute);
        return tasks.size() > 1;
    }

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
// GetExecutionTasks API Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, API_ParallelNodeReturnsMultipleTasks) {
    auto node = CreateParallelNode("ParallelNode");
    node->SetOutput(0, 0, resA_.get());  // Bundle 0
    node->SetOutput(0, 1, resB_.get());  // Bundle 1

    auto tasks = node->GetExecutionTasks(VirtualTaskPhase::Execute);
    EXPECT_EQ(tasks.size(), 2);  // 2 bundles = 2 tasks
    EXPECT_TRUE(node->IsParallel());
}

TEST_F(VirtualTaskIntegrationTest, API_SequentialNodeReturnsSingleTask) {
    auto node = CreateSequentialNode("SequentialNode");
    node->SetOutput(0, 0, resA_.get());  // Bundle 0
    node->SetOutput(0, 1, resB_.get());  // Bundle 1

    auto tasks = node->GetExecutionTasks(VirtualTaskPhase::Execute);
    EXPECT_EQ(tasks.size(), 1);  // Default: 1 task regardless of bundles
    EXPECT_FALSE(node->IsParallel());
}

TEST_F(VirtualTaskIntegrationTest, API_DefaultNodeReturnsSingleTask) {
    auto node = nodeType_->CreateInstance("DefaultNode");
    auto tasks = node->GetExecutionTasks(VirtualTaskPhase::Execute);
    EXPECT_EQ(tasks.size(), 1);  // Default implementation returns 1 task
}

// ============================================================================
// Task Execution Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, Execute_ParallelNodeTasksRunnable) {
    auto node = CreateParallelNode("TestNode");
    node->SetOutput(0, 0, resA_.get());
    node->SetOutput(0, 1, resB_.get());

    auto tasks = node->GetExecutionTasks(VirtualTaskPhase::Execute);
    ASSERT_EQ(tasks.size(), 2);

    // Execute both tasks
    for (auto& task : tasks) {
        ASSERT_TRUE(task.execute);
        task.execute();
    }

    EXPECT_EQ(node->GetExecutionCount(), 2);
    EXPECT_EQ(node->GetExecutedTasksMask(), 0b11u);  // Both bits set
}

TEST_F(VirtualTaskIntegrationTest, Execute_EstimateCost) {
    auto node = CreateParallelNode("TestNode");
    node->SetOutput(0, 0, resA_.get());

    auto tasks = node->GetExecutionTasks(VirtualTaskPhase::Execute);
    ASSERT_EQ(tasks.size(), 1);
    // Cost from profiles (0 if no profiles attached)
    EXPECT_EQ(tasks[0].GetEstimatedCostFromProfiles(), 0);
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

    // Execute
    bool success = executor.ExecutePhase(VirtualTaskPhase::Execute);
    EXPECT_TRUE(success);
    EXPECT_FALSE(executor.HasErrors());
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST_F(VirtualTaskIntegrationTest, BackwardCompat_SequentialNodeWorks) {
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

    // Parallel node returns >1 task, sequential returns 1
    // Stats count "opted-in" as nodes returning >1 task
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
    EXPECT_GT(stats.buildTimeMs, 0.0);
    EXPECT_GE(stats.maxParallelLevel, 1);
}
