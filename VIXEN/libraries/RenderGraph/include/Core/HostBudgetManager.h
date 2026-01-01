#pragma once

#include "Core/ResourceBudgetManager.h"
#include <memory>
#include <span>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Allocation scope for host memory
 */
enum class AllocationScope : uint8_t {
    Frame,            // Reset every frame (uses frame stack arena)
    PersistentStack,  // Persists across frames (uses persistent stack arena)
    Heap              // Individual alloc/free (uses heap with budget tracking)
};

/**
 * @brief Source of a host allocation
 */
enum class AllocationSource : uint8_t {
    FrameStack,       // From frame stack arena
    PersistentStack,  // From persistent stack arena
    Heap              // From heap
};

/**
 * @brief Result of a host allocation request
 */
struct HostAllocation {
    void* data = nullptr;
    size_t size = 0;
    size_t alignment = 0;
    AllocationScope scope = AllocationScope::Frame;
    AllocationSource source = AllocationSource::FrameStack;

#ifdef _DEBUG
    uint64_t debugEpoch = 0;  // For use-after-reset detection
#endif

    explicit operator bool() const { return data != nullptr; }

    template<typename T>
    T* As() { return static_cast<T*>(data); }

    template<typename T>
    std::span<T> AsSpan() {
        return std::span<T>(static_cast<T*>(data), size / sizeof(T));
    }
};

/**
 * @brief Stack arena statistics
 */
struct StackArenaStats {
    size_t capacity = 0;          // Total arena size
    size_t used = 0;              // Current usage
    size_t peakUsage = 0;         // Peak usage this session
    uint32_t allocationCount = 0; // Allocations this frame
    uint32_t fallbackCount = 0;   // Allocations that fell back to heap
    float utilizationRatio = 0.0f; // used / capacity
};

/**
 * @brief Phase A.4: Host Budget Manager with Stack-First Allocation
 *
 * Specialized budget manager for CPU/host memory with stack-first allocation
 * strategy for optimal hot-path performance.
 *
 * Architecture:
 * - StackBudget: Pre-allocated arena with bump allocator (O(1) allocation)
 * - HeapBudget: Fallback for large or persistent allocations
 * - Per-frame reset for stack arena
 * - Automatic stack→heap fallback with tracking
 *
 * Design Goals:
 * - Hot path: 0 heap allocations in render loop
 * - Stack utilization: >90% before reset
 * - Fallback rate: <5% requests hit heap
 *
 * Thread-safe: Yes (internal synchronization)
 */
class HostBudgetManager {
public:
    /**
     * @brief Configuration for host budget manager
     */
    struct Config {
        size_t frameStackSize = 16 * 1024 * 1024;       // 16 MB frame stack (reset per frame)
        size_t persistentStackSize = 64 * 1024 * 1024;  // 64 MB persistent stack (never reset)
        size_t heapBudget = 256 * 1024 * 1024;          // 256 MB heap budget
        size_t heapWarningThreshold = 200 * 1024 * 1024;// Warn at 200 MB
        float fallbackWarningRatio = 0.05f;             // Warn if >5% fallback rate
        bool strictHeapBudget = false;                  // Fail allocations over heap budget
    };

    explicit HostBudgetManager(const Config& config = Config{});
    ~HostBudgetManager();

    // Non-copyable, non-movable (owns arena memory)
    HostBudgetManager(const HostBudgetManager&) = delete;
    HostBudgetManager& operator=(const HostBudgetManager&) = delete;

    // =========================================================================
    // Allocation Interface
    // =========================================================================

    /**
     * @brief Request memory allocation with automatic stack/heap selection
     *
     * Frame-scoped allocations use stack arena (fast, reset per frame).
     * Persistent allocations use heap tracking.
     *
     * @param size Allocation size in bytes
     * @param alignment Alignment requirement (default 16)
     * @param scope Frame or Persistent
     * @return Allocation result (check bool for success)
     */
    [[nodiscard]] HostAllocation Allocate(
        size_t size,
        size_t alignment = 16,
        AllocationScope scope = AllocationScope::Frame);

    /**
     * @brief Request typed allocation from frame stack arena
     *
     * @tparam T Type to allocate
     * @param count Number of elements
     * @return Pointer to allocated memory, or nullptr on failure
     */
    template<typename T>
    [[nodiscard]] T* AllocateFrame(size_t count = 1) {
        auto alloc = Allocate(sizeof(T) * count, alignof(T), AllocationScope::Frame);
        return alloc ? alloc.As<T>() : nullptr;
    }

    /**
     * @brief Request typed allocation from persistent stack arena
     *
     * Use for data that persists across frames (level geometry, caches, etc.)
     *
     * @tparam T Type to allocate
     * @param count Number of elements
     * @return Pointer to allocated memory, or nullptr on failure
     */
    template<typename T>
    [[nodiscard]] T* AllocatePersistent(size_t count = 1) {
        auto alloc = Allocate(sizeof(T) * count, alignof(T), AllocationScope::PersistentStack);
        return alloc ? alloc.As<T>() : nullptr;
    }

