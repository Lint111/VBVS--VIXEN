// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc2 M1: temporal-accumulation compute budget as drift-guarded
// data, wired DISABLED (enabled=0) — pure plumbing, zero visual delta.
// Sampled Lighting Inc2 M2: static EWMA (converging 1/N default) + hard
// reset-on-motion frame counter -- the first visually-active milestone.

#include "Nodes/AccumulationConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/AccumulationConfig.g.h"
#include "VulkanDevice.h"
#include <glm/gtx/norm.hpp>   // glm::distance2 (change-detection epsilon compare)
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t AccumulationConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: DISABLED unless VIXEN_ACCUMULATION_ENABLED=1 (unchanged M1 gate
// lever). When enabled, M2's default behavior is: alpha=0 (sentinel for the converging
// 1/N mode -- BodyInstanceRayMarch.comp's accumulate seam), maxFrames caps that
// convergence, resetOnMotion=1 (hard-reset the frame counter to 1 on any camera motion,
// giving zero ghosting by construction -- see AccumulationConfigNode::ExecuteImpl).
Vixen::Gpu::AccumulationConfig MakeDefaultAccumulationConfig() {
    Vixen::Gpu::AccumulationConfig cfg{};
    cfg.enabled       = 0u;      // disabled by default -- pure passthrough gate
    cfg.alpha         = 0.0f;    // sentinel: converging 1/N mode (the M2 default)
    cfg.maxFrames     = 256u;    // convergence cap
    cfg.resetOnMotion = 1u;      // M2 default: hard reset on camera motion (no ghosting)

    // Gate lever: VIXEN_ACCUMULATION_ENABLED=1 forces the enable path so a live gate can
    // capture both the disabled (byte-identical-to-Inc1) and enabled renders from the SAME
    // binary, no rebuild -- mirrors ShadowConfigNode's own VIXEN_SHADOW_CONFIG_ENABLED A/B
    // convention.
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

    // Force the frame counter to restart on the very next Execute (Sampled Lighting Inc2 M2).
    // Every Compile -- the first one AND every later recompile (e.g. a window resize) -- means
    // AccumulationHistoryNode may have just (re)created historyImage with uninitialized content
    // (see that node's own file header). A resize changes CameraData::aspect, not cameraPos/
    // cameraDir, so the motion-epsilon check alone would NOT catch this case and could blend
    // against garbage. Resetting frameCounterEverEvaluated_ here makes ExecuteImpl take its
    // existing "first frame" path unconditionally, which resets the counter to 1 and (via the
    // shader's alpha>=1.0 guard) skips the historyImage read entirely.
    frameCounterEverEvaluated_ = false;

    NODE_LOG_INFO("[AccumulationConfigNode] Outputs published");
}

namespace {
// Same epsilon convention as VulkanGraphApplication::UpdateBodySceneResidency's own
// cameraPos/cameraDir change-detection (position needs a coarser threshold than a unit
// direction vector).
constexpr float kAccumPosEpsilon = 1e-4f;
constexpr float kAccumDirEpsilon = 1e-5f;
}  // namespace

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

    // -------------------------------------------------------------------------------
    // Sampled Lighting Inc2 M2: consecutive-static-camera frame counter.
    // -------------------------------------------------------------------------------
    // Epsilon-compare this frame's camera pose against the last-seen pose (same idiom as
    // VulkanGraphApplication::UpdateBodySceneResidency). On the very first Execute, or on
    // any motion while cfg.resetOnMotion != 0, the counter resets to 1 -- which, fed back
    // through the shader's alpha = 1/accumFrameCount, forces alpha = 1.0 (pure current
    // frame) the instant the camera moves, i.e. zero ghosting by construction. Otherwise
    // the counter increments, clamped to cfg.maxFrames when set (0 = unbounded).
    const CameraData& cam = ctx.In(AccumulationConfigNodeConfig::CAMERA_DATA);

    const bool moved = !frameCounterEverEvaluated_ ||
        glm::distance2(cam.cameraPos, lastCameraPos_) > kAccumPosEpsilon ||
        glm::distance2(cam.cameraDir, lastCameraDir_) > kAccumDirEpsilon;
    frameCounterEverEvaluated_ = true;
    lastCameraPos_ = cam.cameraPos;
    lastCameraDir_ = cam.cameraDir;

    if (moved && cfg.resetOnMotion != 0u) {
        accumFrameCounter_ = 1u;
    } else {
        ++accumFrameCounter_;
        if (cfg.maxFrames > 0u && accumFrameCounter_ > cfg.maxFrames) {
            accumFrameCounter_ = cfg.maxFrames;
        }
    }

    ctx.Out(AccumulationConfigNodeConfig::FRAME_COUNTER, accumFrameCounter_);
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
