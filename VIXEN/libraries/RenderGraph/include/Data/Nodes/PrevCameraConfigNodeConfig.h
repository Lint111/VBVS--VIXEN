#pragma once
#include "Data/Core/ResourceConfig.h"
#include <glm/glm.hpp>
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace PrevCameraConfigNodeCounts {
    static constexpr size_t INPUTS  = 3;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX, PREV_VIEW_PROJ
    static constexpr size_t OUTPUTS = 1;  // PREV_CAMERA_CONFIG_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for PrevCameraConfigNode
 *
 * Uploads a `Vixen::Gpu::PrevCameraConfig` record (Sampled Lighting Inc2 M3 —
 * see Generated/PrevCameraConfig.g.h) into a ring-buffered, host-visible
 * storage buffer (via PerFrameResources), one SSBO per frame-in-flight —
 * mirrors AccumulationConfigNode's ring pattern exactly (a separate node,
 * not a shared one, for the same separate-vs-extend reasons
 * ShadowConfigNode.h's file header records: this is its own lifecycle/content
 * concern, logically distinct from lighting/shadow/accumulation data).
 *
 * A dedicated SSBO (not a push-constant field) because a mat4 is 64 B and
 * the march's push-constant block is already 72 B (of the 128 B Vulkan-
 * guaranteed minimum) — adding 64 more would overflow it. See
 * PrevCameraConfig.cs's file header for the full rationale.
 *
 * Inputs: 3
 *   - VULKAN_DEVICE_IN     (VulkanDevice*)      - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)           - Ring index from FrameSyncNode, read
 *                                                 every frame (Execute role)
 *   - PREV_VIEW_PROJ       (const glm::mat4&)   - CameraNode's retained last-frame
 *                                                 view*proj (Sampled Lighting Inc2 M3) —
 *                                                 read every Execute and copied verbatim
 *                                                 into the uploaded record.
 * Outputs: 1
 *   - PREV_CAMERA_CONFIG_BUFFER (VkBuffer)  - The current frame's ring buffer (emitted per-frame)
 */
CONSTEXPR_NODE_CONFIG(PrevCameraConfigNodeConfig,
                      PrevCameraConfigNodeCounts::INPUTS,
                      PrevCameraConfigNodeCounts::OUTPUTS,
                      PrevCameraConfigNodeCounts::ARRAY_MODE) {

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

    // CameraNode's retained prev-frame view*proj (Sampled Lighting Inc2 M3) — read every
    // Execute, copied verbatim into the uploaded PrevCameraConfig record.
    INPUT_SLOT(PREV_VIEW_PROJ, const glm::mat4&, 2,
        SlotNullability::Required,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    // PREV_CAMERA_CONFIG_BUFFER is re-emitted every frame from ExecuteImpl (Transient
    // lifetime): the handle rotates through the ring, so the descriptor re-binds
    // to the buffer the CPU just wrote this frame.
    OUTPUT_SLOT(PREV_CAMERA_CONFIG_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    PrevCameraConfigNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Input: prev-frame view*proj (Persistent — the CameraNode instance/handle is
        // stable; the matrix it refers to is read fresh every Execute).
        HandleDescriptor prevViewProjDesc{"glm::mat4"};
        INIT_INPUT_DESC(PREV_VIEW_PROJ, "prev_view_proj", ResourceLifetime::Persistent, prevViewProjDesc);

        // Output: per-frame prev-camera-config storage buffer. Transient — the emitted
        // VkBuffer handle changes every frame as the ring rotates (the underlying
        // ring buffers are owned by PerFrameResources and persist across recompile).
        BufferDescription prevCameraConfigBufDesc{};
        prevCameraConfigBufDesc.usage            = ResourceUsage::StorageBuffer;
        prevCameraConfigBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(PREV_CAMERA_CONFIG_BUFFER, "prev_camera_config_buffer",
            ResourceLifetime::Transient, prevCameraConfigBufDesc);
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

    static_assert(PREV_VIEW_PROJ_Slot::index == 2, "PREV_VIEW_PROJ must be at index 2");
    static_assert(!PREV_VIEW_PROJ_Slot::nullable, "PREV_VIEW_PROJ must not be nullable");
    static_assert(std::is_same_v<PREV_VIEW_PROJ_Slot::Type, const glm::mat4&>,
                  "PREV_VIEW_PROJ must be const glm::mat4&");

    static_assert(PREV_CAMERA_CONFIG_BUFFER_Slot::index == 0, "PREV_CAMERA_CONFIG_BUFFER must be at index 0");
    static_assert(!PREV_CAMERA_CONFIG_BUFFER_Slot::nullable, "PREV_CAMERA_CONFIG_BUFFER must not be nullable");
    static_assert(std::is_same_v<PREV_CAMERA_CONFIG_BUFFER_Slot::Type, VkBuffer>,
                  "PREV_CAMERA_CONFIG_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(PrevCameraConfigNodeConfig, PrevCameraConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
