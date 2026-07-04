#include "Nodes/ComputeDispatchNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
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
    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocating " + std::to_string(imageCount) + " command buffers");

    // Allocate command buffers (one per swapchain image)
    commandBuffers.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    std::vector<VkCommandBuffer> cmdBuffers(imageCount);
    VkResult result = vkAllocateCommandBuffers(vulkanDevice->device, &allocInfo, cmdBuffers.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::CompileImpl] Failed to allocate command buffers: " + std::to_string(result));
    }

    // Store command buffers in stateful container
    for (uint32_t i = 0; i < imageCount; ++i) {
        commandBuffers[i] = cmdBuffers[i];
        commandBuffers.MarkDirty(i);  // Initial state: needs recording
    }

    NODE_LOG_INFO("[ComputeDispatchNode::CompileImpl] Allocated " + std::to_string(imageCount) + " command buffers successfully");

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
    // FrameSyncNode wait can't deadlock on a skipped frame.
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("ComputeDispatchNode: Invalid image index - skipping frame");
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

    // Phase 0.4: Reset fence before submitting (fence was already waited on by FrameSyncNode). In
    // composite mode the downstream UI submit resets + owns the fence, so leave it alone here.
    if (!leaveImageInGeneral) {
        vkResetFences(vulkanDevice->device, 1, &inFlightFence);
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
    VkCommandBuffer cmdBuffer = commandBuffers.GetValue(imageIndex);
    RecordComputeCommands(ctx, cmdBuffer, imageIndex, currentFrameIndex, &pushConstants, leaveImageInGeneral);
    commandBuffers.MarkReady(imageIndex);

    // P5b M1: read timeline primitives from FrameSyncNode slots (Optional — VK_NULL_HANDLE / 0 if not wired)
    VkSemaphore timelineSem = ctx.In(ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

    // Build vkQueueSubmit2 semaphore arrays
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmdBuffer;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Binary acquire wait (imageAvailable is a WSI binary semaphore)
    VkSemaphoreSubmitInfo acquireWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    acquireWait.semaphore = imageAvailableSemaphore;
    acquireWait.value     = 0;  // binary semaphore: value ignored
    acquireWait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    waits.push_back(acquireWait);

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
        renderSig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
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

    // Submit to graphics queue via synchronization2
    VkResult result = vulkanDevice->fpQueueSubmit2(vulkanDevice->queue, 1, &si, submitFence);
    if (result != VK_SUCCESS) {
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

void ComputeDispatchNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData, bool leaveImageInGeneral) {
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

    // Validate descriptor sets
    if (descriptorSets.empty() || imageIndex >= descriptorSets.size()) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Invalid descriptor sets for image " + std::to_string(imageIndex));
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
    VkDescriptorSet descriptorSet = descriptorSets[imageIndex];

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
        TransitionImageToGeneralBarrier2(cmdBuffer, writeImage, priorLayout);
    } else {
        TransitionImageToGeneralBarrier2(cmdBuffer, writeImage);
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
        BlitRenderTargetToSwapchain(cmdBuffer, renderTargetInfo, swapchainImage,
                                    swapchainInfo->GetExtent(), leaveImageInGeneral);
    } else if (!leaveImageInGeneral) {
        // Voxel-only, no render target: compute is the last writer, so hand the image to present.
        // Composite: leave it in GENERAL — the downstream UI render pass loads from GENERAL and owns
        // the →PRESENT_SRC transition. Note: the GENERAL→PRESENT_SRC transition is NOT yet baked into
        // the schedule (P5 concern), so we emit it explicitly here using barrier2.
        TransitionImageToPresentBarrier2(cmdBuffer, swapchainImage);
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

// Fallback barrier2: oldLayout → GENERAL (TOP_OF_PIPE/0-or-BLIT → COMPUTE_SHADER/SHADER_STORAGE_WRITE).
void ComputeDispatchNode::TransitionImageToGeneralBarrier2(VkCommandBuffer cmdBuffer, VkImage image,
                                                            VkImageLayout oldLayout) {
    VkImageMemoryBarrier2 ib{};
    ib.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        // Coming from BlitRenderTargetToSwapchain's read of this image last frame.
        ib.srcStageMask  = VK_PIPELINE_STAGE_2_BLIT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    } else {
        ib.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        ib.srcAccessMask = VK_ACCESS_2_NONE;
    }
    ib.oldLayout           = oldLayout;
    ib.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image               = image;
    ib.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    GetDevice()->fpCmdPipelineBarrier2(cmdBuffer, &dep);
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

// Explicit GENERAL → PRESENT_SRC_KHR transition for the voxel-only (!leaveImageInGeneral) path.
// This is NOT yet baked in the schedule (the present-side group's PresentSrc access is a P5 concern).
void ComputeDispatchNode::TransitionImageToPresentBarrier2(VkCommandBuffer cmdBuffer, VkImage image) {
    VkImageMemoryBarrier2 ib{};
    ib.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
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

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &ib;
    GetDevice()->fpCmdPipelineBarrier2(cmdBuffer, &dep);
}

// M4: blit the offscreen render target's current image up to the swapchain image (LINEAR filter —
// upscales when the render target is smaller than the swapchain). Barrier sequence:
//   render target:  GENERAL (compute write)      -> TRANSFER_SRC_OPTIMAL
//   swapchain:       UNDEFINED (WSI acquire)       -> TRANSFER_DST_OPTIMAL
//   vkCmdBlitImage
//   swapchain:       TRANSFER_DST_OPTIMAL -> GENERAL (composite/UI) or PRESENT_SRC_KHR (voxel-only)
// The render target itself is left in TRANSFER_SRC_OPTIMAL; its next Execute's compute write
// transitions it back to GENERAL via TransitionImageToGeneralBarrier2 above (harmless either way,
// since that barrier's oldLayout is UNDEFINED only as a hint — the dstAccess/stage still applies).
void ComputeDispatchNode::BlitRenderTargetToSwapchain(
    VkCommandBuffer cmdBuffer,
    Vixen::Vulkan::Resources::IRenderTarget* renderTarget,
    VkImage swapchainImage,
    VkExtent2D swapchainExtent,
    bool leaveImageInGeneral)
{
    VkImage renderTargetImage = renderTarget->GetCurrentImage();
    VkExtent2D srcExtent = renderTarget->GetExtent();

    // Swapchain-side counterpart to the KI-007 fix: the entry barrier below used to hardcode
    // oldLayout=UNDEFINED for the swapchain image on EVERY frame, but that's only true for a
    // swapchain image's true first use. On the leaveImageInGeneral path, the downstream UI render
    // pass (PARAM_INITIAL_LAYOUT=General, PARAM_FINAL_LAYOUT=PresentSrc) moves this SAME image
    // handle GENERAL->PRESENT_SRC_KHR and then vkQueuePresentKHR leaves it there — so the NEXT time
    // this ring slot's image index comes back around, its real layout is PRESENT_SRC_KHR, not
    // UNDEFINED. Declaring UNDEFINED anyway produced VUID-vkCmdDraw-None-09600 at the UI render
    // pass's first draw (the render pass's initialLayout=General assertion was already false by
    // the time the pass began), the root cause of the render-view flicker (KI-009).
    const VkImageLayout swapchainPriorLayout = DecideRenderTargetPriorLayoutAndUpdate(
        renderTargetImageLayouts_, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // --- Entry barriers: render target GENERAL->TRANSFER_SRC, swapchain ?->TRANSFER_DST ---
    VkImageMemoryBarrier2 entryBarriers[2]{};

    entryBarriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    entryBarriers[0].srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    entryBarriers[0].srcAccessMask       = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    entryBarriers[0].oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    entryBarriers[0].dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entryBarriers[0].dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
    entryBarriers[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    entryBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[0].image               = renderTargetImage;
    entryBarriers[0].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    entryBarriers[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    // srcStageMask must match (or come after) the acquire semaphore's wait stage
    // (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, set on this command buffer's submit — see
    // acquireWait.stageMask above) so this barrier actually chains an execution dependency off
    // that wait. TOP_OF_PIPE_BIT here (the old value) is a no-op source that doesn't synchronize
    // with anything, which is only harmless when oldLayout is a true first-use UNDEFINED (nothing
    // to wait for) — once the swapchain-tracking fix above declares a real prior layout
    // (PRESENT_SRC_KHR from a previous frame's present), this must correctly wait on the acquire,
    // else validation reports SYNC-HAZARD-WRITE-AFTER-READ against vkAcquireNextImageKHR.
    entryBarriers[1].srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    entryBarriers[1].srcAccessMask       = VK_ACCESS_2_NONE;
    entryBarriers[1].oldLayout           = swapchainPriorLayout;
    entryBarriers[1].dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    entryBarriers[1].dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    entryBarriers[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    entryBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    entryBarriers[1].image               = swapchainImage;
    entryBarriers[1].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo entryDep{};
    entryDep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    entryDep.imageMemoryBarrierCount = 2;
    entryDep.pImageMemoryBarriers    = entryBarriers;
    GetDevice()->fpCmdPipelineBarrier2(cmdBuffer, &entryDep);

    // KI-007: this command buffer's compute write already transitioned renderTargetImage to
    // GENERAL earlier in the SAME recording (guaranteeing entryBarriers[0]'s hardcoded
    // oldLayout=GENERAL above is correct), and this barrier just moved it to
    // TRANSFER_SRC_OPTIMAL — record that so the NEXT command buffer that reuses this ring slot
    // (a future frame) declares the correct oldLayout instead of guessing.
    renderTargetImageLayouts_[renderTargetImage] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // --- Blit (LINEAR filter — upscales/downscales src extent to dst extent) ---
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};

    vkCmdBlitImage(cmdBuffer,
                   renderTargetImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchainImage,    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // --- Exit barrier: swapchain TRANSFER_DST -> today's contract (GENERAL for UI, else PRESENT) ---
    VkImageMemoryBarrier2 exitBarrier{};
    exitBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    exitBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT;
    exitBarrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    exitBarrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    exitBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    exitBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    exitBarrier.image               = swapchainImage;
    exitBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (leaveImageInGeneral) {
        // Downstream UI render pass LOADs from GENERAL and owns the ->PRESENT_SRC transition.
        exitBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        exitBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        exitBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        exitBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        exitBarrier.dstAccessMask = VK_ACCESS_2_NONE;
        exitBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    // Either way, this ring slot's image ends the frame at PRESENT_SRC_KHR by the time it's reused:
    // on the leaveImageInGeneral path this function hands it to the UI render pass in GENERAL, but
    // that pass's own finalLayout=PresentSrc (BuildRenderGraph.cpp) plus the present call moves it
    // there before this same image index comes back around. Track that real end state (not the
    // intermediate GENERAL this function leaves it in) so next frame's entry barrier above declares
    // the correct oldLayout instead of hardcoding UNDEFINED.
    renderTargetImageLayouts_[swapchainImage] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkDependencyInfo exitDep{};
    exitDep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    exitDep.imageMemoryBarrierCount = 1;
    exitDep.pImageMemoryBarriers    = &exitBarrier;
    GetDevice()->fpCmdPipelineBarrier2(cmdBuffer, &exitDep);
}

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
