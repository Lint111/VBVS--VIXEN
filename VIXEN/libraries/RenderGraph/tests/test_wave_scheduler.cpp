// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_wave_scheduler.cpp
 * @brief Unit tests for WaveScheduler wave computation
 *
 * Sprint 6.4: Phase 1 - WaveScheduler Core Algorithm
 * Design Element: #38 Timeline Capacity Tracker
 */

#include <gtest/gtest.h>
#include "Core/WaveScheduler.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/GraphTopology.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <memory>

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST FIXTURES AND MOCKS
// ============================================================================

class MockResource : public Resource {
public:
    MockResource() = default;
    std::string debugName;
    explicit MockResource(const std::string& name) : debugName(name) {}
};

class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& name) : NodeType(name) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<MockNodeType*>(this));
    }
};

class WaveSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeType_ = std::make_unique<MockNodeType>("TestType");
    }

    std::unique_ptr<NodeInstance> CreateNode(const std::string& name) {
        auto node = nodeType_->CreateInstance(name);
        nodes_.push_back(node.get());
        return node;
    }

    std::unique_ptr<MockResource> CreateResource(const std::string& name) {
        auto resource = std::make_unique<MockResource>(name);
        resources_.push_back(resource.get());
        return resource;
    }

    void AddOutput(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) bundles.push_back({});
        if (bundles[0].outputs.size() <= slotIndex) {
            bundles[0].outputs.resize(slotIndex + 1, nullptr);
        }
        bundles[0].outputs[slotIndex] = resource;
    }

    void AddInput(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) bundles.push_back({});
        if (bundles[0].inputs.size() <= slotIndex) {
            bundles[0].inputs.resize(slotIndex + 1, nullptr);
        }
        bundles[0].inputs[slotIndex] = resource;
    }

    std::unique_ptr<MockNodeType> nodeType_;
    std::vector<NodeInstance*> nodes_;
    std::vector<Resource*> resources_;
    std::vector<std::unique_ptr<NodeInstance>> nodeStorage_;
    std::vector<std::unique_ptr<MockResource>> resourceStorage_;

    GraphTopology topology_;
    ResourceAccessTracker accessTracker_;
    WaveScheduler scheduler_;
};

// ============================================================================
// BASIC TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, EmptyGraph_ProducesNoWaves) {
    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));
    EXPECT_EQ(scheduler_.GetWaveCount(), 0u);
    EXPECT_TRUE(scheduler_.IsComputed());
}

TEST_F(WaveSchedulerTest, SingleNode_SingleWave) {
    auto node = CreateNode("A");
    nodeStorage_.push_back(std::move(node));

    topology_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[0]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));
    EXPECT_EQ(scheduler_.GetWaveCount(), 1u);
    EXPECT_EQ(scheduler_.GetWaves()[0].Size(), 1u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), 0u);
}

TEST_F(WaveSchedulerTest, IndependentNodes_SingleWave) {
    // A, B, C - no dependencies, no conflicts → all in wave 0
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    auto nodeC = CreateNode("C");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));
    nodeStorage_.push_back(std::move(nodeC));

    auto resA = CreateResource("RA");
    auto resB = CreateResource("RB");
    auto resC = CreateResource("RC");
    resourceStorage_.push_back(std::move(resA));
    resourceStorage_.push_back(std::move(resB));
    resourceStorage_.push_back(std::move(resC));

    // Each node writes to different resource (no conflict)
    AddOutput(nodes_[0], resources_[0]);
    AddOutput(nodes_[1], resources_[1]);
    AddOutput(nodes_[2], resources_[2]);

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddNode(nodes_[2]);

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);
    accessTracker_.AddNode(nodes_[2]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));
    EXPECT_EQ(scheduler_.GetWaveCount(), 1u);
    EXPECT_EQ(scheduler_.GetWaves()[0].Size(), 3u);

    // All nodes in wave 0
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), 0u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[1]), 0u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[2]), 0u);
}

