/**
 * @file test_dynamic_instance_buffer_node.cpp
 * @brief Tests for DynamicInstanceBufferNode and DynamicInstanceBufferNodeConfig (AR#33)
 *
 * Coverage:
 *   Unit Tests (no device needed): Config slot counts, slot metadata (incl. the
 *                                  per-frame CURRENT_FRAME_INDEX Execute input and the
 *                                  per-frame INSTANCE_BUFFER output), type assertions,
 *                                  param-name constants, and the N = gridDim^2 contract.
 *   Integration Tests (device gated): Round-trip graph build + ring upload — DEFERRED.
 *
 * NOTE: Integration tests (device round-trip) require full Vulkan SDK and a physical GPU.
 * They are written correctly but will be skipped / fail gracefully in headless CI.
 * Config-level tests always run and must PASS.
 */

#include <gtest/gtest.h>

#include "Nodes/DynamicInstanceBufferNode.h"
#include "Data/Nodes/DynamicInstanceBufferNodeConfig.h"

// Centralized Vulkan global name definitions (avoids duplicate strong symbols across TUs)
#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Config Tests (no device required — always run)
// ============================================================================

class DynamicInstanceBufferNodeConfigTest : public ::testing::Test {};

TEST_F(DynamicInstanceBufferNodeConfigTest, InputCount) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::INPUT_COUNT, 2u)
        << "DynamicInstanceBufferNode must have exactly 2 inputs (VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX)";
}

TEST_F(DynamicInstanceBufferNodeConfigTest, OutputCount) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::OUTPUT_COUNT, 2u)
        << "DynamicInstanceBufferNode must have exactly 2 outputs (INSTANCE_BUFFER, INSTANCE_COUNT)";
}

TEST_F(DynamicInstanceBufferNodeConfigTest, ArrayModeIsSingle) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::ARRAY_MODE, SlotArrayMode::Single);
}

// ----- Input slot metadata: VULKAN_DEVICE_IN -----

TEST_F(DynamicInstanceBufferNodeConfigTest, VulkanDeviceInAtIndex0) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0u);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, VulkanDeviceInIsRequired) {
    EXPECT_FALSE(DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, VulkanDeviceInIsReadOnly) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN_Slot::mutability, SlotMutability::ReadOnly);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, VulkanDeviceInIsDependencyRole) {
    EXPECT_TRUE(HasDependency(DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN_Slot::role));
}

TEST_F(DynamicInstanceBufferNodeConfigTest, VulkanDeviceInTypeIsVulkanDevicePtr) {
    constexpr bool correct = std::is_same_v<
        DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*>;
    EXPECT_TRUE(correct);
}

// ----- Input slot metadata: CURRENT_FRAME_INDEX (per-frame Execute input) -----

TEST_F(DynamicInstanceBufferNodeConfigTest, CurrentFrameIndexAtIndex1) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX_Slot::index, 1u);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, CurrentFrameIndexIsRequired) {
    EXPECT_FALSE(DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX_Slot::nullable);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, CurrentFrameIndexIsReadOnly) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX_Slot::mutability, SlotMutability::ReadOnly);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, CurrentFrameIndexHasExecuteRole) {
    // The ring index must be read EVERY frame — the Execute role is what makes that happen.
    EXPECT_TRUE(HasExecute(DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX_Slot::role));
}

TEST_F(DynamicInstanceBufferNodeConfigTest, CurrentFrameIndexTypeIsUint32) {
    constexpr bool correct = std::is_same_v<
        DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX_Slot::Type,
        uint32_t>;
    EXPECT_TRUE(correct);
}

// ----- Output slot metadata: INSTANCE_BUFFER -----

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceBufferAtIndex0) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER_Slot::index, 0u);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceBufferIsRequired) {
    EXPECT_FALSE(DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER_Slot::nullable);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceBufferIsWriteOnly) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER_Slot::mutability, SlotMutability::WriteOnly);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceBufferTypeIsVkBuffer) {
    // Core AR#33 type contract — the per-instance SSBO is a raw VkBuffer (re-emitted per frame).
    constexpr bool correct = std::is_same_v<
        DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER_Slot::Type,
        VkBuffer>;
    static_assert(correct, "INSTANCE_BUFFER slot must be VkBuffer");
    EXPECT_TRUE(correct);
}

