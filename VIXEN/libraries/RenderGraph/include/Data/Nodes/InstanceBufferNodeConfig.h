#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace InstanceBufferNodeCounts {
    static constexpr size_t INPUTS  = 1;  // VULKAN_DEVICE_IN
    static constexpr size_t OUTPUTS = 2;  // INSTANCE_BUFFER, INSTANCE_COUNT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for InstanceBufferNode
 *
 * Allocates a Vulkan storage buffer (SSBO) holding N = gridDim^2 glm::mat4 model
 * matrices arranged as a planar grid of translations. Indexed by gl_InstanceIndex
 * in the vertex shader to drive a hardware-instanced draw (AR#31 increment 1).
 *
 * Inputs: 1
 *   - VULKAN_DEVICE_IN (VulkanDevice*) - Device for allocation
 * Outputs: 2
 *   - INSTANCE_BUFFER (VkBuffer)  - Storage buffer of per-instance model matrices
 *   - INSTANCE_COUNT  (uint32_t)  - Number of instances (gridDim * gridDim)
 * Parameters: gridDim, spacing
 *
 * Type ID: 122
 */
CONSTEXPR_NODE_CONFIG(InstanceBufferNodeConfig,
                      InstanceBufferNodeCounts::INPUTS,
                      InstanceBufferNodeCounts::OUTPUTS,
                      InstanceBufferNodeCounts::ARRAY_MODE) {

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_GRID_DIM = "gridDim";  // grid side length (instances = gridDim^2)
    static constexpr const char* PARAM_SPACING  = "spacing";  // world-space spacing between instances

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(INSTANCE_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(INSTANCE_COUNT, uint32_t, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    InstanceBufferNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Output: instance storage buffer (persistent — survives recompile, per FR-7)
        BufferDescription instanceBufDesc{};
        instanceBufDesc.usage            = ResourceUsage::StorageBuffer;
        instanceBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(INSTANCE_BUFFER, "instance_buffer",
            ResourceLifetime::Persistent, instanceBufDesc);

        // Output: instance count (transient scalar value)
        BufferDescription countDesc{};
        INIT_OUTPUT_DESC(INSTANCE_COUNT, "instance_count",
            ResourceLifetime::Transient, countDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(INSTANCE_BUFFER_Slot::index == 0, "INSTANCE_BUFFER must be at index 0");
    static_assert(!INSTANCE_BUFFER_Slot::nullable, "INSTANCE_BUFFER must not be nullable");
    static_assert(std::is_same_v<INSTANCE_BUFFER_Slot::Type, VkBuffer>,
                  "INSTANCE_BUFFER must be VkBuffer");

    static_assert(INSTANCE_COUNT_Slot::index == 1, "INSTANCE_COUNT must be at index 1");
    static_assert(std::is_same_v<INSTANCE_COUNT_Slot::Type, uint32_t>,
                  "INSTANCE_COUNT must be uint32_t");

    VALIDATE_NODE_CONFIG(InstanceBufferNodeConfig, InstanceBufferNodeCounts);
};

} // namespace Vixen::RenderGraph
