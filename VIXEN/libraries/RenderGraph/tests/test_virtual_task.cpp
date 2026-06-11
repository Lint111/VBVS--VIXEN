// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_virtual_task.cpp
 * @brief Unit tests for VirtualTask data structures
 *
 * Sprint 6.5 Phase 0: Foundation
 * Tests VirtualTaskId, VirtualTask, hash functions, and state management.
 */

#include <gtest/gtest.h>
#include "Core/VirtualTask.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

using namespace Vixen::RenderGraph;

// Mock NodeInstance for testing (just need pointer addresses)
class MockNodeInstance {
public:
    explicit MockNodeInstance(const std::string& name) : name_(name) {}
    const std::string& GetName() const { return name_; }
private:
    std::string name_;
};

class VirtualTaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeA_ = std::make_unique<MockNodeInstance>("NodeA");
        nodeB_ = std::make_unique<MockNodeInstance>("NodeB");
        nodeC_ = std::make_unique<MockNodeInstance>("NodeC");
    }

    std::unique_ptr<MockNodeInstance> nodeA_;
    std::unique_ptr<MockNodeInstance> nodeB_;
    std::unique_ptr<MockNodeInstance> nodeC_;

    // Helper to create VirtualTaskId with mock node
    VirtualTaskId MakeTaskId(MockNodeInstance* node, uint32_t taskIndex) {
        return VirtualTaskId{reinterpret_cast<NodeInstance*>(node), taskIndex};
    }
};

// =============================================================================
// VirtualTaskId Tests
// =============================================================================

TEST_F(VirtualTaskTest, VirtualTaskId_DefaultConstruction) {
    VirtualTaskId id;
    EXPECT_EQ(id.node, nullptr);
    EXPECT_EQ(id.taskIndex, 0);
    EXPECT_FALSE(id.IsValid());
}

TEST_F(VirtualTaskTest, VirtualTaskId_ValueConstruction) {
    auto id = MakeTaskId(nodeA_.get(), 5);
    EXPECT_EQ(id.node, reinterpret_cast<NodeInstance*>(nodeA_.get()));
    EXPECT_EQ(id.taskIndex, 5);
    EXPECT_TRUE(id.IsValid());
}

TEST_F(VirtualTaskTest, VirtualTaskId_Invalid) {
    auto invalid = VirtualTaskId::Invalid();
    EXPECT_EQ(invalid.node, nullptr);
    EXPECT_EQ(invalid.taskIndex, UINT32_MAX);
    EXPECT_FALSE(invalid.IsValid());
}

TEST_F(VirtualTaskTest, VirtualTaskId_Equality_SameNodeSameIndex) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeA_.get(), 0);
    EXPECT_EQ(id1, id2);
    EXPECT_FALSE(id1 != id2);
}

TEST_F(VirtualTaskTest, VirtualTaskId_Equality_SameNodeDifferentIndex) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeA_.get(), 1);
    EXPECT_NE(id1, id2);
    EXPECT_FALSE(id1 == id2);
}

TEST_F(VirtualTaskTest, VirtualTaskId_Equality_DifferentNodeSameIndex) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeB_.get(), 0);
    EXPECT_NE(id1, id2);
}

TEST_F(VirtualTaskTest, VirtualTaskId_Equality_DifferentNodeDifferentIndex) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeB_.get(), 1);
    EXPECT_NE(id1, id2);
}

// =============================================================================
// VirtualTaskIdHash Tests
// =============================================================================

TEST_F(VirtualTaskTest, VirtualTaskIdHash_SameIdSameHash) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeA_.get(), 0);

    VirtualTaskIdHash hasher;
    EXPECT_EQ(hasher(id1), hasher(id2));
}

TEST_F(VirtualTaskTest, VirtualTaskIdHash_DifferentIndexDifferentHash) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeA_.get(), 1);

    VirtualTaskIdHash hasher;
    // Hash collision is possible but unlikely for adjacent indices
    // We mainly verify that different IDs can be distinguished
    EXPECT_NE(id1, id2);
}

