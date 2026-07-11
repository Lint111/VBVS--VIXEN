// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M4: uploads the CPU-computed mip-cut light-tree into a
// ring-buffered SSBO so RIS candidate generation can sample it on the GPU.

#include "Nodes/LightTreeBufferNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/LightTreeBuffer.g.h"
#include "VulkanDevice.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

const uint32_t LightTreeBufferNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

// ====== LightTreeBufferNodeType ======

std::unique_ptr<NodeInstance> LightTreeBufferNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<LightTreeBufferNode>(n, const_cast<LightTreeBufferNodeType*>(this));
}

// ====== LightTreeBufferNode ======

LightTreeBufferNode::LightTreeBufferNode(const std::string& n, NodeType* t)
    : TypedNode<LightTreeBufferNodeConfig>(n, t)
{
}

void LightTreeBufferNode::SetLightTreeCut(std::vector<Vixen::SVO::LightTreeNode> cut) {
    std::lock_guard<std::mutex> lock(cutMutex_);
    cut_ = std::move(cut);
    NODE_LOG_INFO("[LightTreeBufferNode] SetLightTreeCut: " +
                  std::to_string(cut_.size()) + " nodes staged for next Execute");
}

void LightTreeBufferNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[LightTreeBufferNode] Setup (graph-scope initialization)");
}

void LightTreeBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[LightTreeBufferNode] Compile START");

    SetDevice(ctx.In(LightTreeBufferNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[LightTreeBufferNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::LightTreeBuffer);

    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[LightTreeBufferNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[LightTreeBufferNode] Reusing persistent ring buffers across recompile");
    }

    ctx.Out(LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[LightTreeBufferNode] Outputs published");
}

void LightTreeBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    uint32_t frameIndex = ctx.In(LightTreeBufferNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    Vixen::Gpu::LightTreeBuffer gpuBuf{};
    {
        std::lock_guard<std::mutex> lock(cutMutex_);
        uint32_t n = static_cast<uint32_t>(cut_.size());
        constexpr uint32_t kCapacity =
            sizeof(Vixen::Gpu::LightTreeBuffer::nodes) / sizeof(Vixen::Gpu::LightTreeGpuNode);
        if (n > kCapacity) {
            NODE_LOG_WARNING("[LightTreeBufferNode] cut has " + std::to_string(n) +
                             " nodes, exceeds kMaxLightTreeNodes=" + std::to_string(kCapacity) +
                             " — truncating (see LightTreeBuffer.cs's cap rationale)");
            n = kCapacity;
        }
        gpuBuf.nodeCount = n;
        for (uint32_t i = 0; i < n; ++i) {
            const Vixen::SVO::LightTreeNode& src = cut_[i];
            Vixen::Gpu::LightTreeGpuNode& dst = gpuBuf.nodes[i];
            dst.worldPosX   = src.worldPos.x;
            dst.worldPosY   = src.worldPos.y;
            dst.worldPosZ   = src.worldPos.z;
            dst.worldExtent = src.worldExtent;
            dst.intensity   = src.intensity;
            dst.coverage    = src.coverage;
        }
    }

    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &gpuBuf, sizeof(gpuBuf));
    }

    ctx.Out(LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void LightTreeBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[LightTreeBufferNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[LightTreeBufferNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::LightTreeBufferNodeType);
