#include "Nodes/Common/GpuDeviceClass.h"
#include "Nodes/MultiDispatchNode.h"
#include "Core/NodeRegistration.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "Core/RenderGraph.h"  // Sprint 6.3: For TaskProfileRegistry access
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"

#include <stdexcept>
#include <chrono>
#include <sstream>  // Sprint 6.1: Task #314 - For enhanced logging
#include <mutex>    // Inc4 M3: SubmitMutex guard around vkQueueSubmit2
#include <algorithm>
#include <set>      // Inc4 M3: distinctSignalValues dedup

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> MultiDispatchNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<MultiDispatchNode>(
        instanceName,
        const_cast<MultiDispatchNodeType*>(this)
    );
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

MultiDispatchNode::MultiDispatchNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<MultiDispatchNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[MultiDispatchNode] Constructor called for " + instanceName);
}

// ============================================================================
// PUBLIC API
// ============================================================================

size_t MultiDispatchNode::QueueDispatch(DispatchPass&& pass) {
    // Validate pass
    if (!pass.IsValid()) {
        throw std::runtime_error("[MultiDispatchNode::QueueDispatch] Invalid dispatch pass: " +
            (pass.debugName.empty() ? "(unnamed)" : pass.debugName));
    }

    // Check queue limit
    if (taskQueue_.GetQueuedCount() >= MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) {
        throw std::runtime_error("[MultiDispatchNode::QueueDispatch] Queue full (" +
            std::to_string(MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) + " max)");
    }

    // Mandatory work stays accepted, but unknown GPU cost reserves a full frame.
    // The submit owner bounds outstanding work independently of admission.
    size_t index = taskQueue_.GetQueuedCount();
    std::string debugName = pass.debugName;  // Capture before move

    TaskQueue<DispatchPass>::TaskSlot slot;
    slot.data = std::move(pass);
    slot.estimatedCostNs = std::max(uint64_t{1}, taskQueue_.GetFrameBudget());
    slot.priority = 128;        // Default priority

    taskQueue_.EnqueueUnchecked(std::move(slot));

    NODE_LOG_DEBUG("[MultiDispatchNode] Queued dispatch #" + std::to_string(index) +
        ": " + debugName);

    return index;
}

bool MultiDispatchNode::TryQueueDispatch(DispatchPass&& pass, uint64_t estimatedCostNs, uint8_t priority) {
    // Validate pass
    if (!pass.IsValid()) {
        NODE_LOG_ERROR("[MultiDispatchNode::TryQueueDispatch] Invalid dispatch pass: " +
            (pass.debugName.empty() ? "(unnamed)" : pass.debugName));
        return false;
    }

    // Check queue limit. Unlike an invalid pass (a misconfiguration bug), a full queue can be
    // legitimate under sustained heavy load and this path is called per-dispatch, so an
    // always-on ERROR could flood the terminal every frame; throttle it (ponytail: local
    // counter matching the existing idiom in this file/TraceRaysNode, upgrade to a shared
    // once-per-N helper if more sites need it).
    if (taskQueue_.GetQueuedCount() >= MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) {
        static int queueFullLogCounter = 0;
        if (queueFullLogCounter++ < 5) {
            NODE_LOG_ERROR("[MultiDispatchNode::TryQueueDispatch] Queue full (" +
                std::to_string(MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) + " max)");
        }
        return false;
    }

    // An absent/uncalibrated estimate cannot claim free GPU work.
    if (estimatedCostNs == 0)
        estimatedCostNs = std::max(uint64_t{1}, taskQueue_.GetFrameBudget());

    // Sprint 6.2: Budget-aware enqueue
    std::string debugName = pass.debugName;  // Capture before move

    TaskQueue<DispatchPass>::TaskSlot slot;
    slot.data = std::move(pass);
    slot.estimatedCostNs = estimatedCostNs;
    slot.priority = priority;

    bool accepted = taskQueue_.TryEnqueue(std::move(slot));

    if (accepted) {
        NODE_LOG_DEBUG("[MultiDispatchNode] Budget-aware enqueue: " + debugName +
            " (cost=" + std::to_string(estimatedCostNs) + "ns, priority=" + std::to_string(priority) + ")");
    } else {
        NODE_LOG_WARNING("[MultiDispatchNode] Budget-aware enqueue rejected: " + debugName +
            " (cost=" + std::to_string(estimatedCostNs) + "ns, remaining=" +
            std::to_string(taskQueue_.GetRemainingBudget()) + "ns)");
    }

    return accepted;
}

