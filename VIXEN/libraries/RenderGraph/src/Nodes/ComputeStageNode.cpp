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
#include "ShaderDataBundle.h"
#include "IRenderTarget.h"
#include "Core/NodeLogging.h"
#include <mutex>
#include <set>
#include <stdexcept>

namespace Vixen::RenderGraph {

// Command buffers are frame-indexed (ring depth = frames-in-flight), NOT image-indexed:
// the only per-frame GPU-completion fence is per-FLIGHT (FrameSyncNode waits it at frame
// start), so sizing the reusable command-buffer ring to the flight count makes the resource
// ring == the flight ring that fence already guards. Mirrors
// CameraNodeConfig::MAX_FRAMES_IN_FLIGHT / FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT (= 4).
static constexpr uint32_t COMMAND_BUFFER_RING_DEPTH = 4;

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
        throw std::runtime_error("[ComputeStageNode::CompileImpl] vkAllocateCommandBuffers failed: " +
                                 std::to_string(result));
    }
    for (uint32_t i = 0; i < cmdBufferCount; ++i) {
        commandBuffers_[i] = cmdBuffers[i];
        commandBuffers_.MarkDirty(i);
    }

    // Producer role: re-publish the FIRST written buffer handle so a downstream consumer
    // can bind it (descriptor) and so the connection topologically orders producer->
    // consumer. Sampled Lighting Inc3 M5: BUFFER_WRITE_ARRAY generalizes the old single-
    // buffer BUFFER_WRITE into an array; BUFFER_OUT stays a single-handle passthrough
    // (its only consumer, BuildFanInDemoGraph.cpp, wires each producer stage with
    // exactly ONE buffer in its write array) — element 0 is the whole array for every
    // existing/expected caller. The hazard edge itself is baked off the shared array
    // Resource*'s per-entry constituents (see Resource::hazardConstituents_), not this
    // passthrough — this only carries the handle value + ordering edge.
    std::vector<VkBuffer> writtenBuffers = ctx.In(ComputeStageNodeConfig::BUFFER_WRITE_ARRAY);
    VkBuffer writtenBuffer = writtenBuffers.empty() ? VK_NULL_HANDLE : writtenBuffers[0];
    ctx.Out(ComputeStageNodeConfig::BUFFER_OUT, writtenBuffer);
    ctx.Out(ComputeStageNodeConfig::VULKAN_DEVICE_OUT, GetDevice());

    NODE_LOG_INFO("[ComputeStageNode::CompileImpl] Allocated " + std::to_string(cmdBufferCount) +
                  " command buffers (flight-ring depth; swapchain imageCount=" + std::to_string(imageCount) + ")");
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

    // Two separate bounds: the image-derived arrays are indexed by imageIndex (bounded by
    // renderComplete size), while the command-buffer ring is frame-indexed (bounded by its own
    // flight-ring size).
    if (imageIndex == UINT32_MAX || imageIndex >= renderComplete.size() ||
        currentFrameIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[ComputeStageNode] Invalid image/frame index - skipping frame");
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

    // Command buffer is frame-indexed (flight ring), guarded by the per-flight fence FrameSyncNode
    // already waited; RecordComputeCommands still selects its image-derived values by imageIndex.
    VkCommandBuffer cmd = commandBuffers_.GetValue(currentFrameIndex);
    RecordComputeCommands(ctx, cmd, imageIndex, isConsumer);
    commandBuffers_.MarkReady(currentFrameIndex);

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

    VkResult result;
    {
        // Externally synchronized per Vulkan spec (audit V-M11): the TBB parallel executor can
        // schedule this alongside another node's submit on the same queue.
        std::lock_guard<std::mutex> submitLock(GetDevice()->SubmitMutex(GetDevice()->queue));
        result = GetDevice()->fpQueueSubmit2(GetDevice()->queue, 1, &si, submitFence);
    }
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

    // Descriptor SET OBJECTS are frame-indexed by DescriptorSetNode (set ring at flight depth,
    // set[frameIndex]) whenever its CURRENT_FRAME_INDEX is wired, else set[imageIndex]. Bind the
    // SAME index it was written into: frameIndex when THIS node's CURRENT_FRAME_INDEX is wired
    // (main graph: both wired together), else imageIndex (demo graphs: neither wired). The producer/
    // consumer pair is always wired as a set, so the two choices agree.
    const uint32_t currentFrameIndex = ctx.In(ComputeStageNodeConfig::CURRENT_FRAME_INDEX);
    const bool frameIndexWired =
        NodeInstance::GetInput(ComputeStageNodeConfig::CURRENT_FRAME_INDEX_Slot::index, 0) != nullptr;
    const uint32_t setIndex = frameIndexWired ? currentFrameIndex : imageIndex;
    if (descriptorSets.empty() || setIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] Invalid descriptor sets for set index " +
                                 std::to_string(setIndex));
    }

    // Dispatch dims: explicit params, falling back to the swapchain extent (consumer),
    // then to the IMAGE_WRITE target's extent (Sampled Lighting Inc3 M1: an image-producer
    // middle pass with no SWAPCHAIN_INFO, e.g. DirectLightingNode) — re-derived live every
    // Execute (not cached from graph-build time) so a VIXEN_RENDER_SCALE-driven resize of
    // the render target is picked up the same frame, mirroring ComputeDispatchNode's own
    // dispatchTarget->GetExtent() pattern for the identical reason.
    uint32_t dispatchX = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_X, 0u);
    uint32_t dispatchY = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 0u);
    uint32_t dispatchZ = GetParameterValue<uint32_t>(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo =
        ctx.In(ComputeStageNodeConfig::SWAPCHAIN_INFO);
    // Non-swapchain intermediate image this stage writes (e.g. a shading pass's own
    // render target). Fetched once, used both for dispatch-dim fallback (below) and the
    // entry barrier (further below).
    Vixen::Vulkan::Resources::IRenderTarget* imageWriteTarget =
        ctx.In(ComputeStageNodeConfig::IMAGE_WRITE);
    // Sampled Lighting Inc4 M1: N simultaneous image-write targets (e.g. DDGI's
    // irradiance + Chebyshev-visibility atlases), additive alongside imageWriteTarget
    // above — a pass may use either, both, or neither depending on its own output shape.
    std::vector<Vixen::Vulkan::Resources::IRenderTarget*> imageWriteArrayTargets =
        ctx.In(ComputeStageNodeConfig::IMAGE_WRITE_ARRAY);
    if ((dispatchX == 0 || dispatchY == 0) && swapchainInfo) {
        VkExtent2D extent = swapchainInfo->GetExtent();
        dispatchX = (extent.width + 7) / 8;
        dispatchY = (extent.height + 7) / 8;
    } else if ((dispatchX == 0 || dispatchY == 0) && imageWriteTarget) {
        VkExtent2D extent = imageWriteTarget->GetExtent();
        dispatchX = (extent.width + 7) / 8;
        dispatchY = (extent.height + 7) / 8;
    } else if ((dispatchX == 0 || dispatchY == 0) && !imageWriteArrayTargets.empty() && imageWriteArrayTargets[0]) {
        VkExtent2D extent = imageWriteArrayTargets[0]->GetExtent();
        dispatchX = (extent.width + 7) / 8;
        dispatchY = (extent.height + 7) / 8;
    }
    if (dispatchX == 0 || dispatchY == 0 || dispatchZ == 0) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] Dispatch dims unresolved "
                                 "(set dispatchX/Y/Z or connect SWAPCHAIN_INFO/IMAGE_WRITE)");
    }

    // Consumer: acquire-side transition of the swapchain image into GENERAL for the
    // storage write (WSI lifecycle — node-managed in Tier-1).
    if (isConsumer && swapchainInfo) {
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmd, swapchainInfo->GetImage(imageIndex));
    }

    // Image-write (Sampled Lighting Inc3 M1): transitions imageWriteTarget's CURRENT
    // image (GetCurrentImage(), NOT GetImage(imageIndex) — an offscreen IRenderTarget
    // tracks its own currentIndex independent of the swapchain's imageIndex; see
    // IRenderTarget.h / RenderTargetData, and ComputeDispatchNode's own render-target
    // blit path uses the identical accessor for the identical reason) to GENERAL for the
    // storage write. No WSI involvement, no PRESENT_SRC — this role is independent of
    // isConsumer/PARAM_IS_CONSUMER entirely; a pass can be an IMAGE_WRITE producer
    // whether or not it is also the SWAPCHAIN_INFO consumer.
    if (imageWriteTarget) {
        VkImageLayout priorLayout = DecideRenderTargetPriorLayoutAndUpdate(
            imageWriteLayouts_, imageWriteTarget->GetCurrentImage(), VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmd,
            imageWriteTarget->GetCurrentImage(), priorLayout);
    }

    // Sampled Lighting Inc4 M1: IMAGE_WRITE_ARRAY — identical per-target barrier logic to
    // the single-slot case above, looped. imageWriteLayouts_ is keyed by VkImage (not by
    // slot/index), so mixing IMAGE_WRITE + IMAGE_WRITE_ARRAY targets on the same node (not
    // expected in practice, but not disallowed) still tracks each image's own layout
    // independently with no collision.
    for (Vixen::Vulkan::Resources::IRenderTarget* target : imageWriteArrayTargets) {
        if (!target) continue;
        VkImageLayout priorLayout = DecideRenderTargetPriorLayoutAndUpdate(
            imageWriteLayouts_, target->GetCurrentImage(), VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmd,
            target->GetCurrentImage(), priorLayout);
    }

    // Sampled Lighting Inc4 M5: IMAGE_READ_ARRAY needs no RecordComputeCommands handling —
    // the hazard is baked from the graph's slot connections (ResourceAccessTracker::AddNode
    // walks the compiled bundle, not this function), and a producer's IMAGE_WRITE_ARRAY
    // leaves its images in GENERAL with no exit transition, so a storage-image descriptor
    // read of the same image needs no further layout transition either. See the slot's own
    // doc comment on ComputeStageNodeConfig::IMAGE_READ_ARRAY.

    // Frame-indexed SET OBJECT (see setIndex derivation above); the swapchain image VALUES this
    // pass transitions/writes stay imageIndex-selected.
    BindComputePipeline(cmd, pipeline, layout, descriptorSets[setIndex]);
    SetPushConstants(ctx, cmd, layout);
    vkCmdDispatch(cmd, dispatchX, dispatchY, dispatchZ);

    // Consumer is the last writer of the swapchain image → hand it to Present.
    if (isConsumer && swapchainInfo) {
        SwapchainBarriers::TransitionImageToPresentBarrier2(GetDevice(), cmd, swapchainInfo->GetImage(imageIndex));
    }

    // IMAGE_WRITE deliberately does NOT transition on exit — it stays GENERAL (the
    // compute storage-write's own end state, already recorded in imageWriteLayouts_ by
    // DecideRenderTargetPriorLayoutAndUpdate's own update above). The next consumer
    // (another IMAGE_WRITE producer re-entering this same handle, or a
    // presentation-only blit node reading it via TRANSFER_SRC) owns whatever comes
    // next — this pass's job ends at GENERAL.

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
