// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Deep-Field Mip-Accessor Policy, batch 29 stream C: SkySphereNode scaffold config.
#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

using VulkanDevice  = Vixen::Vulkan::Resources::VulkanDevice;
using IRenderTarget = Vixen::Vulkan::Resources::IRenderTarget;

namespace SkySphereNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, COMMAND_POOL
    static constexpr size_t OUTPUTS = 2;  // SKY_SPHERE, CURRENT_VIEW
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for SkySphereNode
 * (Deep-Field Mip-Accessor Policy, batch 29 stream C — scaffold only, UNWIRED).
 *
 * Owns ONE persistent 2D storage image holding the cached regime-3 (COSMIC)
 * transmittance-accumulated sky, addressed by octahedral mapping (see
 * SkySphereNode.h for the cube-vs-octahedral choice). Mirrors ProbeAtlasNode's
 * shape exactly: Setup PARAMETERS (not graph inputs) size the image, since the
 * sky sphere's resolution is a design-time/scale-to-the-box knob, not a
 * per-frame extent to subscribe to — same rationale ProbeAtlasNodeConfig.h
 * documents for the DDGI atlas.
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL     (VkCommandPool)  Pool for the one-shot transition command buffer
 * Outputs: 2
 *   - SKY_SPHERE   (IRenderTarget*) - The persistent octahedral sky image (RenderTargetData, imageCount=1)
 *   - CURRENT_VIEW (VkImageView)    - Raw view handle, mirrors ProbeAtlasNodeConfig::CURRENT_VIEW
 *     (a descriptor-gatherer binding must connect THIS, not SKY_SPHERE — IRenderTarget has no
 *     conversion_type, see ProbeAtlasNodeConfig.h's own CURRENT_VIEW doc for the full reason).
 * Parameters: width, height, format (VkFormat as uint32_t), refreshCadenceFrames
 *
 * UNWIRED this batch: no BuildRenderGraph.cpp instance exists yet, and no consumer reads
 * SKY_SPHERE/CURRENT_VIEW. Zero behavioral change (nothing links to this node type unless a
 * graph instantiates it — a compiled-but-unreferenced TypedNodeType is inert).
 */
CONSTEXPR_NODE_CONFIG(SkySphereNodeConfig,
                      SkySphereNodeCounts::INPUTS,
                      SkySphereNodeCounts::OUTPUTS,
                      SkySphereNodeCounts::ARRAY_MODE) {

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(SKY_SPHERE, IRenderTarget*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_WIDTH  = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    static constexpr const char* PARAM_FORMAT = "format";  // VkFormat stored as uint32_t
    // Lazy-refresh contract (spec "dynamic sky sphere"): re-trace cadence in frames, plus a
    // dirty flag a future content-invalidation hook can set. Stubbed as config this slice —
    // no scheduler reads it yet (that is the wiring batch's job).
    static constexpr const char* PARAM_REFRESH_CADENCE_FRAMES = "refresh_cadence_frames";

    // ----- Constructor: runtime descriptor initialization -----
    SkySphereNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor skyDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(SKY_SPHERE, "sky_sphere", ResourceLifetime::Persistent, skyDesc);

        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_VIEW, "current_view", ResourceLifetime::Persistent, viewDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");

    static_assert(SKY_SPHERE_Slot::index == 0, "SKY_SPHERE must be at index 0");
    static_assert(!SKY_SPHERE_Slot::nullable, "SKY_SPHERE must not be nullable");
    static_assert(std::is_same_v<SKY_SPHERE_Slot::Type, IRenderTarget*>,
                  "SKY_SPHERE must be IRenderTarget*");

    static_assert(CURRENT_VIEW_Slot::index == 1, "CURRENT_VIEW must be at index 1");
    static_assert(!CURRENT_VIEW_Slot::nullable, "CURRENT_VIEW must not be nullable");
    static_assert(std::is_same_v<CURRENT_VIEW_Slot::Type, VkImageView>,
                  "CURRENT_VIEW must be VkImageView");

    VALIDATE_NODE_CONFIG(SkySphereNodeConfig, SkySphereNodeCounts);
};

} // namespace Vixen::RenderGraph
