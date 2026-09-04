#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace ResourceManagement {

// ============================================================================
// Construction / Destruction
// ============================================================================

BatchedUploader::BatchedUploader(
    VkDevice device,
    VkQueue queue,
    uint32_t queueFamilyIndex,
    DeviceBudgetManager* budgetManager,
    const Config& config,
    std::mutex* submitMutex)
    : config_(config)
    , device_(device)
    , queue_(queue)
    , submitMutex_(submitMutex)
    , budgetManager_(budgetManager)
{
    assert(device_ != VK_NULL_HANDLE && "BatchedUploader requires valid VkDevice");
    assert(queue_ != VK_NULL_HANDLE && "BatchedUploader requires valid VkQueue");
    assert(budgetManager_ != nullptr && "BatchedUploader requires DeviceBudgetManager");

    // Create staging buffer pool. minBufferSize must reach maxBufferSize within
    // StagingBufferPool::NumBuckets (12) doublings, or requests above the last
    // bucket's cap silently bypass pooling (see StagingBufferPool's ctor
    // assert) -- 64 KB << 11 = 128 MB comfortably covers the 64 MB max below.
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 64 * 1024,          // 64 KB min
        .maxBufferSize = 64 * 1024 * 1024,   // 64 MB max
        .maxPooledBuffersPerBucket = 8,
        .maxTotalPooledBytes = 256 * 1024 * 1024,  // 256 MB pool
        .persistentMapping = true
    };
    stagingPool_ = std::make_unique<StagingBufferPool>(budgetManager_, poolConfig);

    // Create command pool
    CreateCommandPool(queueFamilyIndex);

    // Try to create timeline semaphore if requested
    if (config_.useTimelineSemaphores) {
        CreateTimelineSemaphore();
    }

    oldestPendingTime_ = std::chrono::steady_clock::now();
}

BatchedUploader::~BatchedUploader() {
    // Wait for all pending work
    WaitIdle();

    // Destroy timeline semaphore
    if (timelineSemaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, timelineSemaphore_, nullptr);
    }

    // Destroy command pool (implicitly frees command buffers)
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

    // Staging pool cleaned up by unique_ptr
}

// ============================================================================
// Upload API
// ============================================================================

UploadHandle BatchedUploader::Upload(
    const void* srcData,
    VkDeviceSize size,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset)
{
    return UploadOrdered({UploadRequest{srcData, size, dstBuffer, dstOffset}});
}

UploadHandle BatchedUploader::UploadOrdered(const std::vector<UploadRequest>& requests) {
    if (requests.empty()) {
        return InvalidUploadHandle;
    }

    std::vector<PendingUpload> uploads;
    uploads.reserve(requests.size());
    uint64_t totalBytes = 0;

    // Stage every request before publishing any of them to pendingUploads_. This gives callers
    // an all-or-nothing brick-plus-config admission when the staging quota is exhausted.
    for (const UploadRequest& request : requests) {
        if (!request.srcData || request.size == 0 || request.dstBuffer == VK_NULL_HANDLE) {
            for (const PendingUpload& upload : uploads) {
                stagingPool_->ReleaseBuffer(upload.stagingHandle);
            }
            return InvalidUploadHandle;
        }

        auto staging = stagingPool_->AcquireBuffer(request.size, "BatchUpload");
        if (!staging || !staging->mappedData) {
            if (staging && staging->handle != InvalidStagingHandle) {
                stagingPool_->ReleaseBuffer(staging->handle);
            }
            for (const PendingUpload& upload : uploads) {
                stagingPool_->ReleaseBuffer(upload.stagingHandle);
            }
            return InvalidUploadHandle;
        }
        std::memcpy(staging->mappedData, request.srcData, request.size);

        const UploadHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);
        uploads.push_back(PendingUpload{
            .handle = handle,
            .stagingHandle = staging->handle,
            .dstBuffer = request.dstBuffer,
            .dstOffset = request.dstOffset,
            .size = request.size,
            .isCopy = false,
            .srcBuffer = staging->buffer,
            .srcOffset = 0
        });
        totalBytes += request.size;
    }

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingUploads_.empty()) {
            oldestPendingTime_ = std::chrono::steady_clock::now();
        }
        pendingUploads_.insert(pendingUploads_.end(), uploads.begin(), uploads.end());
    }

    pendingBytes_.fetch_add(totalBytes, std::memory_order_relaxed);
    for (const PendingUpload& upload : uploads) {
        SetStatus(upload.handle, UploadStatus::Pending);
    }
    totalUploads_.fetch_add(uploads.size(), std::memory_order_relaxed);

    // Check only after the complete ordered group is visible to Flush().
    CheckAutoFlush();
    return uploads.back().handle;
}

