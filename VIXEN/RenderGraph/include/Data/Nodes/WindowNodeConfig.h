#pragma once

#include "Data/Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts (declared early for reuse)
namespace WindowNodeCounts {
    static constexpr size_t INPUTS = 1;   // INSTANCE
    static constexpr size_t OUTPUTS = 5;  // SURFACE, HWND, HINSTANCE, WIDTH, HEIGHT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for WindowNode
 *
 * ALL type information is resolved at compile time.
 * Runtime code is just array[0] access - zero overhead.
 *
 * Inputs: 1
 *   - INSTANCE (VkInstance) - Vulkan instance (from DeviceNode)
 * Outputs: 5
 *   - SURFACE (VkSurfaceKHR) - Vulkan surface
 *   - HWND (::HWND) - Windows window handle
 *   - HINSTANCE (::HINSTANCE) - Windows instance handle
 *   - WIDTH (uint32_t) - Window width
 *   - HEIGHT (uint32_t) - Window height
 * Parameters: width, height
 */
CONSTEXPR_NODE_CONFIG(WindowNodeConfig,
                      WindowNodeCounts::INPUTS,
                      WindowNodeCounts::OUTPUTS,
                      WindowNodeCounts::ARRAY_MODE) {
    // Phase F: Input slots with full metadata
    INPUT_SLOT(INSTANCE, VkInstance, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Phase F: Output slots with full metadata
    OUTPUT_SLOT(SURFACE, VkSurfaceKHR, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HWND_OUT, ::HWND, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HINSTANCE_OUT, ::HINSTANCE, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(WIDTH_OUT, uint32_t, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HEIGHT_OUT, uint32_t, 4,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Compile-time parameter names (constexpr strings for type safety)
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";

    // Constructor only needed for runtime descriptor initialization
    // (descriptors contain strings which can't be fully constexpr)
    WindowNodeConfig() {
        // Runtime descriptor initialization

        // Instance handle input
        HandleDescriptor instanceDesc{"VkInstance"};
        INIT_INPUT_DESC(INSTANCE, "instance", ResourceLifetime::Persistent, instanceDesc);

        // Output descriptors
        ImageDescription surfaceDesc{};
        surfaceDesc.width = 0;
        surfaceDesc.height = 0;
        surfaceDesc.format = VK_FORMAT_UNDEFINED;
        surfaceDesc.usage = ResourceUsage::ColorAttachment;
        INIT_OUTPUT_DESC(SURFACE, "surface", ResourceLifetime::Persistent, surfaceDesc);

        // HWND handle
        HandleDescriptor hwndDesc{"HWND"};
        INIT_OUTPUT_DESC(HWND_OUT, "hwnd", ResourceLifetime::Persistent, hwndDesc);

        // HINSTANCE handle
        HandleDescriptor hinstanceDesc{"HINSTANCE"};
        INIT_OUTPUT_DESC(HINSTANCE_OUT, "hinstance", ResourceLifetime::Persistent, hinstanceDesc);

        // Width parameter as output
        BufferDescription widthDesc{};
        INIT_OUTPUT_DESC(WIDTH_OUT, "width", ResourceLifetime::Persistent, widthDesc);

        // Height parameter as output
        BufferDescription heightDesc{};
        INIT_OUTPUT_DESC(HEIGHT_OUT, "height", ResourceLifetime::Persistent, heightDesc);
    }

    // Optional: Compile-time validation
    // Input validation
    static_assert(INSTANCE_Slot::index == 0, "INSTANCE must be at index 0");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE must not be nullable");
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>, "INSTANCE must be VkInstance");

    // Output validation
    static_assert(SURFACE_Slot::index == 0, "SURFACE must be at index 0");
    static_assert(!SURFACE_Slot::nullable, "SURFACE must not be nullable");
    static_assert(std::is_same_v<SURFACE_Slot::Type, VkSurfaceKHR>, "SURFACE must be VkSurfaceKHR");

    static_assert(HWND_OUT_Slot::index == 1, "HWND_OUT must be at index 1");
    static_assert(HINSTANCE_OUT_Slot::index == 2, "HINSTANCE_OUT must be at index 2");
    static_assert(WIDTH_OUT_Slot::index == 3, "WIDTH_OUT must be at index 3");
    static_assert(HEIGHT_OUT_Slot::index == 4, "HEIGHT_OUT must be at index 4");

    // Validate counts match expectations
    static_assert(INPUT_COUNT == WindowNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == WindowNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == WindowNodeCounts::ARRAY_MODE, "Array mode mismatch");
};

// Compile-time verification (reusing same constants)
static_assert(WindowNodeConfig::INPUT_COUNT == WindowNodeCounts::INPUTS, 
              "WindowNode input count validation");
static_assert(WindowNodeConfig::OUTPUT_COUNT == WindowNodeCounts::OUTPUTS, 
              "WindowNode output count validation");

} // namespace Vixen::RenderGraph