bool MultiDispatchNode::TryQueueWithProfile(DispatchPass&& pass, const ITaskProfile* profile, uint8_t priority) {
    // Sprint 6.3: Get cost from profile directly (type-safe, no string lookup)
    uint64_t estimatedCostNs = 0;

    if (profile && profile->IsCalibrated()) {
        estimatedCostNs = profile->GetEstimatedCostNs();
        NODE_LOG_DEBUG("[MultiDispatchNode] Profile '" + profile->GetTaskId() + "': " +
            std::to_string(estimatedCostNs) + "ns");
    }

    return TryQueueDispatch(std::move(pass), estimatedCostNs, priority);
}

void MultiDispatchNode::QueueBarrier(DispatchBarrier&& barrier) {
    if (barrier.IsEmpty()) {
        NODE_LOG_WARNING("[MultiDispatchNode::QueueBarrier] Empty barrier ignored");
        return;
    }

    // Barrier applies before the next dispatch (current queue size)
    size_t insertIndex = taskQueue_.GetQueuedCount();
    barrierQueue_.emplace_back(insertIndex, std::move(barrier));

    NODE_LOG_DEBUG("[MultiDispatchNode] Queued barrier at index " + std::to_string(insertIndex));
}

void MultiDispatchNode::ClearQueue() {
    taskQueue_.Clear();          // Sprint 6.2: Clear TaskQueue
    barrierQueue_.clear();
    stats_ = MultiDispatchStats{};
}

// ============================================================================
// SETUP
// ============================================================================

void MultiDispatchNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[MultiDispatchNode::SetupImpl] Graph-scope initialization");

    // Read configuration parameters
    autoBarriers_ = GetParameterValue<bool>(MultiDispatchNodeConfig::AUTO_BARRIERS, true);
    enableTimestamps_ = GetParameterValue<bool>(MultiDispatchNodeConfig::ENABLE_TIMESTAMPS, false);

    // Sprint 6.2: Read budget configuration parameters
    // Note: Using uint32_t as parameter system doesn't support uint64_t
    // Max value ~4.29s is sufficient for frame budgets
    uint32_t frameBudgetNs = GetParameterValue<uint32_t>(
        MultiDispatchNodeConfig::FRAME_BUDGET_NS, 16'666'666u);
    std::string overflowModeStr = GetParameterValue<std::string>(
        MultiDispatchNodeConfig::BUDGET_OVERFLOW_MODE, "strict");

    BudgetOverflowMode mode = (overflowModeStr == "lenient")
        ? BudgetOverflowMode::Lenient
        : BudgetOverflowMode::Strict;

    taskQueue_.SetBudget(TaskBudget{static_cast<uint64_t>(frameBudgetNs), mode});

    NODE_LOG_INFO("[MultiDispatchNode] autoBarriers=" + std::to_string(autoBarriers_) +
        ", enableTimestamps=" + std::to_string(enableTimestamps_) +
        ", frameBudgetNs=" + std::to_string(frameBudgetNs) +
        ", budgetMode=" + overflowModeStr);
}

// ============================================================================
// COMPILE
// ============================================================================

void MultiDispatchNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Allocating per-image command buffers");

    // Access device input (compile-time dependency)
    VulkanDevice* devicePtr = ctx.In(MultiDispatchNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] Vulkan device input is null");
    }

    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Get inputs
    commandPool_ = ctx.In(MultiDispatchNodeConfig::COMMAND_POOL);
    Vixen::Vulkan::Resources::IRenderTarget* swapchainInfo = ctx.In(MultiDispatchNodeConfig::SWAPCHAIN_INFO);

    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] Command pool is null/invalid");
    }

    if (!swapchainInfo) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = std::max(swapchainInfo->GetImageCount(), uint32_t{4});
    NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Allocating " +
        std::to_string(imageCount) + " command buffers");

    // Allocate command buffers (one per swapchain image)
    commandBuffers_.resize(imageCount);
    chunkBuffers_.resize(imageCount);
    chunkQueries_.resize(imageCount);
    chunkCounts_.resize(imageCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    std::vector<VkCommandBuffer> cmdBuffers(imageCount);
    VkResult result = vkAllocateCommandBuffers(
        vulkanDevice_->device, &allocInfo, cmdBuffers.data());

    if (result != VK_SUCCESS) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] Failed to allocate "
            "command buffers: " + std::to_string(result));
    }

    // Store command buffers in stateful container
    for (uint32_t i = 0; i < imageCount; ++i) {
        commandBuffers_[i] = cmdBuffers[i];
        commandBuffers_.MarkDirty(i);
    }

    NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Allocated " +
        std::to_string(imageCount) + " command buffers successfully");

    // Sprint 6.3: Phase 2.2 - Allocate GPU query slot for timing
    if (queryManager_ && querySlot_ == GPUQueryManager::INVALID_SLOT) {
        querySlot_ = queryManager_->AllocateQuerySlot("MultiDispatchNode_" + GetInstanceName());
        if (querySlot_ != GPUQueryManager::INVALID_SLOT) {
            NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Allocated GPU query slot: " +
                std::to_string(querySlot_));
        } else {
            NODE_LOG_WARNING("[MultiDispatchNode::CompileImpl] Failed to allocate GPU query slot");
        }
    }

    // Initialize per-frame timing data
    frameTimingData_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        frameTimingData_[i] = FrameTimingData{};
        frameTimingData_.MarkDirty(i);
    }

}

// ============================================================================
// EXECUTE
// ============================================================================

void MultiDispatchNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // The accumulation slot declares Execute lifetime and gathers current producer
    // outputs. It cannot be captured at Compile, before those outputs exist.
    groupedDispatches_.clear();
    for (const auto& pass : ctx.In(MultiDispatchNodeConfig::GROUP_INPUTS)) {
        if (!pass.IsValid())
            throw std::runtime_error("MultiDispatchNode: invalid current GROUP_INPUTS pass " + pass.debugName);
        groupedDispatches_[pass.groupId.value_or(0u)].push_back(pass);
    }
    // Get current indices
    uint32_t imageIndex = ctx.In(MultiDispatchNodeConfig::IMAGE_INDEX);
    uint32_t currentFrameIndex = ctx.In(MultiDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Guard against invalid image index
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[MultiDispatchNode] Invalid image index - skipping frame");
        return;
    }

    // Flight ownership, rather than acquisition order, protects submitted commands.
    const uint32_t slot = ctx.In(MultiDispatchNodeConfig::IN_FLIGHT_FENCE) != VK_NULL_HANDLE
        ? currentFrameIndex % static_cast<uint32_t>(commandBuffers_.size()) : imageIndex;
    CollectChunkTiming(slot);

    // Sprint 6.3: Phase 2.2 - Read previous frame's timing results
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        auto& timingData = frameTimingData_.GetValue(imageIndex);

        // If timestamps were written in previous frame for this image, read results
        if (timingData.timestampsWritten && timingData.frameIndex != UINT32_MAX) {
            uint32_t framesInFlight = queryManager_->GetFrameCount();
            uint32_t queryFrameIndex = timingData.frameIndex % framesInFlight;

            if (queryManager_->TryReadTimestamps(queryFrameIndex, querySlot_)) {
                uint64_t elapsedNs = queryManager_->GetElapsedNs(queryFrameIndex, querySlot_);
                timingData.lastMeasuredGPUTimeNs = elapsedNs;

                // Record to TimelineCapacityTracker
                if (capacityTracker_) {
                    capacityTracker_->RecordGPUTime(elapsedNs);
                    NODE_LOG_DEBUG("[MultiDispatchNode] Recorded GPU time: " +
                        std::to_string(elapsedNs / 1'000'000) + "ms to TimelineCapacityTracker");
                }

                // Record to TaskQueue for all executed tasks
                // Average the measured time across all tasks (simplified feedback)
                size_t taskCount = stats_.dispatchCount > 0 ? stats_.dispatchCount : 1;
                uint64_t avgCostPerTask = elapsedNs / taskCount;

                for (size_t i = 0; i < taskCount; ++i) {
                    taskQueue_.RecordActualCost(i, avgCostPerTask);
                }

                NODE_LOG_DEBUG("[MultiDispatchNode] Recorded " + std::to_string(taskCount) +
                    " task costs (avg " + std::to_string(avgCostPerTask / 1'000'000) + "ms each)");
            }
        }

        // Mark as not written yet for this frame
        timingData.timestampsWritten = false;
        timingData.frameIndex = currentFrameIndex;
    }

    // Reset statistics
    stats_ = MultiDispatchStats{};

    // Get command buffer for this image
    VkCommandBuffer cmdBuffer = commandBuffers_.GetValue(slot);

    // Measure recording time
    auto startTime = std::chrono::high_resolution_clock::now();

    // Record all queued dispatches
    auto recorded = RecordDispatches(cmdBuffer, currentFrameIndex, slot);

    // Publish timing availability after successful command recording.
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        // Mark that timestamps were written for this frame
        auto& timingData = frameTimingData_.GetValue(imageIndex);
        timingData.timestampsWritten = true;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.recordTimeMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    commandBuffers_.MarkReady(slot);

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: submit this node's own recorded command
    // buffer, mirroring ComputeStageNode::ExecuteImpl's PRODUCER branch exactly (this node
    // is never a swapchain consumer — no isConsumer path, no fence reset/ownership, no
    // acquire wait, no PRESENT-facing binary signal). Only runs when IN_FLIGHT_FENCE is
    // actually connected — every pre-M3 consumer (test_group_dispatch,
    // test_multidispatch_integration, the Inc2/3 bucketing GTest harnesses) leaves it
    // unconnected (VK_NULL_HANDLE default) and stays byte-identical to pre-M3 behavior:
    // record-only, no submit, caller does its own vkQueueSubmit exactly as those tests
    // already do.
    VkFence inFlightFence = ctx.In(MultiDispatchNodeConfig::IN_FLIGHT_FENCE);
    if (inFlightFence != VK_NULL_HANDLE) {
        VkSemaphore timelineSem = ctx.In(MultiDispatchNodeConfig::TIMELINE_SEMAPHORE_IN);
        uint64_t frameBase = ctx.In(MultiDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

        std::vector<VkCommandBufferSubmitInfo> cmdInfos;
        for (auto command : recorded) {
            VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            info.commandBuffer = command;
            cmdInfos.push_back(info);
        }

        std::vector<VkSemaphoreSubmitInfo> waits, signals;

        // Producer: timeline SIGNALS only (this node never waits an acquire semaphore or
        // signals a PRESENT-facing binary — it has no swapchain image of its own to hand
        // off). A group signals its OWN completion value once; distinct (offset+frameBase)
        // values dedupe to one, same VUID-VkSubmitInfo2-semaphore-03882 avoidance as
        // ComputeStageNode's own producer branch.
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

        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
        si.pWaitSemaphoreInfos      = waits.data();
        si.commandBufferInfoCount   = static_cast<uint32_t>(cmdInfos.size());
        si.pCommandBufferInfos      = cmdInfos.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
        si.pSignalSemaphoreInfos    = signals.data();

        // Producer submits with NO fence (mirrors ComputeStageNode::ExecuteImpl's own producer
        // branch: "Producer submits with no fence; consumer owns the in-flight fence"). This
        // node is never the frame's consumer/last-submitter -- passing inFlightFence itself as
        // submitFence here was a real bug (VUID-vkQueueSubmit2-fence-04895, found live during
        // this milestone's own gate run): the REAL consumer node (whichever submits last, e.g.
        // ui_composite_render) also associates that same fence with its own submission, and two
        // submissions racing to attach one fence in the same frame is exactly what that VUID
        // forbids. IN_FLIGHT_FENCE stays as an INPUT (gates whether this node participates in
        // the real submit chain at all, matching every pre-M3 consumer's record-only default
        // when unconnected) -- it is deliberately never passed to vkQueueSubmit2 itself.
        // Lock-free phase 1 (design §2.5): publish to the queue-owner channel; the owner drains in
        // canonical order after the wave (a submit fault there surfaces the device-loss path). When
        // the channel is inactive (sequential/headless), fall back to the guarded direct submit.
        if (!GetOwningGraph()->PublishSubmit(this, SubmitRecord::FromSubmitInfo2(si, VK_NULL_HANDLE))) {
            std::lock_guard submitLock(vulkanDevice_->SubmitMutex(vulkanDevice_->queue));
            const VkResult result = SubmitRecord::FromSubmitInfo2(si, VK_NULL_HANDLE).Submit(
                vulkanDevice_->fpQueueSubmit2, vulkanDevice_->queue);
            if (result != VK_SUCCESS) {
                if (result == VK_ERROR_DEVICE_LOST) {
                    GetOwningGraph()->NotifyDeviceLost("MultiDispatchNode::ExecuteImpl vkQueueSubmit2");
                }
                throw std::runtime_error("[MultiDispatchNode::ExecuteImpl] Failed to submit command buffer (vkQueueSubmit2): " +
                                         std::to_string(result));
            }
        }
    }

    // Output command buffer
    ctx.Out(MultiDispatchNodeConfig::COMMAND_BUFFER, cmdBuffer);
    ctx.Out(MultiDispatchNodeConfig::COMMAND_BUFFERS, recorded);
    ctx.Out(MultiDispatchNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice_);

    // Log stats periodically (Sprint 6.1: Task #314 - Enhanced with per-group stats)
    static int logCounter = 0;
    if (logCounter++ % 60 == 0 && stats_.dispatchCount > 0) {
        std::ostringstream oss;
        oss << "[MultiDispatchNode] Frame " << currentFrameIndex
            << ": " << stats_.dispatchCount << " dispatches, "
            << stats_.barrierCount << " barriers, "
            << stats_.recordTimeMs << "ms record time";

        // Sprint 6.1: Task #314 - Log per-group breakdown if available
        if (!stats_.groupStats.empty()) {
            oss << " | " << stats_.groupStats.size() << " groups: ";
            bool first = true;
            for (const auto& [groupId, groupStat] : stats_.groupStats) {
                if (!first) oss << ", ";
                oss << "G" << groupId << "(" << groupStat.dispatchCount << "d/"
                    << groupStat.recordTimeMs << "ms)";
                first = false;
            }
        }

        NODE_LOG_INFO(oss.str());
    }

    // Clear queue for next frame
    ClearQueue();
}

