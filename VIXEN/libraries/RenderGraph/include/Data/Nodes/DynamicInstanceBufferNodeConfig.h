#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace DynamicInstanceBufferNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 2;  // INSTANCE_BUFFER, INSTANCE_COUNT
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for DynamicInstanceBufferNode
 *
 * Dynamic (animated) sibling of InstanceBufferNode. Holds N = gridDim^2 glm::mat4
 * per-instance model matrices that are RE-COMPUTED every frame and uploaded into a
 * ring-buffered, host-visible storage buffer (via PerFrameResources). Each frame
 * the node emits THAT frame's ring buffer on INSTANCE_BUFFER so the descriptor set
 * re-binds to the freshly written buffer — driving visible per-instance animation
 * with no GPU readback and no CPU/GPU race (one buffer per frame-in-flight).
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*) - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)      - Ring index from FrameSyncNode, read
 *                                            every frame (Execute role)
 * Outputs: 2
 *   - INSTANCE_BUFFER (VkBuffer)  - The current frame's ring buffer (emitted per-frame)
 *   - INSTANCE_COUNT  (uint32_t)  - Number of instances (gridDim * gridDim)
 * Parameters: gridDim, spacing, rotationSpeed
 *
 * Type ID: 124  (122 = InstanceBufferNode, 123 = MvpUniformNode)
 */
CONSTEXPR_NODE_CONFIG(DynamicInstanceBufferNodeConfig,
                      DynamicInstanceBufferNodeCounts::INPUTS,
                      DynamicInstanceBufferNodeCounts::OUTPUTS,
                      DynamicInstanceBufferNodeCounts::ARRAY_MODE) {

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_GRID_DIM       = "gridDim";       // grid side length (instances = gridDim^2)
    static constexpr const char* PARAM_SPACING        = "spacing";       // world-space spacing between instances
    static constexpr const char* PARAM_ROTATION_SPEED = "rotationSpeed"; // radians added per frame (base spin rate)

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Per-frame ring index from FrameSyncNode — read each Execute (drives the ring).
    INPUT_SLOT(CURRENT_FRAME_INDEX, uint32_t, 1,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    // INSTANCE_BUFFER is re-emitted every frame from ExecuteImpl (Transient lifetime):
    // the handle rotates through the ring, so the descriptor re-binds to the buffer the
    // CPU just wrote this frame.
    OUTPUT_SLOT(INSTANCE_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(INSTANCE_COUNT, uint32_t, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    DynamicInstanceBufferNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Output: per-frame instance storage buffer. Transient — the emitted VkBuffer
        // handle changes every frame as the ring rotates (the underlying ring buffers
        // are owned by PerFrameResources and persist across recompile, per FR-7).
        BufferDescription instanceBufDesc{};
        instanceBufDesc.usage            = ResourceUsage::StorageBuffer;
        instanceBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(INSTANCE_BUFFER, "instance_buffer",
            ResourceLifetime::Transient, instanceBufDesc);

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

    static_assert(CURRENT_FRAME_INDEX_Slot::index == 1, "CURRENT_FRAME_INDEX must be at index 1");
    static_assert(!CURRENT_FRAME_INDEX_Slot::nullable, "CURRENT_FRAME_INDEX must not be nullable");
    static_assert(std::is_same_v<CURRENT_FRAME_INDEX_Slot::Type, uint32_t>,
                  "CURRENT_FRAME_INDEX must be uint32_t");

    static_assert(INSTANCE_BUFFER_Slot::index == 0, "INSTANCE_BUFFER must be at index 0");
    static_assert(!INSTANCE_BUFFER_Slot::nullable, "INSTANCE_BUFFER must not be nullable");
    static_assert(std::is_same_v<INSTANCE_BUFFER_Slot::Type, VkBuffer>,
                  "INSTANCE_BUFFER must be VkBuffer");

    static_assert(INSTANCE_COUNT_Slot::index == 1, "INSTANCE_COUNT must be at index 1");
    static_assert(std::is_same_v<INSTANCE_COUNT_Slot::Type, uint32_t>,
                  "INSTANCE_COUNT must be uint32_t");

    VALIDATE_NODE_CONFIG(DynamicInstanceBufferNodeConfig, DynamicInstanceBufferNodeCounts);
};

} // namespace Vixen::RenderGraph
