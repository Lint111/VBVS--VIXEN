// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc2 M1: temporal-accumulation compute budget as drift-guarded
// data, wired DISABLED (enabled=0) — pure plumbing, zero visual delta.
// Sampled Lighting Inc2 M2: static EWMA (converging 1/N default) + hard
// reset-on-motion frame counter -- the first visually-active milestone.
// Sampled Lighting Inc2 M4: reprojectionEnabled opt-in mode -- when set, this
// node stops hard-resetting the frame counter on camera motion (resetOnMotion
// is ignored while reprojectionEnabled != 0) so accumulation continues through
// camera movement; the shader's own per-pixel reprojection+validation (against
// prevCameraConfig.prevViewProj, Inc2 M3) is what prevents ghosting instead.

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
// Sampled Lighting Inc2 M4: VIXEN_ACCUMULATION_REPROJECT=1 flips reprojectionEnabled on
// (opt-in, off by default so M1-M3 gates reproduce unchanged) -- the shader then reprojects
// +validates per-pixel instead of this node hard-resetting the whole frame on motion.
Vixen::Gpu::AccumulationConfig MakeDefaultAccumulationConfig() {
    Vixen::Gpu::AccumulationConfig cfg{};
    cfg.enabled             = 0u;      // disabled by default -- pure passthrough gate
    cfg.alpha               = 0.0f;    // sentinel: converging 1/N mode (the M2 default)
    cfg.maxFrames           = 256u;    // convergence cap
    cfg.resetOnMotion       = 1u;      // M2 default: hard reset on camera motion (no ghosting)
    cfg.reprojectionEnabled = 0u;      // M4 default OFF: M1-M3 behavior unchanged

    // Gate lever: VIXEN_ACCUMULATION_ENABLED=1 forces the enable path so a live gate can
    // capture both the disabled (byte-identical-to-Inc1) and enabled renders from the SAME
    // binary, no rebuild -- mirrors ShadowConfigNode's own VIXEN_SHADOW_CONFIG_ENABLED A/B
    // convention.
    if (const char* enabledEnv = std::getenv("VIXEN_ACCUMULATION_ENABLED")) {
        cfg.enabled = (enabledEnv[0] == '1') ? 1u : 0u;
    }

    // Gate lever: VIXEN_ACCUMULATION_REPROJECT=1 turns on M4's per-pixel reprojection mode
    // (implies continuing to accumulate through camera motion instead of M2's hard reset --
    // see ExecuteImpl's use of cfg.reprojectionEnabled below).
    if (const char* reprojectEnv = std::getenv("VIXEN_ACCUMULATION_REPROJECT")) {
        cfg.reprojectionEnabled = (reprojectEnv[0] == '1') ? 1u : 0u;
    }

    // Sampled Lighting Inc3 M4: the equal-error-vs-brute-force live gate demo scene
    // (VIXEN_RESTIR_GATE_DEMO) needs its OWN temporal reservoir reuse (DirectLighting.comp's
    // RIS+Combine block) to actually fire every frame -- that block is itself gated on
    // accumulationConfig.reprojectionEnabled (reusing the M2 worldPos/depth reproject
    // infrastructure), so without this, the demo's reservoir never accumulates past a single
    // frame's M=8 candidates and the readback measures high-variance single-frame noise
    // instead of the converged multi-frame estimate the gate's own kReadbackTick design
    // assumes. Explicit overrides (VIXEN_ACCUMULATION_ENABLED/_REPROJECT, above) still win if
    // set, matching every other VIXEN_*_DEMO block's "explicit override wins" convention.
    if (std::getenv("VIXEN_RESTIR_GATE_DEMO")) {
        if (!std::getenv("VIXEN_ACCUMULATION_ENABLED"))  cfg.enabled = 1u;
        if (!std::getenv("VIXEN_ACCUMULATION_REPROJECT")) cfg.reprojectionEnabled = 1u;
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
    // Sampled Lighting Inc2 M2/M4: consecutive-frame counter.
    // -------------------------------------------------------------------------------
    // Epsilon-compare this frame's camera pose against the last-seen pose (same idiom as
    // VulkanGraphApplication::UpdateBodySceneResidency). On the very first Execute, the
    // counter always resets to 1 (no valid prevViewProj/history yet either way). On motion:
    //   - reprojectionEnabled != 0 (M4): resetOnMotion is IGNORED -- the counter keeps
    //     incrementing through motion, because the shader now reprojects+validates
    //     per-pixel against prevCameraConfig.prevViewProj instead of relying on this
    //     node to blank the whole frame. Hard-resetting here would defeat the entire
    //     point of M4 (accumulating WHILE the camera moves).
    //   - reprojectionEnabled == 0 (M1-M3 behavior, M2's own default): unchanged --
    //     cfg.resetOnMotion != 0 resets the counter to 1 on any motion, which, fed back
    //     through the shader's alpha = 1/accumFrameCount, forces alpha = 1.0 (pure
    //     current frame) the instant the camera moves, i.e. zero ghosting by construction.
    // Otherwise the counter increments, clamped to cfg.maxFrames when set (0 = unbounded).
    const CameraData& cam = ctx.In(AccumulationConfigNodeConfig::CAMERA_DATA);

    const bool isFirstExecute = !frameCounterEverEvaluated_;
    const bool moved = isFirstExecute ||
        glm::distance2(cam.cameraPos, lastCameraPos_) > kAccumPosEpsilon ||
        glm::distance2(cam.cameraDir, lastCameraDir_) > kAccumDirEpsilon;
    frameCounterEverEvaluated_ = true;
    lastCameraPos_ = cam.cameraPos;
    lastCameraDir_ = cam.cameraDir;

    // Unchanged M1-M3 predicate (byte-identical when reprojectionEnabled == 0, the
    // default -- "moved" already folds in "first-ever Execute", exactly as before this
    // milestone). M4 adds ONE extra guard: while reprojectionEnabled != 0, resetOnMotion
    // is ignored so a moving camera no longer hard-resets the whole frame -- the shader's
    // own per-pixel reprojection+validation (against prevCameraConfig.prevViewProj) takes
    // over that job instead. Hard-resetting here in reprojection mode would defeat the
    // entire point of M4 (accumulating WHILE the camera moves). isFirstExecute is NEVER
    // suppressed, in any mode -- prevViewProj is CameraNode's own self-seeded value on
    // frame 1 (see its CompileImpl) and historyImage is uninitialized, so accumFrameCount
    // must be 1 (not 2 -- accumFrameCounter_'s member-init value IS 1, so without this the
    // very first Execute would fall through to the ++ below and report 2) regardless of
    // resetOnMotion/reprojectionEnabled.
    const bool hardReset = isFirstExecute ||
        (moved && cfg.resetOnMotion != 0u && cfg.reprojectionEnabled == 0u);
    if (hardReset) {
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
