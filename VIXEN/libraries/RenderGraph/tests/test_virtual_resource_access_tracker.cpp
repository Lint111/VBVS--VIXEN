// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_virtual_resource_access_tracker.cpp
 * @brief Unit tests for VirtualResourceAccessTracker
 *
 * Sprint 6.5 Phase 1: Per-task resource access tracking
 * Tests conflict detection at (node, taskIndex) granularity.
 */

#include <gtest/gtest.h>
#include "Core/VirtualResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <memory>

using namespace Vixen::RenderGraph;

// ============================================================================
// Mock Classes for Testing
// ============================================================================

// Minimal NodeType for testing
class TestNodeType : public NodeType {
public:
    explicit TestNodeType(const std::string& name)
        : NodeType(name) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<TestNodeType*>(this));
    }
};

// Minimal Resource for testing
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

class VirtualResourceAccessTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create node types
        nodeTypeA_ = std::make_unique<TestNodeType>("TypeA");
        nodeTypeB_ = std::make_unique<TestNodeType>("TypeB");
        nodeTypeC_ = std::make_unique<TestNodeType>("TypeC");

        // Create node instances using CreateInstance (proper factory pattern)
        nodeA_ = nodeTypeA_->CreateInstance("NodeA");
        nodeB_ = nodeTypeB_->CreateInstance("NodeB");
        nodeC_ = nodeTypeC_->CreateInstance("NodeC");

        // Create test resources
        resX_ = std::make_unique<TestResource>("ResourceX");
        resY_ = std::make_unique<TestResource>("ResourceY");
        resZ_ = std::make_unique<TestResource>("ResourceZ");
    }

    // Helper to set up bundles with resources
    void SetupSingleBundle(NodeInstance* node, std::vector<Resource*> inputs, std::vector<Resource*> outputs) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            node->SetInput(static_cast<uint32_t>(i), 0, inputs[i]);
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            node->SetOutput(static_cast<uint32_t>(i), 0, outputs[i]);
        }
    }

    void SetupMultipleBundles(NodeInstance* node, size_t bundleCount,
                              std::vector<Resource*> inputsPerBundle,
                              std::vector<Resource*> outputsPerBundle) {
        for (size_t taskIndex = 0; taskIndex < bundleCount; ++taskIndex) {
            for (size_t i = 0; i < inputsPerBundle.size(); ++i) {
                node->SetInput(static_cast<uint32_t>(i), static_cast<uint32_t>(taskIndex), inputsPerBundle[i]);
            }
            for (size_t i = 0; i < outputsPerBundle.size(); ++i) {
                node->SetOutput(static_cast<uint32_t>(i), static_cast<uint32_t>(taskIndex), outputsPerBundle[i]);
            }
        }
    }

    std::unique_ptr<TestNodeType> nodeTypeA_;
    std::unique_ptr<TestNodeType> nodeTypeB_;
    std::unique_ptr<TestNodeType> nodeTypeC_;

    std::unique_ptr<NodeInstance> nodeA_;
    std::unique_ptr<NodeInstance> nodeB_;
    std::unique_ptr<NodeInstance> nodeC_;

    std::unique_ptr<TestResource> resX_;
    std::unique_ptr<TestResource> resY_;
    std::unique_ptr<TestResource> resZ_;

    VirtualResourceAccessTracker tracker_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, Construction_EmptyTracker) {
    EXPECT_EQ(tracker_.GetResourceCount(), 0);
    EXPECT_EQ(tracker_.GetTaskCount(), 0);
    EXPECT_EQ(tracker_.GetNodeCount(), 0);
}

TEST_F(VirtualResourceAccessTrackerTest, Clear_ResetsAllData) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    EXPECT_GT(tracker_.GetResourceCount(), 0);

    tracker_.Clear();

    EXPECT_EQ(tracker_.GetResourceCount(), 0);
    EXPECT_EQ(tracker_.GetTaskCount(), 0);
    EXPECT_EQ(tracker_.GetNodeCount(), 0);
}

