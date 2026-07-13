// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc4 M2: DDGI probe-grid placement + compute budget as drift-guarded data.

#include "Nodes/ProbeGridConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/ProbeGridConfig.g.h"
#include "VulkanDevice.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t ProbeGridConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: probeGridEnabled=0 (M2 ships scaffolding only — no probe-update/
// gather shading logic reads this yet, that's M3+). origin/spacing/count are pre-set
// to a sane default grid covering a 32-unit cube centered on (16,16,16) -- the same
// world region VIXEN_RESTIR_GATE_DEMO's own emissive gate scene occupies (kCenter,
// kR=10 in BuildRenderGraph.cpp) -- so the struct is ready to flip on with no further
// authoring once the probe-update pass (M3) lands. 8x8x8 = 512 probes at 4-unit
// spacing is a generous-but-bounded starting grid; M6's real-GPU probe-ray-budget
// bench is the design's own flagged pass-2 open question for finalizing this density,
// not a guess made here.
Vixen::Gpu::ProbeGridConfig MakeDefaultProbeGridConfig() {
    Vixen::Gpu::ProbeGridConfig cfg{};
    cfg.probeGridEnabled = 0u;      // M2: no probe-update/gather shading reads this yet
    cfg.originX          = 0.0f;    // grid corner -- covers [0,32) on each axis at 4-unit spacing
    cfg.originY          = 0.0f;
    cfg.originZ          = 0.0f;
    cfg.spacingX         = 4.0f;    // world units between adjacent probes
    cfg.spacingY         = 4.0f;
    cfg.spacingZ         = 4.0f;
    cfg.countX           = 8u;      // 8x8x8 = 512 probes
    cfg.countY           = 8u;
    cfg.countZ           = 8u;
    cfg.raysPerProbe     = 64u;     // M3 fixed-sample-set default (spherical Fibonacci count)
    cfg.hysteresisRate   = 0.02f;   // EWMA blend rate, mirrors AccumulationConfig.alpha's role
    cfg.amortizationFactor = 1u;    // Inc5 M1 default: every probe updates every frame (byte-
                                     // identical escape hatch — see ProbeUpdate.comp's early-out)

    // M2 gate lever: VIXEN_PROBE_GRID_CONFIG_ENABLED=1 forces the enable path so a
    // future live gate can capture both states from the SAME binary, no rebuild —
    // mirrors VIXEN_RESERVOIR_CONFIG_ENABLED's convention. No consumer reads
    // probeGridEnabled yet (M2 scope), so flipping this today has no visual effect;
    // the lever exists so M3 doesn't need to invent it.
    if (const char* enabledEnv = std::getenv("VIXEN_PROBE_GRID_CONFIG_ENABLED")) {
        cfg.probeGridEnabled = (enabledEnv[0] == '1') ? 1u : 0u;
    }

    // M6 bench lever: VIXEN_PROBE_RAYS_PER_PROBE=<n> overrides the fixed raysPerProbe=64
    // default so the design's own flagged pass-2 open question (real-GPU cost vs ray
    // budget) can be measured across multiple values from the SAME binary, no rebuild —
    // same convention as VIXEN_PROBE_GRID_CONFIG_ENABLED above. Clamped to
    // PROBE_UPDATE_MAX_RAYS_PER_PROBE (256, ProbeUpdate.comp's workgroup-size ceiling —
    // see M3's atlas-write-correctness note) so a bad env value can't request more
    // invocations than the shader's fixed local_size_x provides.
    if (const char* raysEnv = std::getenv("VIXEN_PROBE_RAYS_PER_PROBE")) {
        long v = std::strtol(raysEnv, nullptr, 10);
        if (v > 0 && v <= 256) {
            cfg.raysPerProbe = static_cast<uint32_t>(v);
        }
    }

    // Inc5 M1 bench lever: VIXEN_DDGI_AMORTIZATION_FACTOR=<n> overrides the fixed
    // amortizationFactor=1 default so M2's real-GPU cost matrix can be measured
    // across multiple values from the SAME binary, no rebuild — exact same
    // convention as VIXEN_PROBE_RAYS_PER_PROBE above. No upper clamp beyond
    // fitting in uint32_t: any factor >= probeCount degenerates to "one probe
    // updates per N frames," a valid (if extreme) point on the curve, not an
    // error — the shader's modulo math handles it without special-casing.
    if (const char* amortEnv = std::getenv("VIXEN_DDGI_AMORTIZATION_FACTOR")) {
        long v = std::strtol(amortEnv, nullptr, 10);
        if (v > 0) {
            cfg.amortizationFactor = static_cast<uint32_t>(v);
        }
    }
    return cfg;
}

}  // namespace

// ====== ProbeGridConfigNodeType ======

std::unique_ptr<NodeInstance> ProbeGridConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<ProbeGridConfigNode>(n, const_cast<ProbeGridConfigNodeType*>(this));
}

// ====== ProbeGridConfigNode ======

ProbeGridConfigNode::ProbeGridConfigNode(const std::string& n, NodeType* t)
    : TypedNode<ProbeGridConfigNodeConfig>(n, t)
{
}

void ProbeGridConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[ProbeGridConfigNode] Setup (graph-scope initialization)");
}

void ProbeGridConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ProbeGridConfigNode] Compile START");

    SetDevice(ctx.In(ProbeGridConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[ProbeGridConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::ProbeGridConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[ProbeGridConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[ProbeGridConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[ProbeGridConfigNode] Outputs published");
}

void ProbeGridConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(ProbeGridConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Static default content (no UI/authoring this milestone). Re-uploaded every
    // frame anyway: 56 B is negligible and this keeps the node ready for a future
    // SetProbeGridConfig() with no rewiring.
    Vixen::Gpu::ProbeGridConfig cfg = MakeDefaultProbeGridConfig();

    // Inc5 M1: monotonic per-Execute counter driving amortizationFactor's
    // round-robin subset selection in ProbeUpdate.comp -- increments
    // unconditionally every Execute, mirroring ReservoirConfigNode's
    // frameParityCounter_ precedent (deliberately NOT pc.accumFrameCount,
    // which resets on camera motion and would repeat the same active-probe
    // subset on every reset instead of continuing the rotation).
    cfg.frameCounter = amortizationFrameCounter_++;

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void ProbeGridConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors ReservoirConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[ProbeGridConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[ProbeGridConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ProbeGridConfigNodeType);
