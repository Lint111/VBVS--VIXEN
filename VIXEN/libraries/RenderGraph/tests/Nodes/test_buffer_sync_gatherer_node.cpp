/**
 * @file test_buffer_sync_gatherer_node.cpp
 * @brief Config-shape + registration tests for BufferSyncGathererNode (Sampled Lighting Inc3 M5)
 *
 * Mirrors test_push_constant_gatherer_node.cpp's own "ConfigHasCorrectInputs/
 * ConfigArrayModeIsSingle/NodeTypeRegistration/VariadicConstraints" shallow-config-shape
 * tier (that file's own deeper "Pack*" tests turn out to manually replicate the packing
 * math standalone rather than drive the real node's CompileImpl/ExecuteImpl through actual
 * Resource* wiring — this node's real correctness validation is the live render-graph run,
 * same as every other gatherer in this codebase; see the M5 gate artifact).
 */

#include <gtest/gtest.h>
#include "Nodes/BufferSyncGathererNode.h"
#include "Data/Nodes/BufferSyncGathererNodeConfig.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/NodeRegistration.h"
#include <memory>

namespace VRG = Vixen::RenderGraph;

class BufferSyncGathererNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        nodeType = std::make_unique<VRG::BufferSyncGathererNodeType>();
        node = std::make_unique<VRG::BufferSyncGathererNode>("test_buffer_sync_gatherer", nodeType.get());
    }

    void TearDown() override {
        node.reset();
        nodeType.reset();
    }

    std::unique_ptr<VRG::BufferSyncGathererNodeType> nodeType;
    std::unique_ptr<VRG::BufferSyncGathererNode> node;
};

// ============================================================================
// Config shape
// ============================================================================

TEST_F(BufferSyncGathererNodeTest, ConfigHasNoStaticInputs) {
    EXPECT_EQ(VRG::BufferSyncGathererNodeCounts::INPUTS, 0u);
}

TEST_F(BufferSyncGathererNodeTest, ConfigHasOneOutput) {
    EXPECT_EQ(VRG::BufferSyncGathererNodeCounts::OUTPUTS, 1u);
}

TEST_F(BufferSyncGathererNodeTest, ConfigArrayModeIsSingle) {
    EXPECT_EQ(VRG::BufferSyncGathererNodeCounts::ARRAY_MODE, VRG::SlotArrayMode::Single);
}

TEST_F(BufferSyncGathererNodeTest, ConfigBufferArrayOutputIndex) {
    EXPECT_EQ(VRG::BufferSyncGathererNodeConfig::BUFFER_ARRAY_Slot::index, 0u);
}

// ============================================================================
// Pre-registration
// ============================================================================

TEST_F(BufferSyncGathererNodeTest, PreRegisterBufferSlotsSetsVariadicCount) {
    node->PreRegisterBufferSlots(2);
    EXPECT_EQ(node->GetVariadicInputCount(0), 2u);
}

TEST_F(BufferSyncGathererNodeTest, PreRegisterBufferSlotsSetsExactMinMaxConstraints) {
    node->PreRegisterBufferSlots(2);
    EXPECT_EQ(node->GetMinVariadicInputs(), 2u);
    EXPECT_EQ(node->GetMaxVariadicInputs(), 2u);
}

TEST_F(BufferSyncGathererNodeTest, PreRegisteredSlotsAreBufferTyped) {
    node->PreRegisterBufferSlots(2);
    const auto* slot0 = node->GetVariadicSlotInfo(0, 0);
    const auto* slot1 = node->GetVariadicSlotInfo(1, 0);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot0->resourceType, VRG::ResourceType::Buffer);
    EXPECT_EQ(slot1->resourceType, VRG::ResourceType::Buffer);
}

TEST_F(BufferSyncGathererNodeTest, ZeroCountPreRegistrationLeavesUnconstrained) {
    // Default variadic constraints (min=0, max=SIZE_MAX, set by the constructor from
    // BufferSyncGathererNodeType's own defaults) survive an explicit PreRegisterBufferSlots(0)
    // call — mirrors the "count > 0" guard in PreRegisterBufferSlots's own implementation.
    node->PreRegisterBufferSlots(0);
    EXPECT_EQ(node->GetVariadicInputCount(0), 0u);
    EXPECT_EQ(node->GetMinVariadicInputs(), 0u);
    EXPECT_EQ(node->GetMaxVariadicInputs(), SIZE_MAX);
}

// ============================================================================
// Registration
// ============================================================================

TEST_F(BufferSyncGathererNodeTest, NodeTypeRegistration) {
    VRG::NodeTypeRegistry registry;
    VRG::RegisterAllNodes(registry);
    EXPECT_TRUE(registry.Has<VRG::BufferSyncGathererNodeType>());
}

TEST_F(BufferSyncGathererNodeTest, DefaultVariadicConstraintsAreUnbounded) {
    // A fresh node (before any PreRegisterBufferSlots call) inherits
    // BufferSyncGathererNodeType's own defaults (min=0, max=SIZE_MAX).
    auto freshNodeType = std::make_unique<VRG::BufferSyncGathererNodeType>();
    auto freshNode = std::make_unique<VRG::BufferSyncGathererNode>("fresh", freshNodeType.get());
    EXPECT_EQ(freshNode->GetMinVariadicInputs(), 0u);
    EXPECT_EQ(freshNode->GetMaxVariadicInputs(), SIZE_MAX);
}
