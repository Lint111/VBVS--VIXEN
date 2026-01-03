#include "Updates/BatchedUpdater.h"

#include <algorithm>

namespace ResourceManagement {

// ============================================================================
// LIFECYCLE
// ============================================================================

BatchedUpdater::BatchedUpdater(uint32_t frameCount, const Config& config)
    : config_(config)
{
    frameQueues_.resize(frameCount);
}

// ============================================================================
// QUEUE API
// ============================================================================

void BatchedUpdater::Queue(UpdateRequestPtr request) {
    if (!request) {
        return;
    }

    const uint32_t imageIndex = request->imageIndex;
    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    frameQueues_[imageIndex].push_back(std::move(request));
    ++totalQueued_;
}

void BatchedUpdater::Queue(UpdateRequestPtr request, uint32_t imageIndex) {
    if (!request) {
        return;
    }

    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    request->imageIndex = imageIndex;

    std::lock_guard<std::mutex> lock(mutex_);
    frameQueues_[imageIndex].push_back(std::move(request));
    ++totalQueued_;
}

uint32_t BatchedUpdater::GetPendingCount(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint32_t>(frameQueues_[imageIndex].size());
}

uint32_t BatchedUpdater::GetTotalPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t total = 0;
    for (const auto& queue : frameQueues_) {
        total += static_cast<uint32_t>(queue.size());
    }
    return total;
}

bool BatchedUpdater::HasPending(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return !frameQueues_[imageIndex].empty();
}

// ============================================================================
// RECORDING API
// ============================================================================

uint32_t BatchedUpdater::RecordAll(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!cmd || !ValidateImageIndex(imageIndex)) {
        return 0;
    }

    // Move queue out under lock, then process without lock
    std::vector<UpdateRequestPtr> updates;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        updates = std::move(frameQueues_[imageIndex]);
        frameQueues_[imageIndex].clear();
    }

    if (updates.empty()) {
        return 0;
    }

    // Sort by priority if enabled (lower priority value = recorded first)
    if (config_.sortByPriority) {
        std::stable_sort(updates.begin(), updates.end(),
            [](const UpdateRequestPtr& a, const UpdateRequestPtr& b) {
                return a->priority < b->priority;
            });
    }

    // Record each update
    uint32_t recordedCount = 0;
    for (auto& update : updates) {
        if (update) {
            // Insert pre-barriers if needed
            if (config_.insertBarriers && update->RequiresBarriers()) {
                // Memory barrier before operation
                VkMemoryBarrier2 barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                                        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.memoryBarrierCount = 1;
                depInfo.pMemoryBarriers = &barrier;

                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            update->Record(cmd);
            ++recordedCount;
        }
    }

    totalRecorded_ += recordedCount;
    return recordedCount;
}

void BatchedUpdater::Clear(uint32_t imageIndex) {
    if (!ValidateImageIndex(imageIndex)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    frameQueues_[imageIndex].clear();
}

void BatchedUpdater::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& queue : frameQueues_) {
        queue.clear();
    }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void BatchedUpdater::Resize(uint32_t frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    frameQueues_.clear();
    frameQueues_.resize(frameCount);
}

// ============================================================================
// STATISTICS
// ============================================================================

BatchedUpdaterStats BatchedUpdater::GetStats() const {
    BatchedUpdaterStats stats;
    stats.totalUpdatesQueued = totalQueued_.load();
    stats.totalUpdatesRecorded = totalRecorded_.load();
    stats.currentPendingUpdates = GetTotalPendingCount();
    stats.frameCount = static_cast<uint32_t>(frameQueues_.size());
    return stats;
}

// ============================================================================
// INTERNAL
// ============================================================================

bool BatchedUpdater::ValidateImageIndex(uint32_t imageIndex) const {
    return imageIndex < frameQueues_.size();
}

} // namespace ResourceManagement