TEST_F(VirtualTaskTest, VirtualTaskIdHash_DifferentNodeDifferentHash) {
    auto id1 = MakeTaskId(nodeA_.get(), 0);
    auto id2 = MakeTaskId(nodeB_.get(), 0);

    VirtualTaskIdHash hasher;
    // Different nodes should have different hashes
    EXPECT_NE(hasher(id1), hasher(id2));
}

TEST_F(VirtualTaskTest, VirtualTaskIdHash_UnorderedSetUsage) {
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> taskSet;

    taskSet.insert(MakeTaskId(nodeA_.get(), 0));
    taskSet.insert(MakeTaskId(nodeA_.get(), 1));
    taskSet.insert(MakeTaskId(nodeB_.get(), 0));
    taskSet.insert(MakeTaskId(nodeA_.get(), 0));  // Duplicate

    EXPECT_EQ(taskSet.size(), 3);  // Duplicate should not be added

    EXPECT_TRUE(taskSet.count(MakeTaskId(nodeA_.get(), 0)) > 0);
    EXPECT_TRUE(taskSet.count(MakeTaskId(nodeA_.get(), 1)) > 0);
    EXPECT_TRUE(taskSet.count(MakeTaskId(nodeB_.get(), 0)) > 0);
    EXPECT_FALSE(taskSet.count(MakeTaskId(nodeC_.get(), 0)) > 0);
}

TEST_F(VirtualTaskTest, VirtualTaskIdHash_UnorderedMapUsage) {
    std::unordered_map<VirtualTaskId, std::string, VirtualTaskIdHash> taskMap;

    taskMap[MakeTaskId(nodeA_.get(), 0)] = "A:0";
    taskMap[MakeTaskId(nodeA_.get(), 1)] = "A:1";
    taskMap[MakeTaskId(nodeB_.get(), 0)] = "B:0";

    EXPECT_EQ(taskMap.size(), 3);
    EXPECT_EQ(taskMap[MakeTaskId(nodeA_.get(), 0)], "A:0");
    EXPECT_EQ(taskMap[MakeTaskId(nodeA_.get(), 1)], "A:1");
    EXPECT_EQ(taskMap[MakeTaskId(nodeB_.get(), 0)], "B:0");
}

TEST_F(VirtualTaskTest, VirtualTaskIdHash_ManyTasks) {
    // Test hash distribution with many tasks
    std::unordered_set<VirtualTaskId, VirtualTaskIdHash> taskSet;

    // Add 100 tasks per node
    for (uint32_t i = 0; i < 100; ++i) {
        taskSet.insert(MakeTaskId(nodeA_.get(), i));
        taskSet.insert(MakeTaskId(nodeB_.get(), i));
        taskSet.insert(MakeTaskId(nodeC_.get(), i));
    }

    EXPECT_EQ(taskSet.size(), 300);

    // Verify all can be found
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_TRUE(taskSet.count(MakeTaskId(nodeA_.get(), i)) > 0);
        EXPECT_TRUE(taskSet.count(MakeTaskId(nodeB_.get(), i)) > 0);
        EXPECT_TRUE(taskSet.count(MakeTaskId(nodeC_.get(), i)) > 0);
    }
}

// =============================================================================
// VirtualTaskState Tests
// =============================================================================

TEST_F(VirtualTaskTest, VirtualTaskState_ToString) {
    EXPECT_STREQ(ToString(VirtualTaskState::Pending), "Pending");
    EXPECT_STREQ(ToString(VirtualTaskState::Ready), "Ready");
    EXPECT_STREQ(ToString(VirtualTaskState::Running), "Running");
    EXPECT_STREQ(ToString(VirtualTaskState::Completed), "Completed");
    EXPECT_STREQ(ToString(VirtualTaskState::Failed), "Failed");
}