UploadHandle BatchedUploader::CopyBuffer(
    VkBuffer srcBuffer,
    VkDeviceSize srcOffset,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize size)
{
    if (srcBuffer == VK_NULL_HANDLE || dstBuffer == VK_NULL_HANDLE || size == 0) {
        return InvalidUploadHandle;
    }

    UploadHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);

    PendingUpload upload{
        .handle = handle,
        .stagingHandle = InvalidStagingHandle,  // No staging for buffer copy
        .dstBuffer = dstBuffer,
        .dstOffset = dstOffset,
        .size = size,
        .isCopy = true,
        .srcBuffer = srcBuffer,
        .srcOffset = srcOffset
    };

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingUploads_.empty()) {
            oldestPendingTime_ = std::chrono::steady_clock::now();
        }
        pendingUploads_.push_back(upload);
    }

    pendingBytes_.fetch_add(size, std::memory_order_relaxed);
    SetStatus(handle, UploadStatus::Pending);
    ++totalUploads_;

    CheckAutoFlush();

    return handle;
}

UploadStatus BatchedUploader::GetStatus(UploadHandle handle) const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    auto it = uploadStatus_.find(handle);
    if (it == uploadStatus_.end()) {
        return UploadStatus::Failed;  // Unknown handle
    }
    return it->second;
}

bool BatchedUploader::IsComplete(UploadHandle handle) const {
    UploadStatus status = GetStatus(handle);
    return status == UploadStatus::Completed || status == UploadStatus::Failed;
}

bool BatchedUploader::WaitForUpload(UploadHandle handle, std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!IsComplete(handle)) {
        ProcessCompletions();

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

// ============================================================================
// Batch Control
// ============================================================================

void BatchedUploader::Flush() {
    std::vector<PendingUpload> toSubmit;

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingUploads_.empty()) {
            return;
        }
        toSubmit = std::move(pendingUploads_);
        pendingUploads_.clear();
    }

    pendingBytes_.store(0, std::memory_order_relaxed);

    if (!toSubmit.empty()) {
        SubmitBatch(std::move(toSubmit));
    }
}

uint32_t BatchedUploader::ProcessCompletions() {
    uint32_t completed = 0;

    std::lock_guard<std::mutex> lock(submittedMutex_);

    while (!submittedBatches_.empty()) {
        auto& batch = submittedBatches_.front();
        bool batchComplete = false;

        if (useTimelineSemaphores_ && timelineSemaphore_ != VK_NULL_HANDLE) {
            // Check timeline semaphore value
            uint64_t currentValue = 0;
            vkGetSemaphoreCounterValue(device_, timelineSemaphore_, &currentValue);
            batchComplete = (currentValue >= batch.timelineValue);
        } else if (batch.fence != VK_NULL_HANDLE) {
            // Check fence status
            VkResult result = vkGetFenceStatus(device_, batch.fence);
            batchComplete = (result == VK_SUCCESS);
        } else {
            // No sync primitive - assume complete (shouldn't happen)
            batchComplete = true;
        }

        if (!batchComplete) {
            break;  // FIFO - if this batch isn't done, neither are later ones
        }

        // Batch complete - release resources
        for (const auto& upload : batch.uploads) {
            if (upload.stagingHandle != InvalidStagingHandle) {
                stagingPool_->ReleaseBuffer(upload.stagingHandle);
            }
            SetStatus(upload.handle, UploadStatus::Completed);
            // GetStatus() already treats an unknown handle as Failed, so pruning a terminal
            // (Completed/Failed) entry right away is observationally the same as leaving it —
            // callers only ever check IsComplete()/GetStatus() after this point, both of which
            // read "gone" as "done". Keeps the map from growing without bound (audit V-N15).
            PruneStatus(upload.handle);
            ++completed;
            totalBytesUploaded_.fetch_add(upload.size, std::memory_order_relaxed);
        }

        // Return command buffer to pool
        ReleaseCommandBuffer(batch.cmdBuffer);

        // Destroy fence if used
        if (batch.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, batch.fence, nullptr);
        }

        submittedBatches_.pop();
    }

    return completed;
}

void BatchedUploader::WaitIdle() {
    Flush();

    // Wait for all submitted batches
    while (true) {
        {
            std::lock_guard<std::mutex> lock(submittedMutex_);
            if (submittedBatches_.empty()) {
                break;
            }
        }
        ProcessCompletions();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

// ============================================================================
// Statistics
// ============================================================================

BatchedUploaderStats BatchedUploader::GetStats() const {
    BatchedUploaderStats stats;
    stats.totalUploads = totalUploads_.load(std::memory_order_relaxed);
    stats.totalBatches = totalBatches_.load(std::memory_order_relaxed);
    stats.totalBytesUploaded = totalBytesUploaded_.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        stats.currentPendingUploads = pendingUploads_.size();
    }
    stats.currentPendingBytes = pendingBytes_.load(std::memory_order_relaxed);

    if (stats.totalBatches > 0) {
        stats.avgUploadsPerBatch = static_cast<float>(stats.totalUploads) /
                                   static_cast<float>(stats.totalBatches);
    }

    return stats;
}

uint32_t BatchedUploader::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    return static_cast<uint32_t>(pendingUploads_.size());
}

