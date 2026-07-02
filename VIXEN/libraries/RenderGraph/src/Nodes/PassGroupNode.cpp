// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync P4 M3: PassGroupNode — generic multi-pass node (one cmd buf + one submit)

#include "Nodes/PassGroupNode.h"
#include "Core/NodeRegistration.h"
#include "Core/PassGroupSchedule.h"   // BuildPassGroupSchedule
#include "Core/PassRecorder.h"        // RecordPassGroup
#include "Core/NodeLogging.h"
#include "VulkanDevice.h"
#include "IRenderTarget.h"             // Vixen::Vulkan::Resources::IRenderTarget

#include <cassert>
#include <stdexcept>

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> PassGroupNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<PassGroupNode>(
        instanceName,
        const_cast<PassGroupNodeType*>(this)
    );
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

PassGroupNode::PassGroupNode(
    const std::string& instanceName,
    NodeType* nodeType
) : VariadicTypedNode<PassGroupNodeConfig>(instanceName, nodeType)
{
    // Variadic inputs are compile-ordering-only and optional: min=0 so an unwired
    // node (e.g. the M3 smoke test) still constructs and validates.
    SetVariadicInputConstraints(0);
    NODE_LOG_INFO("[PassGroupNode] Constructor called for " + instanceName);
}

// ============================================================================
// HOST ASSEMBLY API
// ============================================================================

void PassGroupNode::SetPasses(std::vector<PassStep> passes) {
    passes_ = std::move(passes);
}

void PassGroupNode::AddComputePass(ComputePassStep step) {
    passes_.emplace_back(std::move(step));
}

void PassGroupNode::AddRenderPass(RenderPassStep step) {
    passes_.emplace_back(std::move(step));
}

// ============================================================================
// COMPILE
// ============================================================================

void PassGroupNode::CompileImpl(VariadicCompileContext& ctx) {
    NODE_LOG_INFO("[PassGroupNode::CompileImpl] Baking node-local schedule + allocating command buffers");

    assert(!passes_.empty() &&
           "[PassGroupNode::CompileImpl] passes_ is empty — call AddComputePass/AddRenderPass before Compile");

    // ---- 1. Device + pool from compile-time dependency slots (mirrors ComputeDispatchNode) ----
    VulkanDevice* devicePtr = ctx.In(PassGroupNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[PassGroupNode::CompileImpl] Vulkan device input is null");
    }
    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    commandPool = ctx.In(PassGroupNodeConfig::COMMAND_POOL);
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[PassGroupNode::CompileImpl] Command pool is null");
    }

    // ---- 2. Bake node-local barrier schedule ----
    intraSchedule_ = BuildPassGroupSchedule(passes_);
    NODE_LOG_INFO("[PassGroupNode::CompileImpl] Baked schedule for " +
                  std::to_string(passes_.size()) + " passes, " +
                  std::to_string(intraSchedule_.groups.size()) + " groups");

    // ---- 3. Allocate one command buffer per swapchain image (mirrors ComputeDispatchNode) ----
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo =
        ctx.In(PassGroupNodeConfig::SWAPCHAIN_INFO);
    if (!swapchainInfo) {
        throw std::runtime_error("[PassGroupNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = swapchainInfo->GetImageCount();
    NODE_LOG_INFO("[PassGroupNode::CompileImpl] Allocating " +
                  std::to_string(imageCount) + " command buffers");

    commandBuffers_.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    std::vector<VkCommandBuffer> rawBuffers(imageCount);
    VkResult result = vkAllocateCommandBuffers(vulkanDevice->device, &allocInfo, rawBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[PassGroupNode::CompileImpl] Failed to allocate command buffers: " +
                                 std::to_string(result));
    }

    for (uint32_t i = 0; i < imageCount; ++i) {
        commandBuffers_[i] = rawBuffers[i];
        commandBuffers_.MarkDirty(i);
    }

    NODE_LOG_INFO("[PassGroupNode::CompileImpl] Compiled OK (" +
                  std::to_string(imageCount) + " command buffers)");
}

// ============================================================================
// EXECUTE
// ============================================================================

void PassGroupNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // ---- Resolve FrameSync inputs (mirrors ComputeDispatchNode::ExecuteImpl:153-166) ----
    uint32_t imageIndex        = ctx.In(PassGroupNodeConfig::IMAGE_INDEX);
    uint32_t currentFrameIndex = ctx.In(PassGroupNodeConfig::CURRENT_FRAME_INDEX);

    const std::vector<VkSemaphore>& imageAvailableSemaphores =
        ctx.In(PassGroupNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores =
        ctx.In(PassGroupNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(PassGroupNodeConfig::IN_FLIGHT_FENCE);

    // Guard against the invalid-image sentinel BEFORE the per-image semaphore indexing
    // (UINT32_MAX is SwapChainNode's out-of-date skip sentinel; also keeps the fence reset
    // below from running on a skipped frame).
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[PassGroupNode] Invalid image index - skipping frame");
        return;
    }

    // Two-tier indexing: imageAvailable by frame, renderComplete by image
    VkSemaphore imageAvailableSemaphore  = imageAvailableSemaphores[currentFrameIndex];
    VkSemaphore renderCompleteSemaphore  = renderCompleteSemaphores[imageIndex];

    VkCommandBuffer cmd = commandBuffers_.GetValue(imageIndex);

    // Reset fence before submitting (matches ComputeDispatchNode non-composite path)
    vkResetFences(vulkanDevice->device, 1, &inFlightFence);

    // ---- Begin command buffer (ONE_TIME_SUBMIT — re-recorded each frame) ----
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(cmd, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[PassGroupNode::ExecuteImpl] Failed to begin command buffer: " +
                                 std::to_string(result));
    }

    // ---- Record all passes (barriers + compute dispatches + graphics draws) ----
    RecordPassGroup(cmd, passes_, intraSchedule_, imageIndex);

    // ---- End command buffer ----
    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[PassGroupNode::ExecuteImpl] Failed to end command buffer: " +
                                 std::to_string(result));
    }

    // ---- Submit (lifted from ComputeDispatchNode:246-269) ----
    // Wait imageAvailable[frame], signal renderComplete[image], fence inFlightFence.
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    submitInfo.waitSemaphoreCount  = 1;
    submitInfo.pWaitSemaphores     = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask   = &waitStage;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderCompleteSemaphore;

    result = vkQueueSubmit(vulkanDevice->queue, 1, &submitInfo, inFlightFence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[PassGroupNode::ExecuteImpl] Failed to submit: " +
                                 std::to_string(result));
    }

    // ---- Outputs ----
    ctx.Out(PassGroupNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);
    ctx.Out(PassGroupNodeConfig::COMMAND_BUFFER, cmd);
}

// ============================================================================
// CLEANUP
// ============================================================================

void PassGroupNode::CleanupImpl(VariadicCleanupContext& ctx) {
    NODE_LOG_INFO("[PassGroupNode::CleanupImpl] Cleaning up resources");

    if (vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
        if (!commandBuffers_.empty() && commandPool != VK_NULL_HANDLE) {
            std::vector<VkCommandBuffer> rawHandles;
            rawHandles.reserve(commandBuffers_.size());
            for (size_t i = 0; i < commandBuffers_.size(); ++i) {
                rawHandles.push_back(commandBuffers_.GetValue(i));
            }
            vkFreeCommandBuffers(
                vulkanDevice->device,
                commandPool,
                static_cast<uint32_t>(rawHandles.size()),
                rawHandles.data()
            );
            commandBuffers_.clear();
        }
        commandPool = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[PassGroupNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::PassGroupNodeType);
