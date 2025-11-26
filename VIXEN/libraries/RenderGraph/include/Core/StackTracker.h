#pragma once

#include "VulkanLimits.h"
#include <atomic>
#include <string_view>
#include <array>
#include <cassert>

namespace Vixen::RenderGraph {

/**
 * @file StackTracker.h
 * @brief Debug utility for monitoring stack allocation usage
 *
 * Tracks cumulative stack allocations to prevent stack overflow when
 * replacing heap allocations (std::vector) with stack allocations (std::array).
 *
 * Features:
 * - Thread-safe tracking using thread_local storage
 * - Scope-based RAII allocation tracking
 * - Configurable warning/error thresholds
 * - Zero overhead in release builds (all tracking disabled)
 * - Human-readable size formatting
 * - Per-frame statistics
 *
 * Usage:
 * @code
 * void MyFunction() {
 *     TRACK_STACK_ALLOCATION("MyFunction:tempBuffer", sizeof(std::array<VkImageView, 4>));
 *     std::array<VkImageView, 4> views;
 *     // ... use views ...
 * }
 * @endcode
 *
 * Or use automatic scope tracking:
 * @code
 * void MyFunction() {
 *     std::array<VkImageView, 4> views;
 *     TRACK_STACK_ARRAY(views, "MyFunction:tempBuffer");
 *     // ... use views ...
 * }
 * @endcode
 */

// ============================================================================
// DEBUG BUILD TRACKING
// ============================================================================

#ifdef NDEBUG
    // Release build: no tracking overhead
    #define STACK_TRACKER_ENABLED 0
#else
    // Debug build: enable tracking
    #define STACK_TRACKER_ENABLED 1
#endif

class StackTracker {
public:
    /**
     * @brief Get the thread-local tracker instance
     */
    static StackTracker& Instance() {
        static thread_local StackTracker instance;
        return instance;
    }

    /**
     * @brief Record a stack allocation
     * @param name Human-readable name for the allocation
     * @param size Size in bytes
     */
    void Allocate(std::string_view name, size_t size) {
        if constexpr (!STACK_TRACKER_ENABLED) return;

        currentUsage += size;
        peakUsage = std::max(peakUsage, currentUsage);
        allocationCount++;

        // Record allocation for debugging
        if (recordIndex < MAX_RECORDED_ALLOCATIONS) {
            allocations[recordIndex] = AllocationRecord{name, size, currentUsage};
            recordIndex++;
        }

        // Check thresholds
        if (currentUsage >= STACK_CRITICAL_THRESHOLD) {
            OnCriticalThreshold(name, size);
        } else if (currentUsage >= STACK_WARNING_THRESHOLD) {
            OnWarningThreshold(name, size);
        }
    }

    /**
     * @brief Record a stack deallocation
     * @param size Size in bytes (must match previous Allocate call)
     */
    void Deallocate(size_t size) {
        if constexpr (!STACK_TRACKER_ENABLED) return;

        assert(currentUsage >= size && "Stack underflow detected!");
        currentUsage -= size;
    }

    /**
     * @brief Get current stack usage in bytes
     */
    size_t GetCurrentUsage() const { return currentUsage; }

    /**
     * @brief Get peak stack usage in bytes (this frame)
     */
    size_t GetPeakUsage() const { return peakUsage; }

    /**
     * @brief Get total number of allocations tracked
     */
    size_t GetAllocationCount() const { return allocationCount; }

    /**
     * @brief Reset frame statistics (call at end of frame)
     */
    void ResetFrame() {
        if constexpr (!STACK_TRACKER_ENABLED) return;

        // Update lifetime stats
        lifetimePeakUsage = std::max(lifetimePeakUsage, peakUsage);
        lifetimeAllocationCount += allocationCount;
        frameCount++;

        // Reset frame stats
        currentUsage = 0;
        peakUsage = 0;
        allocationCount = 0;
        recordIndex = 0;
    }

    /**
     * @brief Get lifetime peak stack usage
     */
    size_t GetLifetimePeakUsage() const { return lifetimePeakUsage; }

    /**
     * @brief Get total frames tracked
     */
    size_t GetFrameCount() const { return frameCount; }

