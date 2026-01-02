// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifdef _WIN32
#define NOMINMAX
#endif

#include "Memory/StagingBufferPool.h"
#include "Memory/DeviceBudgetManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace ResourceManagement {

// ============================================================================
// Construction / Destruction
// ============================================================================

StagingBufferPool::StagingBufferPool(
    DeviceBudgetManager* budgetManager,
    const Config& config)
    : config_(config)
    , budgetManager_(budgetManager)
{
    assert(budgetManager_ != nullptr && "StagingBufferPool requires DeviceBudgetManager");

    // Initialize size-class buckets
    // Bucket 0: 64KB, Bucket 1: 128KB, ..., Bucket 11: 64MB
    VkDeviceSize size = config_.minBufferSize;
    for (size_t i = 0; i < NumBuckets && size <= config_.maxBufferSize; ++i) {
        buckets_[i].minSize = size;
        buckets_[i].maxSize = size * 2 - 1;
        size *= 2;
    }
}

StagingBufferPool::~StagingBufferPool() {
    // Clear all pooled buffers
    Clear();

    // Destroy any remaining active buffers (shouldn't happen in proper usage)
    std::lock_guard<std::mutex> lock(recordsMutex_);
    for (auto& [handle, record] : records_) {
        if (record.allocation.buffer != VK_NULL_HANDLE) {
            // Unmap if mapped
            if (record.mappedData && budgetManager_->GetAllocator()) {
                budgetManager_->GetAllocator()->UnmapBuffer(record.allocation);
            }
            budgetManager_->FreeBuffer(record.allocation);
            budgetManager_->ReleaseStagingQuota(record.size);
        }
    }
    records_.clear();
}

// ============================================================================
// Acquisition API
// ============================================================================

std::optional<StagingBufferAcquisition>
StagingBufferPool::AcquireBuffer(VkDeviceSize requestedSize, std::string_view debugName) {
    // Clamp to valid range (parentheses prevent Windows min/max macro conflicts)
    VkDeviceSize effectiveSize = (std::max)(requestedSize, config_.minBufferSize);
    effectiveSize = (std::min)(effectiveSize, config_.maxBufferSize);

    // Get bucket for this size
    size_t bucketIndex = GetBucketIndex(effectiveSize);
    VkDeviceSize bucketSize = GetBucketSize(bucketIndex);

    // Try to reserve staging quota (this may block with backpressure)
    if (!budgetManager_->TryReserveStagingQuota(bucketSize)) {
        return std::nullopt;
    }

    // Try to get from pool first
    auto result = AcquireFromBucket(bucketIndex, requestedSize);
    if (result) {
        ++poolHits_;
        return result;
    }

    // No pooled buffer available - allocate new one
    result = AllocateNewBuffer(bucketSize, debugName);
    if (!result) {
        // Allocation failed - release quota we reserved
        budgetManager_->ReleaseStagingQuota(bucketSize);
        return std::nullopt;
    }

    ++poolMisses_;
    return result;
}

std::optional<StagingBufferAcquisition>
StagingBufferPool::TryAcquireBuffer(VkDeviceSize requestedSize, std::string_view debugName) {
    // Same as AcquireBuffer but uses TryReserve semantics
    // (already non-blocking in our implementation)
    return AcquireBuffer(requestedSize, debugName);
}

void StagingBufferPool::ReleaseBuffer(StagingBufferHandle handle) {
    if (handle == InvalidStagingHandle) {
        return;
    }

    BufferRecord record;
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        auto it = records_.find(handle);
        if (it == records_.end() || !it->second.inUse) {
            return;  // Invalid or already released
        }
        record = it->second;
        it->second.inUse = false;
    }

    // Update active bytes
    activeBytes_.fetch_sub(record.size, std::memory_order_relaxed);
    ++totalReleases_;

    // Check if pool is full
    uint64_t currentPooled = totalPooledBytes_.load(std::memory_order_acquire);
    if (currentPooled + record.size > config_.maxTotalPooledBytes) {
        // Pool full - destroy this buffer
        DestroyBuffer(handle);
        return;
    }

    // Return to appropriate bucket
    size_t bucketIndex = GetBucketIndex(record.size);
    ReturnToBucket(handle, bucketIndex);
}