// ============================================================================
// Single Bundle Node Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, SingleBundle_TracksInputsAsReads) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    tracker_.AddNode(nodeA_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    auto reads = tracker_.GetTaskReads(taskA);

    EXPECT_EQ(reads.size(), 1);
    EXPECT_EQ(reads[0], resX_.get());
}

TEST_F(VirtualResourceAccessTrackerTest, SingleBundle_TracksOutputsAsWrites) {
    SetupSingleBundle(nodeA_.get(), {}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    auto writes = tracker_.GetTaskWrites(taskA);

    EXPECT_EQ(writes.size(), 1);
    EXPECT_EQ(writes[0], resY_.get());
    EXPECT_TRUE(tracker_.IsWriter(taskA));
}

TEST_F(VirtualResourceAccessTrackerTest, SingleBundle_TracksAllResources) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    auto resources = tracker_.GetTaskResources(taskA);

    EXPECT_EQ(resources.size(), 2);
}

// ============================================================================
// Multiple Bundle Node Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, MultipleBundles_CreatesMultipleTasks) {
    // Create 3 bundles
    SetupMultipleBundles(nodeA_.get(), 3, {resX_.get()}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetNodeTaskCount(nodeA_.get()), 3);

    auto tasks = tracker_.GetNodeTasks(nodeA_.get());
    EXPECT_EQ(tasks.size(), 3);

    // Verify each task ID
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(tasks[i].node, nodeA_.get());
        EXPECT_EQ(tasks[i].taskIndex, i);
    }
}

TEST_F(VirtualResourceAccessTrackerTest, MultipleBundles_EachTaskTrackedSeparately) {
    // Create 2 bundles with same resource
    SetupMultipleBundles(nodeA_.get(), 2, {resX_.get()}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    VirtualTaskId task0{nodeA_.get(), 0};
    VirtualTaskId task1{nodeA_.get(), 1};

    // Both tasks should have same resources (same setup per bundle)
    EXPECT_EQ(tracker_.GetTaskResources(task0).size(), 2);
    EXPECT_EQ(tracker_.GetTaskResources(task1).size(), 2);

    // Both should be tracked
    EXPECT_EQ(tracker_.GetTaskCount(), 2);
}

// ============================================================================
// Conflict Detection Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, NoConflict_BothReadSameResource) {
    // NodeA reads X, NodeB reads X
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_FALSE(tracker_.HasConflict(taskA, taskB));
}

TEST_F(VirtualResourceAccessTrackerTest, Conflict_WriteWrite) {
    // Both write to X
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_TRUE(tracker_.HasConflict(taskA, taskB));
}

TEST_F(VirtualResourceAccessTrackerTest, Conflict_WriteRead) {
    // A writes X, B reads X
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_TRUE(tracker_.HasConflict(taskA, taskB));
}

TEST_F(VirtualResourceAccessTrackerTest, Conflict_ReadWrite) {
    // A reads X, B writes X
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_TRUE(tracker_.HasConflict(taskA, taskB));
}

TEST_F(VirtualResourceAccessTrackerTest, NoConflict_DifferentResources) {
    // A writes X, B writes Y (different resources)
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    EXPECT_FALSE(tracker_.HasConflict(taskA, taskB));
}

TEST_F(VirtualResourceAccessTrackerTest, NoConflict_SameTaskWithItself) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {resY_.get()});
    tracker_.AddNode(nodeA_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};

    EXPECT_FALSE(tracker_.HasConflict(taskA, taskA));
}

// ============================================================================
// Intra-Node Conflict Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, IntraNode_NoConflict_DifferentResources) {
    // Create multiple test resources for different bundles
    auto resBundle0 = std::make_unique<TestResource>("Bundle0Output");
    auto resBundle1 = std::make_unique<TestResource>("Bundle1Output");

    // Set up 2 bundles with different output resources
    nodeA_->SetOutput(0, 0, resBundle0.get());
    nodeA_->SetOutput(0, 1, resBundle1.get());

    tracker_.AddNode(nodeA_.get());

    // Tasks 0 and 1 have no conflict (different resources)
    EXPECT_FALSE(tracker_.HasIntraNodeConflict(nodeA_.get(), 0, 1));
}

