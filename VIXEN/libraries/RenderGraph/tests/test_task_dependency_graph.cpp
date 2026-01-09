// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_task_dependency_graph.cpp
 * @brief Unit tests for TaskDependencyGraph
 *
 * Sprint 6.5 Phase 2: Task-level dependency resolution
 * Tests topological sorting, parallel level computation, and dependency queries.
 */

#include <gtest/gtest.h>
#include "Core/TaskDependencyGraph.h"
#include "Core/VirtualResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <memory>
#include <algorithm>

using namespace Vixen::RenderGraph;

// ============================================================================
// Mock Classes for Testing
// ============================================================================

class TestNodeType : public NodeType {
public:
    explicit TestNodeType(const std::string& name)
        : NodeType(name) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<TestNodeType*>(this));
    }
};

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

class TaskDependencyGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeTypeA_ = std::make_unique<TestNodeType>("TypeA");
        nodeTypeB_ = std::make_unique<TestNodeType>("TypeB");
        nodeTypeC_ = std::make_unique<TestNodeType>("TypeC");
        nodeTypeD_ = std::make_unique<TestNodeType>("TypeD");

        nodeA_ = nodeTypeA_->CreateInstance("NodeA");
        nodeB_ = nodeTypeB_->CreateInstance("NodeB");
        nodeC_ = nodeTypeC_->CreateInstance("NodeC");
        nodeD_ = nodeTypeD_->CreateInstance("NodeD");

        resX_ = std::make_unique<TestResource>("ResourceX");
        resY_ = std::make_unique<TestResource>("ResourceY");
        resZ_ = std::make_unique<TestResource>("ResourceZ");
    }

    void SetupSingleBundle(NodeInstance* node, std::vector<Resource*> inputs, std::vector<Resource*> outputs) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            node->SetInput(static_cast<uint32_t>(i), 0, inputs[i]);
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            node->SetOutput(static_cast<uint32_t>(i), 0, outputs[i]);
        }
    }

    std::unique_ptr<TestNodeType> nodeTypeA_, nodeTypeB_, nodeTypeC_, nodeTypeD_;
    std::unique_ptr<NodeInstance> nodeA_, nodeB_, nodeC_, nodeD_;
    std::unique_ptr<TestResource> resX_, resY_, resZ_;

    VirtualResourceAccessTracker tracker_;
    TaskDependencyGraph graph_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, Construction_EmptyGraph) {
    EXPECT_EQ(graph_.GetTaskCount(), 0);
    EXPECT_EQ(graph_.GetEdgeCount(), 0);
}

TEST_F(TaskDependencyGraphTest, Clear_ResetsAllData) {
    // Build a simple graph
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    EXPECT_GT(graph_.GetTaskCount(), 0);

    graph_.Clear();

    EXPECT_EQ(graph_.GetTaskCount(), 0);
    EXPECT_EQ(graph_.GetEdgeCount(), 0);
}

// ============================================================================
// Dependency Building Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, Build_NoConflicts_NoDependencies) {
    // Independent tasks: A writes X, B writes Y
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    EXPECT_EQ(graph_.GetTaskCount(), 2);
    EXPECT_EQ(graph_.GetEdgeCount(), 0);  // No conflicts = no dependencies
}

TEST_F(TaskDependencyGraphTest, Build_WriteRead_CreatesDependency) {
    // A writes X, B reads X → A must complete before B
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_EQ(graph_.GetEdgeCount(), 1);
    EXPECT_TRUE(graph_.HasDependency(taskA, taskB));  // A → B
    EXPECT_FALSE(graph_.HasDependency(taskB, taskA));
}

TEST_F(TaskDependencyGraphTest, Build_WriteWrite_CreatesDependency) {
    // Both write to X → earlier (by execution order) must complete first
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_EQ(graph_.GetEdgeCount(), 1);
    EXPECT_TRUE(graph_.HasDependency(taskA, taskB));
}

TEST_F(TaskDependencyGraphTest, Build_Chain_A_B_C) {
    // A writes X, B reads X writes Y, C reads Y
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};
    VirtualTaskId taskC{nodeC_.get(), 0};

    EXPECT_TRUE(graph_.HasDependency(taskA, taskB));
    EXPECT_TRUE(graph_.HasDependency(taskB, taskC));
    EXPECT_FALSE(graph_.HasDependency(taskA, taskC));  // No direct edge A→C
}

// ============================================================================
// Dependency Query Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, GetDependencies_ReturnsPrerequisites) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    auto depsA = graph_.GetDependencies(taskA);
    auto depsB = graph_.GetDependencies(taskB);

    EXPECT_TRUE(depsA.empty());  // A has no dependencies
    EXPECT_EQ(depsB.size(), 1);  // B depends on A
    EXPECT_EQ(depsB[0], taskA);
}

TEST_F(TaskDependencyGraphTest, GetDependents_ReturnsSuccessors) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeC_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};

    auto dependents = graph_.GetDependents(taskA);

    EXPECT_EQ(dependents.size(), 2);  // Both B and C depend on A
}

TEST_F(TaskDependencyGraphTest, CanParallelize_Independent) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_TRUE(graph_.CanParallelize(taskA, taskB));
}

TEST_F(TaskDependencyGraphTest, CanParallelize_Dependent) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_FALSE(graph_.CanParallelize(taskA, taskB));
}

