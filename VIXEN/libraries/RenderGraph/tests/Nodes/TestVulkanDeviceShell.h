#pragma once
#include "VulkanDevice.h"
#include <memory>
#include <vulkan/vulkan.h>

// A VulkanDevice shell for tests that create their own VkDevice: it carries everything production
// nodes read from the device (queue, graphics queue index, physical-device properties, memory
// properties and queue family properties). Set only `device` and a node's queue readback breaks.
// The caller must null `device` before destroying the shell, since the shell does not own it.
inline std::unique_ptr<Vixen::Vulkan::Resources::VulkanDevice> MakeTestDeviceShell(
    VkPhysicalDevice& physicalDevice, VkDevice device, VkQueue queue, uint32_t graphicsQueueIndex) {
    auto shell = std::make_unique<Vixen::Vulkan::Resources::VulkanDevice>(&physicalDevice);
    shell->device = device;
    shell->queue = queue;
    shell->graphicsQueueIndex = graphicsQueueIndex;
    vkGetPhysicalDeviceProperties(physicalDevice, &shell->gpuProperties);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &shell->gpuMemoryProperties);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    shell->queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             shell->queueFamilyProperties.data());
    return shell;
}