TEST_F(VirtualResourceAccessTrackerTest, IntraNode_Conflict_SameResource) {
    // Set up 2 bundles that both write to the same resource
    nodeA_->SetOutput(0, 0, resX_.get());
    nodeA_->SetOutput(0, 1, resX_.get());

    tracker_.AddNode(nodeA_.get());

    // Tasks 0 and 1 have conflict (same resource)
    EXPECT_TRUE(tracker_.HasIntraNodeConflict(nodeA_.get(), 0, 1));
}

// ============================================================================
// Cross-Node Task Conflict Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, CrossNode_ConflictBetweenSpecificTasks) {
    // NodeA has 2 bundles, only bundle 1 writes to X
    nodeA_->SetOutput(0, 0, resY_.get());  // Bundle 0 writes Y
    nodeA_->SetOutput(0, 1, resX_.get());  // Bundle 1 writes X

    // NodeB has 2 bundles, only bundle 0 reads X
    nodeB_->SetInput(0, 0, resX_.get());   // Bundle 0 reads X
    nodeB_->SetInput(0, 1, resZ_.get());   // Bundle 1 reads Z

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA0{nodeA_.get(), 0};
    VirtualTaskId taskA1{nodeA_.get(), 1};
    VirtualTaskId taskB0{nodeB_.get(), 0};
    VirtualTaskId taskB1{nodeB_.get(), 1};

    // A:0 (writes Y) vs B:0 (reads X) - no conflict
    EXPECT_FALSE(tracker_.HasConflict(taskA0, taskB0));

    // A:1 (writes X) vs B:0 (reads X) - CONFLICT
    EXPECT_TRUE(tracker_.HasConflict(taskA1, taskB0));

    // A:1 (writes X) vs B:1 (reads Z) - no conflict
    EXPECT_FALSE(tracker_.HasConflict(taskA1, taskB1));
}

// ============================================================================
// GetConflictingTasks Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, GetConflictingTasks_ReturnsAllConflicts) {
    // A writes X
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    // B reads X (conflict with A)
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    // C reads Y (no conflict)
    SetupSingleBundle(nodeC_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    auto conflicting = tracker_.GetConflictingTasks(taskA);

    EXPECT_EQ(conflicting.size(), 1);
    EXPECT_TRUE(conflicting.count(taskB) > 0);
}

