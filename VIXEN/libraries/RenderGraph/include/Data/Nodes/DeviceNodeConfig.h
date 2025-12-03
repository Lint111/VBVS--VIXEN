#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

/**
 * @brief Pure constexpr resource configuration for DeviceNode
 *
 * Creates and manages Vulkan device (wraps VulkanDevice class).
 * Handles both physical device selection and logical device creation.
 *
 * Inputs: 1 (INSTANCE: VkInstance from InstanceNode)
 * Outputs: 2 (VULKAN_DEVICE: VulkanDevice* composite, INSTANCE: VkInstance passthrough)
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
    static constexpr size_t INPUTS = 1;   // INSTANCE input
    static constexpr size_t OUTPUTS = 2;  // VULKAN_DEVICE, INSTANCE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(DeviceNodeConfig,
                      DeviceNodeCounts::INPUTS,
                      DeviceNodeCounts::OUTPUTS,
                      DeviceNodeCounts::ARRAY_MODE) {
    // Phase F: Input slots with full metadata
    INPUT_SLOT(INSTANCE_IN, VkInstance, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Phase F: Output slots with full metadata
    OUTPUT_SLOT(VULKAN_DEVICE_OUT, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(INSTANCE_OUT, VkInstance, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Compile-time parameter names
    static constexpr const char* PARAM_GPU_INDEX = "gpu_index";

    // Constructor for runtime descriptor initialization
    DeviceNodeConfig() {
        // Instance input
        HandleDescriptor instanceInputDesc{"VkInstance"};
        INIT_INPUT_DESC(INSTANCE_IN, "instance_in", ResourceLifetime::Persistent, instanceInputDesc);

        // VulkanDevice pointer (composite wrapper)
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_OUTPUT_DESC(VULKAN_DEVICE_OUT, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Instance handle (passthrough)
        HandleDescriptor instanceOutputDesc{"VkInstance"};
        INIT_OUTPUT_DESC(INSTANCE_OUT, "instance_out", ResourceLifetime::Persistent, instanceOutputDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DeviceNodeConfig, DeviceNodeCounts);

    static_assert(INSTANCE_IN_Slot::index == 0, "INSTANCE_IN must be at index 0");
    static_assert(!INSTANCE_IN_Slot::nullable, "INSTANCE_IN must not be nullable");
    static_assert(VULKAN_DEVICE_OUT_Slot::index == 0, "VULKAN_DEVICE must be at index 0");
    static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE must not be nullable");
    static_assert(INSTANCE_OUT_Slot::index == 1, "INSTANCE_OUT must be at index 1");
    static_assert(!INSTANCE_OUT_Slot::nullable, "INSTANCE_OUT must not be nullable");

    // Type validations
    static_assert(std::is_same_v<INSTANCE_IN_Slot::Type, VkInstance>);
    static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<INSTANCE_OUT_Slot::Type, VkInstance>);
};

} // namespace Vixen::RenderGraph