uint64_t BatchedUploader::GetPendingBytes() const {
    return pendingBytes_.load(std::memory_order_relaxed);
}

// ============================================================================
// Internal Helpers
// ============================================================================

void BatchedUploader::CreateCommandPool(uint32_t queueFamilyIndex) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                     VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    assert(result == VK_SUCCESS && "Failed to create command pool");

    // Pre-allocate command buffers
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = config_.maxBatchCommandBuffers;

    commandBuffers_.resize(config_.maxBatchCommandBuffers);
    result = vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data());
    assert(result == VK_SUCCESS && "Failed to allocate command buffers");

    // All start as available
    for (auto cmdBuffer : commandBuffers_) {
        availableCommandBuffers_.push(cmdBuffer);
    }
}

void BatchedUploader::CreateTimelineSemaphore() {
    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &typeInfo;

    VkResult result = vkCreateSemaphore(device_, &semInfo, nullptr, &timelineSemaphore_);
    if (result == VK_SUCCESS) {
        useTimelineSemaphores_ = true;
    } else {
        // Fallback to fences
        useTimelineSemaphores_ = false;
        timelineSemaphore_ = VK_NULL_HANDLE;
    }
}

VkCommandBuffer BatchedUploader::AcquireCommandBuffer() {
    std::lock_guard<std::mutex> lock(cmdBufferMutex_);

    if (availableCommandBuffers_.empty()) {
        // Need to wait for a batch to complete
        return VK_NULL_HANDLE;
    }

    VkCommandBuffer cmdBuffer = availableCommandBuffers_.front();
    availableCommandBuffers_.pop();

    // Reset the command buffer
    vkResetCommandBuffer(cmdBuffer, 0);

    return cmdBuffer;
}

void BatchedUploader::ReleaseCommandBuffer(VkCommandBuffer cmdBuffer) {
    if (cmdBuffer == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard<std::mutex> lock(cmdBufferMutex_);
    availableCommandBuffers_.push(cmdBuffer);
}

void BatchedUploader::SubmitBatch(std::vector<PendingUpload>&& uploads) {
    if (uploads.empty()) {
        return;
    }

    VkCommandBuffer cmdBuffer = AcquireCommandBuffer();
    if (cmdBuffer == VK_NULL_HANDLE) {
        // No command buffers available - process completions and retry
        ProcessCompletions();
        cmdBuffer = AcquireCommandBuffer();
        if (cmdBuffer == VK_NULL_HANDLE) {
            // Still none - wait for GPU. Externally synchronized per Vulkan spec (audit V-M11).
            {
                std::unique_lock<std::mutex> lock;
                if (submitMutex_) lock = std::unique_lock<std::mutex>(*submitMutex_);
                vkQueueWaitIdle(queue_);
            }
            ProcessCompletions();
            cmdBuffer = AcquireCommandBuffer();
        }
    }

    if (cmdBuffer == VK_NULL_HANDLE) {
        // Pool exhausted even after waiting for the GPU to catch up: give up on this batch rather
        // than call vkBeginCommandBuffer(VK_NULL_HANDLE, ...) (UB, and only an assert away from
        // running in release builds) (audit V-M17).
        FailBatch(uploads);
        return;
    }

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Record all copy commands
    for (const auto& upload : uploads) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = upload.srcOffset;
        copyRegion.dstOffset = upload.dstOffset;
        copyRegion.size = upload.size;

        vkCmdCopyBuffer(cmdBuffer, upload.srcBuffer, upload.dstBuffer, 1, &copyRegion);
        SetStatus(upload.handle, UploadStatus::Submitted);
    }

    vkEndCommandBuffer(cmdBuffer);

    // Submit with appropriate sync
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    SubmittedBatch batch;
    batch.cmdBuffer = cmdBuffer;
    batch.uploads = std::move(uploads);
    batch.submitTime = std::chrono::steady_clock::now();

    // Externally synchronized per Vulkan spec (audit V-M11). submitMutex_ is optional: when the
    // uploader owns the only path to this queue (no cross-node contention), it's null and submitLock
    // owns no mutex — every submitLock.unlock() below must be owns_lock()-guarded, or unlocking a
    // lock that holds no mutex throws std::system_error(operation_not_permitted).
    std::unique_lock<std::mutex> submitLock;
    if (submitMutex_) submitLock = std::unique_lock<std::mutex>(*submitMutex_);

    if (useTimelineSemaphores_ && timelineSemaphore_ != VK_NULL_HANDLE) {
        // Use timeline semaphore
        batch.timelineValue = nextTimelineValue_.fetch_add(1, std::memory_order_relaxed);

        VkTimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.signalSemaphoreValueCount = 1;
        timelineInfo.pSignalSemaphoreValues = &batch.timelineValue;

        submitInfo.pNext = &timelineInfo;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &timelineSemaphore_;

        VkResult submitResult = vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitLock.owns_lock()) submitLock.unlock();
        if (submitResult != VK_SUCCESS) {
            // Submit failed - the timeline value we reserved will never be signalled. ReleaseCommandBuffer
            // returns cmdBuffer to the pool immediately since nothing GPU-side will ever touch it now.
            ReleaseCommandBuffer(cmdBuffer);
            FailBatch(batch.uploads);
            return;
        }
    } else {
        // Use fence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkResult fenceResult = vkCreateFence(device_, &fenceInfo, nullptr, &batch.fence);
        VkResult submitResult = VK_SUCCESS;
        if (fenceResult == VK_SUCCESS) {
            submitResult = vkQueueSubmit(queue_, 1, &submitInfo, batch.fence);
        }
        if (submitLock.owns_lock()) submitLock.unlock();

        SubmitOutcome outcome = DecideSubmitOutcome(fenceResult, submitResult);
        if (outcome != SubmitOutcome::Ok) {
            // Neither branch leaves a fence that will ever signal: FenceCreateFailed never made one;
            // SubmitFailed made one but the GPU will never touch it. Either way ProcessCompletions'
            // FIFO wait-for-fence would block forever if we enqueued this batch (audit V-M16).
            if (outcome == SubmitOutcome::SubmitFailed && batch.fence != VK_NULL_HANDLE) {
                vkDestroyFence(device_, batch.fence, nullptr);
            }
            ReleaseCommandBuffer(cmdBuffer);
            FailBatch(batch.uploads);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(submittedMutex_);
        submittedBatches_.push(std::move(batch));
    }

    ++totalBatches_;
}

