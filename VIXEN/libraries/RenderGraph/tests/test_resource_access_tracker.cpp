// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

/**
 * @file test_resource_access_tracker.cpp
 * @brief Unit tests for ResourceAccessTracker conflict detection
 *
 * Sprint 6.4: Phase 0 - WaveScheduler Foundation
 * Design Element: #38 Timeline Capacity Tracker
 */

#include <gtest/gtest.h>
#include "Core/ResourceAccessTracker.h"
#include "Core/GraphTopology.h"
#include "Core/NodeInstance.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource class
#include <memory>

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST FIXTURES AND MOCKS
// ============================================================================

/**
 * @brief Mock Resource for testing
 *
 * Resource is non-copyable with default constructor only.
 * We use the actual Resource class since we only need pointer identity.
 */
class MockResource : public Resource {
public:
    MockResource() = default;
    std::string debugName;  // For debugging purposes

    explicit MockResource(const std::string& name) : debugName(name) {}
};

/**
 * @brief Mock NodeType for creating test NodeInstances
 */
class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& name)
        : NodeType(name) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<MockNodeType*>(this));
    }
};

/**
 * @brief Test fixture for ResourceAccessTracker
 */
class ResourceAccessTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock node types
        nodeTypeA_ = std::make_unique<MockNodeType>("TypeA");
        nodeTypeB_ = std::make_unique<MockNodeType>("TypeB");
        nodeTypeC_ = std::make_unique<MockNodeType>("TypeC");

        // Create node instances
        nodeA_ = nodeTypeA_->CreateInstance("NodeA");
        nodeB_ = nodeTypeB_->CreateInstance("NodeB");
        nodeC_ = nodeTypeC_->CreateInstance("NodeC");

        // Create test resources
        resource1_ = std::make_unique<MockResource>("Resource1");
        resource2_ = std::make_unique<MockResource>("Resource2");
        resource3_ = std::make_unique<MockResource>("Resource3");
    }

    void TearDown() override {
        // Clean up in reverse order
        resource3_.reset();
        resource2_.reset();
        resource1_.reset();
        nodeC_.reset();
        nodeB_.reset();
        nodeA_.reset();
        nodeTypeC_.reset();
        nodeTypeB_.reset();
        nodeTypeA_.reset();
    }

    /**
     * @brief Helper to add an output (write) to a node's bundles
     */
    void AddOutput(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
        // Access bundles directly (test-only, normally via protected methods)
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) {
            bundles.push_back({});
        }
        if (bundles[0].outputs.size() <= slotIndex) {
            bundles[0].outputs.resize(slotIndex + 1, nullptr);
        }
        bundles[0].outputs[slotIndex] = resource;
    }

    /**
     * @brief Helper to add an input (read) to a node's bundles
     */
    void AddInput(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) {
            bundles.push_back({});
        }
        if (bundles[0].inputs.size() <= slotIndex) {
            bundles[0].inputs.resize(slotIndex + 1, nullptr);
        }
        bundles[0].inputs[slotIndex] = resource;
    }

    // Test data
    std::unique_ptr<MockNodeType> nodeTypeA_;
    std::unique_ptr<MockNodeType> nodeTypeB_;
    std::unique_ptr<MockNodeType> nodeTypeC_;

    std::unique_ptr<NodeInstance> nodeA_;
    std::unique_ptr<NodeInstance> nodeB_;
    std::unique_ptr<NodeInstance> nodeC_;

    std::unique_ptr<MockResource> resource1_;
    std::unique_ptr<MockResource> resource2_;
    std::unique_ptr<MockResource> resource3_;

    ResourceAccessTracker tracker_;
};

// ============================================================================
// BASIC TRACKING TESTS
// ============================================================================

TEST_F(ResourceAccessTrackerTest, EmptyTracker_HasNoResources) {
    EXPECT_EQ(tracker_.GetResourceCount(), 0u);
    EXPECT_EQ(tracker_.GetNodeCount(), 0u);
}

TEST_F(ResourceAccessTrackerTest, AddNode_TracksOutputsAsWrites) {
    AddOutput(nodeA_.get(), resource1_.get());
    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetResourceCount(), 1u);
    EXPECT_EQ(tracker_.GetNodeCount(), 1u);
    EXPECT_TRUE(tracker_.IsWriter(nodeA_.get()));

    auto writes = tracker_.GetNodeWrites(nodeA_.get());
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0], resource1_.get());
}

