// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc4 M1: variadic IRenderTarget* gatherer, the image-typed sibling of
// BufferSyncGathererNodeConfig. See ImageSyncGathererNode.h / this file for the full rationale.
#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"

namespace Vixen::Vulkan::Resources {
    struct IRenderTarget;
}

namespace Vixen::RenderGraph {

/**
 * @brief Configuration for ImageSyncGathererNode
 *
 * Sampled Lighting Inc4 M1: mirrors BufferSyncGathererNodeConfig exactly (see that
 * file's own doc comment for the full "variadic gatherer -> one array-typed output"
 * rationale), but gathers Vixen::Vulkan::Resources::IRenderTarget* handles instead of
 * VkBuffer — the concrete use case ComputeStageNodeConfig::IMAGE_WRITE's own class doc
 * flagged as the trigger to generalize it ("If a genuine multi-image-write need arises
 * later, generalize IMAGE_WRITE THEN, with its own concrete use case driving the
 * design"): DDGI's probe-update pass needs to write an irradiance atlas AND a separate
 * Chebyshev-visibility atlas simultaneously (verified against real DDGI/RTXGI-reference
 * atlas layout — the two use DIFFERENT per-probe texel resolutions, so they are not
 * even the same image dimensions and cannot be channel-packed into one IMAGE_WRITE).
 *
 * Resource::hazardConstituents_ / ResourceAccessTracker::AddNode (the mechanism that
 * lets BUFFER_WRITE_ARRAY/BUFFER_READ_ARRAY bake N independent SyncEdges from one
 * gathered array) is genuinely resource-type-agnostic already — it operates purely on
 * Resource* identity with zero ResourceType/VkBuffer-specific logic anywhere in the
 * tracker or FrameSyncScheduler (verified by direct reading, not assumed) — so THIS
 * node is the only new "gathering" machinery required; no tracker/scheduler changes.
 *
 * AccessKind is declared on the CONSUMING slot (ComputeStageNodeConfig's
 * IMAGE_WRITE_ARRAY), NOT here, mirroring BufferSyncGathererNodeConfig exactly.
 *
 * Deliberately ADDITIVE: the existing single-image IMAGE_WRITE slot on
 * ComputeStageNodeConfig is untouched — DirectLighting.comp/SpatialReuseShade.comp
 * (Inc3's shipped single-IMAGE_WRITE consumers) keep using it exactly as before, with
 * zero risk of regression. IMAGE_WRITE_ARRAY is opt-in, only wired where a pass
 * genuinely needs N simultaneous image outputs.
 *
 * Inputs:
 * - (0 static) IMAGE_ENTRIES (variadic) - N IRenderTarget* connections, pre-registered
 *   via PreRegisterImageSlots(count) before Setup.
 *
 * Outputs:
 * - IMAGE_ARRAY (std::vector<IRenderTarget*>) - gathered render-target handles, in
 *   connection order.
 */

namespace ImageSyncGathererNodeCounts {
    static constexpr size_t INPUTS = 0;   // purely variadic — no static inputs
    static constexpr size_t OUTPUTS = 1;  // IMAGE_ARRAY
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ImageSyncGathererNodeConfig,
                      ImageSyncGathererNodeCounts::INPUTS,
                      ImageSyncGathererNodeCounts::OUTPUTS,
                      ImageSyncGathererNodeCounts::ARRAY_MODE) {

    // ===== INPUTS (0 static + dynamic) =====
    // Variadic image entries are added dynamically via PreRegisterImageSlots(count),
    // then connected in order via batch.Connect(source, sourceSlot, gatherer, /*index*/ i).

    // ===== OUTPUTS (1) =====
    OUTPUT_SLOT(IMAGE_ARRAY, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    ImageSyncGathererNodeConfig() {
        HandleDescriptor imageArrayDesc{"std::vector<IRenderTarget*>"};
        INIT_OUTPUT_DESC(IMAGE_ARRAY, "image_array",
            ResourceLifetime::Transient, imageArrayDesc);
    }

    VALIDATE_NODE_CONFIG(ImageSyncGathererNodeConfig, ImageSyncGathererNodeCounts);

    static_assert(IMAGE_ARRAY_Slot::index == 0, "IMAGE_ARRAY must be at index 0");
    static_assert(!IMAGE_ARRAY_Slot::nullable, "IMAGE_ARRAY is required");
    static_assert(std::is_same_v<IMAGE_ARRAY_Slot::Type, std::vector<Vixen::Vulkan::Resources::IRenderTarget*>>);
};

} // namespace Vixen::RenderGraph
