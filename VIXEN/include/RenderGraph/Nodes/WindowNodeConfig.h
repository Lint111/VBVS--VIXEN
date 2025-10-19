#pragma once

#include "RenderGraph/ResourceConfig.h"

namespace Vixen::RenderGraph {

/**
 * @brief Pure constexpr resource configuration for WindowNode
 *
 * ALL type information is resolved at compile time.
 * Runtime code is just array[0] access - zero overhead.
 *
 * Inputs: 0
 * Outputs: 1 (SURFACE: VkSurfaceKHR, required)
 * Parameters: width, height
 */
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1) {
    // Compile-time output slot definition
    // This creates:
    // - Type alias: SURFACE_Slot = ResourceSlot<VkSurfaceKHR, 0, false>
    // - Constexpr constant: static constexpr SURFACE_Slot SURFACE{};
    CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);

    // Compile-time parameter names (constexpr strings for type safety)
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";

    // Constructor only needed for runtime descriptor initialization
    // (descriptors contain strings which can't be fully constexpr)
    WindowNodeConfig() {
        // Runtime descriptor initialization
        // Uses compile-time constants from SURFACE slot
        ImageDescription surfaceDesc{};
        surfaceDesc.width = 0;
        surfaceDesc.height = 0;
        surfaceDesc.format = VK_FORMAT_UNDEFINED;
        surfaceDesc.usage = ResourceUsage::ColorAttachment;

        INIT_OUTPUT_DESC(SURFACE, "surface", ResourceLifetime::Persistent, surfaceDesc);
    }

    // Optional: Compile-time validation
    static_assert(SURFACE_Slot::index == 0, "SURFACE must be at index 0");
    static_assert(!SURFACE_Slot::nullable, "SURFACE must not be nullable");
    static_assert(std::is_same_v<SURFACE_Slot::Type, VkSurfaceKHR>, "SURFACE must be VkSurfaceKHR");
};

// Compile-time verification (optional - for extra safety)
static_assert(WindowNodeConfig::INPUT_COUNT == 0, "WindowNode should have no inputs");
static_assert(WindowNodeConfig::OUTPUT_COUNT == 1, "WindowNode should have 1 output");

} // namespace Vixen::RenderGraph