// ============================================================================
// Topological Sort Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, TopologicalSort_ValidOrder) {
    // A → B → C chain
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto sorted = graph_.TopologicalSort();

    // Find positions
    auto posA = std::find(sorted.begin(), sorted.end(), VirtualTaskId{nodeA_.get(), 0});
    auto posB = std::find(sorted.begin(), sorted.end(), VirtualTaskId{nodeB_.get(), 0});
    auto posC = std::find(sorted.begin(), sorted.end(), VirtualTaskId{nodeC_.get(), 0});

    EXPECT_NE(posA, sorted.end());
    EXPECT_NE(posB, sorted.end());
    EXPECT_NE(posC, sorted.end());
    EXPECT_LT(posA, posB);  // A before B
    EXPECT_LT(posB, posC);  // B before C
}

TEST_F(TaskDependencyGraphTest, TopologicalSort_ContainsAllTasks) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {}, {resZ_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto sorted = graph_.TopologicalSort();

    EXPECT_EQ(sorted.size(), 3);
}

// ============================================================================
// Ready Tasks Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, GetReadyTasks_AllIndependent) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {}, {resZ_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto ready = graph_.GetReadyTasks();

    EXPECT_EQ(ready.size(), 3);  // All can start immediately
}

TEST_F(TaskDependencyGraphTest, GetReadyTasks_WithDependencies) {
    // A → B, C independent
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeC_.get(), {}, {resY_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto ready = graph_.GetReadyTasks();

    EXPECT_EQ(ready.size(), 2);  // A and C can start (B waits for A)
}

// ============================================================================
// Parallel Levels Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, GetParallelLevels_AllParallel) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {}, {resZ_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto levels = graph_.GetParallelLevels();

    EXPECT_EQ(levels.size(), 1);  // All in same level
    EXPECT_EQ(levels[0].size(), 3);
}

TEST_F(TaskDependencyGraphTest, GetParallelLevels_Chain) {
    // A → B → C sequential chain
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    auto levels = graph_.GetParallelLevels();

    EXPECT_EQ(levels.size(), 3);  // Three sequential levels
    EXPECT_EQ(levels[0].size(), 1);
    EXPECT_EQ(levels[1].size(), 1);
    EXPECT_EQ(levels[2].size(), 1);
}

TEST_F(TaskDependencyGraphTest, GetParallelLevels_Diamond) {
    // A → B, A → C, B → D, C → D (diamond pattern)
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resX_.get()}, {resZ_.get()});
    SetupSingleBundle(nodeD_.get(), {resY_.get(), resZ_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());
    tracker_.AddNode(nodeD_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get(), nodeD_.get()};
    graph_.Build(tracker_, order);

    auto levels = graph_.GetParallelLevels();

    EXPECT_EQ(levels.size(), 3);  // Level 0: A, Level 1: B&C, Level 2: D
    EXPECT_EQ(levels[0].size(), 1);  // A
    EXPECT_EQ(levels[1].size(), 2);  // B and C parallel
    EXPECT_EQ(levels[2].size(), 1);  // D
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(TaskDependencyGraphTest, GetCriticalPathLength_SingleTask) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    graph_.Build(tracker_, order);

    EXPECT_EQ(graph_.GetCriticalPathLength(), 1);
}

TEST_F(TaskDependencyGraphTest, GetCriticalPathLength_Chain) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get()};
    graph_.Build(tracker_, order);

    EXPECT_EQ(graph_.GetCriticalPathLength(), 3);
}

TEST_F(TaskDependencyGraphTest, GetMaxParallelism) {
    // Diamond: max 2 parallel at level 1
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {resY_.get()});
    SetupSingleBundle(nodeC_.get(), {resX_.get()}, {resZ_.get()});
    SetupSingleBundle(nodeD_.get(), {resY_.get(), resZ_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());
    tracker_.AddNode(nodeD_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get(), nodeC_.get(), nodeD_.get()};
    graph_.Build(tracker_, order);

    EXPECT_EQ(graph_.GetMaxParallelism(), 2);
}

TEST_F(TaskDependencyGraphTest, HasCycle_NoCycle) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    std::vector<NodeInstance*> order = {nodeA_.get(), nodeB_.get()};
    graph_.Build(tracker_, order);

    EXPECT_FALSE(graph_.HasCycle());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TaskDependencyGraphTest, EdgeCase_EmptyBuild) {
    std::vector<NodeInstance*> emptyOrder;
    graph_.Build(tracker_, emptyOrder);

    EXPECT_EQ(graph_.GetTaskCount(), 0);
    EXPECT_FALSE(graph_.HasCycle());
}

TEST_F(TaskDependencyGraphTest, EdgeCase_SingleNode) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    std::vector<NodeInstance*> order = {nodeA_.get()};
    graph_.Build(tracker_, order);

    EXPECT_EQ(graph_.GetTaskCount(), 1);
    EXPECT_EQ(graph_.GetEdgeCount(), 0);

    auto ready = graph_.GetReadyTasks();
    EXPECT_EQ(ready.size(), 1);
}

TEST_F(TaskDependencyGraphTest, EdgeCase_InvalidTaskQuery) {
    VirtualTaskId invalid = VirtualTaskId::Invalid();

    EXPECT_TRUE(graph_.GetDependencies(invalid).empty());
    EXPECT_TRUE(graph_.GetDependents(invalid).empty());
    EXPECT_EQ(graph_.GetDependencyCount(invalid), 0);
}