// ============================================================================
// DEPENDENCY TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, LinearDependency_SequentialWaves) {
    // A → B → C (linear chain)
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    auto nodeC = CreateNode("C");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));
    nodeStorage_.push_back(std::move(nodeC));

    nodes_[1]->AddDependency(nodes_[0]);  // B depends on A
    nodes_[2]->AddDependency(nodes_[1]);  // C depends on B

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddNode(nodes_[2]);
    topology_.AddEdge({nodes_[0], 0, nodes_[1], 0});
    topology_.AddEdge({nodes_[1], 0, nodes_[2], 0});

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));
    EXPECT_EQ(scheduler_.GetWaveCount(), 3u);

    // Each node in separate wave
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), 0u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[1]), 1u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[2]), 2u);
}

TEST_F(WaveSchedulerTest, DiamondDependency_ParallelMiddle) {
    //     A
    //    / \
    //   B   C
    //    \ /
    //     D
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    auto nodeC = CreateNode("C");
    auto nodeD = CreateNode("D");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));
    nodeStorage_.push_back(std::move(nodeC));
    nodeStorage_.push_back(std::move(nodeD));

    // Different resources for B and C (no conflict)
    auto resA = CreateResource("RA");
    auto resB = CreateResource("RB");
    auto resC = CreateResource("RC");
    auto resD = CreateResource("RD");
    resourceStorage_.push_back(std::move(resA));
    resourceStorage_.push_back(std::move(resB));
    resourceStorage_.push_back(std::move(resC));
    resourceStorage_.push_back(std::move(resD));

    AddOutput(nodes_[0], resources_[0]);
    AddOutput(nodes_[1], resources_[1]);
    AddOutput(nodes_[2], resources_[2]);
    AddOutput(nodes_[3], resources_[3]);

    nodes_[1]->AddDependency(nodes_[0]);  // B depends on A
    nodes_[2]->AddDependency(nodes_[0]);  // C depends on A
    nodes_[3]->AddDependency(nodes_[1]);  // D depends on B
    nodes_[3]->AddDependency(nodes_[2]);  // D depends on C

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddNode(nodes_[2]);
    topology_.AddNode(nodes_[3]);
    topology_.AddEdge({nodes_[0], 0, nodes_[1], 0});
    topology_.AddEdge({nodes_[0], 0, nodes_[2], 0});
    topology_.AddEdge({nodes_[1], 0, nodes_[3], 0});
    topology_.AddEdge({nodes_[2], 0, nodes_[3], 1});

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);
    accessTracker_.AddNode(nodes_[2]);
    accessTracker_.AddNode(nodes_[3]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // Expected: Wave 0: A, Wave 1: B,C (parallel), Wave 2: D
    EXPECT_EQ(scheduler_.GetWaveCount(), 3u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), 0u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[1]), 1u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[2]), 1u);
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[3]), 2u);

    // B and C should be in same wave
    EXPECT_EQ(scheduler_.GetWaves()[1].Size(), 2u);
}

// ============================================================================
// CONFLICT TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, WriteWriteConflict_SeparateWaves) {
    // A and B both write to same resource → conflict → separate waves
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));

    auto sharedRes = CreateResource("Shared");
    resourceStorage_.push_back(std::move(sharedRes));

    AddOutput(nodes_[0], resources_[0]);  // A writes Shared
    AddOutput(nodes_[1], resources_[0]);  // B writes Shared (conflict!)

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // Should be in separate waves due to conflict
    EXPECT_EQ(scheduler_.GetWaveCount(), 2u);
    EXPECT_NE(scheduler_.GetNodeWave(nodes_[0]), scheduler_.GetNodeWave(nodes_[1]));
}

TEST_F(WaveSchedulerTest, WriteReadConflict_SeparateWaves) {
    // A writes, B reads same resource → conflict
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));

    auto sharedRes = CreateResource("Shared");
    resourceStorage_.push_back(std::move(sharedRes));

    AddOutput(nodes_[0], resources_[0]);  // A writes Shared
    AddInput(nodes_[1], resources_[0]);   // B reads Shared (conflict!)

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // Should be in separate waves
    EXPECT_EQ(scheduler_.GetWaveCount(), 2u);
}

TEST_F(WaveSchedulerTest, ReadReadNoConflict_SameWave) {
    // A and B both read same resource → no conflict → same wave
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));

    auto sharedRes = CreateResource("Shared");
    resourceStorage_.push_back(std::move(sharedRes));

    AddInput(nodes_[0], resources_[0]);  // A reads Shared
    AddInput(nodes_[1], resources_[0]);  // B reads Shared (OK!)

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // Should be in same wave (parallel reads safe)
    EXPECT_EQ(scheduler_.GetWaveCount(), 1u);
    EXPECT_EQ(scheduler_.GetWaves()[0].Size(), 2u);
}