// ============================================================================
// RECORD DISPATCHES
// ============================================================================

void MultiDispatchNode::CollectChunkTiming(uint32_t slot) {
    auto& query = chunkQueries_[slot];
    if (!query || !query->ReadResults(0)) return;
    double total = 0, peak = 0;
    for (uint32_t i = 0; i < chunkCounts_[slot]; ++i) {
        const double ms = query->GetElapsedMs(0, i * 4, i * 4 + 3);
        total += ms;
        peak = std::max(peak, ms);
        if (std::getenv("VIXEN_GPUYIELD_TRACE")) {
            NODE_LOG_INFO("[GPUYIELD_CHUNK] node=" + GetInstanceName() +
                " chunk=" + std::to_string(i) +
                " start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4)) +
                " end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 3)) +
                " dispatch_start_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 1)) +
                " dispatch_end_ticks=" + std::to_string(query->GetRawTimestampTicks(0, i * 4 + 2)) +
                " dispatch_gpu_ms=" + std::to_string(query->GetElapsedMs(0, i * 4 + 1, i * 4 + 2)) +
                " elapsed_gpu_ms=" + std::to_string(ms));
        }
    }
    NODE_LOG_INFO("[GPUYIELD] node=" + GetInstanceName() +
        " submissions=" + std::to_string(chunkCounts_[slot]) +
        " total_gpu_ms=" + std::to_string(total) +
        " max_submission_ms=" + std::to_string(peak) + " budget_ms=2.000000" +
        " device_class=" + GpuDeviceClassName(vulkanDevice_->gpuProperties.deviceType));
}

