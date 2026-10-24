#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace ReservoirConfigNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 1;  // RESERVOIR_CONFIG_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for ReservoirConfigNode
 *
 * Uploads a `Vixen::Gpu::ReservoirConfig` record (Sampled Lighting Inc3 M3's
 * canonical [GpuStruct] — see Generated/ReservoirConfig.g.h) into a
 * ring-buffered, host-visible storage buffer (via PerFrameResources), one
 * SSBO per frame-in-flight — mirrors ShadowConfigNode/AccumulationConfigNode's
 * ring pattern exactly (a separate node, not a shared one: same "config lives
 * with its consuming library, one node per logically distinct concern"
 * convention every prior milestone in this program has used).
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*) - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)      - Ring index from FrameSyncNode, read
 *                                            every frame (Execute role)
 * Outputs: 1
 *   - RESERVOIR_CONFIG_BUFFER (VkBuffer) - The current frame's ring buffer (emitted per-frame)
 */
CONSTEXPR_NODE_CONFIG(ReservoirConfigNodeConfig,
                      ReservoirConfigNodeCounts::INPUTS,
                      ReservoirConfigNodeCounts::OUTPUTS,
                      ReservoirConfigNodeCounts::ARRAY_MODE) {

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
    // RESERVOIR_CONFIG_BUFFER is re-emitted every frame from ExecuteImpl (Transient
    // lifetime): the handle rotates through the ring, so the descriptor re-binds
    // to the buffer the CPU just wrote this frame.
    OUTPUT_SLOT(RESERVOIR_CONFIG_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    ReservoirConfigNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Output: per-frame reservoir config storage buffer. Transient — the emitted
        // VkBuffer handle changes every frame as the ring rotates (the underlying
        // ring buffers are owned by PerFrameResources and persist across recompile).
        BufferDescription reservoirConfigBufDesc{};
        reservoirConfigBufDesc.usage            = ResourceUsage::StorageBuffer;
        reservoirConfigBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(RESERVOIR_CONFIG_BUFFER, "reservoir_config_buffer",
            ResourceLifetime::Transient, reservoirConfigBufDesc);
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

    static_assert(RESERVOIR_CONFIG_BUFFER_Slot::index == 0, "RESERVOIR_CONFIG_BUFFER must be at index 0");
    static_assert(!RESERVOIR_CONFIG_BUFFER_Slot::nullable, "RESERVOIR_CONFIG_BUFFER must not be nullable");
    static_assert(std::is_same_v<RESERVOIR_CONFIG_BUFFER_Slot::Type, VkBuffer>,
                  "RESERVOIR_CONFIG_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(ReservoirConfigNodeConfig, ReservoirConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
