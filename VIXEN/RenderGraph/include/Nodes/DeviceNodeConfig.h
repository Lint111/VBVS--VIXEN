#pragma once

#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

/**
 * @brief Pure constexpr resource configuration for DeviceNode
 *
 * Creates and manages Vulkan device (wraps VulkanDevice class).
 * Handles both physical device selection and logical device creation.
 *
 * Inputs: 0
 * Outputs: 2 (VULKAN_DEVICE: VulkanDevice* composite, INSTANCE: VkInstance)
 * Parameters: gpu_index (which GPU to select)
 *
 * VulkanDevice pointer provides access to:
 * - device (VkDevice logical device)
 * - gpu (VkPhysicalDevice*)
 * - gpuMemoryProperties (for memory allocation)
 * - queue, queueFamilyProperties
 * - Helper: MemoryTypeFromProperties()
 */
// Compile-time slot counts (declared early for reuse)
namespace DeviceNodeCounts {
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 2;  // VULKAN_DEVICE, INSTANCE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DeviceNodeConfig,
                      DeviceNodeCounts::INPUTS,
                      DeviceNodeCounts::OUTPUTS,
                      DeviceNodeCounts::ARRAY_MODE) {
    // Phase F: Auto-indexed output slots with full metadata
    AUTO_OUTPUT(VULKAN_DEVICE_OUT, VulkanDevicePtr,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::WriteOnly);  // Index 0 (auto)

    AUTO_OUTPUT(INSTANCE, VkInstance,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::WriteOnly);  // Index 1 (auto)

    // Compile-time parameter names
    static constexpr const char* PARAM_GPU_INDEX = "gpu_index";

    // Constructor for runtime descriptor initialization
    DeviceNodeConfig() {
        // VulkanDevice pointer (composite wrapper)
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Instance handle
        HandleDescriptor instanceDesc{"VkInstance"};
        INIT_OUTPUT_DESC(INSTANCE, "instance", ResourceLifetime::Persistent, instanceDesc);
    }

    // Compile-time validation
    static_assert(INPUT_COUNT == DeviceNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == DeviceNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == DeviceNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_OUT_Slot::index == 0, "VULKAN_DEVICE must be at index 0");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE must not be nullable");
    static_assert(INSTANCE_Slot::index == 1, "INSTANCE must be at index 1");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE must not be nullable");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
};

// Compile-time verification
static_assert(DeviceNodeConfig::INPUT_COUNT == DeviceNodeCounts::INPUTS,
              "DeviceNode should have no inputs");
static_assert(DeviceNodeConfig::OUTPUT_COUNT == DeviceNodeCounts::OUTPUTS,
              "DeviceNode should have 2 outputs");

} // namespace Vixen::RenderGraph