// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M1 (KI-018): presentation-only render-target->swapchain blit node.
//
// One node = one blit = one vkQueueSubmit2 = its OWN SubmitGroup. Mirrors ComputeStageNode's
// consumer-role submit machinery (per-image command buffer, fence ownership, binary
// renderComplete signal, timeline WAIT on the baked IMAGE_READ<-IMAGE_WRITE edge) but records
// a blit (SwapchainBarriers::BlitRenderTargetToSwapchain) instead of a compute dispatch.

#include "Nodes/BlitNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/BlitNodeConfig.h"
#include "VulkanDevice.h"
#include "IRenderTarget.h"
#include "Core/NodeLogging.h"
#include <mutex>
#include <set>
#include <stdexcept>

namespace Vixen::RenderGraph {

// Command buffers are frame-indexed (ring depth = frames-in-flight), NOT image-indexed:
// the only per-frame GPU-completion fence is per-FLIGHT (FrameSyncNode waits it at frame
// start), so sizing the reusable command-buffer ring to the flight count makes the resource
// ring == the flight ring that fence already guards. This blit is a live composite-chain
// producer that submits with VK_NULL_HANDLE (leaveImageInGeneral), so CB[imageN] otherwise
// has the same reuse-while-pending hazard as the compute nodes. Mirrors
// CameraNodeConfig::MAX_FRAMES_IN_FLIGHT / FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT (= 4).
static constexpr uint32_t COMMAND_BUFFER_RING_DEPTH = 4;

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> BlitNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<BlitNode>(instanceName, const_cast<BlitNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR / SETUP
// ============================================================================

BlitNode::BlitNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<BlitNodeConfig>(instanceName, nodeType) {
    NODE_LOG_INFO("[BlitNode] Constructor: " + instanceName);
}

void BlitNode::SetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_DEBUG("[BlitNode::SetupImpl] Graph-scope initialization");
}

// ============================================================================
// COMPILE — allocate one command buffer per swapchain image
// ============================================================================

void BlitNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* devicePtr = ctx.In(BlitNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[BlitNode::CompileImpl] Vulkan device input is null");
    }
    SetDevice(devicePtr);

    commandPool_ = ctx.In(BlitNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[BlitNode::CompileImpl] Command pool is null/invalid");
    }

    const std::vector<VkSemaphore>& renderComplete =
        ctx.In(BlitNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    uint32_t imageCount = static_cast<uint32_t>(renderComplete.size());
    if (imageCount == 0) {
        throw std::runtime_error("[BlitNode::CompileImpl] Image count is 0 "
                                 "(RENDER_COMPLETE_SEMAPHORES_ARRAY empty)");
    }

    // Command buffers are frame-indexed at the flight-ring depth, NOT imageCount (see
    // COMMAND_BUFFER_RING_DEPTH note above). imageCount above is still read for the image-derived
    // arrays; the reusable command-buffer ring is sized to the flight count.
    const uint32_t cmdBufferCount = COMMAND_BUFFER_RING_DEPTH;
    commandBuffers_.resize(cmdBufferCount);
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = cmdBufferCount;

    std::vector<VkCommandBuffer> cmdBuffers(cmdBufferCount);
    VkResult result = vkAllocateCommandBuffers(GetDevice()->device, &allocInfo, cmdBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[BlitNode::CompileImpl] vkAllocateCommandBuffers failed: " +
                                 std::to_string(result));
    }
    for (uint32_t i = 0; i < cmdBufferCount; ++i) {
        commandBuffers_[i] = cmdBuffers[i];
        commandBuffers_.MarkDirty(i);
    }

    ctx.Out(BlitNodeConfig::VULKAN_DEVICE_OUT, GetDevice());

    NODE_LOG_INFO("[BlitNode::CompileImpl] Allocated " + std::to_string(cmdBufferCount) +
                  " command buffers (flight-ring depth; swapchain imageCount=" + std::to_string(imageCount) + ")");