TEST_F(ResourceAccessTrackerTest, AddNode_TracksInputsAsReads) {
    AddInput(nodeA_.get(), resource1_.get());
    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetResourceCount(), 1u);
    EXPECT_FALSE(tracker_.IsWriter(nodeA_.get()));

    auto reads = tracker_.GetNodeReads(nodeA_.get());
    ASSERT_EQ(reads.size(), 1u);
    EXPECT_EQ(reads[0], resource1_.get());
}

TEST_F(ResourceAccessTrackerTest, AddNode_TracksMultipleResources) {
    AddOutput(nodeA_.get(), resource1_.get(), 0);
    AddOutput(nodeA_.get(), resource2_.get(), 1);
    AddInput(nodeA_.get(), resource3_.get(), 0);

    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetResourceCount(), 3u);

    auto allResources = tracker_.GetNodeResources(nodeA_.get());
    EXPECT_EQ(allResources.size(), 3u);
}

TEST_F(ResourceAccessTrackerTest, Clear_RemovesAllTracking) {
    AddOutput(nodeA_.get(), resource1_.get());
    tracker_.AddNode(nodeA_.get());

    EXPECT_EQ(tracker_.GetResourceCount(), 1u);

    tracker_.Clear();

    EXPECT_EQ(tracker_.GetResourceCount(), 0u);
    EXPECT_EQ(tracker_.GetNodeCount(), 0u);
}

// ============================================================================
// CONFLICT DETECTION TESTS
// ============================================================================

TEST_F(ResourceAccessTrackerTest, NoConflict_DisjointResources) {
    // NodeA writes resource1, NodeB writes resource2 (no overlap)
    AddOutput(nodeA_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource2_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
}

TEST_F(ResourceAccessTrackerTest, NoConflict_BothReadSameResource) {
    // Both nodes read same resource → safe to parallelize
    AddInput(nodeA_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
}

TEST_F(ResourceAccessTrackerTest, Conflict_BothWriteSameResource) {
    // Write-Write conflict
    AddOutput(nodeA_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    EXPECT_TRUE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
}

TEST_F(ResourceAccessTrackerTest, Conflict_OneWritesOneReads) {
    // NodeA writes, NodeB reads same resource → conflict
    AddOutput(nodeA_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    EXPECT_TRUE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
    EXPECT_TRUE(tracker_.HasConflict(nodeB_.get(), nodeA_.get())); // Symmetric
}

TEST_F(ResourceAccessTrackerTest, Conflict_ComplexGraph) {
    // NodeA: writes R1
    // NodeB: reads R1, writes R2
    // NodeC: reads R2, writes R3
    // Expected: A conflicts with B (R1), B conflicts with C (R2), A doesn't conflict with C

    AddOutput(nodeA_.get(), resource1_.get());

    AddInput(nodeB_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource2_.get());

    AddInput(nodeC_.get(), resource2_.get());
    AddOutput(nodeC_.get(), resource3_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    EXPECT_TRUE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
    EXPECT_TRUE(tracker_.HasConflict(nodeB_.get(), nodeC_.get()));
    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nodeC_.get()));
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(ResourceAccessTrackerTest, NoConflict_NullNodes) {
    EXPECT_FALSE(tracker_.HasConflict(nullptr, nullptr));
    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nullptr));
    EXPECT_FALSE(tracker_.HasConflict(nullptr, nodeA_.get()));
}

TEST_F(ResourceAccessTrackerTest, NoConflict_SameNode) {
    AddOutput(nodeA_.get(), resource1_.get());
    tracker_.AddNode(nodeA_.get());

    // Node doesn't conflict with itself
    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nodeA_.get()));
}

TEST_F(ResourceAccessTrackerTest, NoConflict_UntrackedNodes) {
    AddOutput(nodeA_.get(), resource1_.get());
    // Don't add nodeA to tracker

    EXPECT_FALSE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
}

TEST_F(ResourceAccessTrackerTest, GetSharedResources_ReturnsCommonResources) {
    AddInput(nodeA_.get(), resource1_.get());
    AddInput(nodeA_.get(), resource2_.get(), 1);

    AddInput(nodeB_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource3_.get(), 1);

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    auto shared = tracker_.GetSharedResources(nodeA_.get(), nodeB_.get());
    ASSERT_EQ(shared.size(), 1u);
    EXPECT_EQ(shared[0], resource1_.get());
}

