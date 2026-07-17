#include "Nodes/ComputeDispatchNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include <mutex>
#include "Core/GPUPerformanceLogger.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "ShaderDataBundle.h"
#include "Debug/IDebugCapture.h"  // For debug capture passthrough
#include "Core/NodeLogging.h"
#include <stdexcept>
#include <chrono>
#include <set>


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

std::unique_ptr<NodeInstance> ComputeDispatchNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<ComputeDispatchNode>(instanceName, const_cast<ComputeDispatchNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ComputeDispatchNode::ComputeDispatchNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<ComputeDispatchNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[ComputeDispatchNode] Constructor called for " + instanceName);
}

// ============================================================================
// SETUP
// ============================================================================

void ComputeDispatchNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_INFO("[ComputeDispatchNode::SetupImpl] Graph-scope initialization");

    // Create specialized performance logger (enabled for benchmarking, terminal output disabled)
    perfLogger_ = std::make_shared<ComputePerformanceLogger>(instanceName);
    perfLogger_->SetEnabled(true);  // Enabled for benchmark data collection
    perfLogger_->SetTerminalOutput(false);  // Disabled for clean terminal output

    // Register to node logger hierarchy for shared ownership
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_);
    }
}

// ============================================================================
// COMPILE
// ============================================================================

void ComputeDispatchNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocating per-image command buffers");

    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Vulkan device input is null");
    }

    SetDevice(devicePtr);
    vulkanDevice = devicePtr;

    // Get inputs
    commandPool = ctx.In(ComputeDispatchNodeConfig::COMMAND_POOL);
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);

    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Command pool is null/invalid");
    }

    if (!swapchainInfo) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = swapchainInfo->GetImageCount();
    // Command buffers are frame-indexed at the flight-ring depth, NOT imageCount (see
    // COMMAND_BUFFER_RING_DEPTH note above). imageCount is still read for the image-derived
    // arrays' logging context, but the reusable command-buffer ring is sized to the flight count.
    const uint32_t cmdBufferCount = COMMAND_BUFFER_RING_DEPTH;
    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocating " + std::to_string(cmdBufferCount) +
                  " command buffers (flight-ring depth; swapchain imageCount=" + std::to_string(imageCount) + ")");

    // Allocate command buffers (one per frame-in-flight)
    commandBuffers.resize(cmdBufferCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = cmdBufferCount;

    std::vector<VkCommandBuffer> cmdBuffers(cmdBufferCount);
    VkResult result = vkAllocateCommandBuffers(vulkanDevice->device, &allocInfo, cmdBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to allocate command buffers: " + std::to_string(result));
    }

    // Store command buffers in stateful container
    for (uint32_t i = 0; i < cmdBufferCount; ++i) {
        commandBuffers[i] = cmdBuffers[i];
        commandBuffers.MarkDirty(i);  // Initial state: needs recording
    }

    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocated " + std::to_string(cmdBufferCount) + " command buffers successfully");

    // Create GPU performance logger using centralized GPUQueryManager from VulkanDevice
    // Sprint 6.3 Phase 0: All nodes share the same query manager to prevent slot conflicts
    auto* queryMgrPtr = static_cast<GPUQueryManager*>(vulkanDevice->GetQueryManager());
    if (queryMgrPtr) {
        // Wrap raw pointer in shared_ptr with no-op deleter (VulkanDevice owns the manager)
        auto queryManager = std::shared_ptr<GPUQueryManager>(queryMgrPtr, [](GPUQueryManager*){});

        gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(instanceName, queryManager);
        gpuPerfLogger_->SetEnabled(true);  // Enabled for benchmark data collection
        gpuPerfLogger_->SetLogFrequency(120);  // Log every 120 frames (~2 seconds at 60fps)
        gpuPerfLogger_->SetPrintToTerminal(false);  // Disabled for clean terminal output

        if (nodeLogger) {
            nodeLogger->AddChild(gpuPerfLogger_);
        }

        if (gpuPerfLogger_->IsTimingSupported()) {
            NODE_LOG_INFO("[ComputeDispatchNode] GPU performance timing enabled (slot " +
                         std::to_string(gpuPerfLogger_->GetQuerySlot()) + ")");
        } else {
            NODE_LOG_WARNING("[ComputeDispatchNode] GPU timing not supported on this device");
        }
    } else {
        NODE_LOG_WARNING("[ComputeDispatchNode] GPUQueryManager not available from VulkanDevice");
    }

    // Sprint 6.5: Register GPU task profile for cost estimation and learning
    std::string profileId = GetInstanceName() + "_gpu_dispatch";
    gpuProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "compute");
    if (gpuProfile_) {
        RegisterPhaseProfile(VirtualTaskPhase::Execute, gpuProfile_);
        NODE_LOG_INFO("[ComputeDispatchNode] Registered GPU profile: " + profileId);
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void ComputeDispatchNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = ctx.In(ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = ctx.In(ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Get semaphore arrays from FrameSyncNode
    const std::vector<VkSemaphore>& imageAvailableSemaphores = ctx.In(ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores = ctx.In(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(ComputeDispatchNodeConfig::IN_FLIGHT_FENCE);

    // Guard against the invalid-image sentinel BEFORE any per-image indexing or side effect:
    // renderCompleteSemaphores[imageIndex] below read OOB on UINT32_MAX (the maximize crash),
    // and skipping before the fence reset keeps the frame fence signalled so the next
    // FrameSyncNode wait can't deadlock on a skipped frame. Two separate bounds now: the
    // image-derived arrays are indexed by imageIndex (bounded by renderComplete size), while the
    // command-buffer ring is frame-indexed (bounded by its own flight-ring size).
    if (imageIndex == UINT32_MAX || imageIndex >= renderCompleteSemaphores.size() ||
        currentFrameIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("ComputeDispatchNode: Invalid image/frame index - skipping frame");
        return;
    }

    // Two-tier indexing: imageAvailable by frame, renderComplete by image
    VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[currentFrameIndex];
    VkSemaphore renderCompleteSemaphore = renderCompleteSemaphores[imageIndex];

    static int logCounter = 0;
    if (logCounter++ < 20) {
        NODE_LOG_DEBUG("Compute Frame " + std::to_string(currentFrameIndex) + ", Image " + std::to_string(imageIndex));
    }

    // Composite mode: when a downstream graphics pass (UI) follows, it owns the frame fence + the
    // final present transition. The compute then submits with NO fence and leaves the image in GENERAL.
    const bool leaveImageInGeneral =
        GetParameterValue<bool>(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, false);

    // Sampled Lighting Inc3 M1: this dispatch manages no presentable image at all (see
    // PARAM_WRITES_NO_IMAGE's doc comment) — orthogonal to leaveImageInGeneral's fence ownership.
    const bool writesNoImage =
        GetParameterValue<bool>(ComputeDispatchNodeConfig::PARAM_WRITES_NO_IMAGE, false);

    // Phase 0.4: Reset fence before submitting (fence was already waited on by FrameSyncNode). In
    // composite mode the downstream UI submit resets + owns the fence, so leave it alone here.
    if (!leaveImageInGeneral) {
        VkResult resetResult = vkResetFences(vulkanDevice->device, 1, &inFlightFence);
        if (resetResult != VK_SUCCESS) {
            if (resetResult == VK_ERROR_DEVICE_LOST) {
                GetOwningGraph()->NotifyDeviceLost("ComputeDispatchNode::ExecuteImpl vkResetFences");
            }
            throw std::runtime_error("[ComputeDispatchNode::ExecuteImpl] Failed to reset fence: " + std::to_string(resetResult));
        }
    }

    // Collect GPU performance results for this frame-in-flight (after fence wait)
    // The fence for this frame index was waited on, so previous frame's results are ready
    if (gpuPerfLogger_) {
        gpuPerfLogger_->CollectResults(currentFrameIndex);

        // Sprint 6.5: Feed GPU timing to task profile for cost learning
        if (gpuProfile_) {
            auto sample = gpuProfile_->Sample();
            float gpuTimeMs = gpuPerfLogger_->GetLastDispatchMs();
            if (gpuTimeMs > 0.0f) {
                uint64_t gpuTimeNs = static_cast<uint64_t>(gpuTimeMs * 1'000'000.0f);
                sample.Finalize(gpuTimeNs);
            } else {
                sample.Cancel();  // No valid measurement
            }
        }
    }

    // Detect if inputs changed (mark all command buffers dirty if so)
    VkPipeline currentPipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentPipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> currentDescriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);

    if (currentPipeline != lastPipeline ||
        currentPipelineLayout != lastPipelineLayout ||
        currentDescriptorSets != lastDescriptorSets) {
        // Inputs changed - mark all command buffers dirty
        commandBuffers.MarkAllDirty();

        lastPipeline = currentPipeline;
        lastPipelineLayout = currentPipelineLayout;
        lastDescriptorSets = currentDescriptorSets;
    }

    // Calculate push constants (time updates every frame)
    struct PushConstants {
        float time;
        uint32_t frame;
        uint32_t padding[2];
    } pushConstants;

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(currentTime - startTime).count();

    pushConstants.time = elapsedTime;
    pushConstants.frame = static_cast<uint32_t>(elapsedTime * 60.0f);

    // Always re-record to update push constants (they change every frame)
    // TODO: Optimize using secondary command buffers or dynamic state
    // Command buffer is frame-indexed (flight ring), so the per-flight fence FrameSyncNode already
    // waited guards its reuse; the image-derived values RecordComputeCommands reads stay imageIndex.
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(currentFrameIndex);
    RecordComputeCommands(ctx, cmdBuffer, imageIndex, currentFrameIndex, &pushConstants, leaveImageInGeneral, writesNoImage);
    commandBuffers.MarkReady(currentFrameIndex);

    // P5b M1: read timeline primitives from FrameSyncNode slots (Optional — VK_NULL_HANDLE / 0 if not wired)
    VkSemaphore timelineSem = ctx.In(ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

    // Build vkQueueSubmit2 semaphore arrays
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmdBuffer;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Baked-Perf M6 Task 6.1 (audit E2): the binary acquire wait must be consumed by the
    // first submit that actually accesses the swapchain image. On the split baked path this
    // dispatch writes HitRecord only (writesNoImage) — BlitNode is the real first swapchain
    // touch and now owns this wait instead. See ComputeDispatchWaitsForSwapchainAcquire's doc
    // comment (ComputeDispatchNode.h) for why this is safe (the per-flight fence wait in
    // FrameSyncNode, not this semaphore, is what actually guards this dispatch's own
    // cross-frame resource reuse). Swapchain-writing / self-blitting variants (writesNoImage
    // == false) still consume it here, unchanged.
    if (ComputeDispatchWaitsForSwapchainAcquire(writesNoImage)) {
        VkSemaphoreSubmitInfo acquireWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        acquireWait.semaphore = imageAvailableSemaphore;
        acquireWait.value     = 0;  // binary semaphore: value ignored
        acquireWait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        waits.push_back(acquireWait);
    }

    // Timeline SIGNALS (compute is the producer): a group signals its OWN completion value once.
    // All of a producer's signalEdges carry the same timelineOffset (== the producer's groupId,
    // see FrameSyncScheduler.cpp), so distinct (offset+frameBase) values dedupe to one. A timeline
    // value must be signalled at most once per submit, else VUID-VkSubmitInfo2-semaphore-03882.
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

    // Binary signal (renderComplete): voxel-only path ONLY — Present waits it there. In the composite
    // path (leaveImageInGeneral) its only consumer was UI's binary compositeWait, which P5b M3 removed
    // in favour of the baked compute→UI timeline edge; signalling it there would leave an orphaned
    // per-image binary that is re-signalled each frame with no intervening wait (a binary-semaphore
    // re-signal VUID). So skip it in composite — the timeline signalEdges above carry compute→UI.
    if (!leaveImageInGeneral) {
        VkSemaphoreSubmitInfo renderSig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        renderSig.semaphore = renderCompleteSemaphore;
        renderSig.value     = 0;  // binary semaphore: value ignored
        // Baked-Perf M6 Task 6.3 (audit pattern R7): scoped to COMPUTE_SHADER_BIT, this
        // node's only queue-side work on the voxel-only path (matches the acquire wait's
        // and the timeline signal's own stage mask above), not ALL_COMMANDS_BIT.
        renderSig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        signals.push_back(renderSig);
    }

    // Composite mode submits with no fence — the downstream UI submit is the frame's last submit and
    // owns inFlightFence (a binary fence must not be signalled by two submits in one frame).
    VkFence submitFence = leaveImageInGeneral ? VK_NULL_HANDLE : inFlightFence;

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos      = waits.data();
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cmdInfo;
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();

    // Submit to graphics queue via synchronization2. Externally synchronized per Vulkan spec
    // (audit V-M11): the TBB parallel executor can schedule this alongside another node's
    // submit on the same queue.
    VkResult result;
    {
        std::lock_guard<std::mutex> submitLock(vulkanDevice->SubmitMutex(vulkanDevice->queue));
        result = vulkanDevice->fpQueueSubmit2(vulkanDevice->queue, 1, &si, submitFence);
    }
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) {
            GetOwningGraph()->NotifyDeviceLost("ComputeDispatchNode::ExecuteImpl vkQueueSubmit2");
        }
        throw std::runtime_error("[ComputeDispatchNode::ExecuteImpl] Failed to submit command buffer (vkQueueSubmit2): " + std::to_string(result));
    }

    // Output semaphore for Present to wait on
    ctx.Out(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);

    // Pass through debug capture for downstream debug reader nodes
    // Debug capture input comes from DescriptorResourceGathererNode
    Debug::IDebugCapture* debugCapture = ctx.In(ComputeDispatchNodeConfig::DEBUG_CAPTURE);
    ctx.Out(ComputeDispatchNodeConfig::DEBUG_CAPTURE_OUT, debugCapture);
    if (debugCapture) {
        static int debugLogCount = 0;
        if (debugLogCount++ < 3) {
            NODE_LOG_DEBUG("[ComputeDispatchNode] Passing through debug capture: " + debugCapture->GetDebugName());
        }
    }
}

// ============================================================================
// RECORD COMPUTE COMMANDS
// ============================================================================

void ComputeDispatchNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData, bool leaveImageInGeneral, bool writesNoImage) {
    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to begin command buffer");
    }

    // Get inputs
    VkPipeline pipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout pipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);
    // M4: optional offscreen render target. When connected, the shader writes THIS image (at its
    // own, possibly-scaled-down extent) instead of the swapchain image directly; the blit at the
    // end of this function upscales into the swapchain. See RENDER_TARGET_INFO slot doc.
    Vixen::Vulkan::Resources::IRenderTarget* renderTargetInfo = ctx.In(ComputeDispatchNodeConfig::RENDER_TARGET_INFO);

    // Descriptor SET OBJECTS are now frame-indexed by DescriptorSetNode (it allocates the set ring
    // at flight depth and writes set[frameIndex]) whenever its CURRENT_FRAME_INDEX is wired; when it
    // is not wired it falls back to writing set[imageIndex]. This consumer must bind the SAME index
    // it was written into: use frameIndex when THIS node's CURRENT_FRAME_INDEX is wired (main graph:
    // both are wired together), else imageIndex (demo graphs: neither is wired). The producer/
    // consumer pair is always wired as a set, so the two choices agree.
    const bool frameIndexWired =
        NodeInstance::GetInput(ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX_Slot::index, 0) != nullptr;
    const uint32_t setIndex = frameIndexWired ? frameIndex : imageIndex;

    // Validate descriptor sets
    if (descriptorSets.empty() || setIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Invalid descriptor sets for set index " + std::to_string(setIndex));
    }

    // Dispatch dims, shader output image, and GPU-perf extent come from the render target when
    // connected (M4); otherwise from the swapchain, byte-identical to pre-M4 behavior.
    Vixen::Vulkan::Resources::IRenderTarget* dispatchTarget = renderTargetInfo ? renderTargetInfo : swapchainInfo;
    VkExtent2D dispatchExtent = dispatchTarget->GetExtent();

    // Get dispatch dimensions (8x8 workgroup size)
    uint32_t dispatchX = (dispatchExtent.width + 7) / 8;
    uint32_t dispatchY = (dispatchExtent.height + 7) / 8;
    uint32_t dispatchZ = 1;

    static int logCount = 0;
    if (logCount++ < 3) {
        NODE_LOG_DEBUG("[ComputeDispatchNode] Dispatch: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x" + std::to_string(dispatchZ) +
                      " for " + std::string(renderTargetInfo ? "render target" : "swapchain") + " " +
                      std::to_string(dispatchExtent.width) + "x" + std::to_string(dispatchExtent.height));
    }

    // Execute recording steps
    VkImage swapchainImage = swapchainInfo->GetImage(imageIndex);
    // The image the compute shader actually writes: the render target's current image when
    // connected, else the swapchain image (pre-M4 behavior).
    VkImage writeImage = renderTargetInfo ? renderTargetInfo->GetCurrentImage() : swapchainImage;
    // Frame-indexed SET OBJECT (see setIndex derivation above); the image VALUES it references
    // (swapchainImage, writeImage) stay imageIndex-selected.
    VkDescriptorSet descriptorSet = descriptorSets[setIndex];

    // Begin GPU timing frame (reset queries for this frame)
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmdBuffer, frameIndex);
    }

    // WSI lifecycle (acquire): transition the image the shader writes into GENERAL for the compute
    // storage write. Swapchain-lifecycle transition, not an inter-pass hazard, so it stays
    // node-managed in Tier-1. The render target's FIRST write to a given image handle is
    // UNDEFINED->GENERAL (fresh/recreated image); every subsequent write to that SAME handle
    // follows a blit that left it TRANSFER_SRC_OPTIMAL, so the barrier's declared oldLayout must
    // match the image's ACTUAL last-recorded layout (renderTargetImageLayouts_ tracks it exactly,
    // not a seen/not-seen guess — see KI-007 and DecideRenderTargetPriorLayoutAndUpdate's comment;
    // RenderTargetNode's persistent-across-same-extent-recompile lifecycle means "new Compile"
    // does NOT imply "new image").
    if (renderTargetInfo) {
        VkImageLayout priorLayout = DecideRenderTargetPriorLayoutAndUpdate(
            renderTargetImageLayouts_, writeImage, VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmdBuffer, writeImage, priorLayout);
    } else if (!writesNoImage) {
        // Sampled Lighting Inc3 M1: skip entirely when this dispatch manages no presentable image
        // (writesNoImage) — writeImage would otherwise alias the swapchain image, which this
        // dispatch never actually writes; transitioning it here would race a later pass's own
        // transition of the SAME handle (see PARAM_WRITES_NO_IMAGE's doc comment).
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmdBuffer, writeImage);
    }
    // Additionally replay any scheduler-baked INTER-PASS entry barriers for this group
    // (no-op on the single-pass voxel path; active for future multi-pass chains).
    const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
    if (const SubmitGroup* myGroup = FindGroupForNode(sched, this)) {
        ReplayEntryBarriers(cmdBuffer, *myGroup, imageIndex, swapchainInfo);
    }

    BindComputePipeline(cmdBuffer, pipeline, pipelineLayout, descriptorSet);
    SetPushConstants(ctx, cmdBuffer, pipelineLayout, pushConstantData);

    // Record GPU timestamps around dispatch
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmdBuffer, frameIndex);
    }

    // Dispatch compute shader
    vkCmdDispatch(cmdBuffer, dispatchX, dispatchY, dispatchZ);

    // End GPU timing
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(cmdBuffer, frameIndex, dispatchExtent.width, dispatchExtent.height);
    }

    if (renderTargetInfo) {
        // M4: blit the (possibly smaller) offscreen target up to the swapchain extent. Handles the
        // GENERAL->TRANSFER_SRC / swapchain UNDEFINED->TRANSFER_DST / blit / ->GENERAL-or-PRESENT_SRC
        // transitions, ending in the same layout contract the non-render-target path applies below.
        // Sampled Lighting Inc3 M1: calls the shared free function (SwapchainBarriers.h) instead of
        // the old private method — same logic, now reusable by BlitNode too.
        SwapchainBarriers::BlitRenderTargetToSwapchain(GetDevice(), renderTargetImageLayouts_, cmdBuffer,
                                                        renderTargetInfo, swapchainImage,
                                                        swapchainInfo->GetExtent(), leaveImageInGeneral);
    } else if (!leaveImageInGeneral && !writesNoImage) {
        // Voxel-only, no render target: compute is the last writer, so hand the image to present.
        // Composite: leave it in GENERAL — the downstream UI render pass loads from GENERAL and owns
        // the →PRESENT_SRC transition. Note: the GENERAL→PRESENT_SRC transition is NOT yet baked into
        // the schedule (P5 concern), so we emit it explicitly here using barrier2.
        // Sampled Lighting Inc3 M1: skip entirely when writesNoImage — see the pre-dispatch
        // transition's comment above; a no-image dispatch must not touch the swapchain image's
        // layout at all, present-bound or not.
        SwapchainBarriers::TransitionImageToPresentBarrier2(GetDevice(), cmdBuffer, swapchainImage);
    }

    // End command buffer
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to end command buffer");
    }

    NODE_LOG_DEBUG("[ComputeDispatchNode::RecordComputeCommands] Recorded compute commands for image " + std::to_string(imageIndex));
}

