#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace AccumulationConfigNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 1;  // ACCUMULATION_CONFIG_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for AccumulationConfigNode
 *
 * Uploads a `Vixen::Gpu::AccumulationConfig` record (Sampled Lighting Inc2 M1's
 * canonical [GpuStruct] — see Generated/AccumulationConfig.g.h) into a
 * ring-buffered, host-visible storage buffer (via PerFrameResources), one
 * SSBO per frame-in-flight — mirrors ShadowConfigNode's ring pattern exactly
 * (a separate node, not a shared one, for the same separate-vs-extend
 * reasons ShadowConfigNode.h's file header records: accumulation is its own
 * lifecycle/content concern, logically distinct from lighting/shadow data).
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*) - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)      - Ring index from FrameSyncNode, read
 *                                            every frame (Execute role)
 * Outputs: 1
 *   - ACCUMULATION_CONFIG_BUFFER (VkBuffer) - The current frame's ring buffer (emitted per-frame)
 */
CONSTEXPR_NODE_CONFIG(AccumulationConfigNodeConfig,
                      AccumulationConfigNodeCounts::INPUTS,
                      AccumulationConfigNodeCounts::OUTPUTS,
                      AccumulationConfigNodeCounts::ARRAY_MODE) {

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
    // ACCUMULATION_CONFIG_BUFFER is re-emitted every frame from ExecuteImpl (Transient
    // lifetime): the handle rotates through the ring, so the descriptor re-binds
    // to the buffer the CPU just wrote this frame.
    OUTPUT_SLOT(ACCUMULATION_CONFIG_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    AccumulationConfigNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Output: per-frame accumulation config storage buffer. Transient — the emitted
        // VkBuffer handle changes every frame as the ring rotates (the underlying
        // ring buffers are owned by PerFrameResources and persist across recompile).
        BufferDescription accumulationConfigBufDesc{};
        accumulationConfigBufDesc.usage            = ResourceUsage::StorageBuffer;
        accumulationConfigBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(ACCUMULATION_CONFIG_BUFFER, "accumulation_config_buffer",
            ResourceLifetime::Transient, accumulationConfigBufDesc);
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

    static_assert(ACCUMULATION_CONFIG_BUFFER_Slot::index == 0, "ACCUMULATION_CONFIG_BUFFER must be at index 0");
    static_assert(!ACCUMULATION_CONFIG_BUFFER_Slot::nullable, "ACCUMULATION_CONFIG_BUFFER must not be nullable");
    static_assert(std::is_same_v<ACCUMULATION_CONFIG_BUFFER_Slot::Type, VkBuffer>,
                  "ACCUMULATION_CONFIG_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(AccumulationConfigNodeConfig, AccumulationConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
