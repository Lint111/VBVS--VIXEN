// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M3: ReSTIR reservoir-sampling compute budget as drift-guarded data.

#include "Nodes/ReservoirConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/ReservoirConfig.g.h"
#include "VulkanDevice.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t ReservoirConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: reservoirEnabled=0 (M3 ships scaffolding only — no
// reservoir/ReSTIR shading logic reads this yet, that's M4+). candidateCount/
// spatialRadius/spatialCount/temporalCap are pre-set to sane M4/M5 defaults
// so the struct is ready to flip on with no further authoring once the
// reservoir passes land. biasedModeEnabled stays 0 (unbiased-only this
// program, per the plan's Self-Review — the biased 35-65x mode is explicitly
// deferred). lightTreeCutThreshold mirrors LightTree.h's own
// LightTreeCutParams::powerThreshold default.
Vixen::Gpu::ReservoirConfig MakeDefaultReservoirConfig() {
    Vixen::Gpu::ReservoirConfig cfg{};
    cfg.reservoirEnabled      = 0u;      // M3: no reservoir shading reads this yet
    cfg.candidateCount        = 8u;      // M4 RIS candidate count default
    cfg.spatialRadius         = 16.0f;   // M5 spatial-reuse search radius (pixels)
    cfg.spatialCount          = 4u;      // M5 spatial-reuse neighbor count
    cfg.temporalCap           = 32u;     // M4 temporal reservoir sample-count cap
    cfg.biasedModeEnabled     = 0u;      // unbiased-only (biased mode deferred)
    cfg.lightTreeCutThreshold = 0.01f;   // mirrors LightTreeCutParams::powerThreshold default

    // M3 gate lever: VIXEN_RESERVOIR_CONFIG_ENABLED=1 forces the enable path so a
    // future live gate can capture both states from the SAME binary, no rebuild —
    // mirrors VIXEN_SHADOW_CONFIG_ENABLED's convention. No consumer reads
    // reservoirEnabled yet (M3 scope), so flipping this today has no visual effect;
    // the lever exists so M4 doesn't need to invent it.
    if (const char* enabledEnv = std::getenv("VIXEN_RESERVOIR_CONFIG_ENABLED")) {
        cfg.reservoirEnabled = (enabledEnv[0] == '1') ? 1u : 0u;
    }
    return cfg;
}

}  // namespace

// ====== ReservoirConfigNodeType ======

std::unique_ptr<NodeInstance> ReservoirConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<ReservoirConfigNode>(n, const_cast<ReservoirConfigNodeType*>(this));
}

// ====== ReservoirConfigNode ======

ReservoirConfigNode::ReservoirConfigNode(const std::string& n, NodeType* t)
    : TypedNode<ReservoirConfigNodeConfig>(n, t)
{
}

void ReservoirConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[ReservoirConfigNode] Setup (graph-scope initialization)");
}

void ReservoirConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ReservoirConfigNode] Compile START");

    SetDevice(ctx.In(ReservoirConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[ReservoirConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::ReservoirConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[ReservoirConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[ReservoirConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(ReservoirConfigNodeConfig::RESERVOIR_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[ReservoirConfigNode] Outputs published");
}

void ReservoirConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(ReservoirConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Static default content (no UI/authoring this milestone). Re-uploaded every
    // frame anyway: 28 B is negligible and this keeps the node ready for a future
    // SetReservoirConfig() with no rewiring.
    const Vixen::Gpu::ReservoirConfig cfg = MakeDefaultReservoirConfig();

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(ReservoirConfigNodeConfig::RESERVOIR_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void ReservoirConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors ShadowConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[ReservoirConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[ReservoirConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ReservoirConfigNodeType);
