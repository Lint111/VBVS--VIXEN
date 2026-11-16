#include "FrameTrace.h"
#include "Nodes/Common/PresentationSynchronization.h"
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
#include "Core/TaskProfiles/SimpleTaskProfile.h"
#include "Nodes/Common/GpuDeviceClass.h"
#include "SpirvReflector.h"
#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>

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

void ComputeStageNode::TypedSetupImpl(TypedSetupContext& /*ctx*/) {
    NODE_LOG_DEBUG("[ComputeStageNode::SetupImpl] Graph-scope initialization");
}

// ============================================================================
// COMPILE — allocate one command buffer per swapchain image
// ============================================================================

void ComputeStageNode::TypedCompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* devicePtr = ctx.In(ComputeStageNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] Vulkan device input is null");
    }
    SetDevice(devicePtr);

    if (externalInitialize_) {
        externalInitialize_(devicePtr);
    }

    commandPool_ = ctx.In(ComputeStageNodeConfig::COMMAND_POOL);
    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputeStageNode::CompileImpl] Command pool is null/invalid");
    }

    // Command buffers are frame-indexed at the flight-ring depth, not by WSI image count. WSI
    // binaries are optional because an offscreen graph has neither acquire nor present handoffs.
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
                  " command buffers (frame-in-flight ring depth)");

    // Task 0.1 (Baked-Content Perf Audit, top action #9): GPU timing via the centralized
    // GPUQueryManager, same pattern as ComputeDispatchNode/UIRenderNode — lets direct_lighting/
    // spatial_reuse/probe_update each get their own PerfCsvWriter column. Only allocate once
    // (CompileImpl can re-run on recompile; a second AllocateQuerySlot call would leak a slot).
    if (!gpuPerfLogger_) {
        auto* queryMgrPtr = static_cast<GPUQueryManager*>(GetDevice()->GetQueryManager());
        if (queryMgrPtr) {
            auto queryManager = std::shared_ptr<GPUQueryManager>(GetDevice()->GetQueryManagerShared(), queryMgrPtr);
            gpuPerfLogger_ = std::make_shared<GPUPerformanceLogger>(GetInstanceName(), queryManager);
            gpuPerfLogger_->SetEnabled(true);
            gpuPerfLogger_->SetLogFrequency(120);  // ~2s at 60fps, matches ComputeDispatchNode
            gpuPerfLogger_->SetPrintToTerminal(false);
            // Standard stages report each queued tile submission through CollectTileTiming.
            // An external recorder remains one opaque submission and uses the logger's normal
            // whole-dispatch path.
            gpuPerfLogger_->SetSpansSeveralSubmissions(!externalRecorder_);
            if (auto* nodeLogger = GetLogger()) {
                nodeLogger->AddChild(gpuPerfLogger_);
            }
            if (gpuPerfLogger_->IsTimingSupported()) {
                NODE_LOG_INFO("[ComputeStageNode] GPU performance timing enabled (slot " +
                             std::to_string(gpuPerfLogger_->GetQuerySlot()) + ")");
            } else {
                NODE_LOG_WARNING("[ComputeStageNode] GPU timing not supported on this device");
            }
        } else {
            NODE_LOG_WARNING("[ComputeStageNode] GPUQueryManager not available from VulkanDevice");
        }
    }

    const std::string profileId = GetInstanceName() + "_gpu_dispatch";
    chunkProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "compute");
    chunkCalibrationKey_.clear();
    if (chunkProfile_) {
        RegisterPhaseProfile(TaskProfilePhase::Execute, chunkProfile_);
        NODE_LOG_INFO("[ComputeStageNode] Registered GPU profile: " + profileId);
    }
}

// ============================================================================
// EXECUTE — record + vkQueueSubmit2 with timeline edge consumption
// ============================================================================

void ComputeStageNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const bool isConsumer = GetParameterValue<bool>(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);

    const uint32_t imageIndex        = ctx.In(ComputeStageNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(ComputeStageNodeConfig::CURRENT_FRAME_INDEX);
    const std::vector<VkSemaphore>& imageAvailable = ctx.In(ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderComplete = ctx.In(ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(ComputeStageNodeConfig::IN_FLIGHT_FENCE);

    auto* target = ctx.In(ComputeStageNodeConfig::SWAPCHAIN_INFO);
    if (imageIndex == UINT32_MAX || currentFrameIndex >= commandBuffers_.size() ||
        (target && imageIndex >= target->GetImageCount())) {
        NODE_LOG_WARNING("[ComputeStageNode] Invalid image/frame index - skipping frame");
        return;
    }
    const auto handoffs = ResolveTargetSemaphoreHandoffs(
        target, imageAvailable, renderComplete, currentFrameIndex, imageIndex, isConsumer, isConsumer);

    // The designated consumer resets + owns this binary in-flight fence (producers submit with no
    // fence). Other graph submits can follow it, so FrameSyncNode's frame-end timeline marker is
    // the full-frame resource-reuse guard.
    if (isConsumer) {
        vkResetFences(GetDevice()->device, 1, &inFlightFence);
    }

    // Collect GPU performance results for this frame-in-flight (after fence wait) — same
    // placement as ComputeDispatchNode::ExecuteImpl.
    if (gpuPerfLogger_) {
        VIXEN_FRAME_TRACE_SCOPE("perf", GetInstanceName());
        gpuPerfLogger_->CollectResults(currentFrameIndex);
    }
    CollectTileTiming(currentFrameIndex);

    // Re-record on input change (always re-record to refresh push constants each frame).
    VkPipeline currentPipeline = ctx.In(ComputeStageNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout currentLayout = ctx.In(ComputeStageNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> currentSets = ctx.In(ComputeStageNodeConfig::DESCRIPTOR_SETS);
    if (currentPipeline != lastPipeline_ || currentLayout != lastPipelineLayout_ ||
        currentSets != lastDescriptorSets_) {
        const auto bundle = ctx.In(ComputeStageNodeConfig::SHADER_DATA_BUNDLE);
        canTile_ = bundle && ShaderManagement::SpirvReflector::CanPreserveWorkgroupBuiltinsWhenTiled(
            bundle->GetSpirv(ShaderManagement::ShaderStage::Compute));
        if (!canTile_)
            NODE_LOG_WARNING("[GPUYIELD_UNBOUNDED] missing shader bundle or NumWorkgroups dependency: " +
                             GetInstanceName());
        commandBuffers_.MarkAllDirty();
        lastPipeline_ = currentPipeline;
        lastPipelineLayout_ = currentLayout;
        lastDescriptorSets_ = currentSets;
        chunkCost_ = {};
        chunkCalibrationKey_.clear();
    }

    // Command buffer is frame-indexed (flight ring), guarded by FrameSyncNode's full-frame
    // completion timeline wait; RecordComputeCommands still selects image-derived values by imageIndex.
    VkCommandBuffer cmd = commandBuffers_.GetValue(currentFrameIndex);
    std::vector<VkCommandBuffer> recordedBuffers;
    bool recorded;
    {
        VIXEN_FRAME_TRACE_SCOPE("record", GetInstanceName());
        recorded = RecordComputeCommands(ctx, cmd, imageIndex, currentFrameIndex, isConsumer,
                                         recordedBuffers);
    }
    if (!recorded) {
        return;  // fault already reported (R213); nothing recorded to submit this frame.
    }
    commandBuffers_.MarkReady(currentFrameIndex);

    // P5b: timeline primitives from FrameSyncNode (Optional — VK_NULL_HANDLE / 0 if not wired).
    VkSemaphore timelineSem = ctx.In(ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);

    std::vector<VkCommandBufferSubmitInfo> commandInfos;
    commandInfos.reserve(recordedBuffers.size());
    for (VkCommandBuffer recorded : recordedBuffers) {
        VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        info.commandBuffer = recorded;
        commandInfos.push_back(info);
    }

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    if (isConsumer) {
        if (handoffs.acquire != VK_NULL_HANDLE) {
            VkSemaphoreSubmitInfo acquireWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            acquireWait.semaphore = handoffs.acquire;
            acquireWait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            waits.push_back(acquireWait);
        }

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
        // Baked-Perf M6 Task 6.3 (audit pattern R7): scoped to COMPUTE_SHADER_BIT, matching
        // the acquire wait's and the consumer's own timeline wait stage masks above (this
        // node's only queue-side work is the compute dispatch), not ALL_COMMANDS_BIT.
        if (handoffs.present != VK_NULL_HANDLE) {
            VkSemaphoreSubmitInfo renderSig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            renderSig.semaphore = handoffs.present;
            renderSig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            signals.push_back(renderSig);
        }
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
    si.commandBufferInfoCount   = static_cast<uint32_t>(commandInfos.size());
    si.pCommandBufferInfos      = commandInfos.data();
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();

    // R715: the graph's single submission point (published on the lowered executor, issued by the
    // owner at once on the sequential one).
    auto record = SubmitRecord::FromSubmitInfo2(si, submitFence);
    record.submissionBudgetNs = tileBudgets_[currentFrameIndex];
    for (std::size_t command = 0; command < tileWorkGroupCounts_[currentFrameIndex].size(); ++command) {
        const uint32_t groups = tileWorkGroupCounts_[currentFrameIndex][command];
        record.commandCostUpperBoundsNs.push_back(chunkCost_.Estimate(groups));
        const uint64_t lastFrame = frameCommandTimings_.Last(currentFrameIndex, command, groups);
        record.commandCostMeanNs.push_back(lastFrame ? lastFrame : chunkCost_.MeanEstimate(groups));
    }
    submissionBatches_[currentFrameIndex] = record.SubmissionBatches();
    if (const VkResult result = GetOwningGraph()->SubmitNodeWork(this, std::move(record));
        result != VK_SUCCESS) {
        NODE_LOG_ERROR("[ComputeStageNode::ExecuteImpl] vkQueueSubmit2 failed: " + std::to_string(result));
        GetOwningGraph()->ReportGpuFault("ComputeStageNode::ExecuteImpl vkQueueSubmit2", result);
        return;
    }

    ++submissionSerial_;

    // Only WSI consumers have a presentable binary semaphore. Offscreen producers still publish
    // the output slot, with a null handle, so downstream graph topology stays target-agnostic.
    const VkSemaphore renderCompleteSemaphore = handoffs.present;
    ctx.Out(ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE, renderCompleteSemaphore);
    ctx.Out(ComputeStageNodeConfig::VULKAN_DEVICE_OUT, GetDevice());
}

void ComputeStageNode::CollectTileTiming(uint32_t frameIndex) {
    auto& query = tileQueries_[frameIndex];
    if (!query || !query->ReadResults(0)) return;

    frameCommandTimings_.Begin(frameIndex);
    double fullTileMs = 0.0;
    uint32_t fullTiles = 0;
    for (uint32_t i = 0; i < tileCounts_[frameIndex]; ++i) {
        const double elapsedMs = query->GetElapsedMs(0, i * 4, i * 4 + 3);
        if (i >= tileWorkGroupCounts_[frameIndex].size() || !tileWorkGroupCounts_[frameIndex][i]) continue;
        const uint32_t groups = tileWorkGroupCounts_[frameIndex][i];
        frameCommandTimings_.Record(frameIndex, i, groups,
                                    query->GetElapsedNs(0, i * 4, i * 4 + 3));
        if (groups == tileNominalWorkGroupCounts_[frameIndex]) {
            fullTileMs += elapsedMs;
            ++fullTiles;
        }
        chunkCost_.Observe(groups, tileNominalWorkGroupCounts_[frameIndex], elapsedMs * 1.0e6);
    }
    if (fullTiles)
        chunkCost_.ObserveScale(tileNominalWorkGroupCounts_[frameIndex], fullTileMs * 1.0e6 / fullTiles);
    if (chunkProfile_ && chunkCost_.Calibrated() && tileExtentKeys_[frameIndex] == chunkCalibrationKey_) {
        chunkProfile_->SetChunkCalibration(tileExtentKeys_[frameIndex],
            {chunkCost_.AnchorUnits(), chunkCost_.UnitNs(), chunkCost_.FixedNs(),
             chunkCost_.MeanUnitNs(), chunkCost_.MeanFixedNs()});
    }

    double worstSubmissionMs = 0.0;
    for (const auto& batch : submissionBatches_[frameIndex]) {
        if (!batch.count) continue;
        const double elapsedMs = query->GetElapsedMs(0, batch.first * 4,
            (batch.first + batch.count) * 4 - 1);
        worstSubmissionMs = std::max(worstSubmissionMs, elapsedMs);
        if (gpuPerfLogger_)
            GPUPerformanceLogger::RecordSubmission(*gpuPerfLogger_, GetInstanceName(), elapsedMs,
                                                   reportedTileReads_ < 16);
        if (batch.count) {
            uint64_t groups = 0;
            for (std::size_t i = batch.first; i < batch.first + batch.count; ++i)
                groups += tileWorkGroupCounts_[frameIndex][i];
            if (groups) chunkCost_.ObserveBatch(groups, batch.count, elapsedMs * 1.0e6);
        }
        if (reportedTileReads_ < 16) ++reportedTileReads_;
    }
    (void)worstSubmissionMs;
}

// ============================================================================
// RECORD
// ============================================================================

bool ComputeStageNode::RecordComputeCommands(Context& ctx, VkCommandBuffer cmd,
                                             uint32_t imageIndex, uint32_t frameIndex, bool isConsumer,
                                             std::vector<VkCommandBuffer>& recordedBuffers) {
    recordedBuffers.clear();
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VIXEN_FRAME_COUNT(CommandRecordings, 1);
    if (const VkResult beginResult = vkBeginCommandBuffer(cmd, &beginInfo); beginResult != VK_SUCCESS) {
        GetOwningGraph()->ReportGpuFault("ComputeStageNode::RecordComputeCommands vkBeginCommandBuffer",
                                         beginResult);
        return false;
    }

    // R213: a per-frame record failure is reported as a GPU fault (what/where/why in the site string)
    // instead of thrown; the half-recorded buffer is closed so the next frame can begin it again.
    const auto abortRecording = [&](const std::string& site) {
        vkEndCommandBuffer(cmd);
        GetOwningGraph()->ReportGpuFault("ComputeStageNode::RecordComputeCommands " + site, VK_ERROR_UNKNOWN);
        return false;
    };
    const auto endRecording = [&](const char* site) {
        const VkResult endResult = vkEndCommandBuffer(cmd);
        if (endResult != VK_SUCCESS) {
            GetOwningGraph()->ReportGpuFault(std::string("ComputeStageNode::RecordComputeCommands ") + site,
                                             endResult);
            return false;
        }
        return true;
    };

    // Begin GPU timing frame (reset this slot's queries) — mirrors ComputeDispatchNode.
    // Baked-Perf M4 Task 4.3: this reset MUST run every frame regardless of
    // PARAM_DISPATCH_ENABLED below -- GPUQueryManager::AllAllocatedSlotsReset gates
    // vkGetQueryPoolResults on EVERY allocated slot having been reset at least once
    // per frame-in-flight (shared query pool, VUID-vkGetQueryPoolResults-None-09401);
    // an allocated slot that skips its reset here would permanently stall result
    // collection for every OTHER pass sharing the same GPUQueryManager, not just this
    // one (found live: an earlier version of this guard short-circuited the whole
    // ExecuteImpl before BeginFrame ever ran, zeroing whole_frame_gpu_span_ms/
    // spatial_reuse_ms/probe_update_ms too, not just this node's own column).
    if (gpuPerfLogger_) {
        gpuPerfLogger_->BeginFrame(cmd, frameIndex);
    }

    if (externalRecorder_) {
        const auto& capacity = GetOwningGraph()->GetCapacityTracker().GetConfig();
        tileBudgets_[frameIndex] = std::max(uint64_t{1},
            std::min(capacity.gpuTimeBudgetNs, capacity.gpuSubmissionBudgetNs));
        tileCounts_[frameIndex] = 1;
        tileWorkGroupCounts_[frameIndex] = {1};
        std::string failure;
        auto* outputTarget = ctx.In(ComputeStageNodeConfig::IMAGE_WRITE);
        if (!outputTarget) {
            failure = "external recorder has no output image";
        } else {
            const uint32_t outputIndex = outputTarget->GetCurrentIndex();
            const VkImageLayout previousLayout = DecideRenderTargetPriorLayoutAndUpdate(
                *outputTarget, outputIndex, VK_IMAGE_LAYOUT_GENERAL);
            SwapchainBarriers::TransitionImageToGeneralBarrier2(
                GetDevice(), cmd, outputTarget->GetImage(outputIndex), previousLayout);
            if (gpuPerfLogger_) {
                gpuPerfLogger_->RecordDispatchStart(cmd, frameIndex);
            }
            failure = externalRecorder_(ctx, cmd, imageIndex, frameIndex);
            if (gpuPerfLogger_) {
                gpuPerfLogger_->RecordDispatchEnd(cmd, frameIndex,
                    outputTarget->GetExtent().width, outputTarget->GetExtent().height);
            }
            if (postDispatchRecorderForTest_) postDispatchRecorderForTest_(cmd);
        }
        const VkResult endResult = vkEndCommandBuffer(cmd);
        if (endResult != VK_SUCCESS) {
            GetOwningGraph()->ReportGpuFault(
                "ComputeStageNode::RecordComputeCommands external recorder vkEndCommandBuffer", endResult);
        } else if (!failure.empty()) {
            GetOwningGraph()->ReportGpuFault(
                "ComputeStageNode::RecordComputeCommands external recorder: " + failure, VK_ERROR_UNKNOWN);
        }
        recordedBuffers.push_back(cmd);
        return true;  // unchanged: the external-recorder path still submits after reporting its fault.
    }

    // Baked-Perf M4 Task 4.3 (audit C8 + inventory #1/#2): generic no-op dispatch
    // guard (see PARAM_DISPATCH_ENABLED's own doc comment) -- skips the descriptor
    // bind, push constants, image-layout barriers, and the vkCmdDispatch itself
    // (the actual GPU work + the CPU-side setup for it), but the command buffer is
    // still begun/ended/submitted and the query timestamps below are still written
    // (as a genuine, near-zero-duration pair around nothing) so this pass's own GPU-
    // timing column reads a real ~0ms rather than a stale/never-collected value, and
    // the shared query pool's per-frame reset invariant above stays satisfied.
    if (!GetParameterValue<bool>(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED, true)) {
        const auto& capacity = GetOwningGraph()->GetCapacityTracker().GetConfig();
        tileBudgets_[frameIndex] = std::max(uint64_t{1},
            std::min(capacity.gpuTimeBudgetNs, capacity.gpuSubmissionBudgetNs));
        tileCounts_[frameIndex] = 1;
        tileWorkGroupCounts_[frameIndex] = {1};
        recordedBuffers.push_back(cmd);
        if (gpuPerfLogger_) {
            gpuPerfLogger_->RecordDispatchStart(cmd, frameIndex);
            gpuPerfLogger_->RecordDispatchEnd(cmd, frameIndex, 0, 0);
        }
        return endRecording("dispatch-disabled vkEndCommandBuffer");
    }

    VkPipeline pipeline = ctx.In(ComputeStageNodeConfig::COMPUTE_PIPELINE);
    VkPipelineLayout layout = ctx.In(ComputeStageNodeConfig::PIPELINE_LAYOUT);
    std::vector<VkDescriptorSet> descriptorSets = ctx.In(ComputeStageNodeConfig::DESCRIPTOR_SETS);
    if (pipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputeStageNode::RecordComputeCommands] stage '" + GetInstanceName() +
                                 "' has no compute pipeline/layout wired and no external command recorder; "
                                 "wire it through SynthesizeComputeStage or call SetExternalCommandRecorder");
    }

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
        return abortRecording("invalid descriptor sets for set index " + std::to_string(setIndex) +
                              " (have " + std::to_string(descriptorSets.size()) +
                              "; check the DESCRIPTOR_SETS input is wired and sized to the frame ring)");
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
        return abortRecording("dispatch dims unresolved (set dispatchX/Y/Z or connect SWAPCHAIN_INFO/IMAGE_WRITE)");
    }

    const auto& capacity = GetOwningGraph()->GetCapacityTracker().GetConfig();
    const uint64_t budgetNs = std::max(uint64_t{1},
        std::min(capacity.gpuTimeBudgetNs, capacity.gpuSubmissionBudgetNs));
    const std::string extentKey = std::to_string(dispatchX) + "x" + std::to_string(dispatchY);
    if (extentKey != chunkCalibrationKey_) {
        chunkCalibrationKey_ = extentKey;
        const GpuChunkCost previous = chunkCost_;
        chunkCost_ = {};
        if (chunkProfile_) {
            if (const auto restored = chunkProfile_->GetChunkCalibration(extentKey)) {
                chunkCost_.Import(restored->anchorUnits, restored->unitNs, restored->fixedNs,
                                  restored->meanUnitNs, restored->meanFixedNs);
            }
        }
        if (!chunkCost_.Calibrated() && previous.Calibrated()) {
            chunkCost_.Import(previous.AnchorUnits(), previous.UnitNs(), previous.FixedNs(),
                              previous.MeanUnitNs(), previous.MeanFixedNs());
        }
        if (!chunkCost_.Calibrated() && chunkProfile_) {
            if (const auto neighbour = chunkProfile_->NearestChunkCalibration(extentKey)) {
                chunkCost_.Import(neighbour->anchorUnits, neighbour->unitNs, neighbour->fixedNs,
                                  neighbour->meanUnitNs, neighbour->meanFixedNs);
            }
        }
    }
    tileExtentKeys_[frameIndex] = extentKey;

    // The diagnostic override is shared with ComputeDispatchNode; normal operation derives the
    // edge solely from this pass's measured envelope and the graph's submission capacity.
    static const long edgeOverride = [] {
        const char* value = std::getenv("VIXEN_GPUYIELD_EDGE");
        return value && *value ? std::strtol(value, nullptr, 10) : -1L;
    }();
    const uint32_t wholeEdge = std::max(1u, std::max(dispatchX, dispatchY));
    const GpuChunkCost::WholePass whole{uint64_t(dispatchX) * dispatchY * dispatchZ,
        NativeWholePassWindowNs(capacity.gpuSubmissionBudgetNs, capacity.gpuTimeBudgetNs)};
    const GpuChunkCost::WholePass* wholePass =
        GpuDeviceClassIsNative(GpuDeviceClassName(*GetDevice())) ? &whole : nullptr;
    const uint64_t edgeBudgetNs = std::max(uint64_t{1}, budgetNs / std::max(1u, dispatchZ));
    const uint32_t edge = !canTile_ || edgeOverride == 0
        ? wholeEdge
        : edgeOverride > 0
            ? static_cast<uint32_t>(edgeOverride)
            : chunkCost_.Edge(edgeBudgetNs, std::max(dispatchX, dispatchY), 7, wholePass);
    tileNominalWorkGroupCounts_[frameIndex] =
        std::min(edge, dispatchX) * std::min(edge, dispatchY) * dispatchZ;
    const uint32_t count = std::max(1u, ((dispatchX + edge - 1) / edge) * ((dispatchY + edge - 1) / edge));

    auto& extraBuffers = tileCommandBuffers_[frameIndex];
    if (extraBuffers.size() + 1 < count) {
        const uint32_t needed = count - 1 - static_cast<uint32_t>(extraBuffers.size());
        std::vector<VkCommandBuffer> allocated(needed);
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = needed;
        if (vkAllocateCommandBuffers(GetDevice()->device, &allocation, allocated.data()) != VK_SUCCESS) {
            GetOwningGraph()->ReportGpuFault(
                "ComputeStageNode::RecordComputeCommands tile command allocation", VK_ERROR_OUT_OF_HOST_MEMORY);
            return false;
        }
        extraBuffers.insert(extraBuffers.end(), allocated.begin(), allocated.end());
    }
    std::vector<VkCommandBuffer> tileBuffers{cmd};
    if (count > 1)
        tileBuffers.insert(tileBuffers.end(), extraBuffers.begin(), extraBuffers.begin() + (count - 1));
    if (tileQueryCapacities_[frameIndex] < count) {
        tileQueryCapacities_[frameIndex] = std::max(count, tileQueryCapacities_[frameIndex] * 2u);
        tileQueries_[frameIndex] = std::make_unique<Vixen::Vulkan::Resources::GPUTimestampQuery>(
            GetDevice(), 1, tileQueryCapacities_[frameIndex] * 4);
    }
    tileCounts_[frameIndex] = count;
    tileBudgets_[frameIndex] = chunkCost_.SubmissionBudgetNs(budgetNs);
    tileWorkGroupCounts_[frameIndex].clear();
    recordedBuffers = tileBuffers;
    auto& tileQuery = *tileQueries_[frameIndex];
    VkCommandBuffer currentCommand = cmd;
    tileQuery.ResetQueryRange(currentCommand, 0, 0, 4);
    tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

    // Consumer: acquire-side transition of the swapchain image into GENERAL for the
    // storage write (WSI lifecycle — node-managed in Tier-1).
    if (isConsumer && swapchainInfo) {
        const VkImageLayout prior = DecideRenderTargetPriorLayoutAndUpdate(*swapchainInfo, imageIndex, VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmd, swapchainInfo->GetImage(imageIndex), prior);
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
            *imageWriteTarget, imageWriteTarget->GetCurrentIndex(), VK_IMAGE_LAYOUT_GENERAL);
        SwapchainBarriers::TransitionImageToGeneralBarrier2(GetDevice(), cmd,
            imageWriteTarget->GetCurrentImage(), priorLayout);
    }

    // Sampled Lighting Inc4 M1: IMAGE_WRITE_ARRAY — identical per-target barrier logic to
    // the single-slot case above, looped. Each physical buffer owns its own history,
    // including when another recording node previously wrote the same target.
    for (Vixen::Vulkan::Resources::IRenderTarget* target : imageWriteArrayTargets) {
        if (!target) continue;
        VkImageLayout priorLayout = DecideRenderTargetPriorLayoutAndUpdate(
            *target, target->GetCurrentIndex(), VK_IMAGE_LAYOUT_GENERAL);
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

    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchStart(cmd, frameIndex);
    }
    uint32_t tileIndex = 0;
    for (uint32_t baseY = 0; baseY < dispatchY; baseY += edge) {
        for (uint32_t baseX = 0; baseX < dispatchX; baseX += edge) {
            const uint32_t tileX = std::min(edge, dispatchX - baseX);
            const uint32_t tileY = std::min(edge, dispatchY - baseY);
            tileWorkGroupCounts_[frameIndex].push_back(canTile_
                ? tileX * tileY * dispatchZ : dispatchX * dispatchY * dispatchZ);
            tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     tileIndex * 4 + 1);
            if (canTile_) {
                vkCmdDispatchBase(currentCommand, baseX, baseY, 0, tileX, tileY, dispatchZ);
            } else {
                vkCmdDispatch(currentCommand, dispatchX, dispatchY, dispatchZ);
            }
            tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     tileIndex * 4 + 2);
            if (++tileIndex != count) {
                tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                         tileIndex * 4 - 1);
                if (vkEndCommandBuffer(currentCommand) != VK_SUCCESS) {
                    GetOwningGraph()->ReportGpuFault(
                        "ComputeStageNode::RecordComputeCommands tile vkEndCommandBuffer", VK_ERROR_UNKNOWN);
                    return false;
                }
                currentCommand = tileBuffers[tileIndex];
                VIXEN_FRAME_COUNT(CommandRecordings, 1);
                if (vkBeginCommandBuffer(currentCommand, &beginInfo) != VK_SUCCESS) {
                    GetOwningGraph()->ReportGpuFault(
                        "ComputeStageNode::RecordComputeCommands tile vkBeginCommandBuffer", VK_ERROR_UNKNOWN);
                    return false;
                }
                tileQuery.ResetQueryRange(currentCommand, 0, tileIndex * 4, 4);
                tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         tileIndex * 4);
                BindComputePipeline(currentCommand, pipeline, layout, descriptorSets[setIndex]);
                SetPushConstants(ctx, currentCommand, layout);
            }
        }
    }

    tileQuery.WriteTimestamp(currentCommand, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, count * 4 - 1);
    if (gpuPerfLogger_) {
        gpuPerfLogger_->RecordDispatchEnd(currentCommand, frameIndex, dispatchX * 8, dispatchY * 8);
    }
    if (postDispatchRecorderForTest_) postDispatchRecorderForTest_(currentCommand);
    cmd = currentCommand;

    // Consumer is the last writer of the swapchain image → hand it to Present.
    if (isConsumer && swapchainInfo && swapchainInfo->UsesWsiSynchronization()) {
        SwapchainBarriers::TransitionImageToPresentBarrier2(GetDevice(), currentCommand,
            swapchainInfo->GetImage(imageIndex));
        swapchainInfo->SetImageLayout(imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    // IMAGE_WRITE deliberately does NOT transition on exit — it stays GENERAL (the
    // compute storage-write's own end state, already recorded in the target buffer by
    // DecideRenderTargetPriorLayoutAndUpdate's own update above). The next consumer
    // (another IMAGE_WRITE producer re-entering this same handle, or a
    // presentation-only blit node reading it via TRANSFER_SRC) owns whatever comes
    // next — this pass's job ends at GENERAL.

    return endRecording("vkEndCommandBuffer");
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

void ComputeStageNode::TypedCleanupImpl(TypedCleanupContext& /*ctx*/) {
    if (externalCleanup_) {
        externalCleanup_();
    }
    if (GetDevice() && GetDevice()->device != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < tileQueries_.size(); ++i) CollectTileTiming(i);
        for (auto& buffers : tileCommandBuffers_) {
            if (!buffers.empty() && commandPool_ != VK_NULL_HANDLE)
                vkFreeCommandBuffers(GetDevice()->device, commandPool_,
                                     static_cast<uint32_t>(buffers.size()), buffers.data());
            buffers.clear();
        }
        for (auto& query : tileQueries_) query.reset();
        tileCounts_.fill(0);
        tileQueryCapacities_.fill(0);
        tileBudgets_.fill(0);
        chunkCost_ = {};

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
    // The logger holds the old device's GPUQueryManager; a recovered device needs a fresh slot.
    if (gpuPerfLogger_) {
        if (auto* nodeLogger = GetLogger()) nodeLogger->RemoveChild(gpuPerfLogger_.get());
        gpuPerfLogger_.reset();
    }
    NODE_LOG_INFO("[ComputeStageNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ComputeStageNodeType);