// ============================================================================
// COMPLEX GRAPH TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, MixedDependenciesAndConflicts) {
    // A → B, A → C (B and C depend on A)
    // B and C write to same resource (conflict)
    // D depends on B and C

    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    auto nodeC = CreateNode("C");
    auto nodeD = CreateNode("D");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));
    nodeStorage_.push_back(std::move(nodeC));
    nodeStorage_.push_back(std::move(nodeD));

    auto resA = CreateResource("RA");
    auto resShared = CreateResource("Shared");
    auto resD = CreateResource("RD");
    resourceStorage_.push_back(std::move(resA));
    resourceStorage_.push_back(std::move(resShared));
    resourceStorage_.push_back(std::move(resD));

    AddOutput(nodes_[0], resources_[0]);    // A writes RA
    AddOutput(nodes_[1], resources_[1]);    // B writes Shared
    AddOutput(nodes_[2], resources_[1]);    // C writes Shared (conflict with B!)
    AddOutput(nodes_[3], resources_[2]);    // D writes RD

    nodes_[1]->AddDependency(nodes_[0]);
    nodes_[2]->AddDependency(nodes_[0]);
    nodes_[3]->AddDependency(nodes_[1]);
    nodes_[3]->AddDependency(nodes_[2]);

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddNode(nodes_[2]);
    topology_.AddNode(nodes_[3]);
    topology_.AddEdge({nodes_[0], 0, nodes_[1], 0});
    topology_.AddEdge({nodes_[0], 0, nodes_[2], 0});
    topology_.AddEdge({nodes_[1], 0, nodes_[3], 0});
    topology_.AddEdge({nodes_[2], 0, nodes_[3], 1});

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);
    accessTracker_.AddNode(nodes_[2]);
    accessTracker_.AddNode(nodes_[3]);

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // Expected: A in wave 0, B in wave 1, C in wave 2 (conflict), D in wave 3
    // Or: A in wave 0, C in wave 1, B in wave 2 (conflict), D in wave 3
    // Either way, B and C must be in different waves

    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), 0u);  // A always wave 0
    EXPECT_NE(scheduler_.GetNodeWave(nodes_[1]), scheduler_.GetNodeWave(nodes_[2]));  // B ≠ C

    // D must be after both B and C
    uint32_t dWave = scheduler_.GetNodeWave(nodes_[3]);
    EXPECT_GT(dWave, scheduler_.GetNodeWave(nodes_[1]));
    EXPECT_GT(dWave, scheduler_.GetNodeWave(nodes_[2]));
}

// ============================================================================
// STATISTICS TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, Statistics_CorrectValues) {
    // Create diamond pattern: A, then B+C parallel, then D
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    auto nodeC = CreateNode("C");
    auto nodeD = CreateNode("D");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));
    nodeStorage_.push_back(std::move(nodeC));
    nodeStorage_.push_back(std::move(nodeD));

    auto resA = CreateResource("RA");
    auto resB = CreateResource("RB");
    auto resC = CreateResource("RC");
    auto resD = CreateResource("RD");
    resourceStorage_.push_back(std::move(resA));
    resourceStorage_.push_back(std::move(resB));
    resourceStorage_.push_back(std::move(resC));
    resourceStorage_.push_back(std::move(resD));

    AddOutput(nodes_[0], resources_[0]);
    AddOutput(nodes_[1], resources_[1]);
    AddOutput(nodes_[2], resources_[2]);
    AddOutput(nodes_[3], resources_[3]);

    nodes_[1]->AddDependency(nodes_[0]);
    nodes_[2]->AddDependency(nodes_[0]);
    nodes_[3]->AddDependency(nodes_[1]);
    nodes_[3]->AddDependency(nodes_[2]);

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddNode(nodes_[2]);
    topology_.AddNode(nodes_[3]);
    topology_.AddEdge({nodes_[0], 0, nodes_[1], 0});
    topology_.AddEdge({nodes_[0], 0, nodes_[2], 0});
    topology_.AddEdge({nodes_[1], 0, nodes_[3], 0});
    topology_.AddEdge({nodes_[2], 0, nodes_[3], 1});

    accessTracker_.AddNode(nodes_[0]);
    accessTracker_.AddNode(nodes_[1]);
    accessTracker_.AddNode(nodes_[2]);
    accessTracker_.AddNode(nodes_[3]);

    scheduler_.ComputeWaves(topology_, accessTracker_);

    auto stats = scheduler_.GetStats();
    EXPECT_EQ(stats.totalNodes, 4u);
    EXPECT_EQ(stats.waveCount, 3u);
    EXPECT_EQ(stats.maxWaveSize, 2u);  // B and C in same wave
    EXPECT_EQ(stats.minWaveSize, 1u);  // A and D alone

    // Parallelism: 4 nodes / 3 waves ≈ 1.33
    EXPECT_FLOAT_EQ(scheduler_.GetParallelismFactor(), 4.0f / 3.0f);
    EXPECT_FLOAT_EQ(scheduler_.GetTheoreticalSpeedup(), 4.0f / 3.0f);
}

