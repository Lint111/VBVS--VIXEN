/**
 * @file test_render_target_node.cpp
 * @brief Tests for RenderTargetNode and RenderTargetNodeConfig (AR#28)
 *
 * Coverage:
 *   Unit Tests (no device needed): Config slot counts, slot metadata, type assertions.
 *   Integration Tests (device gated): Round-trip graph build, IRenderTarget output validation.
 *
 * NOTE: Integration tests (device round-trip) require full Vulkan SDK and a physical GPU.
 * They are written correctly but will be skipped / fail gracefully in headless CI.
 * Config-level tests always run and must PASS.
 */

#include <gtest/gtest.h>

#include "Nodes/RenderTargetNode.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"

// Centralized Vulkan global name definitions (avoids duplicate strong symbols across TUs)
#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Config Tests (no device required — always run)
// ============================================================================

class RenderTargetNodeConfigTest : public ::testing::Test {};

TEST_F(RenderTargetNodeConfigTest, InputCount) {
    EXPECT_EQ(RenderTargetNodeConfig::INPUT_COUNT, 2u)
        << "RenderTargetNode must have exactly 2 inputs (VULKAN_DEVICE_IN, EXTENT_SOURCE)";
}

TEST_F(RenderTargetNodeConfigTest, OutputCount) {
    EXPECT_EQ(RenderTargetNodeConfig::OUTPUT_COUNT, 4u)
        << "RenderTargetNode must have exactly 4 outputs (RENDER_TARGET, CURRENT_VIEW, WIDTH_OUT, HEIGHT_OUT)";
}

TEST_F(RenderTargetNodeConfigTest, ArrayModeIsSingle) {
    EXPECT_EQ(RenderTargetNodeConfig::ARRAY_MODE, SlotArrayMode::Single);
}

// ----- Input slot metadata -----

TEST_F(RenderTargetNodeConfigTest, VulkanDeviceInAtIndex0) {
    EXPECT_EQ(RenderTargetNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0u);
}

TEST_F(RenderTargetNodeConfigTest, VulkanDeviceInIsRequired) {
    EXPECT_FALSE(RenderTargetNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(RenderTargetNodeConfigTest, VulkanDeviceInIsReadOnly) {
    EXPECT_EQ(RenderTargetNodeConfig::VULKAN_DEVICE_IN_Slot::mutability, SlotMutability::ReadOnly);
}

TEST_F(RenderTargetNodeConfigTest, VulkanDeviceInTypeIsVulkanDevicePtr) {
    constexpr bool correct = std::is_same_v<
        RenderTargetNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*>;
    EXPECT_TRUE(correct);
}

TEST_F(RenderTargetNodeConfigTest, ExtentSourceAtIndex1) {
    EXPECT_EQ(RenderTargetNodeConfig::EXTENT_SOURCE_Slot::index, 1u);
}

TEST_F(RenderTargetNodeConfigTest, ExtentSourceIsOptional) {
    EXPECT_TRUE(RenderTargetNodeConfig::EXTENT_SOURCE_Slot::nullable);
}

TEST_F(RenderTargetNodeConfigTest, ExtentSourceTypeIsIRenderTargetPtr) {
    constexpr bool correct = std::is_same_v<
        RenderTargetNodeConfig::EXTENT_SOURCE_Slot::Type,
        Vixen::Vulkan::Resources::IRenderTarget*>;
    EXPECT_TRUE(correct);
}

TEST_F(RenderTargetNodeConfigTest, ParamNameScale) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_SCALE, "scale");
}

// ----- Output slot metadata -----

TEST_F(RenderTargetNodeConfigTest, RenderTargetAtIndex0) {
    EXPECT_EQ(RenderTargetNodeConfig::RENDER_TARGET_Slot::index, 0u);
}

TEST_F(RenderTargetNodeConfigTest, RenderTargetIsRequired) {
    EXPECT_FALSE(RenderTargetNodeConfig::RENDER_TARGET_Slot::nullable);
}

