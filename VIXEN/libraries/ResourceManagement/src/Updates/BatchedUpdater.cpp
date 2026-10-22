#include "Updates/BatchedUpdater.h"

#include <algorithm>

namespace ResourceManagement {

// ============================================================================
// LIFECYCLE
// ============================================================================

BatchedUpdater::BatchedUpdater(uint32_t frameCount, const Config& config)
    : config_(config)
{
    Resize(frameCount);
}

// ============================================================================
// FRAME QUEUE (lock-free MPSC: CAS push, take-all by exchange)
// ============================================================================

void BatchedUpdater::FrameQueue::Push(UpdateRequestPtr request) {
    auto* node = new Node{std::move(request), nullptr};
    // Count first: a concurrent TakeAll() may then over-count briefly but never underflows.
    pending.fetch_add(1, std::memory_order_relaxed);
    node->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(node->next, node,
               std::memory_order_release, std::memory_order_relaxed)) {
        // node->next was refreshed by the failed exchange; retry the push
    }
}

std::vector<UpdateRequestPtr> BatchedUpdater::FrameQueue::TakeAll() {
    Node* list = head.exchange(nullptr, std::memory_order_acq_rel);
    std::vector<UpdateRequestPtr> taken;
    size_t count = 0;
    for (Node* node = list; node; node = node->next) ++count;
    taken.resize(count);
    // The list is newest-first; fill from the back to restore submission order.
    size_t slot = count;
    while (list) {
        Node* node = list;
        list = node->next;
        taken[--slot] = std::move(node->request);
        delete node;
    }
    pending.fetch_sub(static_cast<uint32_t>(count), std::memory_order_relaxed);
    return taken;
}

void BatchedUpdater::FrameQueue::Clear() {
    (void)TakeAll();
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

    frameQueues_[imageIndex].Push(std::move(request));
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

    frameQueues_[imageIndex].Push(std::move(request));
    ++totalQueued_;
}

uint32_t BatchedUpdater::GetPendingCount(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return 0;
    }

    return frameQueues_[imageIndex].pending.load(std::memory_order_relaxed);
}

uint32_t BatchedUpdater::GetTotalPendingCount() const {
    uint32_t total = 0;
    for (uint32_t index = 0; index < frameCount_; ++index) {
        total += frameQueues_[index].pending.load(std::memory_order_relaxed);
    }
    return total;
}

bool BatchedUpdater::HasPending(uint32_t imageIndex) const {
    if (!ValidateImageIndex(imageIndex)) {
        return false;
    }

    return frameQueues_[imageIndex].pending.load(std::memory_order_relaxed) != 0;
}

// ============================================================================
// RECORDING API
// ============================================================================

uint32_t BatchedUpdater::RecordAll(VkCommandBuffer cmd, uint32_t imageIndex,
                                    PFN_vkCmdPipelineBarrier2KHR cmdPipelineBarrier2) {
    if (!cmd || !ValidateImageIndex(imageIndex)) {
        return 0;
    }

    // Take the frame's queue in one exchange, then process it without any shared state
    std::vector<UpdateRequestPtr> updates = frameQueues_[imageIndex].TakeAll();

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
            if (config_.insertBarriers && update->RequiresBarriers() && cmdPipelineBarrier2) {
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

                cmdPipelineBarrier2(cmd, &depInfo);
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

    frameQueues_[imageIndex].Clear();
}

void BatchedUpdater::ClearAll() {
    for (uint32_t index = 0; index < frameCount_; ++index) {
        frameQueues_[index].Clear();
    }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void BatchedUpdater::Resize(uint32_t frameCount) {
    // Owner phase: no Queue()/RecordAll() runs concurrently with a resize (device rebuild).
    frameQueues_ = frameCount ? std::make_unique<FrameQueue[]>(frameCount) : nullptr;
    frameCount_ = frameCount;
}

// ============================================================================
// STATISTICS
// ============================================================================

BatchedUpdaterStats BatchedUpdater::GetStats() const {
    BatchedUpdaterStats stats;
    stats.totalUpdatesQueued = totalQueued_.load();
    stats.totalUpdatesRecorded = totalRecorded_.load();
    stats.currentPendingUpdates = GetTotalPendingCount();
    stats.frameCount = frameCount_;
    return stats;
}

// ============================================================================
// INTERNAL
// ============================================================================

bool BatchedUpdater::ValidateImageIndex(uint32_t imageIndex) const {
    return imageIndex < frameCount_;
}

} // namespace ResourceManagement