    // Task 0.1 (Baked-Content Perf Audit, top action #9): GPU timing via the centralized
    // GPUQueryManager, same pattern as every other timed node. Only allocate once (CompileImpl
    // can re-run on recompile; a second AllocateQuerySlot call would leak a slot).
    if (!gpuPerfLogger_) {
        auto* queryMgrPtr = static_cast<GPUQueryManager*>(GetDevice()->GetQueryManager());
        if (queryMgrPtr) {
            auto queryManager = std::shared_ptr<GPUQueryManager>(queryMgrPtr, [](GPUQueryManager*){});
            gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(GetInstanceName(), queryManager);
            gpuPerfLogger_->SetEnabled(true);
            gpuPerfLogger_->SetLogFrequency(120);
            gpuPerfLogger_->SetPrintToTerminal(false);
            if (auto* nodeLogger = GetLogger()) {
                nodeLogger->AddChild(gpuPerfLogger_);
            }
            if (gpuPerfLogger_->IsTimingSupported()) {
                NODE_LOG_INFO("[BlitNode] GPU performance timing enabled (slot " +
                             std::to_string(gpuPerfLogger_->GetQuerySlot()) + ")");
            } else {
                NODE_LOG_WARNING("[BlitNode] GPU timing not supported on this device");
            }
        } else {
            NODE_LOG_WARNING("[BlitNode] GPUQueryManager not available from VulkanDevice");
        }
    }
}

// ============================================================================
// EXECUTE — record + vkQueueSubmit2 with timeline edge consumption
// ============================================================================

void BlitNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const bool leaveImageInGeneral =
        GetParameterValue<bool>(BlitNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, false);

    const uint32_t imageIndex = ctx.In(BlitNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(BlitNodeConfig::CURRENT_FRAME_INDEX);
    VkFence inFlightFence = ctx.In(BlitNodeConfig::IN_FLIGHT_FENCE);
    const std::vector<VkSemaphore>& renderComplete = ctx.In(BlitNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // Two separate bounds: the image-derived arrays are indexed by imageIndex (bounded by
    // renderComplete size), while the command-buffer ring is frame-indexed (bounded by its own
    // flight-ring size).
    if (imageIndex == UINT32_MAX || imageIndex >= renderComplete.size() ||
        currentFrameIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[BlitNode] Invalid image/frame index - skipping frame");
        return;
    }

    // Fence ownership mirrors ComputeDispatchNode's leaveImageInGeneral convention exactly: the
    // per-flight in-flight fence must be reset+signalled by EXACTLY ONE submit per frame. When a
    // downstream graphics pass follows (leaveImageInGeneral==true — the composite chain: Blit ->
    // sky -> UI, where UIRenderNode is the true frame-final submit and owns the fence), this blit
    // is NOT the last submit, so it must NOT reset or signal the fence. Two nodes resetting+
    // signalling one binary fence per frame is illegal (VUID-vkResetFences-pFences-01123 "fence in
    // use", plus a binary-fence double-signal). Blit must not own the fence when leaveImageInGeneral
    // because UIRenderNode is the frame-final submit and the sole legitimate fence owner. Blit's
    // GPU ordering before UI is preserved regardless: both submit to the same device->queue in
    // executionOrder, so submission order already sequences them (a fence never orders GPU work).
    // Terminal blit (leaveImageInGeneral==false, no UI after it): Blit stays the sole fence owner.
    if (!leaveImageInGeneral) {
        vkResetFences(GetDevice()->device, 1, &inFlightFence);
    }

    // Collect GPU performance results for this frame-in-flight (after fence wait) — same
    // placement as ComputeDispatchNode::ExecuteImpl.
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrameIndex);
    }

    // Command buffer is frame-indexed (flight ring), guarded by the per-flight fence FrameSyncNode
    // already waited; RecordBlitCommands still targets the physical swapchain image by imageIndex.
    VkCommandBuffer cmd = commandBuffers_.GetValue(currentFrameIndex);
    RecordBlitCommands(ctx, cmd, imageIndex, currentFrameIndex, leaveImageInGeneral);
    commandBuffers_.MarkReady(currentFrameIndex);

    VkSemaphore timelineSem = ctx.In(BlitNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(BlitNodeConfig::TIMELINE_FRAME_BASE_IN);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Timeline WAITS: one per baked waitEdge — the genuine fan-in wait proving the upstream
    // shading pass (IMAGE_WRITE) finished writing before this blit reads it (IMAGE_READ).
    // No binary imageAvailable wait here: the upstream pass chain already consumed the WSI
    // acquire (see BlitNodeConfig.h's class doc comment on why this node isn't the first
    // submit in the default composite chain).
    if (timelineSem != VK_NULL_HANDLE) {
        const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
        if (const SubmitGroup* grp = FindGroupForNode(sched, this)) {
            for (uint32_t idx : grp->waitEdges) {
                VkSemaphoreSubmitInfo twait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                twait.semaphore = timelineSem;
                twait.value     = sched.edges[idx].timelineOffset + frameBase;
                twait.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
                waits.push_back(twait);
            }
        }
    }

    // Binary signal (renderComplete, indexed by image) — Present waits on this.
    VkSemaphoreSubmitInfo renderSig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    renderSig.semaphore = renderComplete[imageIndex];
    renderSig.value     = 0;  // binary: value ignored
    renderSig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals.push_back(renderSig);

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos      = waits.data();
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cmdInfo;
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();

    // Composite (leaveImageInGeneral): downstream UI owns the frame fence, so submit with none —
    // see the fence-ownership comment above. Terminal blit: this is the last submit, own the fence.
    VkFence submitFence = leaveImageInGeneral ? VK_NULL_HANDLE : inFlightFence;

    VkResult result;
    {
        // Externally synchronized per Vulkan spec (audit V-M11): the TBB parallel executor can
        // schedule this alongside another node's submit on the same queue.
        std::lock_guard<std::mutex> submitLock(GetDevice()->SubmitMutex(GetDevice()->queue));
        result = GetDevice()->fpQueueSubmit2(GetDevice()->queue, 1, &si, submitFence);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[BlitNode::ExecuteImpl] vkQueueSubmit2 failed: " +
                                 std::to_string(result));
    }

    ctx.Out(BlitNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderComplete[imageIndex]);
    ctx.Out(BlitNodeConfig::VULKAN_DEVICE_OUT, GetDevice());
}