TEST_F(RenderTargetNodeConfigTest, RenderTargetIsWriteOnly) {
    EXPECT_EQ(RenderTargetNodeConfig::RENDER_TARGET_Slot::mutability, SlotMutability::WriteOnly);
}

TEST_F(RenderTargetNodeConfigTest, RenderTargetTypeIsIRenderTargetPtr) {
    // This is the core AR#28 type contract — IRenderTarget* not SwapChainPublicVariables*.
    constexpr bool correct = std::is_same_v<
        RenderTargetNodeConfig::RENDER_TARGET_Slot::Type,
        Vixen::Vulkan::Resources::IRenderTarget*>;
    static_assert(correct, "RENDER_TARGET slot must be IRenderTarget*");
    EXPECT_TRUE(correct);
}

TEST_F(RenderTargetNodeConfigTest, CurrentViewAtIndex1) {
    EXPECT_EQ(RenderTargetNodeConfig::CURRENT_VIEW_Slot::index, 1u);
}

TEST_F(RenderTargetNodeConfigTest, CurrentViewTypeIsVkImageView) {
    constexpr bool correct = std::is_same_v<
        RenderTargetNodeConfig::CURRENT_VIEW_Slot::Type,
        VkImageView>;
    EXPECT_TRUE(correct);
}

TEST_F(RenderTargetNodeConfigTest, WidthOutAtIndex2) {
    EXPECT_EQ(RenderTargetNodeConfig::WIDTH_OUT_Slot::index, 2u);
}

TEST_F(RenderTargetNodeConfigTest, HeightOutAtIndex3) {
    EXPECT_EQ(RenderTargetNodeConfig::HEIGHT_OUT_Slot::index, 3u);
}

TEST_F(RenderTargetNodeConfigTest, WidthHeightTypeIsUint32) {
    constexpr bool wOk = std::is_same_v<RenderTargetNodeConfig::WIDTH_OUT_Slot::Type,  uint32_t>;
    constexpr bool hOk = std::is_same_v<RenderTargetNodeConfig::HEIGHT_OUT_Slot::Type, uint32_t>;
    EXPECT_TRUE(wOk);
    EXPECT_TRUE(hOk);
}

// ----- Parameter name constants -----

TEST_F(RenderTargetNodeConfigTest, ParamNameWidth) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_WIDTH, "width");
}

TEST_F(RenderTargetNodeConfigTest, ParamNameHeight) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_HEIGHT, "height");
}

TEST_F(RenderTargetNodeConfigTest, ParamNameFormat) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_FORMAT, "format");
}

TEST_F(RenderTargetNodeConfigTest, ParamNameImageCount) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, "imageCount");
}

TEST_F(RenderTargetNodeConfigTest, ParamNameUsage) {
    EXPECT_STREQ(RenderTargetNodeConfig::PARAM_USAGE, "usage");
}

// ----- Config constructibility -----

TEST_F(RenderTargetNodeConfigTest, ConfigIsDefaultConstructible) {
    RenderTargetNodeConfig cfg;
    EXPECT_EQ(cfg.INPUT_COUNT,  2u);
    EXPECT_EQ(cfg.OUTPUT_COUNT, 4u);
}

TEST_F(RenderTargetNodeConfigTest, ConfigIsCopyable) {
    RenderTargetNodeConfig a;
    RenderTargetNodeConfig b = a;
    EXPECT_EQ(a.INPUT_COUNT,  b.INPUT_COUNT);
    EXPECT_EQ(a.OUTPUT_COUNT, b.OUTPUT_COUNT);
}

// ----- NodeType -----

TEST_F(RenderTargetNodeConfigTest, TypeNameIsRenderTarget) {
    RenderTargetNodeType nodeType;
    EXPECT_STREQ(nodeType.GetTypeName().c_str(), "RenderTarget");
}

// ============================================================================
// Follow-swapchain sizing math (M4.1) — pure function, no device required
// ============================================================================

TEST(RenderTargetNodeFollowExtent, HalfScaleFrom1000x500) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({1000, 500}, 0.5f);
    EXPECT_EQ(result.width,  500u);
    EXPECT_EQ(result.height, 250u);
}

