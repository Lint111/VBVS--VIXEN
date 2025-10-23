#pragma once
#include "Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts (declared early for reuse)
namespace CommandPoolNodeCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

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
CONSTEXPR_NODE_CONFIG(CommandPoolNodeConfig, 
                      CommandPoolNodeCounts::INPUTS, 
                      CommandPoolNodeCounts::OUTPUTS, 
                      CommandPoolNodeCounts::ARRAY_MODE) {
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
        CommandPoolDescriptor commandPoolDesc{};
        commandPoolDesc.flags = 0;
        commandPoolDesc.queueFamilyIndex = 0;
        INIT_OUTPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Input descriptor for DeviceObj (uses HandleDescriptor for VkDevice)
        HandleDescriptor deviceObjDesc{"VkDevice"};
        INIT_INPUT_DESC(DeviceObj, "device_obj", ResourceLifetime::Persistent, deviceObjDesc);
    }

    // Compile-time validation using declared constants
    static_assert(INPUT_COUNT == NUM_INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == NUM_OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == SlotArrayMode::Single, "Array mode mismatch");
    
    static_assert(COMMAND_POOL_Slot::index == 0, "COMMAND_POOL must be at index 0");
    static_assert(!COMMAND_POOL_Slot::nullable, "COMMAND_POOL must not be nullable");
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>, "COMMAND_POOL must be VkCommandPool");

    static_assert(DeviceObj_Slot::index == 0, "DeviceObj input must be at index 0");
    static_assert(!DeviceObj_Slot::nullable, "DeviceObj input is required");
    
    // Validate counts match expectations
    static_assert(INPUT_COUNT == CommandPoolNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == CommandPoolNodeCounts::OUTPUTS, "Output count mismatch");
};

// Global compile-time validations (reusing same constants)
static_assert(CommandPoolNodeConfig::INPUT_COUNT == CommandPoolNodeCounts::INPUTS,
              "CommandPoolNode input count validation");
static_assert(CommandPoolNodeConfig::OUTPUT_COUNT == CommandPoolNodeCounts::OUTPUTS,
              "CommandPoolNode output count validation");

} // namespace Vixen::RenderGraph