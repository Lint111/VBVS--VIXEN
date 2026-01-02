#include "Memory/ResourceBudgetManager.h"
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace ResourceManagement {

// Budget configuration
void ResourceBudgetManager::SetBudget(BudgetResourceType type, const ResourceBudget& budget) {
    std::unique_lock lock(mutex_);
    budgets_[type] = budget;
}

void ResourceBudgetManager::SetBudget(const std::string& customType, const ResourceBudget& budget) {
    std::unique_lock lock(mutex_);
    customBudgets_[customType] = budget;
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(BudgetResourceType type) const {
    std::shared_lock lock(mutex_);
    auto it = budgets_.find(type);
    if (it != budgets_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(const std::string& customType) const {
    std::shared_lock lock(mutex_);
    auto it = customBudgets_.find(customType);
    if (it != customBudgets_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Helper to get or create usage entry (must hold exclusive lock)
AtomicResourceUsage* ResourceBudgetManager::GetOrCreateUsage(BudgetResourceType type) {
    auto it = usage_.find(type);
    if (it == usage_.end()) {
        it = usage_.emplace(type, AtomicResourceUsage{}).first;
    }
    return &it->second;
}

AtomicResourceUsage* ResourceBudgetManager::GetOrCreateUsage(const std::string& customType) {
    auto it = customUsage_.find(customType);
    if (it == customUsage_.end()) {
        it = customUsage_.emplace(customType, AtomicResourceUsage{}).first;
    }
    return &it->second;
}

// Allocation attempts (check if allocation would succeed)
bool ResourceBudgetManager::TryAllocate(BudgetResourceType type, uint64_t bytes) {
    std::unique_lock lock(mutex_);

    auto budgetIt = budgets_.find(type);
    const ResourceBudget* budget = (budgetIt != budgets_.end()) ? &budgetIt->second : nullptr;
    AtomicResourceUsage* usage = GetOrCreateUsage(type);

    return TryAllocateImpl(budget, usage, bytes);
}

bool ResourceBudgetManager::TryAllocate(const std::string& customType, uint64_t bytes) {
    std::unique_lock lock(mutex_);

    auto budgetIt = customBudgets_.find(customType);
    const ResourceBudget* budget = (budgetIt != customBudgets_.end()) ? &budgetIt->second : nullptr;
    AtomicResourceUsage* usage = GetOrCreateUsage(customType);

    return TryAllocateImpl(budget, usage, bytes);
}

// Record actual allocations (lock-free atomic updates after map lookup)
void ResourceBudgetManager::RecordAllocation(BudgetResourceType type, uint64_t bytes) {
    AtomicResourceUsage* usage = nullptr;
    {
        std::unique_lock lock(mutex_);
        usage = GetOrCreateUsage(type);
    }
    RecordAllocationImpl(usage, bytes);
}

void ResourceBudgetManager::RecordAllocation(const std::string& customType, uint64_t bytes) {
    AtomicResourceUsage* usage = nullptr;
    {
        std::unique_lock lock(mutex_);
        usage = GetOrCreateUsage(customType);
    }
    RecordAllocationImpl(usage, bytes);
}

// Record deallocations
void ResourceBudgetManager::RecordDeallocation(BudgetResourceType type, uint64_t bytes) {
    std::shared_lock lock(mutex_);
    auto it = usage_.find(type);
    if (it != usage_.end()) {
        RecordDeallocationImpl(&it->second, bytes);
    }
}

void ResourceBudgetManager::RecordDeallocation(const std::string& customType, uint64_t bytes) {
    std::shared_lock lock(mutex_);
    auto it = customUsage_.find(customType);
    if (it != customUsage_.end()) {
        RecordDeallocationImpl(&it->second, bytes);
    }
}

// Query current state (read lock + atomic read)
BudgetResourceUsage ResourceBudgetManager::GetUsage(BudgetResourceType type) const {
    std::shared_lock lock(mutex_);
    auto it = usage_.find(type);
    return (it != usage_.end()) ? it->second.ToUsage() : BudgetResourceUsage{};
}

BudgetResourceUsage ResourceBudgetManager::GetUsage(const std::string& customType) const {
    std::shared_lock lock(mutex_);
    auto it = customUsage_.find(customType);
    return (it != customUsage_.end()) ? it->second.ToUsage() : BudgetResourceUsage{};
}

uint64_t ResourceBudgetManager::GetAvailableBytes(BudgetResourceType type) const {
    std::shared_lock lock(mutex_);

    auto budgetIt = budgets_.find(type);
    auto usageIt = usage_.find(type);

    if (budgetIt == budgets_.end() || budgetIt->second.maxBytes == 0) {
        return (std::numeric_limits<uint64_t>::max)(); // Unlimited
    }

    uint64_t current = (usageIt != usage_.end())
        ? usageIt->second.currentBytes.load(std::memory_order_acquire) : 0;
    uint64_t max = budgetIt->second.maxBytes;

    return (current < max) ? (max - current) : 0;
}

uint64_t ResourceBudgetManager::GetAvailableBytes(const std::string& customType) const {
    std::shared_lock lock(mutex_);

    auto budgetIt = customBudgets_.find(customType);
    auto usageIt = customUsage_.find(customType);

    if (budgetIt == customBudgets_.end() || budgetIt->second.maxBytes == 0) {
        return (std::numeric_limits<uint64_t>::max)(); // Unlimited
    }

    uint64_t current = (usageIt != customUsage_.end())
        ? usageIt->second.currentBytes.load(std::memory_order_acquire) : 0;
    uint64_t max = budgetIt->second.maxBytes;

    return (current < max) ? (max - current) : 0;
}

bool ResourceBudgetManager::IsOverBudget(BudgetResourceType type) const {
    return GetAvailableBytes(type) == 0;
}

bool ResourceBudgetManager::IsOverBudget(const std::string& customType) const {
    return GetAvailableBytes(customType) == 0;
}

bool ResourceBudgetManager::IsNearWarningThreshold(BudgetResourceType type) const {
    std::shared_lock lock(mutex_);

    auto budgetIt = budgets_.find(type);
    auto usageIt = usage_.find(type);

    if (budgetIt == budgets_.end() || budgetIt->second.warningThreshold == 0) {
        return false; // No warning threshold set
    }

    uint64_t current = (usageIt != usage_.end())
        ? usageIt->second.currentBytes.load(std::memory_order_acquire) : 0;
    return current >= budgetIt->second.warningThreshold;
}

bool ResourceBudgetManager::IsNearWarningThreshold(const std::string& customType) const {
    std::shared_lock lock(mutex_);

    auto budgetIt = customBudgets_.find(customType);
    auto usageIt = customUsage_.find(customType);

    if (budgetIt == customBudgets_.end() || budgetIt->second.warningThreshold == 0) {
        return false; // No warning threshold set
    }

    uint64_t current = (usageIt != customUsage_.end())
        ? usageIt->second.currentBytes.load(std::memory_order_acquire) : 0;
    return current >= budgetIt->second.warningThreshold;
}

// System memory detection
uint64_t ResourceBudgetManager::DetectHostMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<uint64_t>(memInfo.ullTotalPhys);
    }
    return 0;
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.totalram) * si.mem_unit;
    }
    return 0;
#endif
}

uint64_t ResourceBudgetManager::DetectDeviceMemoryBytes(VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) {
        return 0;
    }

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    uint64_t totalDeviceMemory = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalDeviceMemory += memProps.memoryHeaps[i].size;
        }
    }

    return totalDeviceMemory;
}

