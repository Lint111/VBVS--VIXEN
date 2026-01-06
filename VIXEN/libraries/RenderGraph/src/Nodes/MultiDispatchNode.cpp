#include "Nodes/MultiDispatchNode.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "Core/NodeLogging.h"

#include <stdexcept>
#include <chrono>

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
    if (dispatchQueue_.size() >= MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) {
        throw std::runtime_error("[MultiDispatchNode::QueueDispatch] Queue full (" +
            std::to_string(MultiDispatchNodeConfig::MAX_DISPATCHES_PER_FRAME) + " max)");
    }

    size_t index = dispatchQueue_.size();
    dispatchQueue_.push_back(std::move(pass));

    NODE_LOG_DEBUG("[MultiDispatchNode] Queued dispatch #" + std::to_string(index) +
        ": " + dispatchQueue_.back().debugName);

    return index;
}

void MultiDispatchNode::QueueBarrier(DispatchBarrier&& barrier) {
    if (barrier.IsEmpty()) {
        NODE_LOG_WARNING("[MultiDispatchNode::QueueBarrier] Empty barrier ignored");
        return;
    }

    // Barrier applies before the next dispatch (current queue size)
    size_t insertIndex = dispatchQueue_.size();
    barrierQueue_.emplace_back(insertIndex, std::move(barrier));

    NODE_LOG_DEBUG("[MultiDispatchNode] Queued barrier at index " + std::to_string(insertIndex));
}

void MultiDispatchNode::ClearQueue() {
    dispatchQueue_.clear();
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

    NODE_LOG_INFO("[MultiDispatchNode] autoBarriers=" + std::to_string(autoBarriers_) +
        ", enableTimestamps=" + std::to_string(enableTimestamps_));
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

    // Sprint 6.1: Read GROUP_INPUTS and partition by group ID
    // GROUP_INPUTS is optional - if not connected, groupedDispatches_ stays empty
    // and we fall back to QueueDispatch() API
    const auto& groupInputs = ctx.In(MultiDispatchNodeConfig::GROUP_INPUTS);

    if (!groupInputs.empty()) {
        NODE_LOG_INFO("[MultiDispatchNode::CompileImpl] Partitioning " +
            std::to_string(groupInputs.size()) + " dispatch passes by group ID");

        groupedDispatches_.clear();

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

    // Reset statistics
    stats_ = MultiDispatchStats{};

    // Get command buffer for this image
    VkCommandBuffer cmdBuffer = commandBuffers_.GetValue(imageIndex);

    // Measure recording time
    auto startTime = std::chrono::high_resolution_clock::now();

    // Record all queued dispatches
    RecordDispatches(cmdBuffer);

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.recordTimeMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    commandBuffers_.MarkReady(imageIndex);

    // Output command buffer
    ctx.Out(MultiDispatchNodeConfig::COMMAND_BUFFER, cmdBuffer);
    ctx.Out(MultiDispatchNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice_);

    // Log stats periodically
    static int logCounter = 0;
    if (logCounter++ % 60 == 0 && stats_.dispatchCount > 0) {
        NODE_LOG_INFO("[MultiDispatchNode] Frame " + std::to_string(currentFrameIndex) +
            ": " + std::to_string(stats_.dispatchCount) + " dispatches, " +
            std::to_string(stats_.barrierCount) + " barriers, " +
            std::to_string(stats_.recordTimeMs) + "ms record time");
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
            // Insert barrier between groups (if auto-barriers enabled and not first group)
            if (autoBarriers_ && !firstGroup) {
                InsertAutoBarrier(cmdBuffer);
                ++stats_.barrierCount;
                NODE_LOG_DEBUG("[MultiDispatchNode] Inserted barrier between groups");
            }
            firstGroup = false;

            NODE_LOG_DEBUG("[MultiDispatchNode] Recording group " + std::to_string(groupId) +
                " with " + std::to_string(passes.size()) + " dispatches");

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

                // Update statistics
                ++stats_.dispatchCount;
                stats_.totalWorkGroups += pass.TotalWorkGroups();

                NODE_LOG_DEBUG("[MultiDispatchNode] Group " + std::to_string(groupId) +
                    ", pass " + std::to_string(i) + ": " + pass.debugName);
            }
        }

        NODE_LOG_DEBUG("[MultiDispatchNode] Recorded " +
            std::to_string(stats_.dispatchCount) + " total dispatches across " +
            std::to_string(groupedDispatches_.size()) + " groups");
    } else {
        // LEGACY QUEUE-BASED DISPATCH: Process dispatchQueue_ (backward compatibility)
        NODE_LOG_DEBUG("[MultiDispatchNode] Recording " +
            std::to_string(dispatchQueue_.size()) + " queued dispatches (legacy mode)");

        // Sort barriers by insertion index
        std::sort(barrierQueue_.begin(), barrierQueue_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        size_t barrierIdx = 0;

        // Record each dispatch
        for (size_t i = 0; i < dispatchQueue_.size(); ++i) {
            const auto& pass = dispatchQueue_[i];

        // Insert any barriers scheduled before this dispatch
        while (barrierIdx < barrierQueue_.size() &&
               barrierQueue_[barrierIdx].first <= i) {
            RecordBarrier(cmdBuffer, barrierQueue_[barrierIdx].second);
            ++barrierIdx;
            ++stats_.barrierCount;
        }

        // Insert automatic barrier between passes (if enabled and not first pass)
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

        // Update statistics
        ++stats_.dispatchCount;
        stats_.totalWorkGroups += pass.TotalWorkGroups();

        NODE_LOG_DEBUG("[MultiDispatchNode] Recorded dispatch #" + std::to_string(i) +
            ": " + pass.debugName + " [" +
            std::to_string(pass.workGroupCount.x) + "x" +
            std::to_string(pass.workGroupCount.y) + "x" +
            std::to_string(pass.workGroupCount.z) + "]");
    }

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
