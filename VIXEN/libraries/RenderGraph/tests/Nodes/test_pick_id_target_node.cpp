/**
 * @file test_pick_id_target_node.cpp
 * @brief Tests for PickIdTargetNode and PickIdTargetNodeConfig (AR#35 GPU picking P1)
 *
 * Coverage:
 *   Unit Tests (no device needed): Config slot counts, slot metadata, type assertions,
 *                                  and the pickID packing contract (brick<<10 | voxel).
 *   Integration Tests (device gated): deferred — see note at bottom (same strategy as
 *                                     test_instance_buffer_node.cpp / test_render_target_node.cpp).
 *
 * Config-level tests always run and must PASS.
 */

#include <gtest/gtest.h>

#include "Nodes/PickIdTargetNode.h"
#include "Data/Nodes/PickIdTargetNodeConfig.h"

// Centralized Vulkan global name definitions (avoids duplicate strong symbols across TUs)
#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Config Tests (no device required — always run)
// ============================================================================

class PickIdTargetNodeConfigTest : public ::testing::Test {};

TEST_F(PickIdTargetNodeConfigTest, InputCount) {
    EXPECT_EQ(PickIdTargetNodeConfig::INPUT_COUNT, 5u)
        << "PickIdTargetNode must have 5 inputs (device, command pool, width, height, frame index)";
}

TEST_F(PickIdTargetNodeConfigTest, OutputCount) {
    EXPECT_EQ(PickIdTargetNodeConfig::OUTPUT_COUNT, 2u)
        << "PickIdTargetNode must have 2 outputs (ID_IMAGE_VIEW, ID_IMAGE)";
}

TEST_F(PickIdTargetNodeConfigTest, ArrayModeIsSingle) {
    EXPECT_EQ(PickIdTargetNodeConfig::ARRAY_MODE, SlotArrayMode::Single);
}

// ----- Input slot metadata -----