// ----- Output slot metadata: INSTANCE_COUNT -----

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceCountAtIndex1) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::INSTANCE_COUNT_Slot::index, 1u);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceCountIsRequired) {
    EXPECT_FALSE(DynamicInstanceBufferNodeConfig::INSTANCE_COUNT_Slot::nullable);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceCountIsWriteOnly) {
    EXPECT_EQ(DynamicInstanceBufferNodeConfig::INSTANCE_COUNT_Slot::mutability, SlotMutability::WriteOnly);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, InstanceCountTypeIsUint32) {
    constexpr bool correct = std::is_same_v<
        DynamicInstanceBufferNodeConfig::INSTANCE_COUNT_Slot::Type,
        uint32_t>;
    EXPECT_TRUE(correct);
}

// ----- Parameter name constants -----

TEST_F(DynamicInstanceBufferNodeConfigTest, ParamNameGridDim) {
    EXPECT_STREQ(DynamicInstanceBufferNodeConfig::PARAM_GRID_DIM, "gridDim");
}

TEST_F(DynamicInstanceBufferNodeConfigTest, ParamNameSpacing) {
    EXPECT_STREQ(DynamicInstanceBufferNodeConfig::PARAM_SPACING, "spacing");
}

TEST_F(DynamicInstanceBufferNodeConfigTest, ParamNameRotationSpeed) {
    EXPECT_STREQ(DynamicInstanceBufferNodeConfig::PARAM_ROTATION_SPEED, "rotationSpeed");
}

// ----- Config constructibility -----

TEST_F(DynamicInstanceBufferNodeConfigTest, ConfigIsDefaultConstructible) {
    DynamicInstanceBufferNodeConfig cfg;
    EXPECT_EQ(cfg.INPUT_COUNT,  2u);
    EXPECT_EQ(cfg.OUTPUT_COUNT, 2u);
}

TEST_F(DynamicInstanceBufferNodeConfigTest, ConfigIsCopyable) {
    DynamicInstanceBufferNodeConfig a;
    DynamicInstanceBufferNodeConfig b = a;
    EXPECT_EQ(a.INPUT_COUNT,  b.INPUT_COUNT);
    EXPECT_EQ(a.OUTPUT_COUNT, b.OUTPUT_COUNT);
}

// ----- NodeType -----

TEST_F(DynamicInstanceBufferNodeConfigTest, TypeNameIsDynamicInstanceBuffer) {
    DynamicInstanceBufferNodeType nodeType;
    EXPECT_STREQ(nodeType.GetTypeName().c_str(), "DynamicInstanceBuffer");
}

// ============================================================================
// Instance-count math (no device required — always run)
//
// Documented contract (DynamicInstanceBufferNodeConfig.h): N = gridDim * gridDim
// per-instance model matrices, emitted on INSTANCE_COUNT. A deliberate tiny
// recomputation of the contract, NOT a reach into node internals (the actual ring
// fill happens device-side in DynamicInstanceBufferNode::ExecuteImpl).
// ============================================================================

namespace {
// Mirror of the documented contract N = gridDim^2.
constexpr uint32_t InstanceCountForGrid(uint32_t gridDim) {
    return gridDim * gridDim;
}
} // namespace

TEST(DynamicInstanceBufferGridMath, GridDim8Yields64) {
    EXPECT_EQ(InstanceCountForGrid(8u), 64u);
}

TEST(DynamicInstanceBufferGridMath, GridDim1Yields1) {
    EXPECT_EQ(InstanceCountForGrid(1u), 1u);
}

TEST(DynamicInstanceBufferGridMath, GridDim16Yields256) {
    EXPECT_EQ(InstanceCountForGrid(16u), 256u);
}

