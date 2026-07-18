// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc0 M3: light data wired via a generated [GpuStruct].

#include "Nodes/LightingConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/LightingConfig.g.h"
#include "VulkanDevice.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t LightingConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: the single directional light Lighting.glsl previously
// hardcoded (direction normalize(1,1,-1), white radiance, ambientIntensity
// 0.3) — reproducing it byte-for-byte through the data path is this
// milestone's whole gate (M2's AFTER capture must stay byte-identical).
Vixen::Gpu::LightingConfig MakeDefaultLightingConfig() {
    Vixen::Gpu::LightingConfig cfg{};
    cfg.lightCount       = 1u;
    cfg.ambientIntensity = 0.3f;

    const float dx = 1.0f, dy = 1.0f, dz = -1.0f;
    const float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);

    cfg.lights[0].direction_or_positionX = dx * invLen;
    cfg.lights[0].direction_or_positionY = dy * invLen;
    cfg.lights[0].direction_or_positionZ = dz * invLen;
    cfg.lights[0].kind      = 0u;  // directional
    cfg.lights[0].radianceX = 1.0f;
    cfg.lights[0].radianceY = 1.0f;
    cfg.lights[0].radianceZ = 1.0f;
    cfg.lights[0].range     = 0.0f;  // unused for directional
    return cfg;
}

// M11.1: the Cornell demo authors its own light -- the ceiling area-emitter
// (body 5, CornellBoxSceneDefinition.h kLightEmissionIntensity), baked into the
// light-tree and lit via ReSTIR-direct + DDGI-indirect. This global default's
// directional light was never authored by the Cornell scene; left on, it adds
// an unwanted second light whose grazing shadow rays produced the M10 floor/
// wall blotching. Scoped to the Cornell demo env-gate only (both variants, so
// baked vs virtual A/B stays apples-to-apples) -- every other scene keeps the
// directional light unchanged. Ambient stays: it's a flat baseline term, not
// the blotching source (the shadow ray on the directional light is).
bool IsCornellDemo() {
    return std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO") != nullptr ||
           std::getenv("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO") != nullptr;
}

}  // namespace

// ====== LightingConfigNodeType ======

std::unique_ptr<NodeInstance> LightingConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<LightingConfigNode>(n, const_cast<LightingConfigNodeType*>(this));
}

// ====== LightingConfigNode ======

LightingConfigNode::LightingConfigNode(const std::string& n, NodeType* t)
    : TypedNode<LightingConfigNodeConfig>(n, t)
{
}

void LightingConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[LightingConfigNode] Setup (graph-scope initialization)");
}

void LightingConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[LightingConfigNode] Compile START");

    SetDevice(ctx.In(LightingConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[LightingConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::LightingConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[LightingConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[LightingConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[LightingConfigNode] Outputs published");
}

void LightingConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(LightingConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Static default content (no UI/authoring this increment — Sampled Lighting
    // Inc0 §4). Re-uploaded every frame anyway: 144 B is negligible and this
    // keeps the node ready for a future SetLights() with no rewiring.
    Vixen::Gpu::LightingConfig cfg = MakeDefaultLightingConfig();
    if (IsCornellDemo()) {
        // Ceiling area-emitter (light-tree/ReSTIR/DDGI) is the Cornell scene's
        // sole light; drop the stray directional light but keep the ambient
        // baseline (not the blotching source -- see IsCornellDemo() comment).
        cfg.lightCount = 0u;
    }

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void LightingConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[LightingConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[LightingConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::LightingConfigNodeType);