    /**
     * @brief Print statistics to logger
     */
    void PrintStatistics() const;

    /**
     * @brief Print detailed allocation records
     */
    void PrintAllocations() const;

    /**
     * @brief Format bytes as human-readable string (e.g., "1.5 KB")
     */
    static std::string FormatBytes(size_t bytes);

private:
    StackTracker() = default;
    ~StackTracker() = default;

    // Non-copyable, non-movable
    StackTracker(const StackTracker&) = delete;
    StackTracker& operator=(const StackTracker&) = delete;

    void OnWarningThreshold(std::string_view name, size_t size);
    void OnCriticalThreshold(std::string_view name, size_t size);

    // ========================================================================
    // TRACKING DATA
    // ========================================================================

    // Current frame statistics
    size_t currentUsage = 0;
    size_t peakUsage = 0;
    size_t allocationCount = 0;

    // Lifetime statistics
    size_t lifetimePeakUsage = 0;
    size_t lifetimeAllocationCount = 0;
    size_t frameCount = 0;

    // Allocation recording (for debugging)
    static constexpr size_t MAX_RECORDED_ALLOCATIONS = 256;
    struct AllocationRecord {
        std::string_view name;
        size_t size;
        size_t cumulativeSize;
    };
    std::array<AllocationRecord, MAX_RECORDED_ALLOCATIONS> allocations;
    size_t recordIndex = 0;
};

// ============================================================================
// RAII SCOPE TRACKER
// ============================================================================

/**
 * @brief RAII helper for automatic allocation/deallocation tracking
 */
class ScopedStackAllocation {
public:
    ScopedStackAllocation(std::string_view name, size_t size)
        : size(size) {
        if constexpr (STACK_TRACKER_ENABLED) {
            StackTracker::Instance().Allocate(name, size);
        }
    }

    ~ScopedStackAllocation() {
        if constexpr (STACK_TRACKER_ENABLED) {
            StackTracker::Instance().Deallocate(size);
        }
    }

    // Non-copyable, non-movable
    ScopedStackAllocation(const ScopedStackAllocation&) = delete;
    ScopedStackAllocation& operator=(const ScopedStackAllocation&) = delete;

private:
    size_t size;
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#if STACK_TRACKER_ENABLED

/**
 * @brief Track a stack allocation with automatic cleanup
 * @param name String literal describing the allocation
 * @param size Size in bytes
 *
 * Usage:
 * @code
 * TRACK_STACK_ALLOCATION("MyArray", sizeof(std::array<int, 100>));
 * @endcode
 */
#define TRACK_STACK_ALLOCATION(name, size) \
    ::Vixen::RenderGraph::ScopedStackAllocation CONCAT_IMPL(__stack_track_, __LINE__)(name, size)

/**
 * @brief Track a stack-allocated array with automatic size detection
 * @param array The array variable (std::array)
 * @param name String literal describing the allocation
 *
 * Usage:
 * @code
 * std::array<VkImageView, 4> views;
 * TRACK_STACK_ARRAY(views, "swapchain views");
 * @endcode
 */
#define TRACK_STACK_ARRAY(array, name) \
    ::Vixen::RenderGraph::ScopedStackAllocation CONCAT_IMPL(__stack_track_, __LINE__)(name, sizeof(array))

/**
 * @brief Print stack tracker statistics (call at end of frame)
 */
#define STACK_TRACKER_PRINT_STATS() \
    ::Vixen::RenderGraph::StackTracker::Instance().PrintStatistics()

/**
 * @brief Reset stack tracker for new frame
 */
#define STACK_TRACKER_RESET_FRAME() \
    ::Vixen::RenderGraph::StackTracker::Instance().ResetFrame()

#else
    // Release build: no-ops
    #define TRACK_STACK_ALLOCATION(name, size) (void)0
    #define TRACK_STACK_ARRAY(array, name) (void)0
    #define STACK_TRACKER_PRINT_STATS() (void)0
    #define STACK_TRACKER_RESET_FRAME() (void)0
#endif

// Helper for macro concatenation
#define CONCAT_IMPL(a, b) a##b

} // namespace Vixen::RenderGraph