// ============================================================================
// VALIDATION TESTS
// ============================================================================

TEST_F(WaveSchedulerTest, Validate_PassesOnCorrectWaves) {
    auto nodeA = CreateNode("A");
    auto nodeB = CreateNode("B");
    nodeStorage_.push_back(std::move(nodeA));
    nodeStorage_.push_back(std::move(nodeB));

    nodes_[1]->AddDependency(nodes_[0]);

    topology_.AddNode(nodes_[0]);
    topology_.AddNode(nodes_[1]);
    topology_.AddEdge({nodes_[0], 0, nodes_[1], 0});

    scheduler_.ComputeWaves(topology_, accessTracker_);

    std::string errorMessage;
    EXPECT_TRUE(scheduler_.Validate(topology_, accessTracker_, errorMessage));
    EXPECT_TRUE(errorMessage.empty());
}

TEST_F(WaveSchedulerTest, GetNodeWave_UnknownNodeReturnsMax) {
    auto nodeA = CreateNode("A");
    nodeStorage_.push_back(std::move(nodeA));

    // Node not added to scheduler
    EXPECT_EQ(scheduler_.GetNodeWave(nodes_[0]), UINT32_MAX);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(WaveSchedulerTest, Clear_ResetsState) {
    auto nodeA = CreateNode("A");
    nodeStorage_.push_back(std::move(nodeA));

    topology_.AddNode(nodes_[0]);
    scheduler_.ComputeWaves(topology_, accessTracker_);

    EXPECT_TRUE(scheduler_.IsComputed());
    EXPECT_EQ(scheduler_.GetWaveCount(), 1u);

    scheduler_.Clear();

    EXPECT_FALSE(scheduler_.IsComputed());
    EXPECT_EQ(scheduler_.GetWaveCount(), 0u);
}

TEST_F(WaveSchedulerTest, Recompute_UpdatesWaves) {
    auto nodeA = CreateNode("A");
    nodeStorage_.push_back(std::move(nodeA));

    topology_.AddNode(nodes_[0]);
    scheduler_.ComputeWaves(topology_, accessTracker_);

    // Add another node and recompute
    auto nodeB = CreateNode("B");
    nodeStorage_.push_back(std::move(nodeB));
    topology_.AddNode(nodes_[1]);

    scheduler_.Recompute();

    EXPECT_EQ(scheduler_.GetTotalNodes(), 2u);
}

TEST_F(WaveSchedulerTest, LargeGraph_HandlesEfficiently) {
    // Create 100 independent nodes
    const size_t N = 100;
    for (size_t i = 0; i < N; ++i) {
        auto node = CreateNode("Node" + std::to_string(i));
        auto res = CreateResource("Res" + std::to_string(i));
        AddOutput(node.get(), res.get());
        topology_.AddNode(node.get());
        accessTracker_.AddNode(node.get());
        nodeStorage_.push_back(std::move(node));
        resourceStorage_.push_back(std::move(res));
    }

    EXPECT_TRUE(scheduler_.ComputeWaves(topology_, accessTracker_));

    // All 100 independent nodes should be in single wave
    EXPECT_EQ(scheduler_.GetWaveCount(), 1u);
    EXPECT_EQ(scheduler_.GetWaves()[0].Size(), N);
    EXPECT_FLOAT_EQ(scheduler_.GetParallelismFactor(), static_cast<float>(N));
}