std::vector<VkCommandBuffer> MultiDispatchNode::RecordDispatches(
    VkCommandBuffer first, uint32_t currentFrameIndex, uint32_t slot) {
    std::vector<const DispatchPass*> passes;
    if (!groupedDispatches_.empty()) {
        for (const auto& [group, groupPasses] : groupedDispatches_) {
            (void)group;
            for (const auto& pass : groupPasses) passes.push_back(&pass);
        }
    } else {
        taskQueue_.ExecuteWithMetadata([&](const TaskQueue<DispatchPass>::TaskSlot& task) {
            passes.push_back(&task.data);
        });
    }
    const auto partition = [](const DispatchPass& pass) {
        if (pass.tiles) return *pass.tiles;
        DispatchPass::TilePartition tile;
        if (!pass.indirectBuffer && pass.groupId) {
            tile.grid = (pass.workGroupCount + tile.groupEdge - 1u) / tile.groupEdge;
        }
        return tile;
    };
    uint32_t count = 0;
    for (const auto* pass : passes) {
        const auto tile = partition(*pass);
        if (!tile.groupEdge || !tile.grid.x || !tile.grid.y || !tile.grid.z)
            throw std::runtime_error("MultiDispatchNode: invalid tile partition");
        count += tile.grid.x * tile.grid.y * tile.grid.z;
    }
    count = std::max(1u, count);
    auto& extras = chunkBuffers_[slot];
    if (extras.size() + 1 < count) {
        std::vector<VkCommandBuffer> allocated(count - 1 - extras.size());
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<uint32_t>(allocated.size());
        if (vkAllocateCommandBuffers(vulkanDevice_->device, &allocation, allocated.data()) != VK_SUCCESS)
            throw std::runtime_error("MultiDispatchNode: tile command allocation failed");
        extras.insert(extras.end(), allocated.begin(), allocated.end());
    }
    std::vector<VkCommandBuffer> commands{first};
    commands.insert(commands.end(), extras.begin(), extras.begin() + count - 1);
    // Mock command-recording tests have no physical device. Production and actual
    // device fixtures use the normal device-owned query API, including record-only paths.
    if (vulkanDevice_->gpu && *vulkanDevice_->gpu != VK_NULL_HANDLE && chunkCounts_[slot] != count)
        chunkQueries_[slot] = std::make_unique<Vixen::Vulkan::Resources::GPUTimestampQuery>(vulkanDevice_, 1, count * 4);
    chunkCounts_[slot] = count;
    auto* query = chunkQueries_[slot].get();
    std::sort(barrierQueue_.begin(), barrierQueue_.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    std::size_t barrierIndex = 0, chunk = 0;
    std::vector<VkDescriptorSet> inheritedSets;
    struct PushState { VkPipelineLayout layout; PushConstantData data; };
    std::vector<PushState> inheritedPush;
    const auto begin = [&](VkCommandBuffer cmd) {
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &info) != VK_SUCCESS)
            throw std::runtime_error("MultiDispatchNode: begin tile command failed");
        if (query) {
            if (chunk == 0) query->ResetQueries(cmd, 0);
            query->WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, static_cast<uint32_t>(chunk * 4));
        }
        if (chunk == 0 && queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
            const uint32_t frame = currentFrameIndex % queryManager_->GetFrameCount();
            queryManager_->BeginFrame(cmd, frame, querySlot_);
            queryManager_->WriteTimestamp(cmd, frame, querySlot_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }
    };
    const auto finish = [&](VkCommandBuffer cmd) {
        if (chunk + 1 == count && queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT)
            queryManager_->WriteTimestamp(cmd, currentFrameIndex % queryManager_->GetFrameCount(),
                querySlot_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        if (query) query->WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            static_cast<uint32_t>(chunk * 4 + 3));
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            throw std::runtime_error("MultiDispatchNode: end tile command failed");
        ++chunk;
    };
    for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
        const auto& pass = *passes[passIndex];
        if (!pass.descriptorSets.empty()) {
            inheritedSets.resize(std::max(inheritedSets.size(), pass.firstSet + pass.descriptorSets.size()), VK_NULL_HANDLE);
            std::copy(pass.descriptorSets.begin(), pass.descriptorSets.end(), inheritedSets.begin() + pass.firstSet);
        }
        if (pass.pushConstants) {
            const auto& pc = *pass.pushConstants;
            inheritedPush.erase(std::remove_if(inheritedPush.begin(), inheritedPush.end(),
                [&](const PushState& old) {
                    return old.layout == pass.layout && old.data.offset == pc.offset &&
                        old.data.stageFlags == pc.stageFlags && old.data.data.size() == pc.data.size();
                }), inheritedPush.end());
            inheritedPush.push_back({pass.layout, pc});
        }
        const auto tile = partition(pass);
        const uint32_t tiles = tile.grid.x * tile.grid.y * tile.grid.z;
        for (uint32_t t = 0; t < tiles; ++t) {
            VkCommandBuffer cmd = commands[chunk];
            begin(cmd);
            if (t == 0) {
                while (barrierIndex < barrierQueue_.size() && barrierQueue_[barrierIndex].first <= passIndex) {
                    RecordBarrier(cmd, barrierQueue_[barrierIndex++].second);
                    ++stats_.barrierCount;
                }
                if (autoBarriers_ && passIndex > 0) { InsertAutoBarrier(cmd); ++stats_.barrierCount; }
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);
            for (uint32_t at = 0; at < inheritedSets.size();) {
                if (inheritedSets[at] == VK_NULL_HANDLE) { ++at; continue; }
                uint32_t end = at + 1;
                while (end < inheritedSets.size() && inheritedSets[end] != VK_NULL_HANDLE) ++end;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pass.layout, at,
                    end - at, inheritedSets.data() + at, 0, nullptr);
                at = end;
            }
            for (const auto& pc : inheritedPush)
                vkCmdPushConstants(cmd, pc.layout, pc.data.stageFlags, pc.data.offset,
                    static_cast<uint32_t>(pc.data.data.size()), pc.data.data.data());
            const glm::uvec3 origin{(t % tile.grid.x) * tile.groupEdge,
                ((t / tile.grid.x) % tile.grid.y) * tile.groupEdge,
                (t / (tile.grid.x * tile.grid.y)) * tile.groupEdge};
            if (query) query->WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                static_cast<uint32_t>(chunk * 4 + 1));
            if (pass.indirectBuffer) {
                if (pass.tiles) {
                    const glm::uvec2 pixelOrigin = glm::uvec2(origin) * tile.localSize;
                    vkCmdPushConstants(cmd, pass.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                        tile.originPushOffset, sizeof(pixelOrigin), &pixelOrigin);
                }
                vkCmdDispatchIndirect(cmd, *pass.indirectBuffer,
                    pass.indirectBufferOffset + VkDeviceSize(t) * sizeof(VkDispatchIndirectCommand));
            } else if (pass.tiles || pass.groupId) {
                const glm::uvec3 groups = glm::min(glm::uvec3(tile.groupEdge), pass.workGroupCount - origin);
                vkCmdDispatchBase(cmd, origin.x, origin.y, origin.z, groups.x, groups.y, groups.z);
            } else {
                vkCmdDispatch(cmd, pass.workGroupCount.x, pass.workGroupCount.y, pass.workGroupCount.z);
            }
            if (query) query->WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                static_cast<uint32_t>(chunk * 4 + 2));
            if (passIndex + 1 == passes.size() && t + 1 == tiles) {
                while (barrierIndex < barrierQueue_.size()) {
                    RecordBarrier(cmd, barrierQueue_[barrierIndex++].second); ++stats_.barrierCount;
                }
            }
            finish(cmd);
        }
        ++stats_.dispatchCount;
        stats_.totalWorkGroups += pass.TotalWorkGroups();
        if (pass.groupId) {
            auto& group = stats_.groupStats[*pass.groupId];
            ++group.dispatchCount;
            group.totalWorkGroups += pass.TotalWorkGroups();
        }
    }
    if (passes.empty()) {
        begin(first);
        while (barrierIndex < barrierQueue_.size()) { RecordBarrier(first, barrierQueue_[barrierIndex++].second); ++stats_.barrierCount; }
        if (query) {
            query->WriteTimestamp(first, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1);
            query->WriteTimestamp(first, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 2);
        }
        finish(first);
    }
    return commands;
}

