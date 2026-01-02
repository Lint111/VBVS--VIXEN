#pragma once

#include "Memory/IMemoryAllocator.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ResourceManagement {

// Forward declarations
class DeviceBudgetManager;

/**
 * @brief Opaque handle for acquired staging buffers
 *
 * Used to track staging buffer lifetime and enable proper release.
 */
using StagingBufferHandle = uint64_t;

/**
 * @brief Invalid staging buffer handle value
 */
constexpr StagingBufferHandle InvalidStagingHandle = 0;

/**
 * @brief Result of acquiring a staging buffer
 */
struct StagingBufferAcquisition {
    StagingBufferHandle handle = InvalidStagingHandle;
    VkBuffer buffer = VK_NULL_HANDLE;
    void* mappedData = nullptr;         // Persistently mapped pointer
    VkDeviceSize size = 0;              // Actual buffer size (may be >= requested)
    VkDeviceSize requestedSize = 0;     // Original requested size

    explicit operator bool() const { return handle != InvalidStagingHandle; }
};

/**
 * @brief Pool statistics for monitoring
 */
struct StagingPoolStats {
    uint64_t totalPooledBuffers = 0;     // Buffers available in pool
    uint64_t totalPooledBytes = 0;       // Bytes in pool (not currently used)
    uint64_t activeBuffers = 0;          // Buffers currently acquired
    uint64_t activeBytes = 0;            // Bytes currently in use
    uint64_t totalAcquisitions = 0;      // Lifetime acquisitions
    uint64_t totalReleases = 0;          // Lifetime releases
    uint64_t poolHits = 0;               // Times a pooled buffer was reused
    uint64_t poolMisses = 0;             // Times a new buffer had to be allocated
    float hitRate = 0.0f;                // poolHits / totalAcquisitions
};

/**
 * @brief Phase 2.5: Staging Buffer Pool
 *
 * High-performance pool for staging buffers used in CPU→GPU uploads.
 * Integrates with DeviceBudgetManager for quota enforcement and provides
 * automatic buffer recycling to reduce allocation overhead.
 *
 * Features:
 * - Acquire/Release API with automatic recycling
 * - Size-class bucketing for efficient buffer reuse
 * - Integration with DeviceBudgetManager::TryReserveStagingQuota()
 * - Thread-safe concurrent access
 * - Per-device pools (matches budget manager isolation)
 * - Persistent memory mapping for all pooled buffers
 *
 * Usage:
 * @code
 * StagingBufferPool pool(&budgetManager);
 *
 * // Acquire a staging buffer
 * auto result = pool.AcquireBuffer(uploadSize);
 * if (result) {
 *     memcpy(result->mappedData, srcData, uploadSize);
 *     // Submit copy command using result->buffer
 *     // ...
 *     pool.ReleaseBuffer(result->handle);
 * }
 * @endcode
 *
 * Thread-safe: Yes (all public methods)
 */
class StagingBufferPool {
public:
    /**
     * @brief Pool configuration
     */
    struct Config {
        uint64_t minBufferSize = 64 * 1024;           // 64 KB minimum buffer
        uint64_t maxBufferSize = 64 * 1024 * 1024;    // 64 MB maximum single buffer
        uint32_t maxPooledBuffersPerBucket = 4;       // Max cached buffers per size class
        uint64_t maxTotalPooledBytes = 128 * 1024 * 1024;  // 128 MB max pool size
        bool persistentMapping = true;                 // Keep buffers mapped
    };

    /**
     * @brief Create a staging buffer pool
     *
     * @param budgetManager Device budget manager for quota and allocation
     * @param config Pool configuration
     */
    explicit StagingBufferPool(
        DeviceBudgetManager* budgetManager,
        const Config& config = Config{});

    /**
     * @brief Destructor - releases all pooled buffers
     */
    ~StagingBufferPool();

    // Non-copyable, non-movable (owns Vulkan resources)
    StagingBufferPool(const StagingBufferPool&) = delete;
    StagingBufferPool& operator=(const StagingBufferPool&) = delete;
    StagingBufferPool(StagingBufferPool&&) = delete;
    StagingBufferPool& operator=(StagingBufferPool&&) = delete;

    // =========================================================================
    // Acquisition API
    // =========================================================================