TEST_F(VirtualResourceAccessTrackerTest, GetConflictingTasks_MultipleConflicts) {
    // A writes X
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    // B reads X (conflict)
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    // C writes X (conflict)
    SetupSingleBundle(nodeC_.get(), {}, {resX_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};

    auto conflicting = tracker_.GetConflictingTasks(taskA);

    EXPECT_EQ(conflicting.size(), 2);
}

// ============================================================================
// GetSharedResources Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, GetSharedResources_ReturnsCommonResources) {
    // A: reads X, writes Y
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {resY_.get()});
    // B: reads X, reads Y
    SetupSingleBundle(nodeB_.get(), {resX_.get(), resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    auto shared = tracker_.GetSharedResources(taskA, taskB);

    EXPECT_EQ(shared.size(), 2);  // X and Y are shared
}

TEST_F(VirtualResourceAccessTrackerTest, GetSharedResources_NoSharedResources) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeB_.get(), {resY_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    VirtualTaskId taskA{nodeA_.get(), 0};
    VirtualTaskId taskB{nodeB_.get(), 0};

    auto shared = tracker_.GetSharedResources(taskA, taskB);

    EXPECT_TRUE(shared.empty());
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, Statistics_ConflictingResourceCount) {
    // X is written by A and read by B (conflict)
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    // Y is only read (no conflict)
    nodeC_->SetInput(0, 0, resY_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    // X has writer + reader = conflicting
    // Y has only reader = not conflicting
    EXPECT_EQ(tracker_.GetConflictingResourceCount(), 1);
}

TEST_F(VirtualResourceAccessTrackerTest, Statistics_MaxWritersPerResource) {
    // X written by A, B, C
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeC_.get(), {}, {resX_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    EXPECT_EQ(tracker_.GetMaxWritersPerResource(), 3);
}

TEST_F(VirtualResourceAccessTrackerTest, Statistics_ParallelismPotential) {
    // All tasks read same resource (no conflicts) = high parallelism
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeC_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    // All pairs can parallelize
    EXPECT_FLOAT_EQ(tracker_.GetParallelismPotential(), 1.0f);
}

TEST_F(VirtualResourceAccessTrackerTest, Statistics_ParallelismPotential_AllConflicting) {
    // All tasks write same resource (all conflict) = no parallelism
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeC_.get(), {}, {resX_.get()});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    // All pairs conflict
    EXPECT_FLOAT_EQ(tracker_.GetParallelismPotential(), 0.0f);
}

// ============================================================================
// VirtualResourceAccessInfo Tests
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, AccessInfo_GetWriters) {
    // Multiple nodes write to X
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeC_.get(), {resX_.get()}, {});  // Reader only

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    auto* info = tracker_.GetAccessInfo(resX_.get());
    ASSERT_NE(info, nullptr);

    auto writers = info->GetWriters();
    EXPECT_EQ(writers.size(), 2);  // A and B
}

TEST_F(VirtualResourceAccessTrackerTest, AccessInfo_GetReaders) {
    SetupSingleBundle(nodeA_.get(), {resX_.get()}, {});
    SetupSingleBundle(nodeB_.get(), {resX_.get()}, {});

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    auto* info = tracker_.GetAccessInfo(resX_.get());
    ASSERT_NE(info, nullptr);

    auto readers = info->GetReaders();
    EXPECT_EQ(readers.size(), 2);
}

TEST_F(VirtualResourceAccessTrackerTest, AccessInfo_HasWriter) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());

    auto* info = tracker_.GetAccessInfo(resX_.get());
    ASSERT_NE(info, nullptr);

    EXPECT_TRUE(info->HasWriter());
}

TEST_F(VirtualResourceAccessTrackerTest, AccessInfo_HasMultipleWriters) {
    SetupSingleBundle(nodeA_.get(), {}, {resX_.get()});
    SetupSingleBundle(nodeB_.get(), {}, {resX_.get()});
    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    auto* info = tracker_.GetAccessInfo(resX_.get());
    ASSERT_NE(info, nullptr);

    EXPECT_TRUE(info->HasMultipleWriters());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(VirtualResourceAccessTrackerTest, EdgeCase_NullNode) {
    tracker_.AddNode(nullptr);
    EXPECT_EQ(tracker_.GetNodeCount(), 0);
}

TEST_F(VirtualResourceAccessTrackerTest, EdgeCase_NodeWithNoBundles) {
    // Node with no bundles still creates one task
    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetNodeTaskCount(nodeA_.get()), 1);

    VirtualTaskId task{nodeA_.get(), 0};
    EXPECT_TRUE(tracker_.GetTaskResources(task).empty());
}

TEST_F(VirtualResourceAccessTrackerTest, EdgeCase_InvalidTaskId) {
    VirtualTaskId invalid = VirtualTaskId::Invalid();

    EXPECT_FALSE(tracker_.HasConflict(invalid, invalid));
    EXPECT_TRUE(tracker_.GetTaskResources(invalid).empty());
    EXPECT_TRUE(tracker_.GetTaskWrites(invalid).empty());
    EXPECT_TRUE(tracker_.GetTaskReads(invalid).empty());
    EXPECT_FALSE(tracker_.IsWriter(invalid));
}
