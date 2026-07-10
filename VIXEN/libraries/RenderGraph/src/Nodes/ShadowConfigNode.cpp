// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc1 M4: shadow-pass compute budget as drift-guarded data.

#include "Nodes/ShadowConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/ShadowConfig.g.h"
#include "VulkanDevice.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t ShadowConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: hard shadows on, whole-scene shadow-ray reach, bias tuned to
// kill self-intersection acne without visible peter-panning against this scene's
// body scale (kWorldGridSize=10 * renderScale, see BodyOctreeSceneNode/
// BuildRenderGraph.cpp's default 3-body layout) — live-tuned on the real GPU per
// the M4 gate.
Vixen::Gpu::ShadowConfig MakeDefaultShadowConfig() {
    Vixen::Gpu::ShadowConfig cfg{};
    cfg.enabled           = 1u;
    cfg.raysPerLight      = 1u;              // Inc1: hard shadows only
    cfg.maxShadowDistance = 1000.0f;          // whole-scene reach (world units)
    cfg.biasEpsilon       = 0.01f;            // self-intersection guard

    // M4 gate lever: VIXEN_SHADOW_CONFIG_ENABLED=0 forces the disable path so the live
    // gate can capture both the shadows-on render and the byte-identity-vs-M3-baseline
    // disabled render from the SAME binary, no rebuild — mirrors the
    // VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE demo-knob convention (BuildRenderGraph.cpp).
    if (const char* enabledEnv = std::getenv("VIXEN_SHADOW_CONFIG_ENABLED")) {
        cfg.enabled = (enabledEnv[0] == '0') ? 0u : 1u;
    }
    return cfg;
}

}  // namespace

// ====== ShadowConfigNodeType ======

std::unique_ptr<NodeInstance> ShadowConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<ShadowConfigNode>(n, const_cast<ShadowConfigNodeType*>(this));
}

// ====== ShadowConfigNode ======

ShadowConfigNode::ShadowConfigNode(const std::string& n, NodeType* t)
    : TypedNode<ShadowConfigNodeConfig>(n, t)
{
}

void ShadowConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[ShadowConfigNode] Setup (graph-scope initialization)");
}

void ShadowConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ShadowConfigNode] Compile START");

    SetDevice(ctx.In(ShadowConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[ShadowConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::ShadowConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[ShadowConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[ShadowConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[ShadowConfigNode] Outputs published");
}

void ShadowConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(ShadowConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Static default content (no UI/authoring this increment). Re-uploaded every
    // frame anyway: 16 B is negligible and this keeps the node ready for a future
    // SetShadowConfig() with no rewiring.
    const Vixen::Gpu::ShadowConfig cfg = MakeDefaultShadowConfig();

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void ShadowConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors LightingConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[ShadowConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[ShadowConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ShadowConfigNodeType);
