#pragma once

#include "Memory/StagingBufferPool.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>
#include <vulkan/vulkan.h>

namespace ResourceManagement {

// Forward declarations
class DeviceBudgetManager;

/**
 * @brief Handle for tracking upload completion
 */
using UploadHandle = uint64_t;

/**
 * @brief Invalid upload handle value
 */
constexpr UploadHandle InvalidUploadHandle = 0;

/**
 * @brief Upload request status
 */
enum class UploadStatus : uint8_t {
    Pending,      // Queued, not yet submitted
    Submitted,    // Command buffer submitted to GPU
    Completed,    // GPU execution complete
    Failed        // Upload failed
};

/**
 * @brief Statistics for BatchedUploader
 */
struct BatchedUploaderStats {
    uint64_t totalUploads = 0;           // Total uploads queued
    uint64_t totalBatches = 0;           // Total batches submitted
    uint64_t totalBytesUploaded = 0;     // Total bytes uploaded
    uint64_t currentPendingUploads = 0;  // Currently queued uploads
    uint64_t currentPendingBytes = 0;    // Currently queued bytes
    float avgUploadsPerBatch = 0.0f;     // Average uploads per batch
    float avgBatchLatencyMs = 0.0f;      // Average time from queue to completion
};

/**
 * @brief Phase 2.5.2: Batched Uploader
 *
 * High-performance CPU→GPU upload system that batches multiple transfers
 * into single command buffer submissions. Integrates with StagingBufferPool
 * for buffer recycling and DeviceBudgetManager for quota enforcement.
 *
 * Features:
 * - Queue multiple uploads before submission
 * - Single command buffer per batch (reduces CPU overhead)
 * - Timeline semaphore completion tracking
 * - Automatic staging buffer release on GPU completion
 * - Deadline-based flush (max latency bound)
 * - Thread-safe concurrent upload queueing
 *
 * Usage:
 * @code
 * BatchedUploader uploader(device, queue, &budgetManager);
 *
 * // Queue uploads (non-blocking)
 * uploader.Upload(data1, size1, destBuffer1);
 * uploader.Upload(data2, size2, destBuffer2);
 *
 * // Flush pending uploads (or wait for deadline)
 * uploader.Flush();
 *
 * // Poll for completions each frame
 * uploader.ProcessCompletions();
 * @endcode
 *
 * Thread-safe: Yes (all public methods)
 */
class BatchedUploader {
public:
    /**
     * @brief Configuration for batched uploader
     */
    struct Config {
        uint32_t maxPendingUploads = 64;           // Max queued before auto-flush
        uint64_t maxPendingBytes = 64 * 1024 * 1024;  // 64 MB before auto-flush
        uint32_t maxBatchCommandBuffers = 4;       // Command buffer pool size
        std::chrono::milliseconds flushDeadline{16};  // Max latency (1 frame at 60fps)
        bool useTimelineSemaphores = true;         // Use timeline semaphores if available
    };

    /**
     * @brief Create a batched uploader
     *
     * @param device Vulkan logical device
     * @param queue Queue for transfer operations
     * @param queueFamilyIndex Queue family index for command pool
     * @param budgetManager Budget manager (provides StagingBufferPool)
     * @param config Uploader configuration
     */
    BatchedUploader(
        VkDevice device,
        VkQueue queue,
        uint32_t queueFamilyIndex,
        DeviceBudgetManager* budgetManager,
        const Config& config = Config{});

    /**
     * @brief Destructor - waits for pending uploads and cleans up
     */
    ~BatchedUploader();

    // Non-copyable, non-movable
    BatchedUploader(const BatchedUploader&) = delete;
    BatchedUploader& operator=(const BatchedUploader&) = delete;
    BatchedUploader(BatchedUploader&&) = delete;
    BatchedUploader& operator=(BatchedUploader&&) = delete;

    // =========================================================================
    // Upload API
    // =========================================================================

    /**
     * @brief Queue a buffer upload
     *
     * Copies data to a staging buffer and queues a transfer command.
     * The upload is batched with other pending uploads for efficiency.
     *
     * @param srcData Source data pointer (copied immediately)
     * @param size Size in bytes
     * @param dstBuffer Destination GPU buffer
     * @param dstOffset Offset in destination buffer
     * @return Upload handle for tracking, or InvalidUploadHandle on failure
     */
    [[nodiscard]] UploadHandle Upload(
        const void* srcData,
        VkDeviceSize size,
        VkBuffer dstBuffer,
        VkDeviceSize dstOffset = 0);

    /**
     * @brief Queue a buffer-to-buffer copy (no staging)
     *
     * For copies between GPU buffers without CPU data.
     *
     * @param srcBuffer Source buffer
     * @param srcOffset Source offset
     * @param dstBuffer Destination buffer
     * @param dstOffset Destination offset
     * @param size Size in bytes
     * @return Upload handle for tracking
     */
    [[nodiscard]] UploadHandle CopyBuffer(
        VkBuffer srcBuffer,
        VkDeviceSize srcOffset,
        VkBuffer dstBuffer,
        VkDeviceSize dstOffset,
        VkDeviceSize size);

    /**
     * @brief Get status of an upload
     *
     * @param handle Upload handle from Upload() call
     * @return Upload status
     */
    [[nodiscard]] UploadStatus GetStatus(UploadHandle handle) const;

