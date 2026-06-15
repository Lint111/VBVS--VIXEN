#include "Nodes/DynamicInstanceBufferNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t DynamicInstanceBufferNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

// ====== DynamicInstanceBufferNodeType ======

std::unique_ptr<NodeInstance> DynamicInstanceBufferNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<DynamicInstanceBufferNode>(n, const_cast<DynamicInstanceBufferNodeType*>(this));
}

// ====== DynamicInstanceBufferNode ======

DynamicInstanceBufferNode::DynamicInstanceBufferNode(const std::string& n, NodeType* t)
    : TypedNode<DynamicInstanceBufferNodeConfig>(n, t)
{
}

void DynamicInstanceBufferNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access).
    NODE_LOG_DEBUG("[DynamicInstanceBufferNode] Setup (graph-scope initialization)");
}

void DynamicInstanceBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[DynamicInstanceBufferNode] Compile START");

    device_ = ctx.In(DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        throw std::runtime_error("[DynamicInstanceBufferNode] VULKAN_DEVICE_IN is null");
    }

    gridDim_       = GetParameterValue<uint32_t>(DynamicInstanceBufferNodeConfig::PARAM_GRID_DIM, 8u);
    spacing_       = GetParameterValue<float>(DynamicInstanceBufferNodeConfig::PARAM_SPACING, 2.0f);
    rotationSpeed_ = GetParameterValue<float>(DynamicInstanceBufferNodeConfig::PARAM_ROTATION_SPEED, 0.01f);
    if (gridDim_ == 0) {
        throw std::runtime_error("[DynamicInstanceBufferNode] gridDim must be > 0");
    }
    instanceCount_ = gridDim_ * gridDim_;

    const VkDeviceSize bufferSize =
        static_cast<VkDeviceSize>(instanceCount_) * sizeof(glm::mat4);

    // FR-7: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(device_, kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, bufferSize);
        }
        NODE_LOG_INFO("[DynamicInstanceBufferNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(bufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[DynamicInstanceBufferNode] Reusing persistent ring buffers across recompile");
    }

    // Publish the instance count (stable) and an initial buffer (frame 0) so any
    // compile-time descriptor wiring has a valid handle before the first Execute.
    ctx.Out(DynamicInstanceBufferNodeConfig::INSTANCE_COUNT, instanceCount_);
    ctx.Out(DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[DynamicInstanceBufferNode] Outputs published (gridDim=" + std::to_string(gridDim_) +
                  ", instances=" + std::to_string(instanceCount_) +
                  ", rotationSpeed=" + std::to_string(rotationSpeed_) + ")");
}

void DynamicInstanceBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // Advance the deterministic animation clock (frame-counter based — no wall clock).
    ++frameCounter_;

    // Recompute the animated per-instance transforms.
    std::vector<glm::mat4> transforms;
    transforms.reserve(instanceCount_);
    const float half = gridDim_ / 2.0f;
    uint32_t linearIndex = 0;
    for (uint32_t y = 0; y < gridDim_; ++y) {
        for (uint32_t x = 0; x < gridDim_; ++x, ++linearIndex) {
            const glm::vec3 pos(
                (static_cast<float>(x) - half) * spacing_,
                (static_cast<float>(y) - half) * spacing_,
                0.0f);
            // Per-instance phase so the grid does not rotate in lockstep.
            const float angle = static_cast<float>(frameCounter_) * rotationSpeed_ *
                                (1.0f + 0.1f * static_cast<float>(linearIndex));
            const glm::mat4 model =
                glm::translate(glm::mat4(1.0f), pos) *
                glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
            transforms.push_back(model);
        }
    }

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, transforms.data(),
                    static_cast<size_t>(instanceCount_) * sizeof(glm::mat4));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void DynamicInstanceBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    // FR-7: persist across recompile; release only on final application teardown.
    if (ctx.reason != CleanupReason::FinalTeardown) {
        NODE_LOG_INFO("[DynamicInstanceBufferNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[DynamicInstanceBufferNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph
