#include "Nodes/MultiDispatchNode.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"

#include <stdexcept>
#include <chrono>
#include <sstream>  // Sprint 6.1: Task #314 - For enhanced logging

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

    // Sprint 6.2: Use EnqueueUnchecked for backward compatibility (no budget checking)
    // Zero-cost estimate = always accepted regardless of budget
    size_t index = taskQueue_.GetQueuedCount();
    std::string debugName = pass.debugName;  // Capture before move

    TaskQueue<DispatchPass>::TaskSlot slot;
    slot.data = std::move(pass);
    slot.estimatedCostNs = 0;  // Zero cost = backward compatible
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

    // Check queue limit
    if (taskQueue_.GetQueuedCount() >= MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) {
        NODE_LOG_ERROR("[MultiDispatchNode::TryQueueDispatch] Queue full (" +
            std::to_string(MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) + " max)");
        return false;
    }

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
    groupedDispatches_.clear();  // Sprint 6.1: Clear group map
    stats_ = MultiDispatchStats{};
}

// ============================================================================
// SETUP
// ============================================================================

void MultiDispatchNode::SetupImpl(TypedSetupContext& ctx) {
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

void MultiDispatchNode::CompileImpl(TypedCompileContext& ctx) {
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
    SwapChainPublicVariables* swapchainInfo = ctx.In(MultiDispatchNodeConfig::SWAPCHAIN_INFO);

    if (commandPool_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] Command pool is null/invalid");
    }

    if (!swapchainInfo) {
        throw std::runtime_error("[MultiDispatchNode::CompileImpl] SwapChain info is null");
    }

    uint32_t imageCount = swapchainInfo->swapChainImageCount;
    NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Allocating " +
        std::to_string(imageCount) + " command buffers");

    // Allocate command buffers (one per swapchain image)
    commandBuffers_.resize(imageCount);

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

    // Sprint 6.1: Read GROUP_INPUTS and partition by group ID
    // GROUP_INPUTS is optional - if not connected, groupedDispatches_ stays empty
    // and we fall back to QueueDispatch() API
    const auto& groupInputs = ctx.In(MultiDispatchNodeConfig::GROUP_INPUTS);

    if (!groupInputs.empty()) {
        NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Partitioning " +
            std::to_string(groupInputs.size()) + " dispatch passes by group ID");

        groupedDispatches_.clear();

        // Validate all passes before partitioning
        for (const auto& pass : groupInputs) {
            if (!pass.IsValid()) {
                throw std::runtime_error("[MultiDispatchNode::CompileImpl] Invalid pass in GROUP_INPUTS: '" +
                    pass.debugName + "' (pipeline=" + std::to_string(reinterpret_cast<uint64_t>(pass.pipeline)) +
                    ", layout=" + std::to_string(reinterpret_cast<uint64_t>(pass.layout)) +
                    ", workGroups=" + std::to_string(pass.workGroupCount.x) + "x" +
                    std::to_string(pass.workGroupCount.y) + "x" + std::to_string(pass.workGroupCount.z) + ")");
            }
        }

        // Partition by group ID
        for (const auto& pass : groupInputs) {
            if (pass.groupId.has_value()) {
                uint32_t groupId = pass.groupId.value();
                groupedDispatches_[groupId].push_back(pass);

                NODE_LOG_DEBUG("[MultiDispatchNode] Group " + std::to_string(groupId) +
                    ": Added dispatch '" + pass.debugName + "'");
            } else {
                // No group ID - add to group 0 as default
                groupedDispatches_[0].push_back(pass);

                NODE_LOG_DEBUG("[MultiDispatchNode] Group 0 (default): Added dispatch '" +
                    pass.debugName + "'");
            }
        }

        NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Created " +
            std::to_string(groupedDispatches_.size()) + " dispatch groups");
    } else {
        NODE_LOG_DEBUG(std::string("[MultiDispatchNode::CompileImpl] No GROUP_INPUTS connected, ") +
            "using QueueDispatch() API");
    }
}

// ============================================================================
// EXECUTE
// ============================================================================

void MultiDispatchNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Get current indices
    uint32_t imageIndex = ctx.In(MultiDispatchNodeConfig::IMAGE_INDEX);
    uint32_t currentFrameIndex = ctx.In(MultiDispatchNodeConfig::CURRENT_FRAME_INDEX);

    // Guard against invalid image index
    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size()) {
        NODE_LOG_WARNING("[MultiDispatchNode] Invalid image index - skipping frame");
        return;
    }

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
    VkCommandBuffer cmdBuffer = commandBuffers_.GetValue(imageIndex);

    // Measure recording time
    auto startTime = std::chrono::high_resolution_clock::now();

    // Sprint 6.3: Phase 2.2 - Write start timestamp if query manager available
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        uint32_t framesInFlight = queryManager_->GetFrameCount();
        uint32_t queryFrameIndex = currentFrameIndex % framesInFlight;

        // Begin frame and write start timestamp
        queryManager_->BeginFrame(cmdBuffer, queryFrameIndex);
        queryManager_->WriteTimestamp(cmdBuffer, queryFrameIndex, querySlot_,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }

    // Record all queued dispatches
    RecordDispatches(cmdBuffer);

    // Sprint 6.3: Phase 2.2 - Write end timestamp
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        uint32_t framesInFlight = queryManager_->GetFrameCount();
        uint32_t queryFrameIndex = currentFrameIndex % framesInFlight;

        queryManager_->WriteTimestamp(cmdBuffer, queryFrameIndex, querySlot_,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        // Mark that timestamps were written for this frame
        auto& timingData = frameTimingData_.GetValue(imageIndex);
        timingData.timestampsWritten = true;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.recordTimeMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    commandBuffers_.MarkReady(imageIndex);

    // Output command buffer
    ctx.Out(MultiDispatchNodeConfig::COMMAND_BUFFER, cmdBuffer);
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

void MultiDispatchNode::RecordDispatches(VkCommandBuffer cmdBuffer) {
    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[MultiDispatchNode::RecordDispatches] "
            "Failed to begin command buffer");
    }

    // Sprint 6.1: Check if using group-based dispatch or legacy queue
    if (!groupedDispatches_.empty()) {
        // GROUP-BASED DISPATCH: Process each group independently
        NODE_LOG_DEBUG("[MultiDispatchNode] Recording " +
            std::to_string(groupedDispatches_.size()) + " dispatch groups");

        bool firstGroup = true;
        for (const auto& [groupId, passes] : groupedDispatches_) {
            // Sprint 6.1: Task #314 - Start timing this group
            auto groupStartTime = std::chrono::high_resolution_clock::now();

            // Insert barrier between groups (if auto-barriers enabled and not first group)
            if (autoBarriers_ && !firstGroup) {
                InsertAutoBarrier(cmdBuffer);
                ++stats_.barrierCount;
                NODE_LOG_DEBUG("[MultiDispatchNode] Inserted barrier between groups");
            }
            firstGroup = false;

            NODE_LOG_DEBUG("[MultiDispatchNode] Recording group " + std::to_string(groupId) +
                " with " + std::to_string(passes.size()) + " dispatches");

            // Sprint 6.1: Task #314 - Initialize group stats
            GroupDispatchStats groupStats{};

            // Record all passes in this group
            for (size_t i = 0; i < passes.size(); ++i) {
                const auto& pass = passes[i];

                // Insert barrier between passes within group (if enabled and not first pass)
                if (autoBarriers_ && i > 0) {
                    InsertAutoBarrier(cmdBuffer);
                    ++stats_.barrierCount;
                }

                // Bind pipeline
                vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);

                // Bind descriptor sets
                if (!pass.descriptorSets.empty()) {
                    vkCmdBindDescriptorSets(
                        cmdBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        pass.layout,
                        pass.firstSet,
                        static_cast<uint32_t>(pass.descriptorSets.size()),
                        pass.descriptorSets.data(),
                        0, nullptr
                    );
                }

                // Push constants
                if (pass.pushConstants.has_value()) {
                    const auto& pc = pass.pushConstants.value();
                    vkCmdPushConstants(
                        cmdBuffer,
                        pass.layout,
                        pc.stageFlags,
                        pc.offset,
                        static_cast<uint32_t>(pc.data.size()),
                        pc.data.data()
                    );
                }

                // Dispatch
                vkCmdDispatch(
                    cmdBuffer,
                    pass.workGroupCount.x,
                    pass.workGroupCount.y,
                    pass.workGroupCount.z
                );

                // Update overall statistics
                ++stats_.dispatchCount;
                stats_.totalWorkGroups += pass.TotalWorkGroups();

                // Sprint 6.1: Task #314 - Update group statistics
                ++groupStats.dispatchCount;
                groupStats.totalWorkGroups += pass.TotalWorkGroups();

                NODE_LOG_DEBUG("[MultiDispatchNode] Group " + std::to_string(groupId) +
                    ", pass " + std::to_string(i) + ": " + pass.debugName);
            }

            // Sprint 6.1: Task #314 - Measure group recording time
            auto groupEndTime = std::chrono::high_resolution_clock::now();
            groupStats.recordTimeMs = std::chrono::duration<double, std::milli>(
                groupEndTime - groupStartTime).count();

            // Sprint 6.1: Task #314 - Store group statistics
            stats_.groupStats[groupId] = groupStats;

            NODE_LOG_DEBUG("[MultiDispatchNode] Group " + std::to_string(groupId) +
                " complete: " + std::to_string(groupStats.dispatchCount) + " dispatches, " +
                std::to_string(groupStats.recordTimeMs) + "ms");
        }

        NODE_LOG_DEBUG("[MultiDispatchNode] Recorded " +
            std::to_string(stats_.dispatchCount) + " total dispatches across " +
            std::to_string(groupedDispatches_.size()) + " groups");
    } else {
        // LEGACY QUEUE-BASED DISPATCH: Process taskQueue_ (backward compatibility)
        // Sprint 6.2: Updated to use TaskQueue with priority execution
        NODE_LOG_DEBUG("[MultiDispatchNode] Recording " +
            std::to_string(taskQueue_.GetQueuedCount()) + " queued dispatches (legacy mode)");

        // Sort barriers by insertion index
        std::sort(barrierQueue_.begin(), barrierQueue_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        size_t barrierIdx = 0;

        // Sprint 6.2: Execute TaskQueue with priority-based ordering
        size_t dispatchIndex = 0;
        taskQueue_.ExecuteWithMetadata([&](const TaskQueue<DispatchPass>::TaskSlot& slot) {
            const auto& pass = slot.data;

            // Insert any barriers scheduled before this dispatch
            while (barrierIdx < barrierQueue_.size() &&
                   barrierQueue_[barrierIdx].first <= dispatchIndex) {
                RecordBarrier(cmdBuffer, barrierQueue_[barrierIdx].second);
                ++barrierIdx;
                ++stats_.barrierCount;
            }

            // Insert automatic barrier between passes (if enabled and not first pass)
            if (autoBarriers_ && dispatchIndex > 0) {
                InsertAutoBarrier(cmdBuffer);
                ++stats_.barrierCount;
            }

            // Bind pipeline
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);

            // Bind descriptor sets
            if (!pass.descriptorSets.empty()) {
                vkCmdBindDescriptorSets(
                    cmdBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pass.layout,
                    pass.firstSet,
                    static_cast<uint32_t>(pass.descriptorSets.size()),
                    pass.descriptorSets.data(),
                    0, nullptr
                );
            }

            // Push constants
            if (pass.pushConstants.has_value()) {
                const auto& pc = pass.pushConstants.value();
                vkCmdPushConstants(
                    cmdBuffer,
                    pass.layout,
                    pc.stageFlags,
                    pc.offset,
                    static_cast<uint32_t>(pc.data.size()),
                    pc.data.data()
                );
            }

            // Dispatch
            vkCmdDispatch(
                cmdBuffer,
                pass.workGroupCount.x,
                pass.workGroupCount.y,
                pass.workGroupCount.z
            );

            // Update statistics
            ++stats_.dispatchCount;
            stats_.totalWorkGroups += pass.TotalWorkGroups();

            NODE_LOG_DEBUG("[MultiDispatchNode] Recorded dispatch #" + std::to_string(dispatchIndex) +
                ": " + pass.debugName + " [priority=" + std::to_string(slot.priority) +
                ", cost=" + std::to_string(slot.estimatedCostNs) + "ns] [" +
                std::to_string(pass.workGroupCount.x) + "x" +
                std::to_string(pass.workGroupCount.y) + "x" +
                std::to_string(pass.workGroupCount.z) + "]");

            ++dispatchIndex;
        });  // End TaskQueue::ExecuteWithMetadata lambda

        // Insert any remaining barriers
        while (barrierIdx < barrierQueue_.size()) {
            RecordBarrier(cmdBuffer, barrierQueue_[barrierIdx].second);
            ++barrierIdx;
            ++stats_.barrierCount;
        }
    }  // End of legacy queue-based dispatch

    // End command buffer
    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[MultiDispatchNode::RecordDispatches] "
            "Failed to end command buffer");
    }
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

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
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

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
}

// ============================================================================
// CLEANUP
// ============================================================================

void MultiDispatchNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[MultiDispatchNode::CleanupImpl] Cleaning up resources");

    // Sprint 6.3: Phase 2.2 - Free GPU query slot
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        queryManager_->FreeQuerySlot(querySlot_);
        querySlot_ = GPUQueryManager::INVALID_SLOT;
        NODE_LOG_INFO("[MultiDispatchNode::CleanupImpl] Freed GPU query slot");
    }

    // Clear queues
    ClearQueue();

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
