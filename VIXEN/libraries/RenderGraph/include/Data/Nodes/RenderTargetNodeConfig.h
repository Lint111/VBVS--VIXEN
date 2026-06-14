#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

// Forward declarations
namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

using VulkanDevice  = Vixen::Vulkan::Resources::VulkanDevice;
using IRenderTarget = Vixen::Vulkan::Resources::IRenderTarget;

// Compile-time slot counts (declared early for reuse)
namespace RenderTargetNodeCounts {
    static constexpr size_t INPUTS  = 1;  // VULKAN_DEVICE_IN
    static constexpr size_t OUTPUTS = 4;  // RENDER_TARGET, CURRENT_VIEW, WIDTH_OUT, HEIGHT_OUT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for RenderTargetNode
 *
 * Allocates an offscreen color render target (imageCount images, one per in-flight frame).
 * followSwapchainExtent / resize is deliberately deferred to a follow-up; this first node
 * takes explicit width / height parameters (see PARAM_WIDTH / PARAM_HEIGHT).
 *
 * Inputs: 1
 *   - VULKAN_DEVICE_IN (VulkanDevice*) - Device for allocation
 * Outputs: 4
 *   - RENDER_TARGET (IRenderTarget*) - Offscreen color target (RenderTargetData)
 *   - CURRENT_VIEW  (VkImageView)    - View for the current in-flight buffer
 *   - WIDTH_OUT     (uint32_t)       - Render target width
 *   - HEIGHT_OUT    (uint32_t)       - Render target height
 * Parameters: width, height, format, imageCount, usage
 */
CONSTEXPR_NODE_CONFIG(RenderTargetNodeConfig,
                      RenderTargetNodeCounts::INPUTS,
                      RenderTargetNodeCounts::OUTPUTS,
                      RenderTargetNodeCounts::ARRAY_MODE) {

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(RENDER_TARGET, IRenderTarget*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(WIDTH_OUT, uint32_t, 2,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(HEIGHT_OUT, uint32_t, 3,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_WIDTH       = "width";
    static constexpr const char* PARAM_HEIGHT      = "height";
    static constexpr const char* PARAM_FORMAT      = "format";       // VkFormat stored as uint32_t
    static constexpr const char* PARAM_IMAGE_COUNT = "imageCount";   // 0 => use MAX_FRAMES_IN_FLIGHT
    static constexpr const char* PARAM_USAGE       = "usage";        // VkImageUsageFlags as uint32_t

    // ----- Constructor: runtime descriptor initialization -----
    RenderTargetNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Output: IRenderTarget* (persistent — survives recompile, per FR-7)
        HandleDescriptor rtDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(RENDER_TARGET, "render_target", ResourceLifetime::Persistent, rtDesc);

        // Output: current image view (persistent — same lifetime as images)
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_VIEW, "current_view", ResourceLifetime::Persistent, viewDesc);

        // Output: width / height (transient scalar values)
        BufferDescription wDesc{};
        INIT_OUTPUT_DESC(WIDTH_OUT,  "width",  ResourceLifetime::Transient, wDesc);
        BufferDescription hDesc{};
        INIT_OUTPUT_DESC(HEIGHT_OUT, "height", ResourceLifetime::Transient, hDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(RENDER_TARGET_Slot::index == 0, "RENDER_TARGET must be at index 0");
    static_assert(!RENDER_TARGET_Slot::nullable, "RENDER_TARGET must not be nullable");
    static_assert(std::is_same_v<RENDER_TARGET_Slot::Type, IRenderTarget*>,
                  "RENDER_TARGET must be IRenderTarget*");

    static_assert(CURRENT_VIEW_Slot::index == 1, "CURRENT_VIEW must be at index 1");
    static_assert(WIDTH_OUT_Slot::index  == 2,   "WIDTH_OUT must be at index 2");
    static_assert(HEIGHT_OUT_Slot::index == 3,   "HEIGHT_OUT must be at index 3");

    VALIDATE_NODE_CONFIG(RenderTargetNodeConfig, RenderTargetNodeCounts);
};

} // namespace Vixen::RenderGraph
