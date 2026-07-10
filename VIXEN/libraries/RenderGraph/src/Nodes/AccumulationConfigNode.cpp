// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc2 M1: temporal-accumulation compute budget as drift-guarded
// data, wired DISABLED (enabled=0) — pure plumbing, zero visual delta.

#include "Nodes/AccumulationConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/AccumulationConfig.g.h"
#include "VulkanDevice.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t AccumulationConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: DISABLED this milestone (M1's byte-identity-vs-Inc1 gate).
// alpha=0 is the sentinel for the DEFAULT converging 1/N mode (M2 default, per the
// plan's Task 2); maxFrames caps that convergence once consumption is wired.
// resetOnMotion=0 (M2's whole-frame-reset fallback, unused until M2/M4 wire it).
Vixen::Gpu::AccumulationConfig MakeDefaultAccumulationConfig() {
    Vixen::Gpu::AccumulationConfig cfg{};
    cfg.enabled       = 0u;      // M1: disabled by default -- pure passthrough gate
    cfg.alpha         = 0.0f;    // sentinel: converging 1/N mode (the eventual default)
    cfg.maxFrames     = 64u;     // convergence cap once M2 wires consumption
    cfg.resetOnMotion = 0u;      // M2/M4 fallback toggle, inert until wired

    // M1 gate lever: VIXEN_ACCUMULATION_ENABLED=1 forces the enable path so a later
    // milestone's live gate can capture both the disabled (byte-identical-to-Inc1) and
    // enabled renders from the SAME binary, no rebuild -- mirrors ShadowConfigNode's own
    // VIXEN_SHADOW_CONFIG_ENABLED A/B convention.
    if (const char* enabledEnv = std::getenv("VIXEN_ACCUMULATION_ENABLED")) {
        cfg.enabled = (enabledEnv[0] == '1') ? 1u : 0u;
    }
    return cfg;
}

}  // namespace

// ====== AccumulationConfigNodeType ======

std::unique_ptr<NodeInstance> AccumulationConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<AccumulationConfigNode>(n, const_cast<AccumulationConfigNodeType*>(this));
}

// ====== AccumulationConfigNode ======

AccumulationConfigNode::AccumulationConfigNode(const std::string& n, NodeType* t)
    : TypedNode<AccumulationConfigNodeConfig>(n, t)
{
}

void AccumulationConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[AccumulationConfigNode] Setup (graph-scope initialization)");
}

void AccumulationConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[AccumulationConfigNode] Compile START");

    SetDevice(ctx.In(AccumulationConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[AccumulationConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::AccumulationConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[AccumulationConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[AccumulationConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[AccumulationConfigNode] Outputs published");
}

void AccumulationConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(AccumulationConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Static default content (no UI/authoring this increment). Re-uploaded every
    // frame anyway: 16 B is negligible and this keeps the node ready for a future
    // SetAccumulationConfig() with no rewiring.
    const Vixen::Gpu::AccumulationConfig cfg = MakeDefaultAccumulationConfig();

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void AccumulationConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors ShadowConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[AccumulationConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[AccumulationConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::AccumulationConfigNodeType);