// ============================================================================
// BARRIER HELPERS
// ============================================================================

void MultiDispatchNode::InsertAutoBarrier(VkCommandBuffer cmdBuffer) {
    // Simple compute-to-compute barrier for UAV hazards
    VkMemoryBarrier2 memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT |
                                   VK_ACCESS_2_SHADER_WRITE_BIT;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;

    vulkanDevice_->fpCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
}

void MultiDispatchNode::RecordBarrier(
    VkCommandBuffer cmdBuffer,
    const DispatchBarrier& barrier
) {
    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

    if (!barrier.memoryBarriers.empty()) {
        dependencyInfo.memoryBarrierCount =
            static_cast<uint32_t>(barrier.memoryBarriers.size());
        dependencyInfo.pMemoryBarriers = barrier.memoryBarriers.data();
    }

    if (!barrier.bufferBarriers.empty()) {
        dependencyInfo.bufferMemoryBarrierCount =
            static_cast<uint32_t>(barrier.bufferBarriers.size());
        dependencyInfo.pBufferMemoryBarriers = barrier.bufferBarriers.data();
    }

    if (!barrier.imageBarriers.empty()) {
        dependencyInfo.imageMemoryBarrierCount =
            static_cast<uint32_t>(barrier.imageBarriers.size());
        dependencyInfo.pImageMemoryBarriers = barrier.imageBarriers.data();
    }

    vulkanDevice_->fpCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
}

