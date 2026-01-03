#pragma once

#include <vector>
#include <memory>
#include <stdexcept>
#include <cstddef>

namespace Vixen::EventBus {

/**
 * @brief Pre-allocated ring buffer queue for zero-allocation runtime operation
 *
 * Designed for EventBus message queue to prevent heap allocations during frame execution.
 * Pre-allocates storage during setup phase, then operates without allocation.
 *
 * Features:
 * - Pre-allocated fixed capacity (set via Reserve)
 * - Ring buffer semantics (O(1) push/pop)
 * - Automatic growth only if explicitly allowed
 * - Swap support for batch processing pattern
 *
 * Usage:
 * ```cpp
 * PreAllocatedQueue<std::unique_ptr<Message>> queue;
 * queue.Reserve(1024);  // Pre-allocate during setup
 *
 * // During frame (zero allocation):
 * queue.Push(std::move(msg));
 * auto& front = queue.Front();
 * queue.Pop();
 * ```
 *
 * @tparam T Element type (typically std::unique_ptr<BaseEventMessage>)
 */
template<typename T>
class PreAllocatedQueue {
public:
    PreAllocatedQueue() = default;

    explicit PreAllocatedQueue(size_t initialCapacity) {
        Reserve(initialCapacity);
    }

    /**
     * @brief Pre-allocate storage for expected maximum queue size
     *
     * Call during setup phase to ensure no allocations during runtime.
     * Can be called multiple times - will grow if needed.
     *
     * @param capacity Number of elements to pre-allocate
     */
    void Reserve(size_t capacity) {
        if (capacity <= buffer_.size()) {
            return;  // Already have enough capacity
        }

        // Create new buffer with requested capacity
        std::vector<T> newBuffer(capacity);

        // Move existing elements to new buffer
        size_t count = Size();
        for (size_t i = 0; i < count; ++i) {
            size_t oldIdx = (head_ + i) % buffer_.size();
            newBuffer[i] = std::move(buffer_[oldIdx]);
        }

        buffer_ = std::move(newBuffer);
        head_ = 0;
        tail_ = count;
    }

    /**
     * @brief Get current pre-allocated capacity
     */
    size_t Capacity() const noexcept {
        return buffer_.size();
    }

    /**
     * @brief Get number of elements currently in queue
     */
    size_t Size() const noexcept {
        return size_;
    }

    /**
     * @brief Check if queue is empty
     */
    bool Empty() const noexcept {
        return size_ == 0;
    }

    /**
     * @brief Check if queue is at capacity
     */
    bool Full() const noexcept {
        return size_ >= buffer_.size();
    }

    /**
     * @brief Push element to back of queue
     *
     * @param value Element to push (moved)
     * @return true if pushed successfully, false if queue full and growth disabled
     */
    bool Push(T value) {
        if (Full()) {
            if (!allowGrowth_) {
                return false;  // Queue full, cannot grow
            }
            // Auto-grow by doubling (only if allowed)
            size_t newCapacity = buffer_.empty() ? 16 : buffer_.size() * 2;
            Reserve(newCapacity);
            growthCount_++;
        }

        buffer_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % buffer_.size();
        size_++;

        // Track high-water mark
        if (size_ > maxSizeReached_) {
            maxSizeReached_ = size_;
        }

        return true;
    }

    /**
     * @brief Access front element (oldest)
     *
     * @throws std::runtime_error if queue is empty
     */
    T& Front() {
        if (Empty()) {
            throw std::runtime_error("PreAllocatedQueue::Front() called on empty queue");
        }
        return buffer_[head_];
    }

    const T& Front() const {
        if (Empty()) {
            throw std::runtime_error("PreAllocatedQueue::Front() called on empty queue");
        }
        return buffer_[head_];
    }

    /**
     * @brief Remove front element
     */
    void Pop() {
        if (Empty()) {
            return;
        }
        buffer_[head_] = T{};  // Clear the slot (important for unique_ptr)
        head_ = (head_ + 1) % buffer_.size();
        size_--;
    }

    /**
     * @brief Clear all elements without deallocating storage
     */
    void Clear() {
        while (!Empty()) {
            Pop();
        }
        // Reset indices but keep buffer allocated
        head_ = 0;
        tail_ = 0;
    }

    /**
     * @brief Swap contents with another queue
     *
     * Used for batch processing pattern where queue is swapped to local
     * for processing while new messages can be queued.
     */
    void Swap(PreAllocatedQueue& other) noexcept {
        buffer_.swap(other.buffer_);
        std::swap(head_, other.head_);
        std::swap(tail_, other.tail_);
        std::swap(size_, other.size_);
        std::swap(maxSizeReached_, other.maxSizeReached_);
        std::swap(growthCount_, other.growthCount_);
        std::swap(allowGrowth_, other.allowGrowth_);
    }

    /**
     * @brief Enable/disable automatic growth when full
     *
     * When disabled, Push() returns false if queue is full.
     * Default: enabled (for backward compatibility)
     */
    void SetAllowGrowth(bool allow) noexcept {
        allowGrowth_ = allow;
    }

    bool GetAllowGrowth() const noexcept {
        return allowGrowth_;
    }

    /**
     * @brief Get high-water mark (max size reached)
     */
    size_t GetMaxSizeReached() const noexcept {
        return maxSizeReached_;
    }

    /**
     * @brief Get number of times queue had to grow
     *
     * If this is > 0 after setup phase, Reserve() capacity was too small.
     */
    size_t GetGrowthCount() const noexcept {
        return growthCount_;
    }

    /**
     * @brief Reset statistics (growth count, max size)
     */
    void ResetStats() noexcept {
        maxSizeReached_ = size_;
        growthCount_ = 0;
    }

private:
    std::vector<T> buffer_;
    size_t head_ = 0;      // Index of front element
    size_t tail_ = 0;      // Index where next element will be inserted
    size_t size_ = 0;      // Current number of elements

    // Statistics
    size_t maxSizeReached_ = 0;
    size_t growthCount_ = 0;

    // Behavior
    bool allowGrowth_ = true;  // Allow auto-growth when full
};

} // namespace Vixen::EventBus
