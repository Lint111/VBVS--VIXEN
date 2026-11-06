#include "RenderGraphHeavyTestFixtures.h"

#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"
#include "VulkanDevice.h"

#include <vector>

namespace Vixen::RenderGraph::Tests {

void RenderGraphHeavyVulkanTest::BeginVulkanTest(const char* applicationName) {
    VixenSelectWslGpuIcd();

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = applicationName;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const auto enabledLayers = EnabledValidationLayers();
    const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    instanceInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.ppEnabledExtensionNames = extensions;

    ASSERT_EQ(vkCreateInstance(&instanceInfo, nullptr, &instance_), VK_SUCCESS)
        << "vkCreateInstance failed — is a Vulkan device available?";
}

void RenderGraphHeavyVulkanTest::CreateTestLogicalDevice(
    bool enableSynchronization2, bool enableStorageImageWriteWithoutFormat) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    ASSERT_GT(queueFamilyCount, 0u);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice_, &queueFamilyCount, queueFamilies.data());

    bool foundComputeQueue = false;
    for (uint32_t index = 0; index < queueFamilyCount; ++index) {
        if ((queueFamilies[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
            queueFamily_ = index;
            foundComputeQueue = true;
            break;
        }
    }
    ASSERT_TRUE(foundComputeQueue) << "No compute queue family on the selected device";

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceSynchronization2Features synchronization2{};
    synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures features{};
    features.shaderStorageImageWriteWithoutFormat =
        enableStorageImageWriteWithoutFormat ? VK_TRUE : VK_FALSE;

    const char* extensions[] = {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = enableSynchronization2 ? &synchronization2 : nullptr;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = enableStorageImageWriteWithoutFormat ? &features : nullptr;
    deviceInfo.enabledExtensionCount = enableSynchronization2 ? 1u : 0u;
    deviceInfo.ppEnabledExtensionNames = enableSynchronization2 ? extensions : nullptr;

    ASSERT_EQ(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &logicalDevice_), VK_SUCCESS);
    vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
}

void RenderGraphHeavyVulkanTest::CreateTestCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
}

void RenderGraphHeavyVulkanTest::EndVulkanTest() {
    if (deviceShell_) {
        deviceShell_->device = VK_NULL_HANDLE;
        deviceShell_.reset();
    }
    if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    if (logicalDevice_ != VK_NULL_HANDLE) {
        vkDestroyDevice(logicalDevice_, nullptr);
        logicalDevice_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

}  // namespace Vixen::RenderGraph::Tests
