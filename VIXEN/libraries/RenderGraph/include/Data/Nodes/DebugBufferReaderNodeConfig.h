#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

// Forward declarations
namespace Vixen::RenderGraph::Debug {
    class IDebugBuffer;
    class IDebugCapture;
}

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Type alias for debug types
using IDebugBuffer = Debug::IDebugBuffer;
using IDebugCapture = Debug::IDebugCapture;

// Compile-time slot counts
namespace DebugBufferReaderNodeCounts {
    static constexpr size_t INPUTS = 4;
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

    // DEBUG_CAPTURE: IDebugCapture interface for automatic detection of debug buffers
    // This accepts any type implementing IDebugCapture (e.g., DebugCaptureResource)
    INPUT_SLOT(DEBUG_CAPTURE, IDebugCapture*, 2,
        SlotNullability::Optional,  // Optional - node does nothing if not provided
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // IN_FLIGHT_FENCE: Fence to wait on before reading (ensures GPU has finished writing)
    INPUT_SLOT(IN_FLIGHT_FENCE, VkFence, 3,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_OUTPUT_PATH = "output_path";
    static constexpr const char* PARAM_MAX_SAMPLES = "max_samples";
    static constexpr const char* PARAM_EXPORT_FORMAT = "export_format";  // "console", "csv", "json", "all"
    static constexpr const char* PARAM_AUTO_EXPORT = "auto_export";
    static constexpr const char* PARAM_FRAMES_PER_EXPORT = "frames_per_export";

    // Constructor for runtime descriptor initialization
    DebugBufferReaderNodeConfig() {
        // Initialize input descriptors
        HandleDescriptor vulkanDeviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, vulkanDeviceDesc);

        CommandPoolDescriptor commandPoolDesc{};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, commandPoolDesc);

        // Debug capture interface
        HandleDescriptor debugCaptureDesc{"IDebugCapture*"};
        INIT_INPUT_DESC(DEBUG_CAPTURE, "debug_capture", ResourceLifetime::Transient, debugCaptureDesc);

        // In-flight fence for synchronization
        HandleDescriptor fenceDesc{"VkFence"};
        INIT_INPUT_DESC(IN_FLIGHT_FENCE, "in_flight_fence", ResourceLifetime::Transient, fenceDesc);
    }

    // Automated config validation
    VALIDATE_NODE_CONFIG(DebugBufferReaderNodeConfig, DebugBufferReaderNodeCounts);

    // Static assertions for slot indices
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(DEBUG_CAPTURE_Slot::index == 2, "DEBUG_CAPTURE must be at index 2");
    static_assert(IN_FLIGHT_FENCE_Slot::index == 3, "IN_FLIGHT_FENCE must be at index 3");

    // Type validations
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>);
    static_assert(std::is_same_v<COMMAND_POOL_Slot::Type, VkCommandPool>);
    static_assert(std::is_same_v<DEBUG_CAPTURE_Slot::Type, IDebugCapture*>);
    static_assert(std::is_same_v<IN_FLIGHT_FENCE_Slot::Type, VkFence>);
};

} // namespace Vixen::RenderGraph