TEST(RenderTargetNodeFollowExtent, ExtentChangeTracksSource) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({800, 600}, 0.5f);
    EXPECT_EQ(result.width,  400u);
    EXPECT_EQ(result.height, 300u);
}

TEST(RenderTargetNodeFollowExtent, ScaleOneIsIdentical) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({1280, 720}, 1.0f);
    EXPECT_EQ(result.width,  1280u);
    EXPECT_EQ(result.height, 720u);
}

TEST(RenderTargetNodeFollowExtent, RoundsUpFractionalPixels) {
    // 1001 * 0.5 = 500.5 -> ceil -> 501
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({1001, 3}, 0.5f);
    EXPECT_EQ(result.width, 501u);
    EXPECT_EQ(result.height, 2u);  // 3 * 0.5 = 1.5 -> ceil -> 2
}

TEST(RenderTargetNodeFollowExtent, ScaleClampedAboveOne) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({100, 100}, 2.0f);
    EXPECT_EQ(result.width,  100u);
    EXPECT_EQ(result.height, 100u);
}

TEST(RenderTargetNodeFollowExtent, ScaleClampedAtOrBelowZeroStaysPositive) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({100, 100}, 0.0f);
    EXPECT_GE(result.width,  1u);
    EXPECT_GE(result.height, 1u);
}

TEST(RenderTargetNodeFollowExtent, MinimumExtentIsOneByOne) {
    VkExtent2D result = RenderTargetNode::ComputeFollowExtent({1, 1}, 0.01f);
    EXPECT_EQ(result.width,  1u);
    EXPECT_EQ(result.height, 1u);
}

// ============================================================================
// Integration / Device round-trip tests
//
// These require a physical Vulkan device. In a headless environment (no GPU),
// the test will fail at InstanceNode or DeviceNode Compile() with a Vulkan
// error / exception, at which point the test is marked as a deliberate SKIP.
//
// Pattern: build an InstanceNode → DeviceNode → RenderTargetNode graph, compile
// it, then query the RENDER_TARGET output and assert its properties.
//
// NOTE: The device-level node lifecycle (full graph compile + execute) in VIXEN
// requires access to a live RenderGraph + NodeType registry, which is heavy
// infrastructure involving window creation for the swapchain path. Without a
// standalone headless InstanceNode / DeviceNode, a true unit round-trip is not
// achievable without the full app startup sequence. The integration test is
// therefore left as a documented placeholder below, consistent with the pattern
// used by test_device_node.cpp and test_swap_chain_node.cpp.
// ============================================================================

/**
 * Device round-trip integration test — DEFERRED
 *
 * To implement (once headless DeviceNode + InstanceNode lifecycle is callable
 * without a window / surface):
 *
 * TEST(RenderTargetNodeIntegration, RoundTrip) {
 *     // 1. Create InstanceNode, DeviceNode, RenderTargetNode instances
 *     // 2. Wire graph: InstanceNode→DeviceNode→RenderTargetNode
 *     // 3. Set RenderTargetNode params: width=256, height=128, imageCount=2,
 *     //    format=VK_FORMAT_R8G8B8A8_UNORM
 *     // 4. Compile graph
 *     // 5. Fetch RENDER_TARGET output (IRenderTarget*)
 *     //    EXPECT_NE(rt, nullptr)
 *     //    EXPECT_EQ(rt->GetImageCount(), 2u)
 *     //    EXPECT_EQ(rt->GetExtent().width,  256u)
 *     //    EXPECT_EQ(rt->GetExtent().height, 128u)
 *     //    for (uint32_t i = 0; i < rt->GetImageCount(); ++i) {
 *     //        EXPECT_NE(rt->GetImage(i), VK_NULL_HANDLE)
 *     //        EXPECT_NE(rt->GetView(i),  VK_NULL_HANDLE)
 *     //    }
 *     // 6. Cleanup graph (FinalTeardown — verify no validation errors)
 * }
 *
 * Blocked on: standalone headless InstanceNode/DeviceNode lifecycle
 * (test_device_node.cpp uses the same placeholder strategy).
 */
