// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/ProbeGridConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for ProbeGridConfigNode.
 */
class ProbeGridConfigNodeType : public TypedNodeType<ProbeGridConfigNodeConfig> {
public:
    ProbeGridConfigNodeType(const std::string& typeName = "ProbeGridConfig")
        : TypedNodeType<ProbeGridConfigNodeConfig>(typeName) {}
    virtual ~ProbeGridConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::ProbeGridConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * ReservoirConfigNode's PerFrameResources ring pattern exactly).
 *
 * Separate node vs. extending an existing config node (same reasoning
 * ReservoirConfigNode's own file header documents): a separate node mirrors
 * the Inc0 precedent 1:1 (one [GpuStruct] record type per node, one ring per
 * node), keeps ProbeGridConfig's own lifecycle/content independent of
 * lighting/shadow/accumulation/reservoir data (a future DDGI authoring UI
 * would only touch this node), and avoids growing an unrelated node's
 * config/slot surface for a logically distinct concern (DDGI probe-grid
 * placement + compute budget, not light or shadow DATA).
 *
 * Content is static this milestone (no UI/authoring): probeGridEnabled=0 by
 * default — M2 ships the struct + upload plumbing as scaffolding for M3-M6's
 * probe-update pass / Chebyshev visibility / shade-pass gather (this
 * program's Task 3-6); nothing reads this buffer's contents yet. Re-uploaded
 * every Execute (56 B, negligible) so a future milestone can mutate it via
 * SetProbeGridConfig() with no graph rewiring — same pattern as every prior
 * *ConfigNode in this program.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl) — identical KI-004-safe pattern to
 * ReservoirConfigNode/ShadowConfigNode.
 */
class ProbeGridConfigNode : public TypedNode<ProbeGridConfigNodeConfig> {
public:
    using Base = TypedNode<ProbeGridConfigNodeConfig>;

    ProbeGridConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~ProbeGridConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;

    // Sampled Lighting Inc5 M1: THIS node's own monotonic per-Execute counter,
    // driving amortizationFactor's round-robin subset selection. Deliberately
    // NOT pc.accumFrameCount (resets on camera motion) -- mirrors
    // ReservoirConfigNode's frameParityCounter_ precedent exactly (same
    // rationale: amortization's active-probe subset must keep rotating
    // regardless of camera motion/accumulation state).
    uint32_t amortizationFrameCounter_ = 0;
};

} // namespace Vixen::RenderGraph
