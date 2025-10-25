#pragma once
#include "Core/ResourceConfig.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice pointer
using VulkanDevicePtr = Vixen::Vulkan::Resources::VulkanDevice*;

// Compile-time slot counts (declared early for reuse)
namespace CommandPoolNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 2;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for CommandPoolNode
 *
 * ALL type information is resolved at compile time.
 * Runtime code is just array[0] access - zero overhead.
 *
 * Inputs: 1 (VULKAN_DEVICE: VulkanDevicePtr, required)
 * Outputs: 1 (COMMAND_POOL: VkCommandPool, required)
 * Parameters: queue_family_index
 */
CONSTEXPR_NODE_CONFIG(CommandPoolNodeConfig, 
                      CommandPoolNodeCounts::INPUTS, 
                      CommandPoolNodeCounts::OUTPUTS, 
                      CommandPoolNodeCounts::ARRAY_MODE) {
    // Compile-time output slot definition
    CONSTEXPR_OUTPUT(COMMAND_POOL, VkCommandPool, 0, false);
	CONSTEXPR_OUTPUT(VULKAN_DEVICE_OUT, VulkanDevicePtr, 1, false);

    // Input: VulkanDevice pointer (contains device, gpu, queue families, etc.)
    CONSTEXPR_INPUT(VULKAN_DEVICE_IN, VulkanDevicePtr, 0, false);



    // Compile-time parameter names (constexpr strings for type safety)
    static constexpr const char* PARAM_QUEUE_FAMILY_INDEX = "queue_family_index";

    // Constructor only needed for runtime descriptor initialization
    // (descriptors contain strings which can't be fully constexpr)
    CommandPoolNodeConfig() {
        // Initialize input descriptor
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        // Initialize output descriptor
        CommandPoolDescriptor commandPoolDesc{};
        commandPoolDesc.flags = 0;
        commandPoolDesc.queueFamilyIndex = 0;
        INIT_OUTPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);
    }

    // Compile-time validation using declared constants
    static_assert(INPUT_COUNT == CommandPoolNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == CommandPoolNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == CommandPoolNodeCounts::ARRAY_MODE, "Array mode mismatch");

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE input must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE input is required");

    static_assert(COMMAND_POOL_Slot::index == 0, "COMMAND_POOL must be at index 0");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL must not be nullable");

	static_assert(VULKAN_DEVICE_OUT_Slot::index == 1, "VULKAN_DEVICE_OUT must be at index 1");
	static_assert(!VULKAN_DEVICE_OUT_Slot::nullable, "VULKAN_DEVICE_OUT must not be nullable");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevicePtr>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
	static_assert(std::is_same_v<VULKAN_DEVICE_OUT_Slot::Type, VulkanDevicePtr>);
};

// Global compile-time validations
static_assert(CommandPoolNodeConfig::INPUT_COUNT == CommandPoolNodeCounts::INPUTS);
static_assert(CommandPoolNodeConfig::OUTPUT_COUNT == CommandPoolNodeCounts::OUTPUTS);

} // namespace Vixen::RenderGraph