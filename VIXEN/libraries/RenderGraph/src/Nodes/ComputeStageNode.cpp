// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// auto-sync FrameGraph P5b M2: generic single-compute-pass submit node.
//
// One node = one compute dispatch = one vkQueueSubmit2 = its OWN SubmitGroup. Lifts
// ComputeDispatchNode's per-image command-buffer allocation (CompileImpl) and
// recording + M1 deduped-signal / per-edge-wait submit (ExecuteImpl), but generic
// over the producer vs consumer role (PARAM_IS_CONSUMER):
//
//   - PRODUCER: compute + timeline SIGNAL only. No WSI wait, no fence, no swapchain
//     transition, NO binary handoff to the consumer — the producer→consumer order is
//     SOLELY the baked timeline edge.
//   - CONSUMER: waits binary imageAvailable (acquire) + per-edge timeline WAITS, writes
//     the swapchain image, signals binary renderComplete (Present), owns the fence, and
//     transitions the image GENERAL→PRESENT_SRC.

#include "Nodes/ComputeStageNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "VulkanDevice.h"
#include "VulkanGlobalNames.h"  // vixenCmdPipelineBarrier2
#include "ShaderDataBundle.h"
#include "IRenderTarget.h"
#include "Core/NodeLogging.h"
#include <set>
#include <stdexcept>

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> ComputeStageNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<ComputeStageNode>(instanceName, const_cast<ComputeStageNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR / SETUP
// ============================================================================

ComputeStageNode::ComputeStageNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<ComputeStageNodeConfig>(instanceName, nodeType) {
    NODE_LOG_INFO("[ComputeStageNode] Constructor: " + instanceName);
}

void ComputeStageNode::SetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_DEBUG("[ComputeStageNode::SetupImpl] Graph-scope initialization");
}

// ============================================================================
// COMPILE — allocate one command buffer per swapchain image
// ============================================================================

void ComputeStageNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* devicePtr = ctx.In(ComputeStageNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] Vulkan device input is null");
    }
    SetDevice(devicePtr);

    commandPool_ = ctx.In(ComputeStageNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] Command pool is null/invalid");
    }

    // Image count drives the per-image command-buffer array. A producer has no
    // swapchain input, so we size from RENDER_COMPLETE_SEMAPHORES_ARRAY (sized to the
    // exact swapchain image count by SwapChainNode), which BOTH roles have wired.
    const std::vector<VkSemaphore>& renderComplete =
        ctx.In(ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    uint32_t imageCount = static_cast<uint32_t>(renderComplete.size());
    if (imageCount == 0) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] Image count is 0 "
                                 "(RENDER_COMPLETE_SEMAPHORES_ARRAY empty)");
    }

    commandBuffers_.resize(imageCount);
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    std::vector<VkCommandBuffer> cmdBuffers(imageCount);
    VkResult result = vkAllocateCommandBuffers(GetDevice()->device, &allocInfo, cmdBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] vkAllocateCommandBuffers failed: " +
                                 std::to_string(result));
    }
    for (uint32_t i = 0; i < imageCount; ++i) {
        commandBuffers_[i] = cmdBuffers[i];
        commandBuffers_.MarkDirty(i);
    }

    // Producer role: re-publish the written buffer handle so a downstream consumer can
    // bind it (descriptor) and so the connection topologically orders producer→consumer.
    // The hazard edge is baked off the shared StorageBufferNode Resource* (the sync
    // slots), not this passthrough — this only carries the handle value + ordering edge.
    VkBuffer writtenBuffer = ctx.In(ComputeStageNodeConfig::BUFFER_WRITE);
    ctx.Out(ComputeStageNodeConfig::BUFFER_OUT, writtenBuffer);
    ctx.Out(ComputeStageNodeConfig::VULKAN_DEVICE_OUT, GetDevice());

    NODE_LOG_INFO("[ComputeStageNode::CompileImpl] Allocated " + std::to_string(imageCount) +
                  " command buffers");
}

// ============================================================================
// EXECUTE — record + vkQueueSubmit2 with timeline edge consumption
// ============================================================================

void ComputeStageNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const bool isConsumer = GetParameterValue<bool>(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);

    const uint32_t imageIndex        = ctx.In(ComputeStageNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(ComputeStageNodeConfig::CURRENT_FRAME_INDEX);
    const std::vector<VkSemaphore>& imageAvailable = ctx.In(ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderComplete = ctx.In(ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(ComputeStageNodeConfig::IN_FLIGHT_FENCE);

    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[ComputeStageNode] Invalid image index - skipping frame");
        return;
    }

    // Consumer is the frame's last submit: it resets + owns the in-flight fence (the
    // producers submit with NO fence). FrameSyncNode already waited on it.
    if (isConsumer) {
        vkResetFences(GetDevice()->device, 1, &inFlightFence);
    }

    // Re-record on input change (always re-record to refresh push constants each frame).
    VkPipeline currentPipeline = ctx.In(ComputeStageNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentLayout = ctx.In(ComputeStageNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> currentSets = ctx.In(ComputeStageNodeConfig::DESCRIPTOR_SETS);
    if (currentPipeline != lastPipeline_ || currentLayout != lastPipelineLayout_ ||
        currentSets != lastDescriptorSets_) {
        commandBuffers_.MarkAllDirty();
        lastPipeline_ = currentPipeline;
        lastPipelineLayout_ = currentLayout;
        lastDescriptorSets_ = currentSets;
    }

    VkCommandBuffer cmd = commandBuffers_.GetValue(imageIndex);
    RecordComputeCommands(ctx, cmd, imageIndex, isConsumer);
    commandBuffers_.MarkReady(imageIndex);

    // P5b: timeline primitives from FrameSyncNode (Optional — VK_NULL_HANDLE / 0 if not wired).
    VkSemaphore timelineSem = ctx.In(ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    if (isConsumer) {
        // Consumer waits the binary acquire (imageAvailable, indexed by frame).
        VkSemaphoreSubmitInfo acquireWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        acquireWait.semaphore = imageAvailable[currentFrameIndex];
        acquireWait.value     = 0;  // binary: value ignored
        acquireWait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        waits.push_back(acquireWait);

        // Timeline WAITS: one per baked waitEdge. This is the genuine fan-in — the
        // consumer waits each producer's signalled completion value (NO binary handoff
        // between producers and consumer). A missing/incorrect wait → the consumer reads
        // ungenerated buffer data → visibly wrong output.
        if (timelineSem != VK_NULL_HANDLE) {
            const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
            if (const SubmitGroup* grp = FindGroupForNode(sched, this)) {
                for (uint32_t idx : grp->waitEdges) {
                    VkSemaphoreSubmitInfo twait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                    twait.semaphore = timelineSem;
                    twait.value     = sched.edges[idx].timelineOffset + frameBase;
                    twait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
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
    } else {
        // Producer: timeline SIGNALS only. A group signals its OWN completion value once.
        // All of a producer's signalEdges carry the same timelineOffset (== producer
        // groupId, see FrameSyncScheduler.cpp), so distinct (offset+frameBase) values
        // dedupe to one — a value must be signalled at most once per submit, else
        // VUID-VkSubmitInfo2-semaphore-03882.
        if (timelineSem != VK_NULL_HANDLE) {
            const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
            if (const SubmitGroup* grp = FindGroupForNode(sched, this)) {
                std::set<uint64_t> distinctSignalValues;
                for (uint32_t idx : grp->signalEdges) {
                    distinctSignalValues.insert(sched.edges[idx].timelineOffset + frameBase);
                }
                for (uint64_t value : distinctSignalValues) {
                    VkSemaphoreSubmitInfo tsig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                    tsig.semaphore = timelineSem;
                    tsig.value     = value;
                    tsig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    signals.push_back(tsig);
                }
            }
        }
    }

    // Producer submits with no fence; consumer owns the in-flight fence.
    VkFence submitFence = isConsumer ? inFlightFence : VK_NULL_HANDLE;

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos      = waits.data();
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cmdInfo;
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();

    VkResult result = vixenQueueSubmit2(GetDevice()->queue, 1, &si, submitFence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeStageNode::ExecuteImpl] vkQueueSubmit2 failed: " +
                                 std::to_string(result));
    }

    // Output the renderComplete semaphore (consumer → Present). For a producer there is
    // no present, but publish the per-image renderComplete anyway for a uniform contract.
    ctx.Out(ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderComplete[imageIndex]);
    ctx.Out(ComputeStageNodeConfig::VULKAN_DEVICE_OUT, GetDevice());
}

// ============================================================================
// RECORD
// ============================================================================

void ComputeStageNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmd,
                                             uint32_t imageIndex, bool isConsumer) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] vkBeginCommandBuffer failed");
    }

    VkPipeline pipeline = ctx.In(ComputeStageNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout layout = ctx.In(ComputeStageNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(ComputeStageNodeConfig::DESCRIPTOR_SETS);
    if (descriptorSets.empty() || imageIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] Invalid descriptor sets for image " +
                                 std::to_string(imageIndex));
    }

    // Dispatch dims: explicit params, falling back to the swapchain extent (consumer).
    uint32_t dispatchX = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_X, 0u);
    uint32_t dispatchY = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 0u);
    uint32_t dispatchZ = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo =
        ctx.In(ComputeStageNodeConfig::SWAPCHAIN_INFO);
    if ((dispatchX == 0 || dispatchY == 0) && swapchainInfo) {
        VkExtent2D extent = swapchainInfo->GetExtent();
        dispatchX = (extent.width + 7) / 8;
        dispatchY = (extent.height + 7) / 8;
    }
    if (dispatchX == 0 || dispatchY == 0 || dispatchZ == 0) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] Dispatch dims unresolved "
                                 "(set dispatchX/Y/Z or connect SWAPCHAIN_INFO)");
    }

    // Consumer: acquire-side transition of the swapchain image into GENERAL for the
    // storage write (WSI lifecycle — node-managed in Tier-1).
    if (isConsumer && swapchainInfo) {
        TransitionImageToGeneralBarrier2(cmd, swapchainInfo->GetImage(imageIndex));
    }

    BindComputePipeline(cmd, pipeline, layout, descriptorSets[imageIndex]);
    SetPushConstants(ctx, cmd, layout);
    vkCmdDispatch(cmd, dispatchX, dispatchY, dispatchZ);

    // Consumer is the last writer of the swapchain image → hand it to Present.
    if (isConsumer && swapchainInfo) {
        TransitionImageToPresentBarrier2(cmd, swapchainInfo->GetImage(imageIndex));
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] vkEndCommandBuffer failed");
    }
}

