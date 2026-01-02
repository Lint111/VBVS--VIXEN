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
    const Config& config)
    : config_(config)
    , device_(device)
    , queue_(queue)
    , budgetManager_(budgetManager)
{
    assert(device_ != VK_NULL_HANDLE && "BatchedUploader requires valid VkDevice");
    assert(queue_ != VK_NULL_HANDLE && "BatchedUploader requires valid VkQueue");
    assert(budgetManager_ != nullptr && "BatchedUploader requires DeviceBudgetManager");

    // Create staging buffer pool
    StagingBufferPool::Config poolConfig{
        .minBufferSize = 4 * 1024,           // 4 KB min
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
    if (!srcData || size == 0 || dstBuffer == VK_NULL_HANDLE) {
        return InvalidUploadHandle;
    }

    // Acquire staging buffer
    auto staging = stagingPool_->AcquireBuffer(size, "BatchUpload");
    if (!staging) {
        return InvalidUploadHandle;  // Staging quota exhausted
    }

    // Copy data to staging buffer
    if (staging->mappedData) {
        std::memcpy(staging->mappedData, srcData, size);
    } else {
        // If not persistently mapped, need to map
        auto* allocator = budgetManager_->GetAllocator();
        if (allocator) {
            // This shouldn't happen with persistentMapping=true
            stagingPool_->ReleaseBuffer(staging->handle);
            return InvalidUploadHandle;
        }
    }

    // Generate handle
    UploadHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);

    // Create pending upload record
    PendingUpload upload{
        .handle = handle,
        .stagingHandle = staging->handle,
        .dstBuffer = dstBuffer,
        .dstOffset = dstOffset,
        .size = size,
        .isCopy = false,
        .srcBuffer = staging->buffer,
        .srcOffset = 0
    };

    // Queue the upload
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

    // Check if we should auto-flush
    CheckAutoFlush();

    return handle;
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
            // Still none - wait for GPU
            vkQueueWaitIdle(queue_);
            ProcessCompletions();
            cmdBuffer = AcquireCommandBuffer();
        }
    }

    assert(cmdBuffer != VK_NULL_HANDLE);

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

        vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    } else {
        // Use fence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device_, &fenceInfo, nullptr, &batch.fence);

        vkQueueSubmit(queue_, 1, &submitInfo, batch.fence);
    }

    {
        std::lock_guard<std::mutex> lock(submittedMutex_);
        submittedBatches_.push(std::move(batch));
    }

    ++totalBatches_;
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

} // namespace ResourceManagement