void BatchedUploader::FailBatch(const std::vector<PendingUpload>& uploads) {
    for (const auto& upload : uploads) {
        if (upload.stagingHandle != InvalidStagingHandle) {
            stagingPool_->ReleaseBuffer(upload.stagingHandle);
        }
        SetStatus(upload.handle, UploadStatus::Failed);
        PruneStatus(upload.handle);
    }
}

void BatchedUploader::CheckAutoFlush() {
    // Check upload count threshold
    size_t pendingCount = 0;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingCount = pendingUploads_.size();
    }

    if (pendingCount >= config_.maxPendingUploads) {
        Flush();
        return;
    }

    // Check bytes threshold
    if (pendingBytes_.load(std::memory_order_relaxed) >= config_.maxPendingBytes) {
        Flush();
        return;
    }

    // Check deadline
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldestPendingTime_);
    if (elapsed >= config_.flushDeadline && pendingCount > 0) {
        Flush();
    }
}

void BatchedUploader::SetStatus(UploadHandle handle, UploadStatus status) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    uploadStatus_[handle] = status;
}

void BatchedUploader::PruneStatus(UploadHandle handle) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    uploadStatus_.erase(handle);
}

// ============================================================================
// Pre-Allocation (Sprint 5 Phase 4.1)
// ============================================================================

void BatchedUploader::PreWarm(const VkDeviceSize* sizes, size_t count) {
    if (!stagingPool_ || !sizes || count == 0) {
        return;
    }
    stagingPool_->PreWarm(sizes, count);
}

void BatchedUploader::PreWarmDefaults() {
    // Default sizes for typical VIXEN upload patterns:
    // - Small (64 KB): Constant buffers, uniform updates, small textures
    // - Medium (1 MB): Texture mipmaps, mesh vertex/index data
    // - Large (16 MB): Large textures, acceleration structure instance buffers
    constexpr VkDeviceSize defaultSizes[] = {
        64 * 1024,          // 64 KB x 4
        64 * 1024,
        64 * 1024,
        64 * 1024,
        1024 * 1024,        // 1 MB x 2
        1024 * 1024,
        16 * 1024 * 1024,   // 16 MB x 2
        16 * 1024 * 1024
    };
    PreWarm(defaultSizes, sizeof(defaultSizes) / sizeof(defaultSizes[0]));
}

} // namespace ResourceManagement
