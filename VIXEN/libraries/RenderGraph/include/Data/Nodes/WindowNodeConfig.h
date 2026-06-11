#pragma once

#include "Data/Core/ResourceConfig.h"

struct GLFWwindow;  // cross-platform window handle (GLFW); concrete type only needed in the .cpp

namespace Vixen::RenderGraph {

// Compile-time slot counts (declared early for reuse)
namespace WindowNodeCounts {
    static constexpr size_t INPUTS = 1;   // INSTANCE
    static constexpr size_t OUTPUTS = 4;  // SURFACE, WINDOW, WIDTH, HEIGHT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for WindowNode
 *
 * ALL type information is resolved at compile time.
 *
 * Inputs: 1
 *   - INSTANCE (VkInstance) - Vulkan instance (from DeviceNode)
 * Outputs: 4
 *   - SURFACE (VkSurfaceKHR) - Vulkan surface
 *   - WINDOW (GLFWwindow*) - cross-platform window handle (replaces the old HWND/HINSTANCE pair)
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

    OUTPUT_SLOT(WINDOW, GLFWwindow*, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(WIDTH_OUT, uint32_t, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HEIGHT_OUT, uint32_t, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Compile-time parameter names (constexpr strings for type safety)
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";

    // Constructor only needed for runtime descriptor initialization
    WindowNodeConfig() {
        // Instance handle input
        HandleDescriptor instanceDesc{"VkInstance"};
        INIT_INPUT_DESC(INSTANCE, "instance", ResourceLifetime::Persistent, instanceDesc);

        // Surface output
        ImageDescription surfaceDesc{};
        surfaceDesc.width = 0;
        surfaceDesc.height = 0;
        surfaceDesc.format = VK_FORMAT_UNDEFINED;
        surfaceDesc.usage = ResourceUsage::ColorAttachment;
        INIT_OUTPUT_DESC(SURFACE, "surface", ResourceLifetime::Persistent, surfaceDesc);

        // Cross-platform window handle
        HandleDescriptor windowDesc{"GLFWwindow"};
        INIT_OUTPUT_DESC(WINDOW, "window", ResourceLifetime::Persistent, windowDesc);

        // Width parameter as output (value type - transient)
        BufferDescription widthDesc{};
        INIT_OUTPUT_DESC(WIDTH_OUT, "width", ResourceLifetime::Transient, widthDesc);

        // Height parameter as output (value type - transient)
        BufferDescription heightDesc{};
        INIT_OUTPUT_DESC(HEIGHT_OUT, "height", ResourceLifetime::Transient, heightDesc);
    }

    // Compile-time validation
    static_assert(INSTANCE_Slot::index == 0, "INSTANCE must be at index 0");
    static_assert(!INSTANCE_Slot::nullable, "INSTANCE must not be nullable");
    static_assert(std::is_same_v<INSTANCE_Slot::Type, VkInstance>, "INSTANCE must be VkInstance");

    static_assert(SURFACE_Slot::index == 0, "SURFACE must be at index 0");
    static_assert(!SURFACE_Slot::nullable, "SURFACE must not be nullable");
    static_assert(std::is_same_v<SURFACE_Slot::Type, VkSurfaceKHR>, "SURFACE must be VkSurfaceKHR");

    static_assert(WINDOW_Slot::index == 1, "WINDOW must be at index 1");
    static_assert(WIDTH_OUT_Slot::index == 2, "WIDTH_OUT must be at index 2");
    static_assert(HEIGHT_OUT_Slot::index == 3, "HEIGHT_OUT must be at index 3");

    VALIDATE_NODE_CONFIG(WindowNodeConfig, WindowNodeCounts);
};

} // namespace Vixen::RenderGraph
