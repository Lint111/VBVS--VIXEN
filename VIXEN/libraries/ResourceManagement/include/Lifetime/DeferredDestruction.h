#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <cstdint>

namespace ResourceManagement {

/**
 * @brief Pending resource destruction
 *
 * Stores a destruction function and the frame number when the resource
 * was submitted for destruction. The resource is destroyed after N frames
 * have passed to ensure GPU has finished using it.
 */
struct PendingDestruction {
    std::function<void()> destructorFunc;
    uint64_t submittedFrame = 0;

    PendingDestruction() = default;

    PendingDestruction(std::function<void()> func, uint64_t frame)
        : destructorFunc(std::move(func))
        , submittedFrame(frame) {}
};

/**
 * @brief Deferred destruction queue for zero-stutter hot-reload
 *
 * Manages destruction of Vulkan resources after they are no longer in use by the GPU.
 * Instead of blocking with vkDeviceWaitIdle(), resources are queued for destruction
 * and destroyed after N frames have passed (typically MAX_FRAMES_IN_FLIGHT).
 *
 * Architecture:
 * - Pre-allocatable ring buffer for zero-allocation runtime operation
 * - FIFO queue ordered by frame number
 * - Resources destroyed after frameNumber - submittedFrame >= maxFramesInFlight
 * - Zero-stutter: No blocking waits during hot-reload
 * - Growth fallback with warning logging for capacity tuning
 *
 * Usage:
 *
 *   // During setup phase
 *   deferredQueue.PreReserve(nodeCount * 5);  // Pre-allocate based on heuristic
 *
 *   // In hot-reload handler
 *   deferredQueue.Add(device, oldPipeline, currentFrame, vkDestroyPipeline);
 *
 *   // In main loop (before rendering)
 *   deferredQueue.ProcessFrame(currentFrame);
 *
 *   // Monitor capacity (for tuning)
 *   auto stats = deferredQueue.GetPreAllocationStats();
 *   if (stats.growthCount > 0) { ... increase PreReserve capacity ... }
 */
class DeferredDestructionQueue {
public:
    /**
     * @brief Statistics for monitoring pre-allocation effectiveness
     */
    struct PreAllocationStats {
        size_t capacity = 0;          ///< Current buffer capacity
        size_t currentSize = 0;       ///< Current number of pending destructions
        size_t maxSizeReached = 0;    ///< High-water mark
        size_t growthCount = 0;       ///< Times buffer had to grow (should be 0 after tuning)
        size_t totalQueued = 0;       ///< Total items ever queued
        size_t totalDestroyed = 0;    ///< Total items destroyed via ProcessFrame
        size_t totalFlushed = 0;      ///< Total items destroyed via Flush
    };

    DeferredDestructionQueue() = default;

    explicit DeferredDestructionQueue(size_t initialCapacity) {
        PreReserve(initialCapacity);
    }

    ~DeferredDestructionQueue() {
        Flush();  // Ensure all pending destructions are executed
    }

    // Disable copy/move (manages resource lifetimes)
    DeferredDestructionQueue(const DeferredDestructionQueue&) = delete;
    DeferredDestructionQueue& operator=(const DeferredDestructionQueue&) = delete;
    DeferredDestructionQueue(DeferredDestructionQueue&&) = delete;
    DeferredDestructionQueue& operator=(DeferredDestructionQueue&&) = delete;

    /**
     * @brief Pre-allocate storage for expected destruction rate
     *
     * Call during setup phase to prevent allocations during runtime.
     * Capacity = maxResourcesPerFrame * maxFramesInFlight is a good heuristic.
     *
     * @param capacity Number of pending destructions to pre-allocate for
     */
    void PreReserve(size_t capacity) {
        if (capacity <= buffer_.size()) {
            return;  // Already have enough capacity
        }

        // Create new buffer with requested capacity
        std::vector<PendingDestruction> newBuffer(capacity);

        // Move existing elements to new buffer (maintain FIFO order)
        for (size_t i = 0; i < size_; ++i) {
            size_t oldIdx = (head_ + i) % buffer_.size();
            newBuffer[i] = std::move(buffer_[oldIdx]);
        }

        buffer_ = std::move(newBuffer);
        head_ = 0;
        tail_ = size_;
    }

    /**
     * @brief Get current pre-allocated capacity
     */
    size_t GetCapacity() const noexcept {
        return buffer_.size();
    }

    /**
     * @brief Add Vulkan resource for deferred destruction
     *
     * @tparam VkHandleT Vulkan handle type (VkPipeline, VkImage, VkBuffer, etc.)
     * @param device Vulkan device
     * @param handle Vulkan handle to destroy
     * @param currentFrame Current frame number
     * @param destroyer Vulkan destruction function (vkDestroyPipeline, vkDestroyImage, etc.)
     *
     * Example:
     * ```cpp
     * queue.Add(device, oldPipeline, frameNum, vkDestroyPipeline);
     * queue.Add(device, oldImage, frameNum, vkDestroyImage);
     * queue.Add(device, oldBuffer, frameNum, vkDestroyBuffer);
     * ```
     */
    template<typename VkHandleT>
    void Add(
        VkDevice device,
        VkHandleT handle,
        uint64_t currentFrame,
        void (*destroyer)(VkDevice, VkHandleT, const VkAllocationCallbacks*)
    ) {
        if (handle == VK_NULL_HANDLE) {
            return;  // Nothing to destroy
        }

        PushInternal(PendingDestruction(
            [device, handle, destroyer]() {
                destroyer(device, handle, nullptr);
            },
            currentFrame
        ));
    }

