// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_tbb_virtual_task_executor.cpp
 * @brief Unit tests for TBBVirtualTaskExecutor
 *
 * Sprint 6.5 Phase 4: TBB-based virtual task execution
 * Tests parallel execution, phase handling, and error recovery.
 */

#include <gtest/gtest.h>
#include "Core/TBBVirtualTaskExecutor.h"
#include "Core/VirtualResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

using namespace Vixen::RenderGraph;

// ============================================================================
// Mock Classes
// ============================================================================

class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& name)
        : NodeType(name) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<MockNodeType*>(this));
    }
};

class MockResource : public Resource {
public:
    explicit MockResource(const std::string& name) : name_(name) {}
    const std::string& GetName() const { return name_; }
private:
    std::string name_;
};

// Node that supports task parallelism (returns multiple tasks)
class ParallelAwareNode : public NodeInstance {
public:
    ParallelAwareNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    // Return N tasks for parallel execution
    std::vector<VirtualTask> GetExecutionTasks(VirtualTaskPhase phase) override {
        if (phase != VirtualTaskPhase::Execute) {
            return NodeInstance::GetExecutionTasks(phase);
        }

        // Return one task per bundle
        std::vector<VirtualTask> tasks;
        uint32_t count = GetVirtualTaskCount();

        for (uint32_t i = 0; i < count; ++i) {
            VirtualTask task;
            task.id = {this, i};
            task.execute = [this, i]() {
                ++executionCount;
                lastExecutedTask = i;
            };
            tasks.push_back(std::move(task));
        }

        return tasks;
    }

    std::atomic<int> executionCount{0};
    std::atomic<uint32_t> lastExecutedTask{UINT32_MAX};
};

// ============================================================================
// Test Fixture
// ============================================================================

class TBBVirtualTaskExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeA_ = std::make_unique<MockNodeType>("TypeA");
        typeB_ = std::make_unique<MockNodeType>("TypeB");
        typeC_ = std::make_unique<MockNodeType>("TypeC");

        nodeA_ = typeA_->CreateInstance("NodeA");
        nodeB_ = typeB_->CreateInstance("NodeB");
        nodeC_ = typeC_->CreateInstance("NodeC");

        resX_ = std::make_unique<MockResource>("ResourceX");
        resY_ = std::make_unique<MockResource>("ResourceY");
        resZ_ = std::make_unique<MockResource>("ResourceZ");
    }

    void SetupBundle(NodeInstance* node, std::vector<Resource*> inputs, std::vector<Resource*> outputs) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            node->SetInput(static_cast<uint32_t>(i), 0, inputs[i]);
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            node->SetOutput(static_cast<uint32_t>(i), 0, outputs[i]);
        }
    }

    std::unique_ptr<MockNodeType> typeA_, typeB_, typeC_;
    std::unique_ptr<NodeInstance> nodeA_, nodeB_, nodeC_;
    std::unique_ptr<MockResource> resX_, resY_, resZ_;

    VirtualResourceAccessTracker tracker_;
    TBBVirtualTaskExecutor executor_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, Construction_NotBuilt) {
    EXPECT_FALSE(executor_.IsBuilt());
    EXPECT_TRUE(executor_.IsEnabled());
}

TEST_F(TBBVirtualTaskExecutorTest, Clear_ResetsState) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    EXPECT_TRUE(executor_.IsBuilt());

    executor_.Clear();

    EXPECT_FALSE(executor_.IsBuilt());
}

TEST_F(TBBVirtualTaskExecutorTest, SetEnabled_TogglesExecution) {
    executor_.SetEnabled(false);
    EXPECT_FALSE(executor_.IsEnabled());

    executor_.SetEnabled(true);
    EXPECT_TRUE(executor_.IsEnabled());
}

// ============================================================================
// Build Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, Build_SingleNode) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    EXPECT_TRUE(executor_.IsBuilt());

    const auto& stats = executor_.GetStats();
    EXPECT_EQ(stats.totalNodes, 1);
    EXPECT_GE(stats.totalTasks, 1);
}

TEST_F(TBBVirtualTaskExecutorTest, Build_MultipleNodes) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    SetupBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    executor_.Build(tracker_, order);

    EXPECT_TRUE(executor_.IsBuilt());

    const auto& stats = executor_.GetStats();
    EXPECT_EQ(stats.totalNodes, 3);
}