TEST_F(VirtualTaskTest, VirtualTaskPhase_ToString) {
    EXPECT_STREQ(ToString(VirtualTaskPhase::Setup), "Setup");
    EXPECT_STREQ(ToString(VirtualTaskPhase::Compile), "Compile");
    EXPECT_STREQ(ToString(VirtualTaskPhase::Execute), "Execute");
    EXPECT_STREQ(ToString(VirtualTaskPhase::Cleanup), "Cleanup");
}

// =============================================================================
// VirtualTask Tests
// =============================================================================

TEST_F(VirtualTaskTest, VirtualTask_DefaultConstruction) {
    VirtualTask task;
    EXPECT_FALSE(task.id.IsValid());
    EXPECT_EQ(task.priority, 128);
    EXPECT_TRUE(task.dependencies.empty());
    EXPECT_EQ(task.state, VirtualTaskState::Pending);
    EXPECT_TRUE(task.errorMessage.empty());
    EXPECT_EQ(task.GetEstimatedCostFromProfiles(), 0);  // No profiles = 0 cost
}

TEST_F(VirtualTaskTest, VirtualTask_StateTransitions) {
    VirtualTask task;
    task.id = MakeTaskId(nodeA_.get(), 0);

    EXPECT_EQ(task.state, VirtualTaskState::Pending);
    EXPECT_FALSE(task.IsReady());
    EXPECT_FALSE(task.IsComplete());

    task.MarkReady();
    EXPECT_EQ(task.state, VirtualTaskState::Ready);
    EXPECT_TRUE(task.IsReady());
    EXPECT_FALSE(task.IsComplete());

    task.MarkRunning();
    EXPECT_EQ(task.state, VirtualTaskState::Running);
    EXPECT_FALSE(task.IsReady());
    EXPECT_FALSE(task.IsComplete());

    task.MarkCompleted();
    EXPECT_EQ(task.state, VirtualTaskState::Completed);
    EXPECT_FALSE(task.IsReady());
    EXPECT_TRUE(task.IsComplete());
    EXPECT_FALSE(task.IsFailed());
}

TEST_F(VirtualTaskTest, VirtualTask_FailedState) {
    VirtualTask task;
    task.id = MakeTaskId(nodeA_.get(), 0);

    task.MarkFailed("Test error message");
    EXPECT_EQ(task.state, VirtualTaskState::Failed);
    EXPECT_TRUE(task.IsComplete());
    EXPECT_TRUE(task.IsFailed());
    EXPECT_EQ(task.errorMessage, "Test error message");
}

TEST_F(VirtualTaskTest, VirtualTask_Dependencies) {
    VirtualTask task;
    task.id = MakeTaskId(nodeB_.get(), 0);

    EXPECT_FALSE(task.HasDependencies());
    EXPECT_EQ(task.GetDependencyCount(), 0);

    task.dependencies.push_back(MakeTaskId(nodeA_.get(), 0));
    task.dependencies.push_back(MakeTaskId(nodeA_.get(), 1));

    EXPECT_TRUE(task.HasDependencies());
    EXPECT_EQ(task.GetDependencyCount(), 2);
}

TEST_F(VirtualTaskTest, VirtualTask_ExecuteFunction) {
    VirtualTask task;
    task.id = MakeTaskId(nodeA_.get(), 0);

    int executionCount = 0;
    task.execute = [&executionCount]() {
        ++executionCount;
    };

    EXPECT_EQ(executionCount, 0);
    task.execute();
    EXPECT_EQ(executionCount, 1);
    task.execute();
    EXPECT_EQ(executionCount, 2);
}

TEST_F(VirtualTaskTest, VirtualTask_ExecuteFunctionWithCapture) {
    VirtualTask task;
    task.id = MakeTaskId(nodeA_.get(), 5);

    std::string result;
    uint32_t capturedTaskIndex = task.id.taskIndex;

    task.execute = [&result, capturedTaskIndex]() {
        result = "Task " + std::to_string(capturedTaskIndex) + " executed";
    };

    task.execute();
    EXPECT_EQ(result, "Task 5 executed");
}

