#pragma once

#include "Data/Core/ResourceConfig.h"
#include <vulkan/vulkan.h>

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for InstanceNode
 *
 * Creates and manages Vulkan instance (VkInstance).
 * Separates instance creation from device management to support multi-device scenarios.
 *
 * Inputs: 0
 * Outputs: 1 (INSTANCE: VkInstance)
 * Parameters: validation_layers, instance_extensions
 */
// Compile-time slot counts (declared early for reuse)
namespace InstanceNodeCounts {
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 1;  // INSTANCE
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(InstanceNodeConfig,
                      InstanceNodeCounts::INPUTS,
                      InstanceNodeCounts::OUTPUTS,
                      InstanceNodeCounts::ARRAY_MODE) {
    // Phase F: Output slots with full metadata
    OUTPUT_SLOT(INSTANCE, VkInstance, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Compile-time parameter names
    static constexpr const char* PARAM_ENABLE_VALIDATION = "enable_validation";
    static constexpr const char* PARAM_APP_NAME = "app_name";
    static constexpr const char* PARAM_ENGINE_NAME = "engine_name";

    // Constructor for runtime descriptor initialization
    InstanceNodeConfig() {
        // Instance handle
        HandleDescriptor instanceDesc{"VkInstance"};
        INIT_OUTPUT_DESC(INSTANCE, "instance", ResourceLifetime::Persistent, instanceDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(InstanceNodeConfig, InstanceNodeCounts);

    static_assert(INSTANCE_Slot::index == 0, "INSTANCE must be at index 0");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE must not be nullable");

    // Type validations
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>);
};

} // namespace Vixen::RenderGraph
