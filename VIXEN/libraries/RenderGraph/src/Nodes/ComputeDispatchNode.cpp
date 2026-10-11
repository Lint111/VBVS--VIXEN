#include "Nodes/Common/PresentationSynchronization.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include <mutex>
#include "Core/GPUPerformanceLogger.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include "Nodes/Common/GpuDeviceClass.h"
#include "VulkanSwapChain.h"  // For SwapChainPublicVariables
#include "ShaderDataBundle.h"
#include "SpirvReflector.h"
#include "Debug/IDebugCapture.h"  // For debug capture passthrough
#include "Core/NodeLogging.h"
#include <stdexcept>
#include <chrono>
#include <set>
#include <cmath>
#include <algorithm>


namespace Vixen::RenderGraph {

// Command buffers are frame-indexed (ring depth = frames-in-flight), NOT image-indexed:
// FrameSyncNode waits the per-flight completion timeline before reusing a slot (and retains its
// binary fence wait for existing mid-frame consumers), so the command-buffer ring matches the
// flight ring whose full-frame use it guards. Mirrors
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

void ComputeDispatchNode::TypedSetupImpl(TypedSetupContext& ctx) {
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

void ComputeDispatchNode::TypedCompileImpl(TypedCompileContext& ctx) {
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
    chunkProfile_ = dynamic_cast<SimpleTaskProfile*>(gpuProfile_);
    chunkCalibrationKey_.clear();
    if (gpuProfile_) {
        RegisterPhaseProfile(TaskProfilePhase::Execute, gpuProfile_);
        NODE_LOG_INFO("[ComputeDispatchNode] Registered GPU profile: " + profileId);
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void ComputeDispatchNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // Get current image index from SwapChainNode
    uint32_t imageIndex = ctx.In(ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Get current frame-in-flight index from FrameSyncNode
    uint32_t currentFrameIndex = ctx.In(ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Get semaphore arrays from FrameSyncNode
    const std::vector<VkSemaphore>& imageAvailableSemaphores = ctx.In(ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderCompleteSemaphores = ctx.In(ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(ComputeDispatchNodeConfig::IN_FLIGHT_FENCE);

    const bool leaveImageInGeneral =
        GetParameterValue<bool>(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, false);
    const bool writesNoImage =
        GetParameterValue<bool>(ComputeDispatchNodeConfig::PARAM_WRITES_NO_IMAGE, false);
    auto* target = ctx.In(ComputeDispatchNodeConfig::SWAPCHAIN_INFO);
    if (!target || imageIndex == UINT32_MAX || imageIndex >= target->GetImageCount() ||
        currentFrameIndex >= commandBuffers.size()) {
        NODE_LOG_WARNING("ComputeDispatchNode: Invalid image/frame index - skipping frame");
        return;
    }
    const auto handoffs = ResolveTargetSemaphoreHandoffs(
        target, imageAvailableSemaphores, renderCompleteSemaphores, currentFrameIndex, imageIndex,
        ComputeDispatchWaitsForSwapchainAcquire(writesNoImage), !leaveImageInGeneral && !writesNoImage);
    const bool waitsForAcquire = handoffs.acquire != VK_NULL_HANDLE;
    const bool signalsPresent = handoffs.present != VK_NULL_HANDLE;
    const VkSemaphore imageAvailableSemaphore = handoffs.acquire;
    const VkSemaphore renderCompleteSemaphore = handoffs.present;

    static int logCounter = 0;
    if (logCounter++ < 20) {
        NODE_LOG_DEBUG("Compute Frame " + std::to_string(currentFrameIndex) + ", Image " + std::to_string(imageIndex));
    }

    // Reset the binary fence before submitting (FrameSyncNode waited on it at frame start). In
    // composite mode the downstream UI submit owns this fence, so leave it alone here; the
    // FrameSyncNode frame-end timeline marker guards reuse after all graph submissions.
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

    CollectTileTiming(currentFrameIndex);

    // Detect if inputs changed (mark all command buffers dirty if so)
    VkPipeline currentPipeline = ctx.In(ComputeDispatchNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentPipelineLayout = ctx.In(ComputeDispatchNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> currentDescriptorSets = ctx.In(ComputeDispatchNodeConfig::DESCRIPTOR_SETS);

    if (currentPipeline != lastPipeline ||
        currentPipelineLayout != lastPipelineLayout ||
        currentDescriptorSets != lastDescriptorSets) {
        const auto bundle = ctx.In(ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE);
        canTile_ = bundle && ShaderManagement::SpirvReflector::CanPreserveWorkgroupBuiltinsWhenTiled(
            bundle->GetSpirv(ShaderManagement::ShaderStage::Compute));
        if (!canTile_)
            NODE_LOG_WARNING("[GPUYIELD_UNBOUNDED] missing shader bundle or NumWorkgroups dependency: " + GetInstanceName());

        // Inputs changed - mark all command buffers dirty
        commandBuffers.MarkAllDirty();

        lastPipeline = currentPipeline;
        lastPipelineLayout = currentPipelineLayout;
        lastDescriptorSets = currentDescriptorSets;
        chunkCost_ = {};
        chunkCalibrationKey_.clear();  // re-enter from the persisted envelope (R503)
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
    const auto recordedBuffers = RecordComputeCommands(ctx, cmdBuffer, imageIndex, currentFrameIndex, &pushConstants, leaveImageInGeneral, writesNoImage);
    commandBuffers.MarkReady(currentFrameIndex);

    // P5b M1: read timeline primitives from FrameSyncNode slots (Optional — VK_NULL_HANDLE / 0 if not wired)
    VkSemaphore timelineSem = ctx.In(ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

    // Build vkQueueSubmit2 semaphore arrays
    std::vector<VkCommandBufferSubmitInfo> cmdInfos;
    cmdInfos.reserve(recordedBuffers.size());
    for (auto buffer : recordedBuffers) {
        VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        info.commandBuffer = buffer;
        cmdInfos.push_back(info);
    }

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Baked-Perf M6 Task 6.1 (audit E2): the binary acquire wait must be consumed by the
    // first submit that actually accesses the swapchain image. On the split baked path this
    // dispatch writes HitRecord only (writesNoImage) — BlitNode is the real first swapchain
    // touch and now owns this wait instead. See ComputeDispatchWaitsForSwapchainAcquire's doc
    // comment (ComputeDispatchNode.h) for why this is safe (the per-flight fence wait in
    // FrameSyncNode, not this semaphore, is what actually guards this dispatch's own
    // cross-frame resource reuse). Swapchain-writing / self-blitting variants (writesNoImage
    // == false) still consume it here, unchanged.
    if (waitsForAcquire) {
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
    if (signalsPresent) {
        VkSemaphoreSubmitInfo renderSig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        renderSig.semaphore = renderCompleteSemaphore;
        renderSig.value     = 0;  // binary semaphore: value ignored
        // Baked-Perf M6 Task 6.3 (audit pattern R7): scoped to COMPUTE_SHADER_BIT, this
        // node's only queue-side work on the voxel-only path (matches the acquire wait's
        // and the timeline signal's own stage mask above), not ALL_COMMANDS_BIT.
        renderSig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        signals.push_back(renderSig);
    }

    // Composite mode submits with no fence — the downstream UI submit owns inFlightFence. Later graph
    // submits may still follow it, so FrameSyncNode's frame-end timeline marker guards full-frame reuse.
    VkFence submitFence = leaveImageInGeneral ? VK_NULL_HANDLE : inFlightFence;

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos      = waits.data();
    si.commandBufferInfoCount   = static_cast<uint32_t>(cmdInfos.size());
    si.pCommandBufferInfos      = cmdInfos.data();
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();

    // Submit to graphics queue via synchronization2. Externally synchronized per Vulkan spec
    // (audit V-M11): the TBB parallel executor can schedule this alongside another node's
    // submit on the same queue.
    // Lock-free phase 1 (design §2.5): publish to the queue-owner channel; owner drains in canonical
    // order after the wave (a fault there surfaces device-loss). Inactive channel -> guarded submit.
    auto record = SubmitRecord::FromSubmitInfo2(si, submitFence);
    record.submissionBudgetNs = tileBudgets_[currentFrameIndex];
    for (uint32_t groups : tileWorkGroupCounts_[currentFrameIndex]) {
        record.commandCostUpperBoundsNs.push_back(chunkCost_.Estimate(groups));
    }
    submissionBatches_[currentFrameIndex] = record.SubmissionBatches();
    if (!GetOwningGraph()->PublishSubmit(this, std::move(record))) {
        std::lock_guard submitLock(vulkanDevice->SubmitMutex(vulkanDevice->queue));
        const VkResult result = record.Submit(
            vulkanDevice->fpQueueSubmit2, vulkanDevice->queue);
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_DEVICE_LOST) {
                GetOwningGraph()->NotifyDeviceLost("ComputeDispatchNode::ExecuteImpl vkQueueSubmit2");
            }
            throw std::runtime_error("[ComputeDispatchNode::ExecuteImpl] Failed to submit command buffer (vkQueueSubmit2): " + std::to_string(result));
        }
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

void ComputeDispatchNode::CollectTileTiming(uint32_t frameIndex) {
    if (auto& query = tileQueries_[frameIndex]; query && query->ReadResults(0)) {
        double totalMs = 0, maximumMs = 0;
        for (uint32_t i = 0; i < tileCounts_[frameIndex]; ++i) {
            const double ms = query->GetElapsedMs(0, i * 4, i * 4 + 3);
            if (std::getenv("VIXEN_GPUYIELD_TRACE")) {
                NODE_LOG_INFO("[GPUYIELD_CHUNK] node=" + GetInstanceName() +
                    " flight=" + std::to_string(frameIndex) + " chunk=" + std::to_string(i) +
                    " start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4)) +
                    " end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 3)) +
                    " dispatch_start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 1)) +
                    " dispatch_end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 2)) +
                    " dispatch_gpu_ms=" + std::to_string(query->GetElapsedMs(0, i * 4 + 1, i * 4 + 2)) +
                    " elapsed_gpu_ms=" + std::to_string(ms));
            }
            totalMs += ms;
            if (i < tileWorkGroupCounts_[frameIndex].size() && tileWorkGroupCounts_[frameIndex][i]) {
                chunkCost_.Observe(tileWorkGroupCounts_[frameIndex][i],
                    tileNominalWorkGroupCounts_[frameIndex], ms * 1.0e6);
            }
        }
        // R503: publish the learned envelope to the task profile; CalibrationStore
        // persists it per device at shutdown, keyed by this flight's extent.
        if (chunkProfile_ && chunkCost_.Calibrated() && tileExtentKeys_[frameIndex] == chunkCalibrationKey_)
            chunkProfile_->SetChunkCalibration(tileExtentKeys_[frameIndex],
                {chunkCost_.AnchorUnits(), chunkCost_.UnitNs(), chunkCost_.FixedNs()});
        totalMs = 0;
        for (const auto& batch : submissionBatches_[frameIndex]) {
            const double elapsed = query->GetElapsedMs(0, batch.first * 4,
                (batch.first + batch.count) * 4 - 1);
            maximumMs = std::max(maximumMs, elapsed);
            totalMs += elapsed;
            uint64_t groups = 0;
            for (std::size_t i = batch.first; i < batch.first + batch.count; ++i)
                groups += tileWorkGroupCounts_[frameIndex][i];
            if (groups) chunkCost_.ObserveBatch(groups, batch.count, elapsed * 1.0e6);
            if (std::getenv("VIXEN_GPUYIELD_TRACE")) {
                NODE_LOG_INFO("[GPUYIELD_SUBMISSION] node=" + GetInstanceName() +
                    " first_chunk=" + std::to_string(batch.first) +
                    " chunks=" + std::to_string(batch.count) +
                    " elapsed_gpu_ms=" + std::to_string(elapsed));
            }
        }
        NODE_LOG_INFO("[GPUYIELD] node=" + GetInstanceName() +
            " submissions=" + std::to_string(submissionBatches_[frameIndex].size()) +
            " chunks=" + std::to_string(tileCounts_[frameIndex]) +
            " total_gpu_ms=" + std::to_string(totalMs) +
            " max_submission_ms=" + std::to_string(maximumMs) +
            " budget_ms=" + std::to_string(tileBudgets_[frameIndex] / 1.0e6) +
            " device_class=" + GpuDeviceClassName(*vulkanDevice));
    }

}

std::vector<VkCommandBuffer> ComputeDispatchNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmdBuffer, uint32_t imageIndex, uint32_t frameIndex, const void* pushConstantData, bool leaveImageInGeneral, bool writesNoImage) {
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

    // Cold-start below the observed Dozen 64px peak; calibrated devices grow the
    // tile to the largest square fitting the existing submission budget. Keep
    // the frame budget separate: smaller chunks do not make an expensive pass cheap.
    const auto& capacity = GetOwningGraph()->GetCapacityTracker().GetConfig();
    const uint64_t budgetNs = std::max(uint64_t{1},
        std::min(capacity.gpuTimeBudgetNs, capacity.gpuSubmissionBudgetNs));
    // R503: the envelope is learned per extent and persisted per device through the
    // task profile, so a known resolution starts warm; a new one starts cold.
    const std::string extentKey = std::to_string(dispatchX) + "x" + std::to_string(dispatchY);
    if (extentKey != chunkCalibrationKey_) {
        chunkCalibrationKey_ = extentKey;
        chunkCost_ = {};
        if (chunkProfile_) {
            if (const auto restored = chunkProfile_->GetChunkCalibration(extentKey)) {
                chunkCost_.Import(restored->anchorUnits, restored->unitNs, restored->fixedNs);
            }
        }
        NODE_LOG_INFO("[GPUYIELD_CALIBRATION] node=" + GetInstanceName() + " extent=" + extentKey +
            (chunkCost_.Calibrated() ? " start=warm anchor_units=" + std::to_string(chunkCost_.AnchorUnits()) +
                " unit_ns=" + std::to_string(chunkCost_.UnitNs()) + " fixed_ns=" + std::to_string(chunkCost_.FixedNs())
                : std::string(" start=cold")));
    }
    tileExtentKeys_[frameIndex] = extentKey;
    const uint32_t edge = canTile_
        ? chunkCost_.Edge(budgetNs, std::max(dispatchX, dispatchY), 7)
        : std::max(1u, std::max(dispatchX, dispatchY));
    tileNominalWorkGroupCounts_[frameIndex] = std::min(edge, dispatchX) * std::min(edge, dispatchY);
    const uint32_t count = std::max(1u, ((dispatchX + edge - 1) / edge) * ((dispatchY + edge - 1) / edge));
    auto& extraBuffers = tileCommandBuffers_[frameIndex];
    if (extraBuffers.size() + 1 < count) {
        const uint32_t needed = count - 1 - static_cast<uint32_t>(extraBuffers.size());
        std::vector<VkCommandBuffer> allocated(needed);
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = needed;
        if (vkAllocateCommandBuffers(vulkanDevice->device, &allocation, allocated.data()) != VK_SUCCESS)
            throw std::runtime_error("ComputeDispatchNode: failed to allocate tile commands");
        extraBuffers.insert(extraBuffers.end(), allocated.begin(), allocated.end());
    }
    std::vector<VkCommandBuffer> recordedBuffers{cmdBuffer};
    if (count > 1)
        recordedBuffers.insert(recordedBuffers.end(), extraBuffers.begin(), extraBuffers.begin() + (count - 1));
    if (tileQueryCapacities_[frameIndex] < count) {
        if (tileQueries_[frameIndex]) {
            // Release recorded pool references before replacing their backing storage.
            if (vkResetCommandBuffer(cmdBuffer, 0) != VK_SUCCESS)
                throw std::runtime_error("ComputeDispatchNode: query-growth command reset failed");
            for (VkCommandBuffer buffer : extraBuffers)
                if (vkResetCommandBuffer(buffer, 0) != VK_SUCCESS)
                    throw std::runtime_error("ComputeDispatchNode: query-growth tile reset failed");
        }
        tileQueryCapacities_[frameIndex] = std::max(count, tileQueryCapacities_[frameIndex] * 2u);
        tileQueries_[frameIndex] = std::make_unique<GPUTimestampQuery>(
            vulkanDevice, 1, tileQueryCapacities_[frameIndex] * 4);
    }
    tileCounts_[frameIndex] = count;
    tileBudgets_[frameIndex] = budgetNs;
    tileWorkGroupCounts_[frameIndex].clear();
    auto& tileQuery = *tileQueries_[frameIndex];
    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to begin command buffer");
    }

    tileQuery.ResetQueryRange(cmdBuffer, 0, 0, 4);
    tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);


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
    // match the image's ACTUAL last-recorded layout (the target buffer tracks it exactly,
    // not a seen/not-seen guess — see KI-007 and DecideRenderTargetPriorLayoutAndUpdate's comment;
    // RenderTargetNode's persistent-across-same-extent-recompile lifecycle means "new Compile"
    // does NOT imply "new image").
    if (renderTargetInfo) {
        VkImageLayout priorLayout = DecideRenderTargetPriorLayoutAndUpdate(
            *renderTargetInfo, renderTargetInfo->GetCurrentIndex(), VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmdBuffer, writeImage, priorLayout);
    } else if (!writesNoImage) {
        // Sampled Lighting Inc3 M1: skip entirely when this dispatch manages no presentable image
        // (writesNoImage) — writeImage would otherwise alias the swapchain image, which this
        // dispatch never actually writes; transitioning it here would race a later pass's own
        // transition of the SAME handle (see PARAM_WRITES_NO_IMAGE's doc comment).
        const VkImageLayout prior = DecideRenderTargetPriorLayoutAndUpdate(*swapchainInfo, imageIndex, VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmdBuffer, writeImage, prior);
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

    uint32_t tileIndex = 0;
    for (uint32_t baseY = 0; baseY < dispatchY; baseY += edge) {
        for (uint32_t baseX = 0; baseX < dispatchX; baseX += edge) {
            tileWorkGroupCounts_[frameIndex].push_back(canTile_
                ? std::min(edge, dispatchX - baseX) * std::min(edge, dispatchY - baseY)
                : dispatchX * dispatchY);
            tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     tileIndex * 4 + 1);
            if (canTile_) {
                vkCmdDispatchBase(cmdBuffer, baseX, baseY, 0,
                                  std::min(edge, dispatchX - baseX),
                                  std::min(edge, dispatchY - baseY), dispatchZ);
            } else {
                vkCmdDispatch(cmdBuffer, dispatchX, dispatchY, dispatchZ);
            }
            tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     tileIndex * 4 + 2);
            if (++tileIndex != count) {
                tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                         tileIndex * 4 - 1);
                if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS)
                    throw std::runtime_error("ComputeDispatchNode: failed to end tile commands");
                cmdBuffer = recordedBuffers[tileIndex];
                if (vkBeginCommandBuffer(cmdBuffer, &beginInfo) != VK_SUCCESS)
                    throw std::runtime_error("ComputeDispatchNode: failed to begin tile commands");
                // Each retired command owns four queries. Resetting the entire
                // capacity in chunk zero makes instrumentation grow with tile count.
                tileQuery.ResetQueryRange(cmdBuffer, 0, tileIndex * 4, 4);
                tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         tileIndex * 4);
                BindComputePipeline(cmdBuffer, pipeline, pipelineLayout, descriptorSet);
                SetPushConstants(ctx, cmdBuffer, pipelineLayout, pushConstantData);
            }
        }
    }

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
        SwapchainBarriers::BlitRenderTargetToSwapchain(GetDevice(), cmdBuffer,
                                                        renderTargetInfo, swapchainInfo, imageIndex, leaveImageInGeneral);
    } else if (!leaveImageInGeneral && !writesNoImage && swapchainInfo->UsesWsiSynchronization()) {
        // Voxel-only, no render target: compute is the last writer, so hand the image to present.
        // Composite: leave it in GENERAL — the downstream UI render pass loads from GENERAL and owns
        // the →PRESENT_SRC transition. Note: the GENERAL→PRESENT_SRC transition is NOT yet baked into
        // the schedule (P5 concern), so we emit it explicitly here using barrier2.
        // Sampled Lighting Inc3 M1: skip entirely when writesNoImage — see the pre-dispatch
        // transition's comment above; a no-image dispatch must not touch the swapchain image's
        // layout at all, present-bound or not.
        SwapchainBarriers::TransitionImageToPresentBarrier2(GetDevice(), cmdBuffer, swapchainImage);
        swapchainInfo->SetImageLayout(imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    tileQuery.WriteTimestamp(cmdBuffer, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, count * 4 - 1);

    // End command buffer
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputeDispatchNode::RecordComputeCommands] Failed to end command buffer");
    }

    NODE_LOG_DEBUG("[ComputeDispatchNode::RecordComputeCommands] Recorded compute commands for image " + std::to_string(imageIndex));
    return recordedBuffers;
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
            swapchainInfo->SetImageLayout(imageIndex, ib.newLayout);
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

void ComputeDispatchNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[ComputeDispatchNode::CleanupImpl] Cleaning up resources");

    // GPU resources (QueryPools) will be automatically released by GPUQueryManager destructor
    // when the node is destroyed. Logger object stays alive for parent log extraction.

    if (vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < tileQueries_.size(); ++i) CollectTileTiming(i);
        for (auto& buffers : tileCommandBuffers_) {
            if (!buffers.empty() && commandPool != VK_NULL_HANDLE)
                vkFreeCommandBuffers(vulkanDevice->device, commandPool,
                                     static_cast<uint32_t>(buffers.size()), buffers.data());
            buffers.clear();
        }
        for (auto& query : tileQueries_) query.reset();
        tileCounts_.fill(0);
        tileQueryCapacities_.fill(0);
        chunkCost_ = {};
        tileBudgets_.fill(0);

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