// ============================================================================
// RECORD
// ============================================================================

void BlitNode::RecordBlitCommands(Context& ctx, VkCommandBuffer cmd, uint32_t imageIndex,
                                  uint32_t frameIndex, bool leaveImageInGeneral) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("[BlitNode::RecordBlitCommands] vkBeginCommandBuffer failed");
    }

    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmd, frameIndex);
        gpuPerfLogger_->RecordDispatchStart(cmd, frameIndex);
    }

    Vixen::Vulkan::Resources::IRenderTarget* imageReadTarget = ctx.In(BlitNodeConfig::IMAGE_READ);
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo   = ctx.In(BlitNodeConfig::SWAPCHAIN_INFO);
    if (!imageReadTarget) {
        throw std::runtime_error("[BlitNode::RecordBlitCommands] IMAGE_READ is null");
    }
    if (!swapchainInfo) {
        throw std::runtime_error("[BlitNode::RecordBlitCommands] SWAPCHAIN_INFO is null");
    }

    VkImage swapchainImage = swapchainInfo->GetImage(imageIndex);

    // Reuses the SAME logic ComputeDispatchNode's own render-scale blit uses (extracted,
    // Sampled Lighting Inc3 M1) — see SwapchainBarriers::BlitRenderTargetToSwapchain's doc
    // comment for the full barrier sequence. imageReadTarget's image is expected GENERAL on
    // entry (the upstream ComputeStageNode's IMAGE_WRITE role leaves it there); this call
    // transitions it to TRANSFER_SRC_OPTIMAL, blits, and hands the swapchain image to the
    // same leaveImageInGeneral-gated contract every other blit consumer in this codebase uses.
    SwapchainBarriers::BlitRenderTargetToSwapchain(GetDevice(), layoutTracking_, cmd,
                                                   imageReadTarget, swapchainImage,
                                                   swapchainInfo->GetExtent(), leaveImageInGeneral);

    if (gpuPerfLogger_) {
        VkExtent2D extent = swapchainInfo->GetExtent();
        gpuPerfLogger_->RecordDispatchEnd(cmd, frameIndex, extent.width, extent.height);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("[BlitNode::RecordBlitCommands] vkEndCommandBuffer failed");
    }
}

// ============================================================================
// CLEANUP
// ============================================================================

void BlitNode::CleanupImpl(TypedCleanupContext& /*ctx*/) {
    if (GetDevice() && GetDevice()->device != VK_NULL_HANDLE) {
        if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
            std::vector<VkCommandBuffer> rawHandles;
            rawHandles.reserve(commandBuffers_.size());
            for (size_t i = 0; i < commandBuffers_.size(); ++i) {
                rawHandles.push_back(commandBuffers_.GetValue(i));
            }
            vkFreeCommandBuffers(GetDevice()->device, commandPool_,
                                 static_cast<uint32_t>(rawHandles.size()), rawHandles.data());
            commandBuffers_.clear();
        }
        commandPool_ = VK_NULL_HANDLE;
    }
    NODE_LOG_INFO("[BlitNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::BlitNodeType);
