// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/ShadowConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ShadowConfigNode.
 */
class ShadowConfigNodeType : public TypedNodeType<ShadowConfigNodeConfig> {
public:
    ShadowConfigNodeType(const std::string& typeName = "ShadowConfig")
        : TypedNodeType<ShadowConfigNodeConfig>(typeName) {}
    virtual ~ShadowConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::ShadowConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * LightingConfigNode's PerFrameResources ring pattern exactly).
 *
 * Separate node vs. extending LightingConfigNode to carry both records
 * (Sampled Lighting Inc1 M4 decision): a separate node mirrors the Inc0
 * precedent 1:1 (one [GpuStruct] record type per node, one ring per node),
 * keeps ShadowConfig's own lifecycle/content independent of lighting data
 * (a future per-light shadow toggle or authored biasEpsilon tuning UI would
 * only touch this node), and avoids growing LightingConfigNode's config/
 * slot surface for a logically distinct concern (shadow-pass COMPUTE BUDGET,
 * not light DATA). The two nodes upload independently-sized, independently-
 * changing records; combining them would only save one small ring alloc at
 * the cost of coupling two orthogonal data paths.
 *
 * Content is static this increment (no UI/authoring): enabled=1,
 * raysPerLight=1 (hard shadows), maxShadowDistance=large (whole scene),
 * biasEpsilon tuned to kill acne without peter-panning on the real GPU (see
 * MakeDefaultShadowConfig in the .cpp for the tuned value + rationale).
 * Re-uploaded every Execute (16 B, negligible) so a future milestone can
 * mutate it via SetShadowConfig() with no graph rewiring.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * LightingConfigNode.
 */
class ShadowConfigNode : public TypedNode<ShadowConfigNodeConfig> {
public:
    using Base = TypedNode<ShadowConfigNodeConfig>;

    ShadowConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~ShadowConfigNode() override = default;

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