    /**
     * @brief Add generic destruction function for deferred execution
     *
     * More flexible than the Vulkan-specific overload. Use for resources
     * that need custom cleanup logic (e.g., allocator-managed buffers).
     *
     * @param destructorFunc Function to call when destruction occurs
     * @param currentFrame Current frame number
     *
     * Example:
     * ```cpp
     * queue.AddGeneric([allocator, buffer]() {
     *     allocator->FreeBuffer(buffer);
     * }, frameNumber);
     * ```
     */
    void AddGeneric(std::function<void()> destructorFunc, uint64_t currentFrame) {
        if (!destructorFunc) {
            return;
        }

        PushInternal(PendingDestruction(std::move(destructorFunc), currentFrame));
    }

    /**
     * @brief Process deferred destructions for current frame
     *
     * Destroys resources that were submitted >= maxFramesInFlight frames ago.
     * Call once per frame before rendering.
     *
     * @param currentFrame Current frame number
     * @param maxFramesInFlight Number of frames to wait (default: 3)
     *
     * Example:
     * ```cpp
     * while (running) {
     *     messageBus.ProcessMessages();
     *     renderGraph->RecompileDirtyNodes();
     *     deferredQueue.ProcessFrame(frameNumber);  // Before rendering
     *     renderGraph->RenderFrame();
     *     frameNumber++;
     * }
     * ```
     */
    void ProcessFrame(uint64_t currentFrame, uint32_t maxFramesInFlight = 3) {
        size_t destroyedThisFrame = 0;

        while (size_ > 0) {
            const auto& pending = buffer_[head_];

            // Safe to destroy after N frames have passed
            // Guard against unsigned underflow: only destroy if current >= submitted
            // and the difference is >= maxFramesInFlight
            if (currentFrame >= pending.submittedFrame &&
                (currentFrame - pending.submittedFrame) >= maxFramesInFlight) {
                pending.destructorFunc();
                PopInternal();
                destroyedThisFrame++;
            } else {
                break;  // Queue is FIFO ordered by submission time
            }
        }

        totalDestroyed_ += destroyedThisFrame;
    }

    /**
     * @brief Force destroy all pending resources immediately
     *
     * Blocks until all pending destructions are executed.
     * Call during application shutdown.
     *
     * Example:
     * ```cpp
     * // Shutdown sequence
     * deferredQueue.Flush();
     * renderGraph->ExecuteCleanup();
     * ```
     */
    void Flush() {
        size_t flushedCount = 0;

        while (size_ > 0) {
            buffer_[head_].destructorFunc();
            PopInternal();
            flushedCount++;
        }

        totalFlushed_ += flushedCount;
    }

    /**
     * @brief Get number of pending destructions
     */
    size_t GetPendingCount() const noexcept {
        return size_;
    }

    /**
     * @brief Get pre-allocation statistics for capacity tuning
     *
     * Use this to monitor if PreReserve capacity is adequate:
     * - growthCount > 0 after setup → increase PreReserve capacity
     * - maxSizeReached << capacity → decrease PreReserve capacity
     */
    PreAllocationStats GetPreAllocationStats() const noexcept {
        PreAllocationStats stats;
        stats.capacity = buffer_.size();
        stats.currentSize = size_;
        stats.maxSizeReached = maxSizeReached_;
        stats.growthCount = growthCount_;
        stats.totalQueued = totalQueued_;
        stats.totalDestroyed = totalDestroyed_;
        stats.totalFlushed = totalFlushed_;
        return stats;
    }

    /**
     * @brief Reset statistics counters
     *
     * Resets growth count and high-water mark for fresh measurement period.
     */
    void ResetStats() noexcept {
        maxSizeReached_ = size_;
        growthCount_ = 0;
        totalQueued_ = 0;
        totalDestroyed_ = 0;
        totalFlushed_ = 0;
    }

private:
    /**
     * @brief Push element to ring buffer with automatic growth fallback
     */
    void PushInternal(PendingDestruction pending) {
        // Check if we need to grow
        if (size_ >= buffer_.size()) {
            // Growth fallback - double capacity (indicates PreReserve was too small)
            size_t newCapacity = buffer_.empty() ? 16 : buffer_.size() * 2;
            PreReserve(newCapacity);
            growthCount_++;
            // Note: In production, log warning here for capacity tuning
        }

        buffer_[tail_] = std::move(pending);
        tail_ = (tail_ + 1) % buffer_.size();
        size_++;
        totalQueued_++;

        // Track high-water mark
        if (size_ > maxSizeReached_) {
            maxSizeReached_ = size_;
        }
    }

    /**
     * @brief Pop element from ring buffer front
     */
    void PopInternal() noexcept {
        if (size_ == 0) {
            return;
        }
        buffer_[head_] = PendingDestruction{};  // Clear the slot
        head_ = (head_ + 1) % buffer_.size();
        size_--;
    }

    // Ring buffer storage
    std::vector<PendingDestruction> buffer_;
    size_t head_ = 0;      ///< Index of front element (oldest)
    size_t tail_ = 0;      ///< Index where next element will be inserted
    size_t size_ = 0;      ///< Current number of elements

    // Statistics for capacity tuning (always tracked, not just DEBUG)
    size_t maxSizeReached_ = 0;
    size_t growthCount_ = 0;
    size_t totalQueued_ = 0;
    size_t totalDestroyed_ = 0;
    size_t totalFlushed_ = 0;
};

} // namespace ResourceManagement