// ============================================================================
// HELPER METHODS
// ============================================================================

// Replay entry barriers baked by the FrameSyncScheduler for this node's SubmitGroup.
// Image barriers use the runtime swapchain image handle (node-local correlation).
void ComputeDispatchNode::ReplayEntryBarriers(
    VkCommandBuffer cmd, const SubmitGroup& group,
    uint32_t imageIndex, Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo) {
    if (group.entryBarriers.empty()) return;

    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkMemoryBarrier2>      memBarriers;

    for (const GroupBarrier& b : group.entryBarriers) {
        if (b.isImage) {
            VkImageMemoryBarrier2 ib{};
            ib.sType           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            ib.srcStageMask    = b.src.stage;
            ib.srcAccessMask   = b.src.access;
            ib.oldLayout       = b.src.layout;
            ib.dstStageMask    = b.dst.stage;
            ib.dstAccessMask   = b.dst.access;
            ib.newLayout       = b.dst.layout;
            ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            // Tier-1 node-local correlation placeholder: only reached once image barriers are
            // baked (Tier-2+). In Tier-1 the scheduler bakes buffer/memory barriers only, so on
            // the voxel path entryBarriers is empty and this branch is a no-op.
            ib.image           = swapchainInfo->GetImage(imageIndex);
            ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            imageBarriers.push_back(ib);
        } else {
            VkMemoryBarrier2 mb{};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mb.srcStageMask  = b.src.stage;
            mb.srcAccessMask = b.src.access;
            mb.dstStageMask  = b.dst.stage;
            mb.dstAccessMask = b.dst.access;
            memBarriers.push_back(mb);
        }
    }

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dep.pImageMemoryBarriers    = imageBarriers.data();
    dep.memoryBarrierCount      = static_cast<uint32_t>(memBarriers.size());
    dep.pMemoryBarriers         = memBarriers.data();
    GetDevice()->fpCmdPipelineBarrier2(cmd, &dep);
}