TEST_F(ResourceAccessTrackerTest, GetConflictingNodes_ReturnsAllConflicts) {
    // NodeA writes resource1
    // NodeB reads resource1 → conflicts with A
    // NodeC reads resource1 → conflicts with A
    AddOutput(nodeA_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource1_.get());
    AddInput(nodeC_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    auto conflicting = tracker_.GetConflictingNodes(nodeA_.get());
    EXPECT_EQ(conflicting.size(), 2u);
    EXPECT_TRUE(conflicting.count(nodeB_.get()) > 0);
    EXPECT_TRUE(conflicting.count(nodeC_.get()) > 0);

    // B and C don't conflict with each other (both readers)
    auto conflictingWithB = tracker_.GetConflictingNodes(nodeB_.get());
    EXPECT_EQ(conflictingWithB.size(), 1u);
    EXPECT_TRUE(conflictingWithB.count(nodeA_.get()) > 0);
}

// ============================================================================
// RESOURCE ACCESS INFO TESTS
// ============================================================================

TEST_F(ResourceAccessTrackerTest, GetAccessInfo_ReturnsCorrectInfo) {
    AddOutput(nodeA_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    auto* info = tracker_.GetAccessInfo(resource1_.get());
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->resource, resource1_.get());
    EXPECT_TRUE(info->HasWriter());
    EXPECT_FALSE(info->HasMultipleWriters());

    auto writers = info->GetWriters();
    ASSERT_EQ(writers.size(), 1u);
    EXPECT_EQ(writers[0], nodeA_.get());

    auto readers = info->GetReaders();
    ASSERT_EQ(readers.size(), 1u);
    EXPECT_EQ(readers[0], nodeB_.get());
}

TEST_F(ResourceAccessTrackerTest, HasMultipleWriters_DetectsConflict) {
    AddOutput(nodeA_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    auto* info = tracker_.GetAccessInfo(resource1_.get());
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->HasMultipleWriters());
}

// ============================================================================
// STATISTICS TESTS
// ============================================================================

TEST_F(ResourceAccessTrackerTest, GetConflictingResourceCount_CountsCorrectly) {
    // Resource1: written by A, read by B → conflict
    // Resource2: read by A, read by B → no conflict
    // Resource3: written by A only → no conflict (single accessor)

    AddOutput(nodeA_.get(), resource1_.get());
    AddInput(nodeA_.get(), resource2_.get(), 1);
    AddOutput(nodeA_.get(), resource3_.get(), 2);

    AddInput(nodeB_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource2_.get(), 1);

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());

    // Only resource1 has writer + multiple accessors
    EXPECT_EQ(tracker_.GetConflictingResourceCount(), 1u);
}

TEST_F(ResourceAccessTrackerTest, GetMaxWritersPerResource_ReturnsMax) {
    AddOutput(nodeA_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource1_.get());
    AddOutput(nodeC_.get(), resource1_.get());

    tracker_.AddNode(nodeA_.get());
    tracker_.AddNode(nodeB_.get());
    tracker_.AddNode(nodeC_.get());

    EXPECT_EQ(tracker_.GetMaxWritersPerResource(), 3u);
}

// ============================================================================
// GRAPH TOPOLOGY INTEGRATION TEST
// ============================================================================

TEST_F(ResourceAccessTrackerTest, BuildFromTopology_TracksAllNodes) {
    AddOutput(nodeA_.get(), resource1_.get());
    AddInput(nodeB_.get(), resource1_.get());
    AddOutput(nodeB_.get(), resource2_.get());

    GraphTopology topology;
    topology.AddNode(nodeA_.get());
    topology.AddNode(nodeB_.get());
    topology.AddEdge({nodeA_.get(), 0, nodeB_.get(), 0});

    tracker_.BuildFromTopology(topology);

    EXPECT_EQ(tracker_.GetNodeCount(), 2u);
    EXPECT_EQ(tracker_.GetResourceCount(), 2u);
    EXPECT_TRUE(tracker_.HasConflict(nodeA_.get(), nodeB_.get()));
}

// ============================================================================
// MULTIPLE BUNDLES TEST
// ============================================================================

TEST_F(ResourceAccessTrackerTest, MultipleBundle_TracksAllAccesses) {
    // Simulate node with multiple bundles (array processing)
    auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(nodeA_->GetBundles());
    bundles.push_back({{resource1_.get()}, {resource2_.get()}});  // Bundle 0
    bundles.push_back({{resource1_.get()}, {resource3_.get()}});  // Bundle 1

    tracker_.AddNode(nodeA_.get());

    // Should track all resources from all bundles
    auto allResources = tracker_.GetNodeResources(nodeA_.get());
    EXPECT_EQ(allResources.size(), 3u);  // R1 (deduplicated), R2, R3

    // R1 is read twice (from both bundles), R2 and R3 are written
    auto reads = tracker_.GetNodeReads(nodeA_.get());
    EXPECT_EQ(reads.size(), 1u);  // R1 (deduplicated)

    auto writes = tracker_.GetNodeWrites(nodeA_.get());
    EXPECT_EQ(writes.size(), 2u);  // R2, R3
}
