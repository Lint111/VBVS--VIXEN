#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace MvpUniformNodeCounts {
    static constexpr size_t INPUTS  = 1;  // VULKAN_DEVICE_IN
    static constexpr size_t OUTPUTS = 1;  // MVP_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for MvpUniformNode
 *
 * Allocates a host-visible Vulkan uniform buffer (UBO, binding 0) holding a single
 * glm::mat4 mvp = proj * view. The general Draw.vert shader reads this as
 * `layout(std140, binding=0) uniform bufferVals { mat4 mvp; }` and applies the
 * per-instance model matrix plus the Vulkan Y-flip / Z-remap itself (AR#31).
 *
 * Inputs: 1
 *   - VULKAN_DEVICE_IN (VulkanDevice*) - Device for allocation
 * Outputs: 1
 *   - MVP_BUFFER (VkBuffer) - Uniform buffer holding the proj*view matrix
 * Parameters: fovDegrees, aspect, nearZ, farZ, cameraDistance
 *
 * Type ID: 123
 */
CONSTEXPR_NODE_CONFIG(MvpUniformNodeConfig,
                      MvpUniformNodeCounts::INPUTS,
                      MvpUniformNodeCounts::OUTPUTS,
                      MvpUniformNodeCounts::ARRAY_MODE) {

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_FOV_DEGREES     = "fovDegrees";     // vertical field of view, degrees
    static constexpr const char* PARAM_ASPECT          = "aspect";         // width / height aspect ratio
    static constexpr const char* PARAM_NEAR            = "nearZ";          // near clip plane
    static constexpr const char* PARAM_FAR             = "farZ";           // far clip plane
    static constexpr const char* PARAM_CAMERA_DISTANCE = "cameraDistance"; // camera distance along -Z

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(MVP_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    MvpUniformNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Output: MVP uniform buffer (persistent — survives recompile, per FR-7)
        BufferDescription mvpBufDesc{};
        mvpBufDesc.usage            = ResourceUsage::UniformBuffer;
        mvpBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(MVP_BUFFER, "mvp_buffer",
            ResourceLifetime::Persistent, mvpBufDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(MVP_BUFFER_Slot::index == 0, "MVP_BUFFER must be at index 0");
    static_assert(!MVP_BUFFER_Slot::nullable, "MVP_BUFFER must not be nullable");
    static_assert(std::is_same_v<MVP_BUFFER_Slot::Type, VkBuffer>,
                  "MVP_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(MvpUniformNodeConfig, MvpUniformNodeCounts);
};

} // namespace Vixen::RenderGraph