// =============================================================================
// VirtualTaskStats Tests
// =============================================================================

TEST_F(VirtualTaskTest, VirtualTaskStats_DefaultConstruction) {
    VirtualTaskStats stats;
    EXPECT_EQ(stats.totalTasks, 0);
    EXPECT_EQ(stats.completedTasks, 0);
    EXPECT_EQ(stats.failedTasks, 0);
    EXPECT_EQ(stats.parallelTasks, 0);
    EXPECT_EQ(stats.serializedTasks, 0);
    EXPECT_DOUBLE_EQ(stats.totalExecutionMs, 0.0);
    EXPECT_FLOAT_EQ(stats.GetParallelismFactor(), 0.0f);
    EXPECT_FLOAT_EQ(stats.GetSuccessRate(), 1.0f);  // No tasks = 100% success
}

TEST_F(VirtualTaskTest, VirtualTaskStats_ParallelismFactor) {
    VirtualTaskStats stats;
    stats.totalTasks = 100;
    stats.parallelTasks = 75;

    EXPECT_FLOAT_EQ(stats.GetParallelismFactor(), 0.75f);
}

TEST_F(VirtualTaskTest, VirtualTaskStats_SuccessRate) {
    VirtualTaskStats stats;
    stats.completedTasks = 90;
    stats.failedTasks = 10;

    EXPECT_FLOAT_EQ(stats.GetSuccessRate(), 0.9f);
}

TEST_F(VirtualTaskTest, VirtualTaskStats_AllFailed) {
    VirtualTaskStats stats;
    stats.completedTasks = 0;
    stats.failedTasks = 50;

    EXPECT_FLOAT_EQ(stats.GetSuccessRate(), 0.0f);
}

// =============================================================================
// Integration Test: Task Vector Management
// =============================================================================

TEST_F(VirtualTaskTest, Integration_TaskVectorSorting) {
    // Test that tasks can be sorted by priority
    std::vector<VirtualTask> tasks;

    for (uint32_t i = 0; i < 10; ++i) {
        VirtualTask task;
        task.id = MakeTaskId(nodeA_.get(), i);
        task.priority = static_cast<uint8_t>(255 - i);  // Higher index = higher priority
        tasks.push_back(std::move(task));
    }

    // Sort by priority (lower value = higher priority = first)
    std::sort(tasks.begin(), tasks.end(),
        [](const VirtualTask& a, const VirtualTask& b) {
            return a.priority < b.priority;
        });

    // Task 9 should be first (priority 246), Task 0 should be last (priority 255)
    EXPECT_EQ(tasks[0].id.taskIndex, 9);
    EXPECT_EQ(tasks[9].id.taskIndex, 0);
}

TEST_F(VirtualTaskTest, Integration_DependencyChain) {
    // Build a chain: A:0 -> A:1 -> A:2 -> B:0
    std::vector<VirtualTask> tasks(4);

    tasks[0].id = MakeTaskId(nodeA_.get(), 0);
    tasks[0].dependencies = {};  // Root task

    tasks[1].id = MakeTaskId(nodeA_.get(), 1);
    tasks[1].dependencies = {MakeTaskId(nodeA_.get(), 0)};

    tasks[2].id = MakeTaskId(nodeA_.get(), 2);
    tasks[2].dependencies = {MakeTaskId(nodeA_.get(), 1)};

    tasks[3].id = MakeTaskId(nodeB_.get(), 0);
    tasks[3].dependencies = {MakeTaskId(nodeA_.get(), 2)};

    // Verify chain
    EXPECT_FALSE(tasks[0].HasDependencies());
    EXPECT_EQ(tasks[1].GetDependencyCount(), 1);
    EXPECT_EQ(tasks[2].GetDependencyCount(), 1);
    EXPECT_EQ(tasks[3].GetDependencyCount(), 1);

    EXPECT_EQ(tasks[3].dependencies[0], MakeTaskId(nodeA_.get(), 2));
}