void StagingBufferPool::ReleaseAndDestroy(StagingBufferHandle handle) {
    if (handle == InvalidStagingHandle) {
        return;
    }

    BufferRecord record;
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        auto it = records_.find(handle);
        if (it == records_.end() || !it->second.inUse) {
            return;
        }
        record = it->second;
        it->second.inUse = false;
    }

    activeBytes_.fetch_sub(record.size, std::memory_order_relaxed);
    ++totalReleases_;

    DestroyBuffer(handle);
}

// ============================================================================
// Pool Management
// ============================================================================

uint64_t StagingBufferPool::Trim(uint64_t targetBytes) {
    uint64_t freedBytes = 0;
    uint64_t currentPooled = totalPooledBytes_.load(std::memory_order_acquire);

    if (currentPooled <= targetBytes) {
        return 0;
    }

    // Iterate buckets from largest to smallest (more efficient trimming)
    for (int i = static_cast<int>(NumBuckets) - 1; i >= 0 && currentPooled > targetBytes; --i) {
        auto& bucket = buckets_[i];
        std::lock_guard<std::mutex> lock(bucket.mutex);

        while (!bucket.available.empty() && currentPooled > targetBytes) {
            StagingBufferHandle handle = bucket.available.back();
            bucket.available.pop_back();

            // Get buffer size before destroying
            VkDeviceSize bufferSize = 0;
            {
                std::lock_guard<std::mutex> recordLock(recordsMutex_);
                auto it = records_.find(handle);
                if (it != records_.end()) {
                    bufferSize = it->second.size;
                }
            }

            DestroyBuffer(handle);
            freedBytes += bufferSize;
            currentPooled = totalPooledBytes_.load(std::memory_order_acquire);
        }
    }

    return freedBytes;
}

void StagingBufferPool::Clear() {
    // Collect all pooled buffer handles
    std::vector<StagingBufferHandle> toDestroy;

    for (auto& bucket : buckets_) {
        std::lock_guard<std::mutex> lock(bucket.mutex);
        for (auto handle : bucket.available) {
            toDestroy.push_back(handle);
        }
        bucket.available.clear();
    }

    // Destroy all collected buffers
    for (auto handle : toDestroy) {
        DestroyBuffer(handle);
    }
}

void StagingBufferPool::PreWarm(const VkDeviceSize* sizes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto acquisition = AcquireBuffer(sizes[i], "PreWarm");
        if (acquisition) {
            ReleaseBuffer(acquisition->handle);
        }
    }
}

// ============================================================================
// Statistics
// ============================================================================

StagingPoolStats StagingBufferPool::GetStats() const {
    StagingPoolStats stats;

    // Count pooled buffers
    for (const auto& bucket : buckets_) {
        std::lock_guard<std::mutex> lock(bucket.mutex);
        stats.totalPooledBuffers += bucket.available.size();
    }

    stats.totalPooledBytes = totalPooledBytes_.load(std::memory_order_relaxed);
    stats.activeBytes = activeBytes_.load(std::memory_order_relaxed);
    stats.totalAcquisitions = totalAcquisitions_.load(std::memory_order_relaxed);
    stats.totalReleases = totalReleases_.load(std::memory_order_relaxed);
    stats.poolHits = poolHits_.load(std::memory_order_relaxed);
    stats.poolMisses = poolMisses_.load(std::memory_order_relaxed);

    // Calculate active buffers from records
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        for (const auto& [handle, record] : records_) {
            if (record.inUse) {
                ++stats.activeBuffers;
            }
        }
    }

    // Calculate hit rate
    if (stats.totalAcquisitions > 0) {
        stats.hitRate = static_cast<float>(stats.poolHits) /
                        static_cast<float>(stats.totalAcquisitions);
    }

    return stats;
}

// ============================================================================
// Internal Helpers
// ============================================================================

size_t StagingBufferPool::GetBucketIndex(VkDeviceSize size) const {
    // Find bucket: log2(size / minBufferSize)
    if (size <= config_.minBufferSize) {
        return 0;
    }

    VkDeviceSize normalized = size / config_.minBufferSize;
    size_t index = static_cast<size_t>(std::log2(static_cast<double>(normalized)));

    // Handle rounding up for non-power-of-2 sizes
    if (size > GetBucketSize(index)) {
        ++index;
    }

    return (std::min)(index, NumBuckets - 1);
}

VkDeviceSize StagingBufferPool::GetBucketSize(size_t bucketIndex) const {
    return config_.minBufferSize << bucketIndex;
}

