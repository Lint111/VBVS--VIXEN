#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for DeviceNode
 *
 * Creates and manages Vulkan device (wraps VulkanDevice class).
 * Handles both physical device selection and logical device creation.
 *
 * Inputs: 0
 * Outputs: 1 (DEVICE: VulkanDevice*, required)
 * Parameters: gpu_index (which GPU to select)
 */
// Compile-time slot counts (declared early for reuse)
namespace DeviceNodeCounts {
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 3;  // DEVICE, INSTANCE, PHYSICAL_DEVICE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DeviceNodeConfig, 
                      DeviceNodeCounts::INPUTS, 
                      DeviceNodeCounts::OUTPUTS, 
                      DeviceNodeCounts::ARRAY_MODE) {
    // Compile-time output slot definitions
    // Using VkDevice as type placeholder (actual output is VulkanDevice*)
    CONSTEXPR_OUTPUT(DEVICE, VkDevice, 0, false);
    CONSTEXPR_OUTPUT(INSTANCE, VkInstance, 1, false);
    CONSTEXPR_OUTPUT(PHYSICAL_DEVICE, VkPhysicalDevice, 2, false);

    // Compile-time parameter names
    static constexpr const char* PARAM_GPU_INDEX = "gpu_index";

    // Constructor for runtime descriptor initialization
    DeviceNodeConfig() {
        // Device is not an image or buffer - use undefined format
        ImageDescription deviceDesc{};
        deviceDesc.width = 0;
        deviceDesc.height = 0;
        deviceDesc.format = VK_FORMAT_UNDEFINED;
        deviceDesc.usage = ResourceUsage::None;
        INIT_OUTPUT_DESC(DEVICE, "device", ResourceLifetime::Persistent, deviceDesc);

        // Instance handle
        HandleDescriptor instanceDesc{"VkInstance"};
        INIT_OUTPUT_DESC(INSTANCE, "instance", ResourceLifetime::Persistent, instanceDesc);

        // Physical device handle
        HandleDescriptor physicalDeviceDesc{"VkPhysicalDevice"};
        INIT_OUTPUT_DESC(PHYSICAL_DEVICE, "physical_device", ResourceLifetime::Persistent, physicalDeviceDesc);
    }

    // Compile-time validation
    static_assert(INPUT_COUNT == DeviceNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == DeviceNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == DeviceNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(DEVICE_Slot::index == 0, "DEVICE must be at index 0");
    static_assert(!DEVICE_Slot::nullable, "DEVICE must not be nullable");
    static_assert(INSTANCE_Slot::index == 1, "INSTANCE must be at index 1");
    static_assert(PHYSICAL_DEVICE_Slot::index == 2, "PHYSICAL_DEVICE must be at index 2");
};

// Compile-time verification
static_assert(DeviceNodeConfig::INPUT_COUNT == 0,
              "DeviceNode should have no inputs");
static_assert(DeviceNodeConfig::OUTPUT_COUNT == 3,
              "DeviceNode should have 3 outputs");

} // namespace Vixen::RenderGraph