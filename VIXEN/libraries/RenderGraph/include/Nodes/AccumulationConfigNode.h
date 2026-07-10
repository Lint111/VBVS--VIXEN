// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/AccumulationConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for AccumulationConfigNode.
 */
class AccumulationConfigNodeType : public TypedNodeType<AccumulationConfigNodeConfig> {
public:
    AccumulationConfigNodeType(const std::string& typeName = "AccumulationConfig")
        : TypedNodeType<AccumulationConfigNodeConfig>(typeName) {}
    virtual ~AccumulationConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::AccumulationConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * ShadowConfigNode's PerFrameResources ring pattern exactly).
 *
 * Separate node vs. extending LightingConfigNode/ShadowConfigNode to carry
 * more records (Sampled Lighting Inc2 M1 decision, same rationale as
 * ShadowConfigNode.h's file header): a separate node keeps
 * AccumulationConfig's own lifecycle/content independent of lighting/shadow
 * data (a future UI-driven alpha/maxFrames tuning control would only touch
 * this node), and avoids growing an unrelated node's config/slot surface for
 * a logically distinct concern (temporal-accumulation COMPUTE BUDGET, not
 * light or shadow DATA).
 *
 * Content this milestone: enabled=0 (M1's zero-visual-delta gate — the
 * accumulate seam in BodyInstanceRayMarch.comp is a pure passthrough),
 * alpha=0 (sentinel for the DEFAULT converging 1/N mode once M2 wires
 * consumption), maxFrames tuned for the converging mode, resetOnMotion=0
 * (M2 default fallback path, unused until wired). A
 * VIXEN_ACCUMULATION_ENABLED env A/B lever mirrors ShadowConfigNode's own
 * VIXEN_SHADOW_CONFIG_ENABLED convention — see MakeDefaultAccumulationConfig
 * in the .cpp. Re-uploaded every Execute (16 B, negligible) so a future
 * milestone can mutate it via SetAccumulationConfig() with no graph rewiring.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * ShadowConfigNode.
 */
class AccumulationConfigNode : public TypedNode<AccumulationConfigNodeConfig> {
public:
    using Base = TypedNode<AccumulationConfigNodeConfig>;

    AccumulationConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~AccumulationConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;
};

} // namespace Vixen::RenderGraph
