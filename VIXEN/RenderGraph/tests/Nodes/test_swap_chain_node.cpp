/**
 * @file test_swap_chain_node.cpp
 * @brief Tests for SwapChainNode class
 *
 * Coverage: SwapChainNode.h (Target: 50%+ unit, 35%+ integration)
 *
 * Unit Tests: Config validation, slot metadata, parameter handling
 * Integration Tests: Swapchain creation, image acquisition, present modes, resize
 *
 * NOTE: Swapchain creation requires VkDevice and VkSurfaceKHR.
 */

#include <gtest/gtest.h>
#include "../../include/Nodes/SwapChainNode.h"
#include "../../include/Data/Nodes/SwapChainNodeConfig.h"

using namespace Vixen::RenderGraph;

class SwapChainNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(SwapChainNodeTest, ConfigHasTwoInputs) {
    EXPECT_EQ(SwapChainNodeConfig::INPUT_COUNT, 2) << "Requires DEVICE and SURFACE";
}

TEST_F(SwapChainNodeTest, ConfigHasMultipleOutputs) {
    EXPECT_GE(SwapChainNodeConfig::OUTPUT_COUNT, 2) << "Outputs SWAPCHAIN and images";
}

TEST_F(SwapChainNodeTest, ConfigDeviceInputIndex) {
    EXPECT_EQ(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceInputIndex) {
    EXPECT_EQ(SwapChainNodeConfig::SURFACE_IN_Slot::index, 1);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainOutputIndex) {
    EXPECT_EQ(SwapChainNodeConfig::SWAPCHAIN_Slot::index, 0);
}

TEST_F(SwapChainNodeTest, ConfigDeviceIsRequired) {
    EXPECT_FALSE(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceIsRequired) {
    EXPECT_FALSE(SwapChainNodeConfig::SURFACE_IN_Slot::nullable);
}

TEST_F(SwapChainNodeTest, ConfigDeviceTypeIsVulkanDevicePtr) {
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*
    >;
    EXPECT_TRUE(isCorrect);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceTypeIsVkSurfaceKHR) {
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::SURFACE_IN_Slot::Type,
        VkSurfaceKHR
    >;
    EXPECT_TRUE(isCorrect);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainTypeIsVkSwapchainKHR) {
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::SWAPCHAIN_Slot::Type,
        VkSwapchainKHR
    >;
    EXPECT_TRUE(isCorrect);
}

// Slot Metadata
TEST_F(SwapChainNodeTest, ConfigInputsAreReadOnly) {
    EXPECT_EQ(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::mutability,
              SlotMutability::ReadOnly);
    EXPECT_EQ(SwapChainNodeConfig::SURFACE_IN_Slot::mutability,
              SlotMutability::ReadOnly);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainIsWriteOnly) {
    EXPECT_EQ(SwapChainNodeConfig::SWAPCHAIN_Slot::mutability,
              SlotMutability::WriteOnly);
}

// Type System
TEST_F(SwapChainNodeTest, TypeNameIsSwapChain) {
    SwapChainNodeType swapChainType;
    EXPECT_STREQ(swapChainType.GetTypeName().c_str(), "SwapChain");
}

/**
 * Integration Test Placeholders (Require VkDevice + VkSurfaceKHR):
 * - QuerySurfaceCapabilities: Surface formats, present modes
 * - CreateSwapChain: vkCreateSwapchainKHR with selected format/mode
 * - AcquireSwapChainImages: vkGetSwapchainImagesKHR
 * - ImageAcquisition: vkAcquireNextImageKHR
 * - PresentModeSelection: FIFO, MAILBOX, IMMEDIATE
 * - SwapChainRecreation: Resize handling, old swapchain cleanup
 * - CleanupSwapChain: vkDestroySwapchainKHR
 */