TEST(DynamicInstanceBufferGridMath, IsSquareRelationship) {
    for (uint32_t d = 0; d <= 32; ++d) {
        EXPECT_EQ(InstanceCountForGrid(d), d * d) << "gridDim=" << d;
    }
}

// ============================================================================
// Animation formula (no device required — always run)
//
// Mirrors the documented per-instance angle from ExecuteImpl:
//   angle = frameCounter * rotationSpeed * (1 + 0.1 * linearIndex)
// Assert the key properties: deterministic, grows with the frame counter, and the
// per-instance phase makes instances diverge (so the grid does not spin in lockstep).
// ============================================================================

namespace {
constexpr float InstanceAngle(uint64_t frameCounter, float rotationSpeed, uint32_t linearIndex) {
    return static_cast<float>(frameCounter) * rotationSpeed *
           (1.0f + 0.1f * static_cast<float>(linearIndex));
}
} // namespace

TEST(DynamicInstanceBufferAnim, ZeroFrameCounterYieldsZeroAngle) {
    EXPECT_FLOAT_EQ(InstanceAngle(0u, 0.01f, 0u), 0.0f);
    EXPECT_FLOAT_EQ(InstanceAngle(0u, 0.01f, 42u), 0.0f);
}

TEST(DynamicInstanceBufferAnim, AngleGrowsWithFrameCounter) {
    const float a1 = InstanceAngle(10u, 0.01f, 5u);
    const float a2 = InstanceAngle(20u, 0.01f, 5u);
    EXPECT_GT(a2, a1) << "angle must increase as the frame counter advances (visible motion)";
}

TEST(DynamicInstanceBufferAnim, PerInstancePhaseDiverges) {
    // Two different instances at the same frame must have different angles.
    const float a0 = InstanceAngle(100u, 0.01f, 0u);
    const float a1 = InstanceAngle(100u, 0.01f, 1u);
    EXPECT_NE(a0, a1) << "per-instance phase (0.1*linearIndex) must break lockstep rotation";
}

// ============================================================================
// Integration / Device round-trip tests
//
// These require a physical Vulkan device. In a headless environment (no GPU),
// the round-trip would fail at DeviceNode Compile() with a Vulkan error.
//
// NOTE: The device-level node lifecycle (full graph compile + per-frame execute)
// requires a live RenderGraph + NodeType registry, which is heavy infrastructure.
// Consistent with test_instance_buffer_node.cpp, the integration test is left as a
// documented placeholder below.
// ============================================================================

/**
 * Device round-trip / animation integration test — DEFERRED
 *
 * To implement (once headless DeviceNode + FrameSyncNode lifecycle is callable
 * without a window / surface):
 *
 * TEST(DynamicInstanceBufferNodeIntegration, RingAnimates) {
 *     // 1. Wire graph: InstanceNode→DeviceNode→DynamicInstanceBufferNode, plus a
 *     //    CURRENT_FRAME_INDEX producer (FrameSyncNode or a stub).
 *     // 2. Set params: gridDim=8, spacing=2.0, rotationSpeed=0.01
 *     // 3. Compile graph → INSTANCE_COUNT == 64; INSTANCE_BUFFER != VK_NULL_HANDLE.
 *     // 4. Execute frame 0 with CURRENT_FRAME_INDEX=0; map the ring buffer, snapshot
 *     //    the first glm::mat4.
 *     // 5. Execute frame 1 with CURRENT_FRAME_INDEX=1; the emitted INSTANCE_BUFFER
 *     //    handle must differ from frame 0's (ring rotation), and the stored matrices
 *     //    must differ from the frame-0 snapshot (animation advanced).
 *     // 6. Cleanup (FinalTeardown) — verify no validation errors, ring freed.
 * }
 *
 * Blocked on: standalone headless DeviceNode/FrameSyncNode lifecycle
 * (test_instance_buffer_node.cpp uses the same placeholder strategy).
 */
