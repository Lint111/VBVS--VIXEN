#pragma once

#include "Core/HostBudgetManager.h"
#include "Core/DeviceBudgetManager.h"
#include <functional>
#include <queue>

namespace Vixen::RenderGraph {

/**
 * @brief Pending upload tracking info
 */
struct PendingUpload {
    uint64_t stagingBytes = 0;      // Staging buffer size
    uint64_t frameSubmitted = 0;    // Frame when upload was submitted
    uint64_t fenceValue = 0;        // GPU fence/timeline value to wait for
};

/**
 * @brief Upload completion callback
 */
using UploadCompleteCallback = std::function<void(uint64_t bytesCompleted)>;

/**
 * @brief Phase A.5: Communication Bridge between Host and Device Budget Managers
 *
 * Coordinates staging buffer allocation and GPU upload tracking between
 * HostBudgetManager (CPU) and DeviceBudgetManager (GPU).
 *
 * Features:
 * - Staging quota reservation from device budget
 * - Upload tracking with frame/fence integration
 * - Automatic staging reclamation when GPU completes
 * - Backpressure when staging quota exhausted
 *
 * Usage:
 * 1. ReserveStagingBuffer() before CPU→GPU upload
 * 2. RecordUpload() when upload submitted to GPU
 * 3. ProcessCompletedUploads() each frame to reclaim staging
 *
 * Thread-safe: Yes (atomic operations + mutex for pending queue)
 */
class BudgetBridge {
public:
    /**
     * @brief Configuration for budget bridge
     */
    struct Config {
        uint64_t maxStagingQuota = 256 * 1024 * 1024;  // 256 MB max staging
        uint64_t stagingWarningThreshold = 200 * 1024 * 1024;  // Warn at 200 MB
        uint32_t maxPendingUploads = 1024;  // Max tracked uploads
        uint32_t framesToKeepPending = 3;   // Frames before assuming completion
    };

    /**
     * @brief Create budget bridge
     *
     * @param hostBudget Host budget manager (for staging allocation tracking)
     * @param deviceBudget Device budget manager (for staging quota)
     * @param config Bridge configuration
     */
    BudgetBridge(
        HostBudgetManager* hostBudget,
        DeviceBudgetManager* deviceBudget,
        const Config& config = Config{});

    ~BudgetBridge() = default;

    // Non-copyable
    BudgetBridge(const BudgetBridge&) = delete;
    BudgetBridge& operator=(const BudgetBridge&) = delete;

    // =========================================================================
    // Staging Buffer Management
    // =========================================================================

    /**
     * @brief Reserve staging buffer quota for upload
     *
     * Call before allocating a staging buffer for CPU→GPU transfer.
     * Blocks if staging quota exhausted (backpressure).
     *
     * @param bytes Size of staging buffer needed
     * @return true if quota reserved, false if would exceed limits
     */
    [[nodiscard]] bool ReserveStagingQuota(uint64_t bytes);

    /**
     * @brief Release staging quota (upload cancelled or completed)
     */
    void ReleaseStagingQuota(uint64_t bytes);

    /**
     * @brief Record an upload submission to GPU
     *
     * Call after submitting a staging buffer upload command.
     *
     * @param stagingBytes Size of staging buffer
     * @param fenceValue GPU fence value that signals completion
     */
    void RecordUpload(uint64_t stagingBytes, uint64_t fenceValue);

    /**
     * @brief Process completed uploads based on GPU fence
     *
     * Call each frame to reclaim completed staging buffers.
     *
     * @param completedFenceValue Current completed GPU fence value
     * @return Bytes of staging reclaimed this call
     */
    uint64_t ProcessCompletedUploads(uint64_t completedFenceValue);

    /**
     * @brief Process completed uploads based on frame age
     *
     * Alternative for simple frame-based tracking without fences.
     *
     * @param currentFrame Current frame number
     * @return Bytes of staging reclaimed this call
     */
    uint64_t ProcessCompletedUploads(uint64_t currentFrame, bool useFrameTracking);

    // =========================================================================
    // Status & Monitoring
    // =========================================================================

    /**
     * @brief Get current staging quota usage
     */
    [[nodiscard]] uint64_t GetStagingQuotaUsed() const;

    /**
     * @brief Get available staging quota
     */
    [[nodiscard]] uint64_t GetAvailableStagingQuota() const;

    /**
     * @brief Get pending upload count
     */
    [[nodiscard]] size_t GetPendingUploadCount() const;

    /**
     * @brief Get total bytes pending GPU completion
     */
    [[nodiscard]] uint64_t GetPendingUploadBytes() const;

    /**
     * @brief Check if staging quota is near warning threshold
     */
    [[nodiscard]] bool IsStagingNearLimit() const;

    /**
     * @brief Set callback for upload completion
     *
     * @warning Callback is invoked while holding internal lock.
     * Do NOT call BudgetBridge methods from within the callback
     * to avoid deadlock.
     */
    void SetUploadCompleteCallback(UploadCompleteCallback callback);

    // =========================================================================
    // Configuration
    // =========================================================================

    [[nodiscard]] const Config& GetConfig() const { return config_; }

    /**
     * @brief Update staging quota limit
     */
    void SetStagingQuotaLimit(uint64_t newLimit);

private:
    Config config_;
    HostBudgetManager* hostBudget_;      // Reserved for future staging allocation tracking
    DeviceBudgetManager* deviceBudget_;

    // Staging quota tracking
    std::atomic<uint64_t> stagingQuotaUsed_{0};

    // Pending upload queue
    mutable std::mutex pendingMutex_;
    std::queue<PendingUpload> pendingUploads_;
    std::atomic<uint64_t> pendingBytes_{0};

    // Optional callback
    UploadCompleteCallback onUploadComplete_;

    // Current frame (for frame-based tracking)
    std::atomic<uint64_t> currentFrame_{0};
};

} // namespace Vixen::RenderGraph
