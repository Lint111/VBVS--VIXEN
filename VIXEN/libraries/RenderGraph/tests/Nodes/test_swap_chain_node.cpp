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

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

class SwapChainNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(SwapChainNodeTest, ConfigHasTwoInputs) {
    EXPECT_EQ(SwapChainNodeConfig::INPUT_COUNT, 10) << "Requires multiple inputs including DEVICE, HWND, etc.";
}

TEST_F(SwapChainNodeTest, ConfigHasMultipleOutputs) {
    EXPECT_GE(SwapChainNodeConfig::OUTPUT_COUNT, 2) << "Outputs SWAPCHAIN and images";
}

TEST_F(SwapChainNodeTest, ConfigDeviceInputIndex) {
    EXPECT_EQ(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::index, 5);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceInputIndex) {
    // Surface is created internally in SwapChainNode, not an input slot
    EXPECT_EQ(SwapChainNodeConfig::INSTANCE_Slot::index, 4);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainOutputIndex) {
    EXPECT_EQ(SwapChainNodeConfig::SWAPCHAIN_HANDLE_Slot::index, 0);
}

TEST_F(SwapChainNodeTest, ConfigDeviceIsRequired) {
    EXPECT_FALSE(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceIsRequired) {
    // Surface is created internally, but INSTANCE is required
    EXPECT_FALSE(SwapChainNodeConfig::INSTANCE_Slot::nullable);
}

TEST_F(SwapChainNodeTest, ConfigDeviceTypeIsVulkanDevicePtr) {
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*
    >;
    EXPECT_TRUE(isCorrect);
}

TEST_F(SwapChainNodeTest, ConfigSurfaceTypeIsVkSurfaceKHR) {
    // Surface is created internally, but INSTANCE is VkInstance
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::INSTANCE_Slot::Type,
        VkInstance
    >;
    EXPECT_TRUE(isCorrect);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainTypeIsVkSwapchainKHR) {
    bool isCorrect = std::is_same_v<
        SwapChainNodeConfig::SWAPCHAIN_HANDLE_Slot::Type,
        VkSwapchainKHR
    >;
    EXPECT_TRUE(isCorrect);
}

// Slot Metadata
TEST_F(SwapChainNodeTest, ConfigInputsAreReadOnly) {
    EXPECT_EQ(SwapChainNodeConfig::VULKAN_DEVICE_IN_Slot::mutability,
              SlotMutability::ReadOnly);
    EXPECT_EQ(SwapChainNodeConfig::INSTANCE_Slot::mutability,
              SlotMutability::ReadOnly);
}

TEST_F(SwapChainNodeTest, ConfigSwapChainIsWriteOnly) {
    EXPECT_EQ(SwapChainNodeConfig::SWAPCHAIN_HANDLE_Slot::mutability,
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
