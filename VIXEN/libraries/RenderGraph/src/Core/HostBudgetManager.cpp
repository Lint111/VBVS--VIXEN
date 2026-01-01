#include "Core/HostBudgetManager.h"
#include <cassert>
#include <cstring>

namespace Vixen::RenderGraph {

HostBudgetManager::HostBudgetManager(const Config& config)
    : config_(config)
{
    // Pre-allocate both stack arenas
    frameStack_.resize(config_.frameStackSize);
    persistentStack_.resize(config_.persistentStackSize);

    // Configure heap budget
    ResourceBudget heapBudgetConfig(
        config_.heapBudget,
        config_.heapWarningThreshold,
        config_.strictHeapBudget
    );
    heapBudget_.SetBudget(BudgetResourceType::HostMemory, heapBudgetConfig);
}

HostBudgetManager::~HostBudgetManager() {
    // Stack arenas are automatically freed via vector destructor
    // Heap allocations should have been freed by callers
}

HostAllocation HostBudgetManager::Allocate(
    size_t size,
    size_t alignment,
    AllocationScope scope)
{
    if (size == 0) {
        return HostAllocation{};
    }

    HostAllocation result{};
    result.size = size;
    result.alignment = alignment;
    result.scope = scope;

    switch (scope) {
        case AllocationScope::Frame: {
            void* ptr = AllocateFromFrameStack(size, alignment);
            if (ptr) {
                result.data = ptr;
                result.source = AllocationSource::FrameStack;
#ifdef _DEBUG
                result.debugEpoch = frameEpoch_.load(std::memory_order_acquire);
#endif
                return result;
            }
            // Frame stack full - fall back to heap
            fallbackCount_.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        case AllocationScope::PersistentStack: {
            void* ptr = AllocateFromPersistentStack(size, alignment);
            if (ptr) {
                result.data = ptr;
                result.source = AllocationSource::PersistentStack;
#ifdef _DEBUG
                result.debugEpoch = persistentEpoch_.load(std::memory_order_acquire);
#endif
                return result;
            }
            // Persistent stack full - fall back to heap
            fallbackCount_.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        case AllocationScope::Heap:
            // Direct heap allocation
            break;
    }

    // Heap allocation (either requested or stack fallback)
    void* ptr = AllocateFromHeap(size);
    if (ptr) {
        result.data = ptr;
        result.source = AllocationSource::Heap;
    }

    return result;
}

void HostBudgetManager::Free(HostAllocation& allocation) {
    if (!allocation.data) {
        return;
    }

    // Only heap allocations can be individually freed
    if (allocation.source == AllocationSource::Heap) {
        std::free(allocation.data);
        heapBudget_.RecordDeallocation(BudgetResourceType::HostMemory, allocation.size);
    }
    // Stack allocations are freed in bulk via Reset methods

    allocation.data = nullptr;
}

void HostBudgetManager::ResetFrame() {
    // Reset frame stack arena (O(1) - just reset offset)
    frameStackOffset_.store(0, std::memory_order_release);
    frameStackAllocCount_.store(0, std::memory_order_relaxed);
    fallbackCount_.store(0, std::memory_order_relaxed);

    // Increment frame counter
    frameNumber_.fetch_add(1, std::memory_order_relaxed);

#ifdef _DEBUG
    // Increment epoch to invalidate previous frame allocations
    frameEpoch_.fetch_add(1, std::memory_order_release);
#endif
}

void HostBudgetManager::ResetPersistentStack() {
    std::lock_guard<std::mutex> lock(arenaMutex_);
    persistentStackOffset_.store(0, std::memory_order_release);
    persistentStackPeak_.store(0, std::memory_order_release);
    persistentStackAllocCount_.store(0, std::memory_order_relaxed);

#ifdef _DEBUG
    // Increment epoch to invalidate all persistent allocations
    persistentEpoch_.fetch_add(1, std::memory_order_release);
#endif
}

StackArenaStats HostBudgetManager::GetFrameStackStats() const {
    StackArenaStats stats{};
    stats.capacity = frameStack_.size();
    stats.used = frameStackOffset_.load(std::memory_order_acquire);
    stats.peakUsage = frameStackPeak_.load(std::memory_order_acquire);
    stats.allocationCount = frameStackAllocCount_.load(std::memory_order_relaxed);
    stats.fallbackCount = fallbackCount_.load(std::memory_order_relaxed);

    if (stats.capacity > 0) {
        stats.utilizationRatio = static_cast<float>(stats.used) /
                                  static_cast<float>(stats.capacity);
    }

    return stats;
}

StackArenaStats HostBudgetManager::GetPersistentStackStats() const {
    StackArenaStats stats{};
    stats.capacity = persistentStack_.size();
    stats.used = persistentStackOffset_.load(std::memory_order_acquire);
    stats.peakUsage = persistentStackPeak_.load(std::memory_order_acquire);
    stats.allocationCount = persistentStackAllocCount_.load(std::memory_order_relaxed);
    stats.fallbackCount = 0;  // Persistent stack doesn't track fallbacks separately

    if (stats.capacity > 0) {
        stats.utilizationRatio = static_cast<float>(stats.used) /
                                  static_cast<float>(stats.capacity);
    }

    return stats;
}

BudgetResourceUsage HostBudgetManager::GetHeapUsage() const {
    return heapBudget_.GetUsage(BudgetResourceType::HostMemory);
}

bool HostBudgetManager::IsFallbackRateHigh() const {
    uint32_t frameCount = frameStackAllocCount_.load(std::memory_order_relaxed);
    uint32_t fallbackCount = fallbackCount_.load(std::memory_order_relaxed);

    if (frameCount == 0) {
        return false;
    }

    float ratio = static_cast<float>(fallbackCount) /
                  static_cast<float>(frameCount + fallbackCount);
    return ratio > config_.fallbackWarningRatio;
}

size_t HostBudgetManager::GetAvailableFrameStackBytes() const {
    size_t used = frameStackOffset_.load(std::memory_order_acquire);
    return (used < frameStack_.size()) ? (frameStack_.size() - used) : 0;
}

size_t HostBudgetManager::GetAvailablePersistentStackBytes() const {
    size_t used = persistentStackOffset_.load(std::memory_order_acquire);
    return (used < persistentStack_.size()) ? (persistentStack_.size() - used) : 0;
}

size_t HostBudgetManager::GetAvailableHeapBytes() const {
    return heapBudget_.GetAvailableBytes(BudgetResourceType::HostMemory);
}

void HostBudgetManager::ResizeFrameStack(size_t newSize) {
    std::lock_guard<std::mutex> lock(arenaMutex_);

    // Reset offset before resize
    frameStackOffset_.store(0, std::memory_order_release);
    frameStackPeak_.store(0, std::memory_order_release);

    // Resize arena
    frameStack_.resize(newSize);
    config_.frameStackSize = newSize;
}

void* HostBudgetManager::AllocateFromFrameStack(size_t size, size_t alignment) {
    // Bump allocator with alignment
    size_t currentOffset = frameStackOffset_.load(std::memory_order_acquire);

    while (true) {
        // Calculate aligned offset
        size_t alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
        size_t newOffset = alignedOffset + size;

        // Check if fits in arena
        if (newOffset > frameStack_.size()) {
            return nullptr;  // Stack full
        }

        // Try to claim this space atomically
        if (frameStackOffset_.compare_exchange_weak(
                currentOffset, newOffset,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            // Success - update stats and return
            frameStackAllocCount_.fetch_add(1, std::memory_order_relaxed);
            UpdatePeakUsage(frameStackPeak_, newOffset);
            return frameStack_.data() + alignedOffset;
        }

        // CAS failed, currentOffset was updated, retry
    }
}

void* HostBudgetManager::AllocateFromPersistentStack(size_t size, size_t alignment) {
    // Bump allocator with alignment
    size_t currentOffset = persistentStackOffset_.load(std::memory_order_acquire);

    while (true) {
        // Calculate aligned offset
        size_t alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
        size_t newOffset = alignedOffset + size;

        // Check if fits in arena
        if (newOffset > persistentStack_.size()) {
            return nullptr;  // Stack full
        }

        // Try to claim this space atomically
        if (persistentStackOffset_.compare_exchange_weak(
                currentOffset, newOffset,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            // Success - update stats and return
            persistentStackAllocCount_.fetch_add(1, std::memory_order_relaxed);
            UpdatePeakUsage(persistentStackPeak_, newOffset);
            return persistentStack_.data() + alignedOffset;
        }

        // CAS failed, currentOffset was updated, retry
    }
}

void* HostBudgetManager::AllocateFromHeap(size_t size) {
    // Check budget
    if (!heapBudget_.TryAllocate(BudgetResourceType::HostMemory, size)) {
        return nullptr;
    }

    // Allocate from system heap
    void* ptr = std::malloc(size);
    if (ptr) {
        heapBudget_.RecordAllocation(BudgetResourceType::HostMemory, size);
    }

    return ptr;
}

void HostBudgetManager::UpdatePeakUsage(std::atomic<size_t>& peak, size_t currentUsage) {
    size_t peakVal = peak.load(std::memory_order_relaxed);
    while (currentUsage > peakVal &&
           !peak.compare_exchange_weak(
               peakVal, currentUsage,
               std::memory_order_release,
               std::memory_order_relaxed)) {
        // peakVal updated by CAS, retry if still greater
    }
}

#ifdef _DEBUG
bool HostBudgetManager::IsValid(const HostAllocation& allocation) const {
    if (!allocation.data) {
        return false;
    }

    switch (allocation.source) {
        case AllocationSource::FrameStack:
            return allocation.debugEpoch == frameEpoch_.load(std::memory_order_acquire);
        case AllocationSource::PersistentStack:
            return allocation.debugEpoch == persistentEpoch_.load(std::memory_order_acquire);
        case AllocationSource::Heap:
            return true;  // Heap allocations are always valid until freed
    }
    return false;
}

void HostBudgetManager::AssertValid(const HostAllocation& allocation) const {
    assert(IsValid(allocation) &&
           "HostAllocation used after reset! "
           "Frame allocations are invalidated by ResetFrame(), "
           "persistent allocations by ResetPersistentStack().");
}
#endif

} // namespace Vixen::RenderGraph
