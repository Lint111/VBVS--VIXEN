// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc2 M3: prev-frame camera matrix plumbing, wired but NOT
// consumed by the shader — pure plumbing, zero visual delta (M4 consumes it).

#include "Nodes/PrevCameraConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/PrevCameraConfig.g.h"
#include "VulkanDevice.h"
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t PrevCameraConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

// ====== PrevCameraConfigNodeType ======

std::unique_ptr<NodeInstance> PrevCameraConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<PrevCameraConfigNode>(n, const_cast<PrevCameraConfigNodeType*>(this));
}

// ====== PrevCameraConfigNode ======

PrevCameraConfigNode::PrevCameraConfigNode(const std::string& n, NodeType* t)
    : TypedNode<PrevCameraConfigNodeConfig>(n, t)
{
}

void PrevCameraConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[PrevCameraConfigNode] Setup (graph-scope initialization)");
}

void PrevCameraConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[PrevCameraConfigNode] Compile START");

    SetDevice(ctx.In(PrevCameraConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[PrevCameraConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::PrevCameraConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[PrevCameraConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[PrevCameraConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[PrevCameraConfigNode] Outputs published");
}

void PrevCameraConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(PrevCameraConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Copy CameraNode's retained prev-frame view*proj verbatim into this frame's record —
    // no other content, no computation (see this node's file header).
    Vixen::Gpu::PrevCameraConfig cfg{};
    cfg.prevViewProj = ctx.In(PrevCameraConfigNodeConfig::PREV_VIEW_PROJ);

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void PrevCameraConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors AccumulationConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[PrevCameraConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[PrevCameraConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::PrevCameraConfigNodeType);
