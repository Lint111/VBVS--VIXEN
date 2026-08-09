// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
// SkySphereNode.h — Deep-Field Mip-Accessor Policy, batch 29 stream C (scaffold, UNWIRED).
//
// Spec: docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md, "The dynamic sky
// sphere": at cosmic distance parallax is negligible, so regime-3 (COSMIC — footprint >= K*cell,
// no hits, pure transmittance accumulation) results are rotation-invariant and CACHEABLE. This
// node owns that cache target: trace the deep field into a sky image at a lazy cadence, composite
// per frame at near-zero cost. Mirrors ProbeAtlasNode's shape (persistent single storage image,
// no ring — a probe-like cached target is exactly the spec's own "the sky sphere is a probe-like
// cached target (W1b/probe machinery is the in-house shape)").
//
// OCTAHEDRAL vs CUBE: this scaffold picks a SINGLE 2D octahedral-mapped image over a 6-face cube
// texture. Why: (1) one VkImage/VkImageView/descriptor binding instead of six (or a cube array
// view) — matches ProbeAtlasNode's own "one persistent 2D image" precedent, no new descriptor
// shape to add to the (currently untouched) DescriptorResourceGathererNode; (2) octahedral mapping
// has no per-face seam-duplication bookkeeping a cubemap needs (edge/corner texel sharing across
// faces) — the accumulation loop below writes each output texel exactly once, independent of its
// neighbors; (3) precedent for octahedral-as-simplification already exists in this codebase's DDGI
// literature context (Chebyshev-visibility octahedral probes are the standard DDGI shape ProbeAtlasNode's
// own header cites) even though ProbeAtlasNode itself stores per-probe atlas texels, not a whole-sphere
// map — same underlying mapping, applied here to one full sphere instead of many small probes.
// Downside accepted: octahedral has a non-uniform texel density (poles vs equator) — acceptable for a
// COSMIC-regime target where nothing is pixel-critical (the composite is a coarse background layer).
//
// LAZY REFRESH CONTRACT (stubbed): SkySphereNodeConfig::PARAM_REFRESH_CADENCE_FRAMES (a frame-count
// cadence) plus a CPU-side dirty flag (content-invalidation hook: a nebula recipe animating, a star
// igniting — set externally, not by this node, in a future increment) together decide whether
// Execute re-dispatches the accumulate pass or just re-emits last frame's cached image. THIS SLICE:
// the flag/cadence are read as config and stored, but nothing dispatches the compute pass yet (no
// pipeline/descriptor wiring — see file-level "wiring plan" note in the batch report). Execute is a
// pure passthrough, identical in shape to ProbeAtlasNode::ExecuteImpl re-emitting its persistent image.
//
// UNWIRED means: no BuildRenderGraph.cpp instance, no shader dispatch, zero behavioral change to
// any existing frame. The node type self-registers (VIXEN_REGISTER_NODE) but nothing in the live
// graph ever calls CreateInstance on it, so it is inert at runtime — build/link-time proof only.

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/SkySphereNodeConfig.h"
#include "IRenderTarget.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the persistent octahedral sky-sphere cache image.
 */
class SkySphereNodeType : public TypedNodeType<SkySphereNodeConfig> {
public:
    SkySphereNodeType(const std::string& typeName = "SkySphere")
        : TypedNodeType<SkySphereNodeConfig>(typeName) {}
    virtual ~SkySphereNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates the persistent octahedral sky-sphere storage image and stubs the lazy-refresh
 * contract (dirty flag + cadence). SCAFFOLD SCOPE (this batch): allocate + transition + publish
 * outputs, exactly like ProbeAtlasNode's own M2 scope note — no accumulate-pass dispatch, no
 * descriptor-gatherer wiring, no BuildRenderGraph.cpp instance. The accumulate compute shader
 * (shaders/SkySphereAccumulate.comp) compiles standalone (own descriptor namespace, mirrors
 * HiZDownsample.comp's "does NOT #include SceneBindings.glsl" convention) but this node never
 * builds a pipeline from it this slice — wiring is the next batch's job.
 *
 * Persistent, not a ring (same rationale as ProbeAtlasNode/AccumulationHistoryNode: the cached
 * sky must survive across frames — that's the entire point of the lazy-refresh contract).
 *
 * Lifecycle: persists across graph recompile; released only on FinalTeardown.
 */
class SkySphereNode : public TypedNode<SkySphereNodeConfig> {
public:
    using Base = TypedNode<SkySphereNodeConfig>;

    SkySphereNode(const std::string& instanceName, NodeType* nodeType);
    ~SkySphereNode() override = default;

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

    // Lazy-refresh contract (stubbed — see file header). refreshCadenceFrames_ is read from
    // config; dirty_ defaults true so a future wiring increment's first Execute always refreshes
    // once before settling into the cadence. Neither field is consulted by anything yet.
    uint32_t refreshCadenceFrames_ = 0;
    bool     dirty_ = true;

    // Single-image IRenderTarget (RenderTargetData, imageCount==1, currentIndex==0 always) —
    // mirrors ProbeAtlasNode's own target_ exactly.
    Vixen::Vulkan::Resources::RenderTargetData target_;

    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
};

} // namespace Vixen::RenderGraph
