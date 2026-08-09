// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// B2 (docs/plans/2026-08-04-wavefront-recipe-shading.md): ring-buffered
// replacement for the un-ringed hit_accum_params_buffer StorageBufferNode.

#include "Nodes/HitAccumParamsConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t HitAccumParamsConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

// HitAccumParams: uint frameEpoch, float primaryCoef/primaryBias/detailSize0,
// vec4 camPos, vec4 camForward — 48 bytes (mirrors VulkanGraphApplication::
// PreTick's HitAccumParamsCpu / the shader's HitAccumParams).
static constexpr VkDeviceSize kHitAccumParamsBufferSize = 48;

// ====== HitAccumParamsConfigNodeType ======

std::unique_ptr<NodeInstance> HitAccumParamsConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<HitAccumParamsConfigNode>(n, const_cast<HitAccumParamsConfigNodeType*>(this));
}

// ====== HitAccumParamsConfigNode ======

HitAccumParamsConfigNode::HitAccumParamsConfigNode(const std::string& n, NodeType* t)
    : TypedNode<HitAccumParamsConfigNodeConfig>(n, t)
{
}

void HitAccumParamsConfigNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[HitAccumParamsConfigNode] Setup (graph-scope initialization)");
}

void HitAccumParamsConfigNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[HitAccumParamsConfigNode] Compile START");

    SetDevice(ctx.In(HitAccumParamsConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[HitAccumParamsConfigNode] VULKAN_DEVICE_IN is null");
    }

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kHitAccumParamsBufferSize);
        }
        NODE_LOG_INFO("[HitAccumParamsConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kHitAccumParamsBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[HitAccumParamsConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(HitAccumParamsConfigNodeConfig::HIT_ACCUM_PARAMS_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[HitAccumParamsConfigNode] Outputs published");
}

void HitAccumParamsConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    // Content is written by VulkanGraphApplication::PreTick (before this
    // Execute pass runs), via MapCurrentForWrite(frameIndex) with the SAME
    // frameIndex FrameSyncNode::GetCurrentFrameIndex() gave it — this node
    // only re-emits that slot so the descriptor binds what PreTick wrote.
    uint32_t frameIndex = ctx.In(HitAccumParamsConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;
    ctx.Out(HitAccumParamsConfigNodeConfig::HIT_ACCUM_PARAMS_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void* HitAccumParamsConfigNode::MapCurrentForWrite(uint32_t frameIndex) const {
    if (!perFrame_.IsInitialized()) return nullptr;
    return perFrame_.GetUniformBufferMapped(frameIndex % kRingSize);
}

void HitAccumParamsConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (KI-004 class) left stale handles
    // that crashed the first post-recovery use/teardown. Mirrors ShadowConfigNode's guard.
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[HitAccumParamsConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[HitAccumParamsConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::HitAccumParamsConfigNodeType);
