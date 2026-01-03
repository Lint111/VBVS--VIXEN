#pragma once

#include "Updates/UpdateRequest.h"

#include <algorithm>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

namespace ResourceManagement {

/**
 * @brief Statistics for BatchedUpdater
 */
struct BatchedUpdaterStats {
    uint64_t totalUpdatesQueued = 0;      // Total updates queued
    uint64_t totalUpdatesRecorded = 0;    // Total updates recorded
    uint32_t currentPendingUpdates = 0;   // Currently queued (all frames)
    uint32_t frameCount = 0;              // Number of frame queues
};

/**
 * @brief Phase 3.5: Batched GPU Update System
 *
 * Collects GPU update requests (TLAS rebuilds, buffer writes, etc.)
 * and records them into command buffers during the Execute phase.
 *
 * Mirrors BatchedUploader pattern but for per-frame command recording
 * rather than CPU→GPU data transfers.
 *
 * Key differences from BatchedUploader:
 * - Does not submit command buffers (caller provides active cmd buffer)
 * - Per-frame queues (indexed by imageIndex)
 * - Polymorphic request types (each knows how to record itself)
 *
 * Usage:
 * @code
 * // During resource setup (Compile phase)
 * BatchedUpdater updater(3);  // 3 swapchain images
 *
 * // During Execute phase
 * updater.Queue(std::make_unique<TLASUpdateRequest>(...));
 * updater.Queue(std::make_unique<BufferWriteRequest>(...));
 *
 * // During command buffer recording
 * updater.RecordAll(cmdBuffer, currentImageIndex);
 * @endcode
 *
 * Thread-safe: Yes (for Queue operations)
 */
class BatchedUpdater {
public:
    /**
     * @brief Configuration for batched updater
     */
    struct Config {
        uint32_t maxPendingPerFrame = 256;  // Max queued per frame before warning
        bool sortByPriority = true;         // Sort updates by priority before recording
        bool insertBarriers = true;         // Auto-insert barriers where needed
    };

    /**
     * @brief Create a batched updater
     *
     * @param frameCount Number of swapchain images / frames in flight
     * @param config Updater configuration
     */
    explicit BatchedUpdater(uint32_t frameCount, const Config& config = Config{});

    ~BatchedUpdater() = default;

    // Non-copyable, movable
    BatchedUpdater(const BatchedUpdater&) = delete;
    BatchedUpdater& operator=(const BatchedUpdater&) = delete;
    BatchedUpdater(BatchedUpdater&&) = default;
    BatchedUpdater& operator=(BatchedUpdater&&) = default;

    // =========================================================================
    // Queue API
    // =========================================================================

    /**
     * @brief Queue an update request
     *
     * The request's imageIndex determines which frame queue it goes to.
     * Request is moved into the updater.
     *
     * @param request Update request (ownership transferred)
     */
    void Queue(UpdateRequestPtr request);

    /**
     * @brief Queue an update request for a specific frame
     *
     * Overrides the request's imageIndex with the provided value.
     *
     * @param request Update request
     * @param imageIndex Target frame index
     */
    void Queue(UpdateRequestPtr request, uint32_t imageIndex);

    /**
     * @brief Get number of pending updates for a frame
     *
     * @param imageIndex Frame index
     * @return Number of pending updates
     */
    [[nodiscard]] uint32_t GetPendingCount(uint32_t imageIndex) const;

    /**
     * @brief Get total pending updates across all frames
     */
    [[nodiscard]] uint32_t GetTotalPendingCount() const;

    /**
     * @brief Check if any updates are pending for a frame
     *
     * @param imageIndex Frame index
     * @return true if updates pending
     */
    [[nodiscard]] bool HasPending(uint32_t imageIndex) const;

    // =========================================================================
    // Recording API
    // =========================================================================

    /**
     * @brief Record all pending updates for a frame
     *
     * Sorts by priority (if enabled), then calls Record() on each.
     * Clears the frame's queue after recording.
     *
     * @param cmd Active command buffer in recording state
     * @param imageIndex Frame index to record
     * @return Number of updates recorded
     */
    uint32_t RecordAll(VkCommandBuffer cmd, uint32_t imageIndex);

    /**
     * @brief Clear pending updates for a frame without recording
     *
     * Use when skipping a frame or on error.
     *
     * @param imageIndex Frame index
     */
    void Clear(uint32_t imageIndex);

    /**
     * @brief Clear all pending updates for all frames
     */
    void ClearAll();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Resize for different frame count
     *
     * Clears all pending updates.
     *
     * @param frameCount New frame count
     */
    void Resize(uint32_t frameCount);

    /**
     * @brief Get frame count
     */
    [[nodiscard]] uint32_t GetFrameCount() const { return static_cast<uint32_t>(frameQueues_.size()); }

    /**
     * @brief Get configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get updater statistics
     */
    [[nodiscard]] BatchedUpdaterStats GetStats() const;

private:
    Config config_;

    // Per-frame queues
    mutable std::mutex mutex_;
    std::vector<std::vector<UpdateRequestPtr>> frameQueues_;

    // Statistics
    std::atomic<uint64_t> totalQueued_{0};
    std::atomic<uint64_t> totalRecorded_{0};

    /**
     * @brief Validate image index
     */
    bool ValidateImageIndex(uint32_t imageIndex) const;
};

} // namespace ResourceManagement
