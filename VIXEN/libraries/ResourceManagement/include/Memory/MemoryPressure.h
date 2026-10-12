#pragma once

// R516 part 2 (gpuharden): memory-pressure classification and the defined degrade ladder.
//
// The live VK_EXT_memory_budget figures are what the OS is willing to give THIS process right now:
// another process on the GPU (a concurrent capture, the compositor) shrinks heapBudget without this
// process allocating anything. The pure functions below turn (budget, usage, request) into a typed
// decision so allocation never reaches vkAllocateMemory just to receive VK_ERROR_OUT_OF_DEVICE_MEMORY.

#include <cstdint>

#include <vulkan/vulkan.h>

namespace ResourceManagement {

enum class MemoryPressure : uint8_t {
    Normal,     ///< Below 75% of the live budget.
    Elevated,   ///< 75-90%: trim caches, halve the staging quota.
    Critical,   ///< 90-100%: additionally refuse optional allocations (staging reservations).
    Exhausted,  ///< At or over the budget: required allocations fail with a typed OverBudget.
};

enum class DegradeAction : uint8_t {
    None,             ///< Allocate normally.
    TrimCaches,       ///< Allocate, and ask caches/pools to release what they can.
    RefuseOptional,   ///< Allocate required resources; refuse optional ones.
    FailAllocation,   ///< Do not allocate: return AllocationError::OverBudget.
};

[[nodiscard]] constexpr const char* MemoryPressureName(MemoryPressure p) {
    switch (p) {
        case MemoryPressure::Normal: return "normal";
        case MemoryPressure::Elevated: return "elevated";
        case MemoryPressure::Critical: return "critical";
        case MemoryPressure::Exhausted: return "exhausted";
    }
    return "normal";
}

/// Pressure of `usage` against `budget`. A zero budget means "unknown": Normal.
[[nodiscard]] constexpr MemoryPressure ClassifyMemoryPressure(uint64_t budget, uint64_t usage) {
    if (budget == 0) return MemoryPressure::Normal;
    if (usage >= budget) return MemoryPressure::Exhausted;
    const uint64_t headroom = budget - usage;
    if (headroom * 10 <= budget) return MemoryPressure::Critical;        // >= 90% used
    if (headroom * 4 <= budget) return MemoryPressure::Elevated;         // >= 75% used
    return MemoryPressure::Normal;
}

/// Decision for one allocation of `requestBytes` under the live budget. `optional` marks work that can
/// be skipped (prefetch, staging for a non-essential upload); required allocations degrade later.
[[nodiscard]] constexpr DegradeAction ChooseDegradeAction(
    uint64_t budget, uint64_t usage, uint64_t requestBytes, bool optional) {
    if (budget == 0) return DegradeAction::None;
    const uint64_t projected = usage + requestBytes;
    if (projected > budget) return DegradeAction::FailAllocation;
    const MemoryPressure after = ClassifyMemoryPressure(budget, projected);
    if (after == MemoryPressure::Critical || after == MemoryPressure::Exhausted) {
        return optional ? DegradeAction::RefuseOptional : DegradeAction::TrimCaches;
    }
    if (after == MemoryPressure::Elevated) return DegradeAction::TrimCaches;
    return DegradeAction::None;
}

/// Device-local heap totals as reported by the driver.
struct LiveDeviceBudget {
    uint64_t budgetBytes = 0;   ///< Sum of heapBudget over device-local heaps (0 when unknown).
    uint64_t usageBytes = 0;    ///< Sum of heapUsage over device-local heaps.
    bool fromExtension = false; ///< True when VK_EXT_memory_budget supplied the numbers.
};

/// Reads the live device-local budget. `extensionEnabled` must be true only if VK_EXT_memory_budget
/// was enabled at vkCreateDevice; otherwise the result is the static heap size (fromExtension=false).
[[nodiscard]] LiveDeviceBudget QueryLiveDeviceBudget(VkPhysicalDevice physicalDevice, bool extensionEnabled);

}  // namespace ResourceManagement
