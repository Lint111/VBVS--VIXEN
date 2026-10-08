#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice = Vixen::Vulkan::Resources::VulkanDevice;

// Compile-time slot counts (declared early for reuse)
namespace LightingConfigNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, CURRENT_FRAME_INDEX
    static constexpr size_t OUTPUTS = 1;  // LIGHTING_CONFIG_BUFFER
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for LightingConfigNode
 *
 * Uploads a `Vixen::Gpu::LightingConfig` record (Sampled Lighting Inc0 M1's
 * canonical [GpuStruct] — see Generated/LightingConfig.g.h) into a
 * ring-buffered, host-visible storage buffer (via PerFrameResources), one
 * SSBO per frame-in-flight, mirroring DynamicInstanceBufferNode's ring
 * pattern. The default content is one directional light matching
 * Lighting.glsl's previously-hardcoded default. Cel shading is tuned through
 * this node's runtime parameters, and hosts may replace the shared light set
 * through SetLights() without node-graph rewiring. Re-uploaded every Execute
 * (208 B) so parameter and light changes take effect immediately.
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN     (VulkanDevice*) - Device for allocation (Dependency)
 *   - CURRENT_FRAME_INDEX  (uint32_t)      - Ring index from FrameSyncNode, read
 *                                            every frame (Execute role)
 * Outputs: 1
 *   - LIGHTING_CONFIG_BUFFER (VkBuffer) - The current frame's ring buffer (emitted per-frame)
 */
CONSTEXPR_NODE_CONFIG(LightingConfigNodeConfig,
                      LightingConfigNodeCounts::INPUTS,
                      LightingConfigNodeCounts::OUTPUTS,
                      LightingConfigNodeCounts::ARRAY_MODE) {

    // Runtime tuning parameters read by LightingConfigNode::TypedExecuteImpl.
    // Lambert+GGX remains selectable with mode 0; cel is the node default (1).
    static constexpr const char* PARAM_SHADING_MODE = "shadingMode";
    static constexpr const char* PARAM_CEL_BAND_COUNT = "celBandCount";
    static constexpr const char* PARAM_CEL_SHADOW_THRESHOLD = "celShadowThreshold";
    static constexpr const char* PARAM_CEL_LIT_THRESHOLD = "celLitThreshold";
    static constexpr const char* PARAM_CEL_RAMP_SOFTNESS = "celRampSoftness";
    static constexpr const char* PARAM_CEL_LIT_HUE_SHIFT_DEGREES = "celLitHueShiftDegrees";
    static constexpr const char* PARAM_CEL_SHADOW_HUE_SHIFT_DEGREES = "celShadowHueShiftDegrees";
    static constexpr const char* PARAM_CEL_BAND_FALLOFF_START = "celBandFalloffStart";
    static constexpr const char* PARAM_CEL_BAND_FALLOFF_END = "celBandFalloffEnd";
    static constexpr const char* PARAM_CEL_LIGHT_SPILL_SCALE = "celLightSpillScale";
    static constexpr const char* PARAM_EXPOSURE_COMPENSATION_EV = "exposureCompensationEV";

    static constexpr uint32_t SHADING_MODE_LAMBERT_GGX = 0u;
    static constexpr uint32_t SHADING_MODE_CEL = 1u;
    static constexpr uint32_t DEFAULT_CEL_BAND_COUNT = 3u;
    static constexpr float DEFAULT_CEL_SHADOW_THRESHOLD = 0.16f;
    static constexpr float DEFAULT_CEL_LIT_THRESHOLD = 0.86f;
    static constexpr float DEFAULT_CEL_RAMP_SOFTNESS = 0.08f;
    static constexpr float DEFAULT_CEL_LIT_HUE_SHIFT_DEGREES = 18.0f;
    static constexpr float DEFAULT_CEL_SHADOW_HUE_SHIFT_DEGREES = -18.0f;
    static constexpr float DEFAULT_CEL_BAND_FALLOFF_START = 0.0f;
    static constexpr float DEFAULT_CEL_BAND_FALLOFF_END = 0.0f;
    static constexpr float DEFAULT_CEL_LIGHT_SPILL_SCALE = 0.012f;
    static constexpr float DEFAULT_EXPOSURE_COMPENSATION_EV = 0.0f;
    static constexpr float MIN_EXPOSURE_COMPENSATION_EV = -16.0f;
    static constexpr float MAX_EXPOSURE_COMPENSATION_EV = 16.0f;

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
    // LIGHTING_CONFIG_BUFFER is re-emitted every frame from ExecuteImpl (Transient
    // lifetime): the handle rotates through the ring, so the descriptor re-binds
    // to the buffer the CPU just wrote this frame.
    OUTPUT_SLOT(LIGHTING_CONFIG_BUFFER, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Constructor: runtime descriptor initialization -----
    LightingConfigNodeConfig() {
        // Input: VulkanDevice
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        // Input: per-frame ring index (transient scalar)
        INIT_INPUT_DESC(CURRENT_FRAME_INDEX, "current_frame_index",
            ResourceLifetime::Transient, BufferDescription{});

        // Output: per-frame lighting config storage buffer. Transient — the emitted
        // VkBuffer handle changes every frame as the ring rotates (the underlying
        // ring buffers are owned by PerFrameResources and persist across recompile).
        BufferDescription lightingConfigBufDesc{};
        lightingConfigBufDesc.usage            = ResourceUsage::StorageBuffer;
        lightingConfigBufDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        INIT_OUTPUT_DESC(LIGHTING_CONFIG_BUFFER, "lighting_config_buffer",
            ResourceLifetime::Transient, lightingConfigBufDesc);
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

    static_assert(LIGHTING_CONFIG_BUFFER_Slot::index == 0, "LIGHTING_CONFIG_BUFFER must be at index 0");
    static_assert(!LIGHTING_CONFIG_BUFFER_Slot::nullable, "LIGHTING_CONFIG_BUFFER must not be nullable");
    static_assert(std::is_same_v<LIGHTING_CONFIG_BUFFER_Slot::Type, VkBuffer>,
                  "LIGHTING_CONFIG_BUFFER must be VkBuffer");

    VALIDATE_NODE_CONFIG(LightingConfigNodeConfig, LightingConfigNodeCounts);
};

} // namespace Vixen::RenderGraph