    /**
     * @brief Free a heap allocation
     *
     * Stack allocations (Frame/PersistentStack) don't need explicit free.
     * Only call for Heap scope allocations.
     */
    void Free(HostAllocation& allocation);

    // =========================================================================
    // Frame Management
    // =========================================================================

    /**
     * @brief Reset stack arena for new frame
     *
     * Call at start of each frame. Invalidates all Frame-scoped allocations.
     */
    void ResetFrame();

    /**
     * @brief Get current frame number
     */
    [[nodiscard]] uint64_t GetCurrentFrame() const { return frameNumber_; }

    // =========================================================================
    // Statistics & Monitoring
    // =========================================================================

    /**
     * @brief Get frame stack arena statistics
     */
    [[nodiscard]] StackArenaStats GetFrameStackStats() const;

    /**
     * @brief Get persistent stack arena statistics
     */
    [[nodiscard]] StackArenaStats GetPersistentStackStats() const;

    /**
     * @brief Get heap budget usage
     */
    [[nodiscard]] BudgetResourceUsage GetHeapUsage() const;

    /**
     * @brief Check if fallback rate exceeds warning threshold
     */
    [[nodiscard]] bool IsFallbackRateHigh() const;

    /**
     * @brief Get available frame stack space
     */
    [[nodiscard]] size_t GetAvailableFrameStackBytes() const;

    /**
     * @brief Get available persistent stack space
     */
    [[nodiscard]] size_t GetAvailablePersistentStackBytes() const;

    /**
     * @brief Get available heap budget
     */
    [[nodiscard]] size_t GetAvailableHeapBytes() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

    /**
     * @brief Resize frame stack arena (resets all frame allocations)
     *
     * Call between frames when arena needs resizing.
     */
    void ResizeFrameStack(size_t newSize);

    /**
     * @brief Reset persistent stack arena
     *
     * WARNING: Invalidates ALL persistent stack allocations!
     * Only call during level unload or major state transitions.
     */
    void ResetPersistentStack();

    // =========================================================================
    // Debug Validation (Debug builds only)
    // =========================================================================

#ifdef _DEBUG
    /**
     * @brief Validate allocation is still valid (not used after reset)
     *
     * Debug-only check that catches use-after-reset bugs.
     * Compiles to nothing in release builds.
     *
     * @param allocation The allocation to validate
     * @return true if valid, false if allocation was invalidated by reset
     */
    [[nodiscard]] bool IsValid(const HostAllocation& allocation) const;

    /**
     * @brief Assert allocation is valid, abort if not
     *
     * Use this in debug builds to catch use-after-reset bugs early.
     */
    void AssertValid(const HostAllocation& allocation) const;

    /**
     * @brief Get current frame epoch (for external validation)
     */
    [[nodiscard]] uint64_t GetFrameEpoch() const { return frameEpoch_.load(std::memory_order_acquire); }

    /**
     * @brief Get current persistent epoch (for external validation)
     */
    [[nodiscard]] uint64_t GetPersistentEpoch() const { return persistentEpoch_.load(std::memory_order_acquire); }
#endif

private:
    Config config_;

    // Frame stack arena (reset every frame)
    std::vector<uint8_t> frameStack_;
    std::atomic<size_t> frameStackOffset_{0};
    std::atomic<size_t> frameStackPeak_{0};
    std::atomic<uint32_t> frameStackAllocCount_{0};

    // Persistent stack arena (never auto-reset)
    std::vector<uint8_t> persistentStack_;
    std::atomic<size_t> persistentStackOffset_{0};
    std::atomic<size_t> persistentStackPeak_{0};
    std::atomic<uint32_t> persistentStackAllocCount_{0};

    // Fallback tracking
    std::atomic<uint32_t> fallbackCount_{0};

    // Heap budget tracking (delegates to ResourceBudgetManager)
    ResourceBudgetManager heapBudget_;

    // Frame tracking
    std::atomic<uint64_t> frameNumber_{0};

#ifdef _DEBUG
    // Epoch counters for use-after-reset detection (debug only)
    std::atomic<uint64_t> frameEpoch_{0};
    std::atomic<uint64_t> persistentEpoch_{0};
#endif

    // Mutex for arena resize
    mutable std::mutex arenaMutex_;

    // Internal helpers
    void* AllocateFromFrameStack(size_t size, size_t alignment);
    void* AllocateFromPersistentStack(size_t size, size_t alignment);
    void* AllocateFromHeap(size_t size);
    void UpdatePeakUsage(std::atomic<size_t>& peak, size_t currentUsage);
};

} // namespace Vixen::RenderGraph
