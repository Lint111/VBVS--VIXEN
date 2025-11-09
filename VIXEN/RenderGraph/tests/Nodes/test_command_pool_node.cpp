/**
 * @file test_command_pool_node.cpp
 * @brief Tests for CommandPoolNode class
 *
 * Coverage: CommandPoolNode.h (Target: 50%+ unit, 30%+ integration)
 *
 * Unit Tests: Config validation, slot metadata
 * Integration Tests: Command pool creation, command buffer allocation, reset
 *
 * NOTE: Command pool creation requires VkDevice.
 */

#include <gtest/gtest.h>
#include "../../include/Nodes/CommandPoolNode.h"
#include "../../include/Data/Nodes/CommandPoolNodeConfig.h"

using namespace Vixen::RenderGraph;

// Define globals required by DeviceNode
std::vector<const char*> deviceExtensionNames;
std::vector<const char*> layerNames;

class CommandPoolNodeTest : public ::testing::Test {};

// Configuration Tests
TEST_F(CommandPoolNodeTest, ConfigHasOneInput) {
    EXPECT_EQ(CommandPoolNodeConfig::INPUT_COUNT, 1) << "Requires DEVICE input";
}

TEST_F(CommandPoolNodeTest, ConfigHasOneOutput) {
    EXPECT_EQ(CommandPoolNodeConfig::OUTPUT_COUNT, 1) << "Outputs COMMAND_POOL";
}

TEST_F(CommandPoolNodeTest, ConfigDeviceInputIndex) {
    EXPECT_EQ(CommandPoolNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0);
}

TEST_F(CommandPoolNodeTest, ConfigCommandPoolOutputIndex) {
    EXPECT_EQ(CommandPoolNodeConfig::COMMAND_POOL_Slot::index, 0);
}

TEST_F(CommandPoolNodeTest, ConfigDeviceIsRequired) {
    EXPECT_FALSE(CommandPoolNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(CommandPoolNodeTest, ConfigCommandPoolIsRequired) {
    EXPECT_FALSE(CommandPoolNodeConfig::COMMAND_POOL_Slot::nullable);
}

TEST_F(CommandPoolNodeTest, ConfigDeviceTypeIsVulkanDevicePtr) {
    bool isCorrect = std::is_same_v<
        CommandPoolNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*
    >;
    EXPECT_TRUE(isCorrect);
}

TEST_F(CommandPoolNodeTest, ConfigCommandPoolTypeIsVkCommandPool) {
    bool isCorrect = std::is_same_v<
        CommandPoolNodeConfig::COMMAND_POOL_Slot::Type,
        VkCommandPool
    >;
    EXPECT_TRUE(isCorrect);
}

// Slot Metadata
TEST_F(CommandPoolNodeTest, ConfigDeviceIsReadOnly) {
    EXPECT_EQ(CommandPoolNodeConfig::VULKAN_DEVICE_IN_Slot::mutability,
              SlotMutability::ReadOnly);
}

TEST_F(CommandPoolNodeTest, ConfigCommandPoolIsWriteOnly) {
    EXPECT_EQ(CommandPoolNodeConfig::COMMAND_POOL_Slot::mutability,
              SlotMutability::WriteOnly);
}

// Type System
TEST_F(CommandPoolNodeTest, TypeNameIsCommandPool) {
    CommandPoolNodeType commandPoolType;
    EXPECT_STREQ(commandPoolType.GetTypeName().c_str(), "CommandPool");
}

/**
 * Integration Test Placeholders (Require VkDevice):
 * - CreateCommandPool: vkCreateCommandPool with queue family
 * - AllocateCommandBuffers: Command buffer allocation
 * - ResetCommandPool: Pool reset capability
 * - CleanupCommandPool: vkDestroyCommandPool
 */