// Utilities
void ResourceBudgetManager::Reset() {
    std::unique_lock lock(mutex_);
    budgets_.clear();
    usage_.clear();
    customBudgets_.clear();
    customUsage_.clear();
}

void ResourceBudgetManager::ResetUsage(BudgetResourceType type) {
    std::shared_lock lock(mutex_);
    auto it = usage_.find(type);
    if (it != usage_.end()) {
        it->second.Reset();  // Atomic reset
    }
}

void ResourceBudgetManager::ResetUsage(const std::string& customType) {
    std::shared_lock lock(mutex_);
    auto it = customUsage_.find(customType);
    if (it != customUsage_.end()) {
        it->second.Reset();  // Atomic reset
    }
}

// Internal helpers
bool ResourceBudgetManager::TryAllocateImpl(const ResourceBudget* budget, AtomicResourceUsage* usage, uint64_t bytes) {
    if (!budget || budget->maxBytes == 0) {
        return true; // No budget limit - allow
    }

    uint64_t current = usage->currentBytes.load(std::memory_order_acquire);
    uint64_t newTotal = current + bytes;

    if (budget->strict && newTotal > budget->maxBytes) {
        return false; // Strict mode - reject allocation over budget
    }

    return true; // Allow allocation (may exceed budget if not strict)
}

void ResourceBudgetManager::RecordAllocationImpl(AtomicResourceUsage* usage, uint64_t bytes) {
    // Atomic add for current bytes
    uint64_t oldValue = usage->currentBytes.fetch_add(bytes, std::memory_order_acq_rel);
    uint64_t newValue = oldValue + bytes;

    // Update peak using CAS loop
    uint64_t peak = usage->peakBytes.load(std::memory_order_relaxed);
    while (newValue > peak &&
           !usage->peakBytes.compare_exchange_weak(peak, newValue,
               std::memory_order_release, std::memory_order_relaxed)) {
        // peak updated by CAS, retry if still greater
    }

    usage->allocationCount.fetch_add(1, std::memory_order_relaxed);
}

void ResourceBudgetManager::RecordDeallocationImpl(AtomicResourceUsage* usage, uint64_t bytes) {
    // Atomic subtract with underflow protection
    uint64_t current = usage->currentBytes.load(std::memory_order_acquire);
    uint64_t newValue;

    do {
        newValue = (current >= bytes) ? (current - bytes) : 0;
    } while (!usage->currentBytes.compare_exchange_weak(current, newValue,
                std::memory_order_release, std::memory_order_acquire));

    // Decrement allocation count (saturating at 0)
    uint32_t count = usage->allocationCount.load(std::memory_order_relaxed);
    while (count > 0 &&
           !usage->allocationCount.compare_exchange_weak(count, count - 1,
               std::memory_order_release, std::memory_order_relaxed)) {
        // count updated, retry
    }
}

} // namespace ResourceManagement
