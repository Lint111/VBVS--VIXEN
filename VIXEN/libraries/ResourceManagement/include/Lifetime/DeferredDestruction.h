#pragma once

#include <vulkan/vulkan.h>
#include <queue>
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
    uint64_t submittedFrame;

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
 * - FIFO queue ordered by frame number
 * - Resources destroyed after frameNumber - submittedFrame >= maxFramesInFlight
 * - Zero-stutter: No blocking waits during hot-reload
 *
 * Usage:
 * ```cpp
 * // In hot-reload handler
 * deferredQueue.Add(device, oldPipeline, currentFrame, vkDestroyPipeline);
 *
 * // In main loop (before rendering)
 * deferredQueue.ProcessFrame(currentFrame);
 * ```
 */
class DeferredDestructionQueue {
public:
    DeferredDestructionQueue() = default;
    ~DeferredDestructionQueue() {
        Flush();  // Ensure all pending destructions are executed
    }

    // Disable copy/move (manages resource lifetimes)
    DeferredDestructionQueue(const DeferredDestructionQueue&) = delete;
    DeferredDestructionQueue& operator=(const DeferredDestructionQueue&) = delete;
    DeferredDestructionQueue(DeferredDestructionQueue&&) = delete;
    DeferredDestructionQueue& operator=(DeferredDestructionQueue&&) = delete;

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

        PendingDestruction pending(
            [device, handle, destroyer]() {
                destroyer(device, handle, nullptr);
            },
            currentFrame
        );

        queue.push(pending);

#ifdef _DEBUG
        totalQueued++;
#endif
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

        PendingDestruction pending(std::move(destructorFunc), currentFrame);
        queue.push(pending);

#ifdef _DEBUG
        totalQueued++;
#endif
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

        while (!queue.empty()) {
            const auto& pending = queue.front();

            // Safe to destroy after N frames have passed
            if (currentFrame - pending.submittedFrame >= maxFramesInFlight) {
                pending.destructorFunc();
                queue.pop();
                destroyedThisFrame++;
            } else {
                break;  // Queue is ordered, stop checking
            }
        }

#ifdef _DEBUG
        if (destroyedThisFrame > 0) {
            totalDestroyed += destroyedThisFrame;
            // Optional: Log destruction activity
            // std::cout << "[DeferredDestruction] Destroyed " << destroyedThisFrame
            //           << " resources at frame " << currentFrame << std::endl;
        }
#endif
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

        while (!queue.empty()) {
            queue.front().destructorFunc();
            queue.pop();
            flushedCount++;
        }

#ifdef _DEBUG
        if (flushedCount > 0) {
            totalFlushed += flushedCount;
            // Optional: Log flush activity
            // std::cout << "[DeferredDestruction] Flushed " << flushedCount
            //           << " pending resources" << std::endl;
        }
#endif
    }

    /**
     * @brief Get number of pending destructions
     */
    size_t GetPendingCount() const {
        return queue.size();
    }

#ifdef _DEBUG
    /**
     * @brief Get statistics (debug builds only)
     */
    struct Stats {
        size_t totalQueued = 0;
        size_t totalDestroyed = 0;
        size_t totalFlushed = 0;
        size_t currentPending = 0;
    };

    Stats GetStats() const {
        Stats stats;
        stats.totalQueued = totalQueued;
        stats.totalDestroyed = totalDestroyed;
        stats.totalFlushed = totalFlushed;
        stats.currentPending = queue.size();
        return stats;
    }

    void ResetStats() {
        totalQueued = 0;
        totalDestroyed = 0;
        totalFlushed = 0;
    }
#endif

private:
    std::queue<PendingDestruction> queue;

#ifdef _DEBUG
    size_t totalQueued = 0;
    size_t totalDestroyed = 0;
    size_t totalFlushed = 0;
#endif
};

} // namespace ResourceManagement
