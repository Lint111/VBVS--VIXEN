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
        uint64_t resourceHash;           // Full hash (scope + member)
        uint64_t scopeHash;              // Scope hash (nodeInstance + bundle) for cleanup queries
        size_t sizeBytes;                // Size of allocation
        const void* stackAddress;        // Stack address for tracking
        uint32_t nodeId;                 // Node that made the allocation
        uint64_t frameNumber;            // Frame when allocated
        bool isTemporary;                // True for temporary allocations (auto-cleanup)
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
     *
     * @param resourceHash Full resource hash (scope + member)
     * @param scopeHash Scope hash (nodeInstance + bundle) for cleanup queries
     * @param stackAddress Pointer to stack allocation
     * @param sizeBytes Size of allocation in bytes
     * @param nodeId Node instance ID
     * @param isTemporary True if resource should be auto-cleaned at scope exit
     */
    void TrackAllocation(
        uint64_t resourceHash,
        uint64_t scopeHash,
        const void* stackAddress,
        size_t sizeBytes,
        uint32_t nodeId,
        bool isTemporary = false
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

    /**
     * @brief Release all temporary resources from a specific scope
     *
     * Automatically called at the end of Execute/Compile phases to reclaim
     * stack space from temporary allocations.
     *
     * @param scopeHash Scope hash (nodeInstance + bundle) identifying resources to release
     * @return Number of allocations released
     *
     * Example:
     * @code
     * // At end of ExecuteImpl
     * uint64_t scopeHash = ComputeScopeHash(GetInstanceId(), GetBundleIndex());
     * size_t released = tracker.ReleaseTemporaryResources(scopeHash);
     * @endcode
     */
    size_t ReleaseTemporaryResources(uint64_t scopeHash);

    /**
     * @brief Release a specific resource by its full hash
     *
     * Manual cleanup for specific resources before scope exit.
     *
     * @param resourceHash Full resource hash
     * @return True if resource was found and released
     */
    bool ReleaseResource(uint64_t resourceHash);

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
 *     uint64_t scopeHash = ComputeScopeHash(GetInstanceId(), 0);
 *     StackArray<VkWriteDescriptorSet, 32> writes;
 *     auto tracker = ctx.AutoTrackStack(hash, scopeHash, writes);
 *     // ... automatic tracking cleanup
 * }
 * @endcode
 */
class ScopedStackTracker {
public:
    ScopedStackTracker(
        StackResourceTracker& tracker,
        uint64_t resourceHash,
        uint64_t scopeHash,
        const void* address,
        size_t size,
        uint32_t nodeId,
        bool isTemporary = false
    ) : tracker_(tracker) {
        tracker_.TrackAllocation(resourceHash, scopeHash, address, size, nodeId, isTemporary);
    }

    ~ScopedStackTracker() = default;

    ScopedStackTracker(const ScopedStackTracker&) = delete;
    ScopedStackTracker& operator=(const ScopedStackTracker&) = delete;

private:
    StackResourceTracker& tracker_;
};

/**
 * @brief RAII helper for automatic cleanup of temporary resources
 *
 * Use at the start of Execute phases to automatically clean up temporary
 * resources when the scope exits.
 *
 * Example:
 * @code
 * void ExecuteImpl(TypedExecuteContext& ctx) {
 *     // Auto-cleanup on scope exit
 *     uint64_t scopeHash = ComputeScopeHash(GetInstanceId(), 0);
 *     TemporaryResourceScope autoCleanup(GetBudgetManager()->GetStackTracker(), scopeHash);
 *
 *     // Request temporary resources
 *     uint64_t hash = ctx.GetMemberHash("tempCmdBuffer");
 *     auto cmdBuf = ctx.RequestStackResource<VkCommandBuffer, 1>(hash);
 *
 *     // ... use resources ...
 *     // Automatically cleaned up when autoCleanup goes out of scope
 * }
 * @endcode
 */
class TemporaryResourceScope {
public:
    TemporaryResourceScope(StackResourceTracker& tracker, uint64_t scopeHash)
        : tracker_(tracker), scopeHash_(scopeHash) {}

    ~TemporaryResourceScope() {
        tracker_.ReleaseTemporaryResources(scopeHash_);
    }

    // Non-copyable, non-movable
    TemporaryResourceScope(const TemporaryResourceScope&) = delete;
    TemporaryResourceScope& operator=(const TemporaryResourceScope&) = delete;
    TemporaryResourceScope(TemporaryResourceScope&&) = delete;
    TemporaryResourceScope& operator=(TemporaryResourceScope&&) = delete;

private:
    StackResourceTracker& tracker_;
    uint64_t scopeHash_;
};

}  // namespace VIXEN