// ============================================================================
// CLEANUP
// ============================================================================

void MultiDispatchNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[MultiDispatchNode::CleanupImpl] Cleaning up resources");

    // Sprint 6.3: Phase 2.2 - Free GPU query slot
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        queryManager_->FreeQuerySlot(querySlot_);
        querySlot_ = GPUQueryManager::INVALID_SLOT;
        NODE_LOG_INFO("[MultiDispatchNode::CleanupImpl] Freed GPU query slot");
    }

    // Clear queues
    ClearQueue();

    groupedDispatches_.clear();
    chunkQueries_.clear();
    for (auto& buffers : chunkBuffers_) {
        if (!buffers.empty()) vkFreeCommandBuffers(vulkanDevice_->device, commandPool_,
            static_cast<uint32_t>(buffers.size()), buffers.data());
    }
    chunkBuffers_.clear();
    chunkCounts_.clear();
    // Free command buffers
    if (vulkanDevice_ && vulkanDevice_->device != VK_NULL_HANDLE) {
        if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
            std::vector<VkCommandBuffer> rawHandles;
            rawHandles.reserve(commandBuffers_.size());
            for (size_t i = 0; i < commandBuffers_.size(); ++i) {
                rawHandles.push_back(commandBuffers_.GetValue(i));
            }

            vkFreeCommandBuffers(
                vulkanDevice_->device,
                commandPool_,
                static_cast<uint32_t>(rawHandles.size()),
                rawHandles.data()
            );
            commandBuffers_.clear();
        }

        commandPool_ = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("[MultiDispatchNode::CleanupImpl] Cleanup complete");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::MultiDispatchNodeType);