TEST_F(TBBVirtualTaskExecutorTest, Build_CapturesStatistics) {
    // Diamond pattern: A → B, A → C, B → D, C → D
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    SetupBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupBundle(nodeC_.get(), {resX_.get()}, {resZ_.get()});

    auto typeD = std::make_unique<MockNodeType>("TypeD");
    auto nodeD = typeD->CreateInstance("NodeD");
    nodeD->SetInput(0, 0, resY_.get());
    nodeD->SetInput(1, 0, resZ_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());
    tracker_.AddNode(nodeD.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get(), nodeD.get()};
    executor_.Build(tracker_, order);

    const auto& stats = executor_.GetStats();
    EXPECT_EQ(stats.totalNodes, 4);
    EXPECT_GT(stats.buildTimeMs, 0.0);
    EXPECT_GE(stats.maxParallelLevel, 2);  // B and C can run in parallel
}

// ============================================================================
// Execution Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, ExecutePhase_WhenNotBuilt_ReturnsFalse) {
    EXPECT_FALSE(executor_.ExecutePhase(VirtualTaskPhase::Execute));
}

TEST_F(TBBVirtualTaskExecutorTest, ExecutePhase_WhenDisabled_ReturnsFalse) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    executor_.SetEnabled(false);

    EXPECT_FALSE(executor_.ExecutePhase(VirtualTaskPhase::Execute));
}

TEST_F(TBBVirtualTaskExecutorTest, ExecutePhase_SingleNode_Succeeds) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    // Execute phase should succeed (default implementation)
    EXPECT_TRUE(executor_.ExecutePhase(VirtualTaskPhase::Execute));
    EXPECT_FALSE(executor_.HasErrors());
}

TEST_F(TBBVirtualTaskExecutorTest, ExecuteAllPhases_Succeeds) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    EXPECT_TRUE(executor_.ExecuteAllPhases());
    EXPECT_FALSE(executor_.HasErrors());
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, Stats_TracksBuildTime) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    const auto& stats = executor_.GetStats();
    EXPECT_GT(stats.buildTimeMs, 0.0);
}

TEST_F(TBBVirtualTaskExecutorTest, Stats_TracksExecutionTime) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    executor_.ResetStats();
    executor_.ExecutePhase(VirtualTaskPhase::Execute);

    const auto& stats = executor_.GetStats();
    EXPECT_GE(stats.executionTimeMs, 0.0);
}

TEST_F(TBBVirtualTaskExecutorTest, ResetStats_ClearsAllCounters) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);
    executor_.ExecutePhase(VirtualTaskPhase::Execute);

    executor_.ResetStats();

    const auto& stats = executor_.GetStats();
    EXPECT_EQ(stats.totalTasks, 0);
    EXPECT_EQ(stats.totalNodes, 0);
}

// ============================================================================
// Dependency Graph Access Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, GetDependencyGraph_ReturnsValidGraph) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    SetupBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    executor_.Build(tracker_, order);

    const auto& depGraph = executor_.GetDependencyGraph();
    EXPECT_EQ(depGraph.GetTaskCount(), 2);
    EXPECT_GE(depGraph.GetEdgeCount(), 1);  // A → B
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, ErrorHandling_NoErrorsInitially) {
    EXPECT_FALSE(executor_.HasErrors());
    EXPECT_TRUE(executor_.GetErrors().empty());
}

TEST_F(TBBVirtualTaskExecutorTest, ClearErrors_RemovesAllErrors) {
    // Build something first
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    executor_.ClearErrors();
    EXPECT_FALSE(executor_.HasErrors());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, EdgeCase_EmptyNodeList) {
    std::vector<NodeInstance*> emptyOrder;
    executor_.Build(tracker_, emptyOrder);

    EXPECT_TRUE(executor_.IsBuilt());
    EXPECT_EQ(executor_.GetStats().totalNodes, 0);
}

TEST_F(TBBVirtualTaskExecutorTest, EdgeCase_NullNodeInList) {
    std::vector<NodeInstance*> order = {nullptr, nodeA_.get()};

    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    executor_.Build(tracker_, order);

    // Should handle null gracefully
    EXPECT_TRUE(executor_.IsBuilt());
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_F(TBBVirtualTaskExecutorTest, MoveConstructor_TransfersState) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    TBBVirtualTaskExecutor moved(std::move(executor_));

    EXPECT_TRUE(moved.IsBuilt());
    EXPECT_FALSE(executor_.IsBuilt());  // NOLINT - testing moved-from state
}

TEST_F(TBBVirtualTaskExecutorTest, MoveAssignment_TransfersState) {
    SetupBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    executor_.Build(tracker_, order);

    TBBVirtualTaskExecutor other;
    other = std::move(executor_);

    EXPECT_TRUE(other.IsBuilt());
    EXPECT_FALSE(executor_.IsBuilt());  // NOLINT - testing moved-from state
}