// ============================================================================
// HELPERS
// ============================================================================

void ComputeStageNode::BindComputePipeline(VkCommandBuffer cmd, VkPipeline pipeline,
                                           VkPipelineLayout layout, VkDescriptorSet descriptorSet) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptorSet, 0, nullptr);
}

void ComputeStageNode::SetPushConstants(Context& ctx, VkCommandBuffer cmd, VkPipelineLayout layout) {
    // Preferred: gathered push-constant bytes (from a PushConstantGathererNode).
    std::vector<uint8_t> data = ctx.In(ComputeStageNodeConfig::PUSH_CONSTANT_DATA);
    std::vector<VkPushConstantRange> ranges = ctx.In(ComputeStageNodeConfig::PUSH_CONSTANT_RANGES);
    if (!data.empty() && !ranges.empty()) {
        for (const auto& range : ranges) {
            if (range.offset + range.size <= data.size()) {
                vkCmdPushConstants(cmd, layout, range.stageFlags, range.offset, range.size,
                                   data.data() + range.offset);
            }
        }
        return;
    }

    // Fallback: a host-provided {width,height} blob pushed through the shader's reflected
    // push-constant range (no PushConstantGathererNode needed for a simple extent PC).
    uint32_t pcWidth  = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_PC_WIDTH, 0u);
    uint32_t pcHeight = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_PC_HEIGHT, 0u);
    if (pcWidth == 0u && pcHeight == 0u) return;

    auto shaderBundle = ctx.In(ComputeStageNodeConfig::SHADER_DATA_BUNDLE);
    if (shaderBundle && shaderBundle->reflectionData &&
        !shaderBundle->reflectionData->pushConstants.empty()) {
        const auto& pc = shaderBundle->reflectionData->pushConstants[0];
        const uint32_t blob[2] = { pcWidth, pcHeight };
        const uint32_t pushSize = (pc.size < sizeof(blob)) ? pc.size : static_cast<uint32_t>(sizeof(blob));
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, pc.offset, pushSize, blob);
    }
}

void ComputeStageNode::TransitionImageToGeneralBarrier2(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    ib.srcAccessMask       = VK_ACCESS_2_NONE;
    ib.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = image;
    ib.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    vixenCmdPipelineBarrier2(cmd, &dep);
}

void ComputeStageNode::TransitionImageToPresentBarrier2(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.srcAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ib.dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    ib.dstAccessMask       = VK_ACCESS_2_NONE;
    ib.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = image;
    ib.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    vixenCmdPipelineBarrier2(cmd, &dep);
}

// ============================================================================
// CLEANUP
// ============================================================================

void ComputeStageNode::CleanupImpl(TypedCleanupContext& /*ctx*/) {
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
    NODE_LOG_INFO("[ComputeStageNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ComputeStageNodeType);
