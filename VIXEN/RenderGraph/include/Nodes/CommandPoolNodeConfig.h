#pragma once
#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for CommandPoolNode
 *
 * ALL type information is resolved at compile time.
 * Runtime code is just array[0] access - zero overhead.
 *
 * Inputs: 1 (DeviceObj: VulkanDevice*, required)
 * Outputs: 1 (COMMAND_POOL: VkCommandPool, required)
 * Parameters: queue_family_index
 */
CONSTEXPR_NODE_CONFIG(CommandPoolNodeConfig, 1, 1, false) {
    // Compile-time output slot definition
    // This creates:
    // - Type alias: COMMAND_POOL_Slot = ResourceSlot<VkCommandPool, 0, false>
    // - Constexpr constant: static constexpr COMMAND_POOL_Slot COMMAND_POOL{};
    CONSTEXPR_OUTPUT(COMMAND_POOL, VkCommandPool, 0, false);

    // Input: Device object (type-punned as VkDevice like other configs)
    CONSTEXPR_INPUT(DeviceObj, VkDevice, 0, false);

    // Compile-time parameter names (constexpr strings for type safety)
    static constexpr const char* PARAM_QUEUE_FAMILY_INDEX = "queue_family_index";
    static constexpr const char* INPUT_DEVICE_OBJ = "DeviceObj";

    // Constructor only needed for runtime descriptor initialization
    // (descriptors contain strings which can't be fully constexpr)
    CommandPoolNodeConfig() {
        // Runtime descriptor initialization
        // Uses compile-time constants from COMMAND_POOL slot
        BufferDescription commandPoolDesc{};
        commandPoolDesc.size = 0;
        commandPoolDesc.usage = ResourceUsage::CommandPool;
        INIT_OUTPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Input descriptor for DeviceObj
        DeviceObjectDescription deviceObjDesc{};
        INIT_INPUT_DESC(DeviceObj, "device_obj", ResourceLifetime::Persistent, deviceObjDesc);
    }

    // Optional: Compile-time validation
    static_assert(COMMAND_POOL_Slot::index == 0, "COMMAND_POOL must be at index 0");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL must not be nullable");
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>, "COMMAND_POOL must be VkCommandPool");

    static_assert(DeviceObj_Slot::index == 0, "DeviceObj input must be at index 0");
    static_assert(!DeviceObj_Slot::nullable, "DeviceObj input is required");
};

// Global compile-time validations
static_assert(CommandPoolNodeConfig::INPUT_COUNT == 1,
              "CommandPoolNode should have 1 input");
static_assert(CommandPoolNodeConfig::OUTPUT_COUNT == 1,
              "CommandPoolNode should have 1 output");

} // namespace Vixen::RenderGraph