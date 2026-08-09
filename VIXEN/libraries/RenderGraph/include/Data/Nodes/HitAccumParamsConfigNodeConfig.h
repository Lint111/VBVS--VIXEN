// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace HitAccumParamsConfigNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 1;  // HIT_ACCUM_PARAMS_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for HitAccumParamsConfigNode
 *
 * B2 (docs/plans/2026-08-04-wavefront-recipe-shading.md): the hit-accumulate
 * pass's 48-byte per-frame params block (epoch, primary cone, detail, camera),
 * ring-buffered via PerFrameResources exactly like ShadowConfigNode/
 * PrevCameraConfigNode — one SSBO per frame-in-flight instead of the single
 * un-ringed StorageBufferNode this replaces (batch-22 root cause: k frames in
 * flight shared one epoch stamp with no ring).
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*) - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)      - Ring index from FrameSyncNode, read
 *                                            every frame (Execute role)
 * Outputs: 1
 *   - HIT_ACCUM_PARAMS_BUFFER (VkBuffer) - The current frame's ring buffer (emitted per-frame)
 */
CONSTEXPR_NODE_CONFIG(HitAccumParamsConfigNodeConfig,
                      HitAccumParamsConfigNodeCounts::INPUTS,
                      HitAccumParamsConfigNodeCounts::OUTPUTS,
                      HitAccumParamsConfigNodeCounts::ARRAY_MODE) {

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
    // HIT_ACCUM_PARAMS_BUFFER is re-emitted every frame from ExecuteImpl (Transient
    // lifetime): the handle rotates through the ring, so the descriptor re-binds
    // to the buffer PreTick just wrote for this frame's index.
    OUTPUT_SLOT(HIT_ACCUM_PARAMS_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    HitAccumParamsConfigNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Output: per-frame hit-accum params storage buffer. Transient — the emitted
        // VkBuffer handle changes every frame as the ring rotates (the underlying
        // ring buffers are owned by PerFrameResources and persist across recompile).
        BufferDescription paramsBufDesc{};
        paramsBufDesc.usage            = ResourceUsage::StorageBuffer;
        paramsBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(HIT_ACCUM_PARAMS_BUFFER, "hit_accum_params_buffer",
            ResourceLifetime::Transient, paramsBufDesc);
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

    static_assert(HIT_ACCUM_PARAMS_BUFFER_Slot::index == 0, "HIT_ACCUM_PARAMS_BUFFER must be at index 0");
    static_assert(!HIT_ACCUM_PARAMS_BUFFER_Slot::nullable, "HIT_ACCUM_PARAMS_BUFFER must not be nullable");
    static_assert(std::is_same_v<HIT_ACCUM_PARAMS_BUFFER_Slot::Type, VkBuffer>,
                  "HIT_ACCUM_PARAMS_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(HitAccumParamsConfigNodeConfig, HitAccumParamsConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
