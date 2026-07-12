// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/ProbeAtlasNodeConfig.h"
#include "IRenderTarget.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the persistent DDGI probe atlas image (irradiance or visibility).
 */
class ProbeAtlasNodeType : public TypedNodeType<ProbeAtlasNodeConfig> {
public:
    ProbeAtlasNodeType(const std::string& typeName = "ProbeAtlas")
        : TypedNodeType<ProbeAtlasNodeConfig>(typeName) {}
    virtual ~ProbeAtlasNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates ONE persistent DDGI probe atlas image (Sampled Lighting Inc4 M2): a SINGLE
 * persistent 2D STORAGE image, sized/formatted via Setup PARAMETERS (width/height/format — NOT
 * graph inputs, since atlas dimensions derive from ProbeGridConfig's grid counts, a build-time
 * quantity, not a live per-frame extent to subscribe to).
 *
 * Two instances of this node type are wired in BuildRenderGraph.cpp — one for the irradiance
 * atlas, one for the Chebyshev-visibility atlas — per M1's own resolved finding
 * (ImageSyncGathererNodeConfig.h's file header) that real DDGI atlas layouts use DIFFERENT
 * per-probe texel resolutions for the two, so they cannot be channel-packed into one image.
 *
 * Why a dedicated node, not RenderTargetNode directly: RenderTargetNode's image COUNT defaults
 * to frames-in-flight and its lifecycle assumes a rotating current-index ring (see
 * RenderTargetNode::ExecuteImpl advancing currentIndex every frame) — DDGI's hysteresis blend
 * needs the SAME physical image across frames (this frame's probe-update reads what LAST frame
 * wrote), the identical "history must survive across frames" requirement
 * AccumulationHistoryNode's own file header documents. So this node mirrors
 * AccumulationHistoryNode's persistence discipline (one persistent image, no ring, no
 * currentIndex advance) while exposing an IRenderTarget* (RenderTargetData, imageCount=1)
 * instead of raw VkImage/VkImageView, because THIS output must connect into
 * ImageSyncGathererNode::PreRegisterImageSlots (Inc4 M1), which gathers IRenderTarget* handles —
 * the same interface RenderTargetNode's own RENDER_TARGET output already uses for the single-slot
 * IMAGE_WRITE consumers (DirectLighting.comp/SpatialReuseShade.comp).
 *
 * Usage = STORAGE only (no TRANSFER_SRC/DST needed — no clear-on-resize copy; M2 has no reader/
 * writer yet, so uninitialized content on (re)creation is safe, mirroring RenderTargetNode/
 * AccumulationHistoryNode's own M1-scope precedent).
 *
 * Layout: the compute shader will use the image as a STORAGE image, requiring
 * VK_IMAGE_LAYOUT_GENERAL. The image is created UNDEFINED and transitioned UNDEFINED->GENERAL
 * exactly once, at Compile, via a one-shot command buffer submitted on the device queue —
 * identical mechanics to AccumulationHistoryNode::TransitionToGeneral, just parameterized by
 * this instance's own width_/height_/format_ instead of AccumulationHistoryNode's hardcoded
 * kFormat/render-extent.
 *
 * M2 scope: allocate + transition + wire into ImageSyncGathererNode's IMAGE_WRITE_ARRAY slot,
 * but NOT yet read/written by any shader — pure plumbing, zero visual delta (no probe-update
 * pass exists yet; that's M3). Extent/format are FIXED at graph-build time this milestone (no
 * live resize cascade — DDGI grid density is a design-time/M6-benched decision, not a
 * per-frame-varying render-target extent), so there is no "extent changed" recreate path the
 * way AccumulationHistoryNode has for the render target's own resize; a genuine future resize
 * of the PROBE GRID (not the swapchain) would be a content-authoring change, handled the same
 * way ProbeGridConfigNode's own SetProbeGridConfig() will be — not a per-frame extent input.
 *
 * Lifecycle: persists across graph recompile; released only on FinalTeardown.
 */
class ProbeAtlasNode : public TypedNode<ProbeAtlasNodeConfig> {
public:
    using Base = TypedNode<ProbeAtlasNodeConfig>;

    ProbeAtlasNode(const std::string& instanceName, NodeType* nodeType);
    ~ProbeAtlasNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateImage(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool commandPool);
    void TransitionToGeneral(VkCommandPool commandPool);
    void DestroyImage();

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;

    // Single-image IRenderTarget (RenderTargetData with buffers.size()==1, currentIndex==0
    // always) — the interface ImageSyncGathererNode gathers. Persists across recompile.
    Vixen::Vulkan::Resources::RenderTargetData target_;

    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
};

} // namespace Vixen::RenderGraph