    /**
     * @brief Acquire a staging buffer from the pool
     *
     * Returns a buffer of at least the requested size. The actual buffer
     * size may be larger due to size-class bucketing. Blocks if staging
     * quota is exhausted (backpressure from DeviceBudgetManager).
     *
     * @param requestedSize Minimum buffer size needed
     * @param debugName Optional debug name for new allocations
     * @return Buffer acquisition or nullopt if allocation fails
     */
    [[nodiscard]] std::optional<StagingBufferAcquisition>
    AcquireBuffer(VkDeviceSize requestedSize, std::string_view debugName = "");

    /**
     * @brief Try to acquire a staging buffer without blocking
     *
     * Unlike AcquireBuffer(), this returns immediately if quota is unavailable.
     *
     * @param requestedSize Minimum buffer size needed
     * @param debugName Optional debug name for new allocations
     * @return Buffer acquisition or nullopt if unavailable
     */
    [[nodiscard]] std::optional<StagingBufferAcquisition>
    TryAcquireBuffer(VkDeviceSize requestedSize, std::string_view debugName = "");

    /**
     * @brief Release a staging buffer back to the pool
     *
     * The buffer will be returned to the appropriate size-class bucket
     * for reuse by future acquisitions. The handle becomes invalid after
     * this call.
     *
     * @param handle Handle from previous AcquireBuffer call
     */
    void ReleaseBuffer(StagingBufferHandle handle);

    /**
     * @brief Release a buffer and immediately destroy it (don't pool)
     *
     * Use for oversized or one-time buffers that shouldn't be cached.
     *
     * @param handle Handle from previous AcquireBuffer call
     */
    void ReleaseAndDestroy(StagingBufferHandle handle);

    // =========================================================================
    // Pool Management
    // =========================================================================

    /**
     * @brief Trim the pool by releasing unused buffers
     *
     * @param targetBytes Target pool size after trimming
     * @return Bytes freed
     */
    uint64_t Trim(uint64_t targetBytes = 0);

    /**
     * @brief Release all pooled (unused) buffers
     *
     * Does not affect currently acquired buffers.
     */
    void Clear();

    /**
     * @brief Pre-warm the pool with buffers of common sizes
     *
     * @param sizes Array of sizes to pre-allocate
     * @param count Number of sizes
     */
    void PreWarm(const VkDeviceSize* sizes, size_t count);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get pool statistics
     */
    [[nodiscard]] StagingPoolStats GetStats() const;

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

private:
    /**
     * @brief Internal buffer record
     */
    struct BufferRecord {
        BufferAllocation allocation;
        VkDeviceSize size = 0;
        void* mappedData = nullptr;
        bool inUse = false;
    };

    /**
     * @brief Size-class bucket for buffer pooling
     */
    struct SizeClassBucket {
        VkDeviceSize minSize = 0;
        VkDeviceSize maxSize = 0;
        std::deque<StagingBufferHandle> available;  // FIFO for reuse
        mutable std::mutex mutex;
    };

    // Configuration
    Config config_;
    DeviceBudgetManager* budgetManager_;

    // Handle generation
    std::atomic<StagingBufferHandle> nextHandle_{1};

    // Buffer tracking (handle → record)
    mutable std::mutex recordsMutex_;
    std::unordered_map<StagingBufferHandle, BufferRecord> records_;

    // Size-class buckets
    static constexpr size_t NumBuckets = 12;  // 64KB to 64MB in powers of 2
    std::array<SizeClassBucket, NumBuckets> buckets_;

    // Pool statistics (atomic for lock-free reads)
    std::atomic<uint64_t> totalPooledBytes_{0};
    std::atomic<uint64_t> activeBytes_{0};
    std::atomic<uint64_t> totalAcquisitions_{0};
    std::atomic<uint64_t> totalReleases_{0};
    std::atomic<uint64_t> poolHits_{0};
    std::atomic<uint64_t> poolMisses_{0};

    // Internal helpers
    size_t GetBucketIndex(VkDeviceSize size) const;
    VkDeviceSize GetBucketSize(size_t bucketIndex) const;
    [[nodiscard]] std::optional<StagingBufferAcquisition>
    AcquireFromBucket(size_t bucketIndex, VkDeviceSize requestedSize);
    [[nodiscard]] std::optional<StagingBufferAcquisition>
    AllocateNewBuffer(VkDeviceSize size, std::string_view debugName);
    void ReturnToBucket(StagingBufferHandle handle, size_t bucketIndex);
    void DestroyBuffer(StagingBufferHandle handle);
};

} // namespace ResourceManagement
