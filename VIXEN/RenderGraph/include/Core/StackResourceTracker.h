#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VIXEN {

/**
 * @brief Stack allocation tracking and monitoring
 *
 * Tracks CPU stack usage per-frame to prevent stack overflow while
 * maximizing hot-path performance by avoiding heap allocations.
 *
 * Design Philosophy:
 * - Move predetermined-size resources to stack (VkWriteDescriptorSet arrays, etc.)
 * - Monitor stack usage per-frame to detect overuse
 * - Provide warnings when approaching stack limits
 * - Track allocation patterns for optimization opportunities
 */
class StackResourceTracker {
public:
    // Stack allocation limits
    static constexpr size_t MAX_STACK_PER_FRAME = 64 * 1024;      // 64KB safe limit
    static constexpr size_t WARNING_THRESHOLD = 48 * 1024;         // Warn at 75%
    static constexpr size_t CRITICAL_THRESHOLD = 56 * 1024;        // Critical at 87.5%

    struct StackAllocation {
        uint64_t resourceHash;           // Persistent hash for resource identification
        size_t sizeBytes;                // Size of allocation
        const void* stackAddress;        // Stack address for tracking
        uint32_t nodeId;                 // Node that made the allocation
        uint64_t frameNumber;            // Frame when allocated
    };

    struct FrameStackUsage {
        uint64_t frameNumber = 0;
        size_t totalStackUsed = 0;
        size_t peakStackUsed = 0;
        uint32_t allocationCount = 0;
        std::vector<StackAllocation> allocations;
    };

    StackResourceTracker() = default;

    /**
     * @brief Begin tracking for a new frame
     */
    void BeginFrame(uint64_t frameNumber);

    /**
     * @brief End frame tracking and report usage
     */
    void EndFrame();

    /**
     * @brief Register a stack allocation
     *
     * Call this at the start of Execute() when using stack arrays:
     *
     * @code
     * void ExecuteImpl(...) {
     *     uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, "writes");
     *     StackArray<VkWriteDescriptorSet, 32> writes;
     *     ctx.TrackStack(hash, writes.data(), writes.capacity_bytes());
     *     // ... use writes
     * }
     * @endcode
     */
    void TrackAllocation(
        uint64_t resourceHash,
        const void* stackAddress,
        size_t sizeBytes,
        uint32_t nodeId
    );

    /**
     * @brief Get current frame stack usage
     */
    const FrameStackUsage& GetCurrentFrameUsage() const { return currentFrame_; }

    /**
     * @brief Get historical frame usage (last N frames)
     */
    const std::vector<FrameStackUsage>& GetHistory() const { return history_; }

    /**
     * @brief Check if current usage exceeds threshold
     */
    bool IsOverWarningThreshold() const {
        return currentFrame_.totalStackUsed > WARNING_THRESHOLD;
    }

    bool IsOverCriticalThreshold() const {
        return currentFrame_.totalStackUsed > CRITICAL_THRESHOLD;
    }

    /**
     * @brief Get usage statistics
     */
    struct UsageStats {
        size_t averageStackPerFrame;
        size_t peakStackUsage;
        size_t minStackUsage;
        uint32_t framesTracked;
        uint32_t warningFrames;
        uint32_t criticalFrames;
    };
    UsageStats GetStats() const;

    /**
     * @brief Clear history (useful for profiling specific sections)
     */
    void ClearHistory();

private:
    FrameStackUsage currentFrame_;
    std::vector<FrameStackUsage> history_;
    static constexpr size_t MAX_HISTORY_FRAMES = 300;  // Keep last 300 frames (5s @ 60fps)

    void CheckThresholds();
    void LogWarning(const char* message) const;
};

/**
 * @brief Fixed-size stack-based array with bounds checking
 *
 * Replaces std::vector in hot paths with compile-time sized stack arrays.
 * Provides similar interface to std::array but with size tracking.
 *
 * Usage:
 * @code
 * // Instead of: std::vector<VkWriteDescriptorSet> writes;
 * StackArray<VkWriteDescriptorSet, 32> writes;
 *
 * writes.push_back(write1);
 * writes.push_back(write2);
 * // ... use like vector with bounds checking
 * @endcode
 */
template<typename T, size_t Capacity>
class StackArray {
public:
    StackArray() : size_(0) {}

    // STL-like interface
    T* data() { return data_; }
    const T* data() const { return data_; }

    size_t size() const { return size_; }
    size_t capacity() const { return Capacity; }
    size_t capacity_bytes() const { return Capacity * sizeof(T); }
    bool empty() const { return size_ == 0; }

    void push_back(const T& value) {
        if (size_ >= Capacity) {
            LogOverflow();
            return;  // Silent failure in release, assert in debug
        }
        data_[size_++] = value;
    }

    void push_back(T&& value) {
        if (size_ >= Capacity) {
            LogOverflow();
            return;
        }
        data_[size_++] = std::move(value);
    }

    void clear() { size_ = 0; }

    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }

    T& at(size_t index) {
        if (index >= size_) {
            LogOutOfBounds(index);
        }
        return data_[index];
    }

    // Iterator support
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    // Resize support (for compatibility with vector patterns)
    void resize(size_t newSize) {
        if (newSize > Capacity) {
            LogOverflow();
            newSize = Capacity;
        }
        size_ = newSize;
    }

    void reserve(size_t requestedCapacity) {
        // No-op for stack arrays (capacity is fixed)
        if (requestedCapacity > Capacity) {
            LogOverflow();
        }
    }

private:
    T data_[Capacity];
    size_t size_;

    void LogOverflow() const;
    void LogOutOfBounds(size_t index) const;
};

// Template method implementations
template<typename T, size_t Capacity>
void StackArray<T, Capacity>::LogOverflow() const {
    // In debug builds, assert. In release, log warning.
    #ifdef NDEBUG
        // TODO: Log to ResourceBudgetManager
    #else
        // Debug builds should assert
        assert(false && "StackArray overflow - increase Capacity or use heap allocation");
    #endif
}

template<typename T, size_t Capacity>
void StackArray<T, Capacity>::LogOutOfBounds(size_t index) const {
    #ifdef NDEBUG
        // TODO: Log to ResourceBudgetManager
    #else
        assert(false && "StackArray out of bounds access");
    #endif
}

/**
 * @brief RAII helper for automatic stack tracking
 *
 * Usage:
 * @code
 * void ExecuteImpl(Context& ctx) {
 *     uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, "writes");
 *     StackArray<VkWriteDescriptorSet, 32> writes;
 *     auto tracker = ctx.AutoTrackStack(hash, writes);
 *     // ... automatic tracking cleanup
 * }
 * @endcode
 */
class ScopedStackTracker {
public:
    ScopedStackTracker(
        StackResourceTracker& tracker,
        uint64_t resourceHash,
        const void* address,
        size_t size,
        uint32_t nodeId
    ) : tracker_(tracker) {
        tracker_.TrackAllocation(resourceHash, address, size, nodeId);
    }

    ~ScopedStackTracker() = default;

    ScopedStackTracker(const ScopedStackTracker&) = delete;
    ScopedStackTracker& operator=(const ScopedStackTracker&) = delete;

private:
    StackResourceTracker& tracker_;
};

}  // namespace VIXEN