void ComputeDispatchNode::BindComputePipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptorSet) {
    // Bind compute pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // Bind descriptor set from DescriptorSetNode
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        layout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );
}

void ComputeDispatchNode::SetPushConstants(Context& ctx, VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const void* pushConstantData) {
    // Check for push constant data from PushConstantGathererNode
    std::vector<uint8_t> pushConstantDataVec = ctx.In(ComputeDispatchNodeConfig::PUSH_CONSTANT_DATA);
    std::vector<VkPushConstantRange> pushConstantRanges = ctx.In(ComputeDispatchNodeConfig::PUSH_CONSTANT_RANGES);

    // Use gathered push constants if available
    if (!pushConstantDataVec.empty() && !pushConstantRanges.empty()) {
        // Apply each push constant range
        for (const auto& range : pushConstantRanges) {
            if (range.offset + range.size <= pushConstantDataVec.size()) {
                vkCmdPushConstants(
                    cmdBuffer,
                    layout,
                    range.stageFlags,
                    range.offset,
                    range.size,
                    pushConstantDataVec.data() + range.offset
                );

                static int pcLogCount = 0;
                if (pcLogCount++ < 3) {
                    NODE_LOG_DEBUG("[ComputeDispatchNode] Setting gathered push constants: offset=" +
                                  std::to_string(range.offset) + ", size=" + std::to_string(range.size));
                }
            }
        }
    }
    // Fall back to legacy push constant data if no gatherer connected
    else if (pushConstantData != nullptr) {
        // Get shader bundle to check for push constants
        auto shaderBundle = ctx.In(ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE);

        if (shaderBundle && shaderBundle->reflectionData &&
            !shaderBundle->reflectionData->pushConstants.empty()) {

            // Get first push constant range (we assume single range for now)
            const auto& pc = shaderBundle->reflectionData->pushConstants[0];

            vkCmdPushConstants(
                cmdBuffer,
                layout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                pc.offset,
                pc.size,
                pushConstantData
            );

            static int pcLogCount = 0;
            if (pcLogCount++ < 3) {
                NODE_LOG_DEBUG("[ComputeDispatchNode] Setting legacy push constants: offset=" +
                              std::to_string(pc.offset) + ", size=" + std::to_string(pc.size));
            }
        }
    }
}

