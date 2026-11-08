#pragma once

#include "VulkanDevice.h"

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Vixen::RenderGraph::Tests {

// Shared bring-up for the standalone RenderGraph GPU gates.  The individual tests still own
// their device-selection policy and feature-specific helpers, but the high-fanout Vulkan
// instance/device lifetime is compiled once in RenderGraphHeavyTestFixtures.cpp.
class RenderGraphHeavyVulkanTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;
    bool             softwareConfirmed_ = false;
    bool             discreteGpuSelected_ = false;
    std::unique_ptr<::Vixen::Vulkan::Resources::VulkanDevice> deviceShell_;

    void BeginVulkanTest(const char* applicationName);
    void EndVulkanTest();
    void CreateTestLogicalDevice(bool enableSynchronization2,
                                 bool enableStorageImageWriteWithoutFormat);
    void CreateTestCommandPool();
};

}  // namespace Vixen::RenderGraph::Tests
