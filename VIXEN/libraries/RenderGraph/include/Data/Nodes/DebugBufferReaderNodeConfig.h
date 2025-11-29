#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts
namespace DebugBufferReaderNodeCounts {
    static constexpr size_t INPUTS = 3;
    static constexpr size_t OUTPUTS = 0;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Configuration for DebugBufferReaderNode
 *
 * Reads GPU debug buffers (like ray traversal samples) back to CPU
 * and exports them for analysis.
 *
 * Inputs: 3 (VULKAN_DEVICE_IN, COMMAND_POOL, DEBUG_BUFFER)
 * Outputs: 0 (data is exported to console/file, not passed through graph)
 *
 * The node reads the debug buffer, parses the DebugRaySample data,
 * and exports to console, CSV, or JSON based on configuration.
 */
CONSTEXPR_NODE_CONFIG(DebugBufferReaderNodeConfig,
                      DebugBufferReaderNodeCounts::INPUTS,
                      DebugBufferReaderNodeCounts::OUTPUTS,
                      DebugBufferReaderNodeCounts::ARRAY_MODE) {
    // ===== INPUTS (3) =====

    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(DEBUG_BUFFER, VkBuffer, 2,
        SlotNullability::Optional,  // Optional - node does nothing if not provided
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_OUTPUT_PATH = "output_path";
    static constexpr const char* PARAM_MAX_SAMPLES = "max_samples";
    static constexpr const char* PARAM_EXPORT_FORMAT = "export_format";  // "console", "csv", "json", "all"
    static constexpr const char* PARAM_AUTO_EXPORT = "auto_export";

    // Constructor for runtime descriptor initialization
    DebugBufferReaderNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Debug buffer - SSBO containing DebugRaySample array
        BufferDescriptor debugBufferDesc{};
        debugBufferDesc.size = 0;  // Size determined at runtime
        debugBufferDesc.usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc;
        INIT_INPUT_DESC(DEBUG_BUFFER, "debug_buffer", ResourceLifetime::Transient, debugBufferDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DebugBufferReaderNodeConfig, DebugBufferReaderNodeCounts);

    // Static assertions for slot indices
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(DEBUG_BUFFER_Slot::index == 2, "DEBUG_BUFFER must be at index 2");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<DEBUG_BUFFER_Slot::Type, VkBuffer>);
};

} // namespace Vixen::RenderGraph