    /**
     * @brief Check if an upload is complete
     *
     * @param handle Upload handle
     * @return true if completed or failed
     */
    [[nodiscard]] bool IsComplete(UploadHandle handle) const;

    /**
     * @brief Wait for a specific upload to complete
     *
     * @param handle Upload handle
     * @param timeout Max wait time
     * @return true if completed, false if timeout
     */
    bool WaitForUpload(UploadHandle handle,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

    // =========================================================================
    // Batch Control
    // =========================================================================

    /**
     * @brief Flush all pending uploads immediately
     *
     * Submits a command buffer with all queued transfers.
     * Call this when you need uploads to start executing.
     */
    void Flush();

    /**
     * @brief Process completed uploads
     *
     * Checks GPU completion status and releases staging buffers.
     * Call this once per frame.
     *
     * @return Number of uploads completed this call
     */
    uint32_t ProcessCompletions();

    /**
     * @brief Wait for all pending uploads to complete
     *
     * Flushes and blocks until GPU finishes all transfers.
     */
    void WaitIdle();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get uploader statistics
     */
    [[nodiscard]] BatchedUploaderStats GetStats() const;

    /**
     * @brief Get number of pending uploads
     */
    [[nodiscard]] uint32_t GetPendingCount() const;

    /**
     * @brief Get pending bytes
     */
    [[nodiscard]] uint64_t GetPendingBytes() const;

    /**
     * @brief Get configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

    // =========================================================================
    // Pre-Allocation (Sprint 5 Phase 4.1)
    // =========================================================================

    /**
     * @brief Pre-warm the staging buffer pool
     *
     * Allocates staging buffers upfront to avoid runtime allocation during
     * first frame uploads. Call during device initialization.
     *
     * @param sizes Array of buffer sizes to pre-allocate
     * @param count Number of sizes in the array
     */
    void PreWarm(const VkDeviceSize* sizes, size_t count);

    /**
     * @brief Pre-warm with default sizes for typical upload patterns
     *
     * Pre-allocates buffers for common upload sizes:
     * - Small (64 KB): 4 buffers - small constant/uniform updates
     * - Medium (1 MB): 2 buffers - texture mipmaps, mesh data
     * - Large (16 MB): 2 buffers - large textures, AS instance buffers
     */
    void PreWarmDefaults();

private:
    /**
     * @brief Pending upload record
     */
    struct PendingUpload {
        UploadHandle handle = InvalidUploadHandle;
        StagingBufferHandle stagingHandle = InvalidStagingHandle;
        VkBuffer dstBuffer = VK_NULL_HANDLE;
        VkDeviceSize dstOffset = 0;
        VkDeviceSize size = 0;
        bool isCopy = false;  // Buffer-to-buffer copy (no staging)
        VkBuffer srcBuffer = VK_NULL_HANDLE;  // For copies
        VkDeviceSize srcOffset = 0;
    };

    /**
     * @brief Submitted batch record
     */
    struct SubmittedBatch {
        uint64_t timelineValue = 0;           // Timeline semaphore value
        VkFence fence = VK_NULL_HANDLE;       // Fallback fence if no timeline
        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
        std::vector<PendingUpload> uploads;   // Uploads in this batch
        std::chrono::steady_clock::time_point submitTime;
    };

    // Configuration
    Config config_;
    VkDevice device_;
    VkQueue queue_;
    DeviceBudgetManager* budgetManager_;

    // Staging buffer pool
    std::unique_ptr<StagingBufferPool> stagingPool_;

    // Command pool and buffers
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::queue<VkCommandBuffer> availableCommandBuffers_;
    std::mutex cmdBufferMutex_;

    // Timeline semaphore (if supported)
    VkSemaphore timelineSemaphore_ = VK_NULL_HANDLE;
    std::atomic<uint64_t> nextTimelineValue_{1};
    bool useTimelineSemaphores_ = false;

    // Pending uploads (not yet submitted)
    mutable std::mutex pendingMutex_;
    std::vector<PendingUpload> pendingUploads_;
    std::atomic<uint64_t> pendingBytes_{0};
    std::chrono::steady_clock::time_point oldestPendingTime_;

    // Submitted batches (awaiting GPU completion)
    mutable std::mutex submittedMutex_;
    std::queue<SubmittedBatch> submittedBatches_;

    // Handle generation
    std::atomic<UploadHandle> nextHandle_{1};

    // Handle status tracking
    mutable std::mutex statusMutex_;
    std::unordered_map<UploadHandle, UploadStatus> uploadStatus_;

    // Statistics
    std::atomic<uint64_t> totalUploads_{0};
    std::atomic<uint64_t> totalBatches_{0};
    std::atomic<uint64_t> totalBytesUploaded_{0};

    // Internal helpers
    void CreateCommandPool(uint32_t queueFamilyIndex);
    void CreateTimelineSemaphore();
    VkCommandBuffer AcquireCommandBuffer();
    void ReleaseCommandBuffer(VkCommandBuffer cmdBuffer);
    void SubmitBatch(std::vector<PendingUpload>&& uploads);
    void CheckAutoFlush();
    void SetStatus(UploadHandle handle, UploadStatus status);
};

} // namespace ResourceManagement