TEST_F(PickIdTargetNodeConfigTest, VulkanDeviceInAtIndex0Required) {
    EXPECT_EQ(PickIdTargetNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0u);
    EXPECT_FALSE(PickIdTargetNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
    constexpr bool correct = std::is_same_v<
        PickIdTargetNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*>;
    EXPECT_TRUE(correct);
}

TEST_F(PickIdTargetNodeConfigTest, CommandPoolAtIndex1IsDependency) {
    EXPECT_EQ(PickIdTargetNodeConfig::COMMAND_POOL_Slot::index, 1u);
    // Dependency role: the one-shot UNDEFINED->GENERAL transition runs at Compile, not per-frame.
    EXPECT_EQ(PickIdTargetNodeConfig::COMMAND_POOL_Slot::role, SlotRole::Dependency);
    constexpr bool correct = std::is_same_v<
        PickIdTargetNodeConfig::COMMAND_POOL_Slot::Type, VkCommandPool>;
    EXPECT_TRUE(correct);
}

TEST_F(PickIdTargetNodeConfigTest, WidthHeightAtIndex2And3) {
    EXPECT_EQ(PickIdTargetNodeConfig::WIDTH_Slot::index,  2u);
    EXPECT_EQ(PickIdTargetNodeConfig::HEIGHT_Slot::index, 3u);
    constexpr bool wOk = std::is_same_v<PickIdTargetNodeConfig::WIDTH_Slot::Type,  uint32_t>;
    constexpr bool hOk = std::is_same_v<PickIdTargetNodeConfig::HEIGHT_Slot::Type, uint32_t>;
    EXPECT_TRUE(wOk);
    EXPECT_TRUE(hOk);
}

TEST_F(PickIdTargetNodeConfigTest, CurrentFrameIndexAtIndex4IsExecute) {
    EXPECT_EQ(PickIdTargetNodeConfig::CURRENT_FRAME_INDEX_Slot::index, 4u);
    // Execute role: advances the ring + selects the exposed view every frame.
    EXPECT_EQ(PickIdTargetNodeConfig::CURRENT_FRAME_INDEX_Slot::role, SlotRole::Execute);
}

// ----- Output slot metadata -----

TEST_F(PickIdTargetNodeConfigTest, IdImageViewAtIndex0RequiredVkImageView) {
    EXPECT_EQ(PickIdTargetNodeConfig::ID_IMAGE_VIEW_Slot::index, 0u);
    EXPECT_FALSE(PickIdTargetNodeConfig::ID_IMAGE_VIEW_Slot::nullable);
    // Core contract: the binding-9 storage image is wired as a plain VkImageView (Execute-only),
    // exactly like the swapchain output at binding 0.
    constexpr bool correct = std::is_same_v<
        PickIdTargetNodeConfig::ID_IMAGE_VIEW_Slot::Type, VkImageView>;
    static_assert(correct, "ID_IMAGE_VIEW slot must be VkImageView");
    EXPECT_TRUE(correct);
}

TEST_F(PickIdTargetNodeConfigTest, IdImageAtIndex1VkImage) {
    EXPECT_EQ(PickIdTargetNodeConfig::ID_IMAGE_Slot::index, 1u);
    constexpr bool correct = std::is_same_v<
        PickIdTargetNodeConfig::ID_IMAGE_Slot::Type, VkImage>;
    EXPECT_TRUE(correct);
}

// ----- Config constructibility -----

TEST_F(PickIdTargetNodeConfigTest, ConfigIsDefaultConstructible) {
    PickIdTargetNodeConfig cfg;
    EXPECT_EQ(cfg.INPUT_COUNT,  5u);
    EXPECT_EQ(cfg.OUTPUT_COUNT, 2u);
}

// ----- NodeType -----

TEST_F(PickIdTargetNodeConfigTest, TypeNameIsPickIdTarget) {
    PickIdTargetNodeType nodeType;
    EXPECT_STREQ(nodeType.GetTypeName().c_str(), "PickIdTarget");
}

// ============================================================================
// pickID packing contract (no device required — always run)
//
// The shaders write pickID = (brickIndex << 10) | (voxelLinearIdx & 0x3FF) on a hit, and
// 0xFFFFFFFF on a miss. The CPU readback (P2) will decode brick = id >> 10, voxel = id & 0x3FF.
// These tests assert that round-trip directly — a tiny recomputation of the documented contract,
// guarding the bit layout the GPU and CPU sides must agree on.
// ============================================================================

namespace {
constexpr uint32_t PackPickID(uint32_t brick, uint32_t voxel) {
    return (brick << 10u) | (voxel & 0x3FFu);
}
constexpr uint32_t DecodeBrick(uint32_t id) { return id >> 10u; }
constexpr uint32_t DecodeVoxel(uint32_t id) { return id & 0x3FFu; }
constexpr uint32_t kMissSentinel = 0xFFFFFFFFu;
} // namespace

TEST(PickIdPacking, RoundTripBasic) {
    const uint32_t id = PackPickID(1234u, 511u);
    EXPECT_EQ(DecodeBrick(id), 1234u);
    EXPECT_EQ(DecodeVoxel(id), 511u);
}

TEST(PickIdPacking, VoxelUsesLow10Bits) {
    // voxelLinearIdx is 0..511 (8x8x8), which fits in 9 bits; the field is masked to 10 bits.
    for (uint32_t v = 0; v < 512u; ++v) {
        const uint32_t id = PackPickID(7u, v);
        EXPECT_EQ(DecodeVoxel(id), v) << "voxel=" << v;
        EXPECT_EQ(DecodeBrick(id), 7u) << "voxel=" << v;
    }
}

TEST(PickIdPacking, BrickZeroIsValid) {
    // brickIndex 0 is a valid brick (the shaders only reject SVO_INVALID_INDEX), so a packed id
    // with brick 0 must decode cleanly and not collide with the miss sentinel.
    const uint32_t id = PackPickID(0u, 42u);
    EXPECT_EQ(DecodeBrick(id), 0u);
    EXPECT_EQ(DecodeVoxel(id), 42u);
    EXPECT_NE(id, kMissSentinel);
}

TEST(PickIdPacking, MissSentinelDistinctFromHits) {
    // The sentinel must differ from any plausible hit. (A hit only reaches 0xFFFFFFFF if both
    // brick and voxel fields are saturated, i.e. brick = 0x3FFFFF AND voxel = 0x3FF — far beyond
    // any real scene's brick count, so the sentinel is safe in practice.)
    EXPECT_NE(PackPickID(0u, 0u),     kMissSentinel);
    EXPECT_NE(PackPickID(1u, 0u),     kMissSentinel);
    EXPECT_NE(PackPickID(1000u, 256u),kMissSentinel);
}

// ============================================================================
// Integration / Device round-trip — DEFERRED
//
// Requires a physical Vulkan device + headless DeviceNode/CommandPoolNode lifecycle. Same
// placeholder strategy as test_instance_buffer_node.cpp and test_render_target_node.cpp.
//
// TEST(PickIdTargetNodeIntegration, RoundTrip) {
//     // 1. Wire Instance->Device->CommandPool->PickIdTarget (+ width/height)
//     // 2. Compile; fetch ID_IMAGE_VIEW / ID_IMAGE / EXTENT
//     //    EXPECT_NE(view,  VK_NULL_HANDLE); EXPECT_NE(image, VK_NULL_HANDLE)
//     //    EXPECT_EQ(extent.width/height, requested)
//     // 3. (Optional) verify the image is R32_UINT and in GENERAL layout (no VUIDs on dispatch)
//     // 4. Cleanup (FinalTeardown — verify images freed, no validation errors)
// }
// ============================================================================