// M4: BlitRenderTargetToSwapchain's implementation moved to the free function
// SwapchainBarriers::BlitRenderTargetToSwapchain (Nodes/Common/SwapchainBarriers.h,
// Sampled Lighting Inc3 M1/KI-018) — see that function's doc comment for the full
// barrier sequence. Extracted so the new BlitNode can call the SAME logic instead of
// a second, divergent copy. This class's call site is in RecordComputeCommands above.

// ============================================================================
// CLEANUP
// ============================================================================

void ComputeDispatchNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[ComputeDispatchNode::CleanupImpl] Cleaning up resources");

    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor
    // when the node is destroyed. Logger object stays alive for parent log extraction.

    if (vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
        // Free command buffers
        if (!commandBuffers.empty() && commandPool != VK_NULL_HANDLE) {
            std::vector<VkCommandBuffer> rawHandles;
            rawHandles.reserve(commandBuffers.size());
            for (size_t i = 0; i < commandBuffers.size(); ++i) {
                rawHandles.push_back(commandBuffers.GetValue(i));
            }

            vkFreeCommandBuffers(
                vulkanDevice->device,
                commandPool,
                static_cast<uint32_t>(rawHandles.size()),
                rawHandles.data()
            );
            commandBuffers.clear();
        }

        // Reset command pool to avoid using stale handle during recompilation
        commandPool = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[ComputeDispatchNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ComputeDispatchNodeType);
