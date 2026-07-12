#pragma once
#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>

// Forward declarations
namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

// Type alias for VulkanDevice (use VulkanDevice* explicitly in slots)
using VulkanDevice  = Vixen::Vulkan::Resources::VulkanDevice;
using IRenderTarget = Vixen::Vulkan::Resources::IRenderTarget;

// Compile-time slot counts (declared early for reuse)
namespace ProbeAtlasNodeCounts {
    static constexpr size_t INPUTS  = 2;  // VULKAN_DEVICE_IN, COMMAND_POOL
    static constexpr size_t OUTPUTS = 2;  // PROBE_ATLAS, CURRENT_VIEW
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for ProbeAtlasNode
 * (Sampled Lighting Inc4 M2)
 *
 * Allocates ONE persistent DDGI probe atlas image (irradiance OR visibility —
 * two instances of this node type are wired in BuildRenderGraph.cpp, one per
 * atlas, per M1's own resolved finding that the two CANNOT be channel-packed
 * into a single image at real DDGI fidelity: different per-probe texel
 * resolutions and different formats). Extent/format are Setup PARAMETERS
 * (like RenderTargetNode's PARAM_WIDTH/HEIGHT/FORMAT), not graph inputs —
 * atlas dimensions are derived from ProbeGridConfig's grid counts, a build-
 * time (not per-frame) quantity, so there is no live extent-cascade to
 * subscribe to the way AccumulationHistoryNode follows the render target.
 *
 * PERSISTENT, NOT a ring (mirrors AccumulationHistoryNode's own "history must
 * survive across frames" rationale exactly — DDGI hysteresis blend needs
 * last frame's atlas content, the same reason accumulation's history image
 * isn't rotated on RenderTargetNode's per-frame ring, see KI-009). Exposed as
 * an IRenderTarget* (RenderTargetData, imageCount=1) rather than raw
 * VkImage/VkImageView (AccumulationHistoryNode's own shape) because this
 * output feeds ImageSyncGathererNode::PreRegisterImageSlots, which gathers
 * IRenderTarget* handles (Sampled Lighting Inc4 M1) — mirrors RenderTargetNode's
 * own RENDER_TARGET output shape, just single-image and persistent instead of
 * a frames-in-flight ring.
 *
 * Inputs: 2
 *   - VULKAN_DEVICE_IN (VulkanDevice*)  Device for allocation + the one-shot transition queue
 *   - COMMAND_POOL     (VkCommandPool)  Pool for the one-shot transition command buffer
 * Outputs: 2
 *   - PROBE_ATLAS  (IRenderTarget*) - The persistent atlas image (RenderTargetData, imageCount=1)
 *   - CURRENT_VIEW (VkImageView)    - The atlas's raw view handle, mirroring RenderTargetNode's
 *     own CURRENT_VIEW output. A DescriptorResourceGathererNode binding a resource by IRenderTarget*
 *     gets a Resource typed PassThroughStorage (IRenderTarget has no `conversion_type`, only an
 *     `operator VkImageView()` -- Resource::SetHandle's descriptorExtractor_ capture only fires for
 *     types with `conversion_type`), which GetDescriptorHandle() can never turn into a real
 *     VkImageView -- the descriptor is left unpopulated (VUID-vkCmdDispatch-None-08114). Every
 *     existing IMAGE_WRITE-shaped gatherer binding (e.g. DirectLighting/SpatialReuseShade's own
 *     binding 0) connects RenderTargetNode's CURRENT_VIEW, not RENDER_TARGET, for exactly this
 *     reason -- this output exists so probeUpdateGatherer's bindings 29/30 can follow the same
 *     precedent instead of wiring PROBE_ATLAS directly into a descriptor slot.
 * Parameters: width, height, format (VkFormat as uint32_t)
 *
 * Layout: the compute shader will use the image as a STORAGE image (VK_IMAGE_LAYOUT_GENERAL).
 * The node performs a one-time UNDEFINED -> GENERAL transition at Compile (storage images stay
 * GENERAL thereafter), mirroring AccumulationHistoryNode/PickIdTargetNode's own transition pattern.
 *
 * Lifecycle: the image persists across graph recompile (same extent/format); released only on
 * FinalTeardown. M2 scope: allocate + transition + wire — no probe-update shader reads/writes it
 * yet (that's M3). Uninitialized content on (re)creation is therefore safe this milestone (no
 * consumer reads it), mirroring RenderTargetNode/AccumulationHistoryNode's own M1-scope precedent.
 */
CONSTEXPR_NODE_CONFIG(ProbeAtlasNodeConfig,
                      ProbeAtlasNodeCounts::INPUTS,
                      ProbeAtlasNodeCounts::OUTPUTS,
                      ProbeAtlasNodeCounts::ARRAY_MODE) {

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
    OUTPUT_SLOT(PROBE_ATLAS, IRenderTarget*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Parameter name constants -----
    static constexpr const char* PARAM_WIDTH  = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    static constexpr const char* PARAM_FORMAT = "format";  // VkFormat stored as uint32_t

    // ----- Constructor: runtime descriptor initialization -----
    ProbeAtlasNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        // Output: the persistent atlas IRenderTarget* (Persistent lifetime — constant
        // across frames by design, unlike a ring's per-frame-rotating handle).
        HandleDescriptor atlasDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(PROBE_ATLAS, "probe_atlas", ResourceLifetime::Persistent, atlasDesc);

        // Output: the atlas's raw view handle (Persistent — same lifetime as the image), mirroring
        // RenderTargetNodeConfig::CURRENT_VIEW. Consume THIS in a DescriptorResourceGathererNode
        // binding (see CURRENT_VIEW's own doc comment above for why PROBE_ATLAS itself cannot be).
        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_VIEW, "current_view", ResourceLifetime::Persistent, viewDesc);
    }

    // ----- Compile-time validation -----
    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(!VULKAN_DEVICE_IN_Slot::nullable, "VULKAN_DEVICE_IN must not be nullable");
    static_assert(std::is_same_v<VULKAN_DEVICE_IN_Slot::Type, VulkanDevice*>,
                  "VULKAN_DEVICE_IN must be VulkanDevice*");

    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");

    static_assert(PROBE_ATLAS_Slot::index == 0, "PROBE_ATLAS must be at index 0");
    static_assert(!PROBE_ATLAS_Slot::nullable, "PROBE_ATLAS must not be nullable");
    static_assert(std::is_same_v<PROBE_ATLAS_Slot::Type, IRenderTarget*>,
                  "PROBE_ATLAS must be IRenderTarget*");

    static_assert(CURRENT_VIEW_Slot::index == 1, "CURRENT_VIEW must be at index 1");
    static_assert(!CURRENT_VIEW_Slot::nullable, "CURRENT_VIEW must not be nullable");
    static_assert(std::is_same_v<CURRENT_VIEW_Slot::Type, VkImageView>,
                  "CURRENT_VIEW must be VkImageView");

    VALIDATE_NODE_CONFIG(ProbeAtlasNodeConfig, ProbeAtlasNodeCounts);
};

} // namespace Vixen::RenderGraph