std::optional<StagingBufferAcquisition>
StagingBufferPool::AcquireFromBucket(size_t bucketIndex, VkDeviceSize requestedSize) {
    auto& bucket = buckets_[bucketIndex];

    StagingBufferHandle handle = InvalidStagingHandle;
    {
        std::lock_guard<std::mutex> lock(bucket.mutex);
        if (bucket.available.empty()) {
            return std::nullopt;
        }
        handle = bucket.available.front();
        bucket.available.pop_front();
    }

    // Mark as in use
    BufferRecord* record = nullptr;
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) {
            return std::nullopt;  // Record was destroyed
        }
        record = &it->second;
        record->inUse = true;
    }

    // Update stats
    totalPooledBytes_.fetch_sub(record->size, std::memory_order_relaxed);
    activeBytes_.fetch_add(record->size, std::memory_order_relaxed);
    ++totalAcquisitions_;

    StagingBufferAcquisition result;
    result.handle = handle;
    result.buffer = record->allocation.buffer;
    result.mappedData = record->mappedData;
    result.size = record->size;
    result.requestedSize = requestedSize;

    return result;
}

std::optional<StagingBufferAcquisition>
StagingBufferPool::AllocateNewBuffer(VkDeviceSize size, std::string_view debugName) {
    // Create buffer allocation request
    BufferAllocationRequest request;
    request.size = size;
    request.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    request.location = MemoryLocation::HostVisible;
    request.debugName = debugName.empty() ? "StagingBuffer" : debugName;

    auto result = budgetManager_->AllocateBuffer(request);
    if (!result) {
        return std::nullopt;
    }

    // Map the buffer if configured for persistent mapping
    void* mappedData = nullptr;
    if (config_.persistentMapping) {
        auto* allocator = budgetManager_->GetAllocator();
        if (allocator) {
            mappedData = allocator->MapBuffer(*result);
        }
    }

    // Generate handle and create record
    StagingBufferHandle handle = nextHandle_.fetch_add(1, std::memory_order_relaxed);

    BufferRecord record;
    record.allocation = *result;
    record.size = size;
    record.mappedData = mappedData;
    record.inUse = true;

    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        records_[handle] = record;
    }

    // Update stats
    activeBytes_.fetch_add(size, std::memory_order_relaxed);
    ++totalAcquisitions_;

    StagingBufferAcquisition acquisition;
    acquisition.handle = handle;
    acquisition.buffer = result->buffer;
    acquisition.mappedData = mappedData;
    acquisition.size = size;
    acquisition.requestedSize = size;

    return acquisition;
}

void StagingBufferPool::ReturnToBucket(StagingBufferHandle handle, size_t bucketIndex) {
    auto& bucket = buckets_[bucketIndex];

    VkDeviceSize bufferSize = 0;
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        auto it = records_.find(handle);
        if (it != records_.end()) {
            bufferSize = it->second.size;
        }
    }

    // Check bucket capacity
    {
        std::lock_guard<std::mutex> lock(bucket.mutex);
        if (bucket.available.size() >= config_.maxPooledBuffersPerBucket) {
            // Bucket full - destroy oldest buffer
            StagingBufferHandle oldest = bucket.available.front();
            bucket.available.pop_front();

            // Get size of buffer being destroyed
            VkDeviceSize oldestSize = 0;
            {
                std::lock_guard<std::mutex> recordLock(recordsMutex_);
                auto it = records_.find(oldest);
                if (it != records_.end()) {
                    oldestSize = it->second.size;
                }
            }

            DestroyBuffer(oldest);

            // Account for swap (destroy old, add new)
            totalPooledBytes_.fetch_sub(oldestSize, std::memory_order_relaxed);
        }
        bucket.available.push_back(handle);
    }

    totalPooledBytes_.fetch_add(bufferSize, std::memory_order_relaxed);
}

void StagingBufferPool::DestroyBuffer(StagingBufferHandle handle) {
    BufferRecord record;
    {
        std::lock_guard<std::mutex> lock(recordsMutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) {
            return;
        }
        record = it->second;
        records_.erase(it);
    }

    // Unmap if mapped
    if (record.mappedData && budgetManager_->GetAllocator()) {
        budgetManager_->GetAllocator()->UnmapBuffer(record.allocation);
    }

    // Free the buffer
    budgetManager_->FreeBuffer(record.allocation);

    // Release staging quota
    budgetManager_->ReleaseStagingQuota(record.size);

    // Update pool stats if this was a pooled (not active) buffer
    if (!record.inUse) {
        totalPooledBytes_.fetch_sub(record.size, std::memory_order_relaxed);
    }
}

} // namespace ResourceManagement
