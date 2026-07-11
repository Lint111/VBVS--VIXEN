// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/ReservoirConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ReservoirConfigNode.
 */
class ReservoirConfigNodeType : public TypedNodeType<ReservoirConfigNodeConfig> {
public:
    ReservoirConfigNodeType(const std::string& typeName = "ReservoirConfig")
        : TypedNodeType<ReservoirConfigNodeConfig>(typeName) {}
    virtual ~ReservoirConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::ReservoirConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * ShadowConfigNode/AccumulationConfigNode's PerFrameResources ring pattern
 * exactly).
 *
 * Separate node vs. extending an existing config node (same reasoning
 * ShadowConfigNode's own file header documents): a separate node mirrors the
 * Inc0 precedent 1:1 (one [GpuStruct] record type per node, one ring per
 * node), keeps ReservoirConfig's own lifecycle/content independent of
 * lighting/shadow/accumulation data (a future ReSTIR authoring UI would only
 * touch this node), and avoids growing an unrelated node's config/slot
 * surface for a logically distinct concern (reservoir-sampling COMPUTE
 * BUDGET, not light or shadow DATA).
 *
 * Content is static this milestone (no UI/authoring): reservoirEnabled=0 by
 * default — M3 ships the struct + upload plumbing as scaffolding for M4/M5's
 * RIS + temporal/spatial reservoir reuse (this program's Task 4/5); nothing
 * reads this buffer's contents yet. Re-uploaded every Execute (28 B,
 * negligible) so a future milestone can mutate it via SetReservoirConfig()
 * with no graph rewiring — same pattern as every prior *ConfigNode in this
 * program.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * ShadowConfigNode/LightingConfigNode.
 */
class ReservoirConfigNode : public TypedNode<ReservoirConfigNodeConfig> {
public:
    using Base = TypedNode<ReservoirConfigNodeConfig>;

    ReservoirConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~ReservoirConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;

    // Sampled Lighting Inc3 M4: monotonic per-Execute counter driving ReservoirConfig.
    // frameParity -- see ExecuteImpl's own comment for why this must NOT be
    // pc.accumFrameCount (which resets on camera motion).
    uint32_t frameParityCounter_ = 0u;
};

} // namespace Vixen::RenderGraph
