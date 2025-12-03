#include "Core/ResourceBudgetManager.h"
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace Vixen::RenderGraph {

// Budget configuration
void ResourceBudgetManager::SetBudget(BudgetResourceType type, const ResourceBudget& budget) {
    budgets_[type] = budget;
}

void ResourceBudgetManager::SetBudget(const std::string& customType, const ResourceBudget& budget) {
    customBudgets_[customType] = budget;
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(BudgetResourceType type) const {
    auto it = budgets_.find(type);
    if (it != budgets_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(const std::string& customType) const {
    auto it = customBudgets_.find(customType);
    if (it != customBudgets_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Allocation attempts (check if allocation would succeed)
bool ResourceBudgetManager::TryAllocate(BudgetResourceType type, uint64_t bytes) {
    auto budgetIt = budgets_.find(type);
    auto usageIt = usage_.find(type);

    const ResourceBudget* budget = (budgetIt != budgets_.end()) ? &budgetIt->second : nullptr;
    BudgetResourceUsage* usage = (usageIt != usage_.end()) ? &usageIt->second : nullptr;

    // Create usage entry if doesn't exist
    if (!usage) {
        usage = &usage_[type];
    }

    return TryAllocateImpl(budget, usage, bytes);
}

bool ResourceBudgetManager::TryAllocate(const std::string& customType, uint64_t bytes) {
    auto budgetIt = customBudgets_.find(customType);
    auto usageIt = customUsage_.find(customType);

    const ResourceBudget* budget = (budgetIt != customBudgets_.end()) ? &budgetIt->second : nullptr;
    BudgetResourceUsage* usage = (usageIt != customUsage_.end()) ? &usageIt->second : nullptr;

    // Create usage entry if doesn't exist
    if (!usage) {
        usage = &customUsage_[customType];
    }

    return TryAllocateImpl(budget, usage, bytes);
}

// Record actual allocations
void ResourceBudgetManager::RecordAllocation(BudgetResourceType type, uint64_t bytes) {
    RecordAllocationImpl(&usage_[type], bytes);
}

void ResourceBudgetManager::RecordAllocation(const std::string& customType, uint64_t bytes) {
    RecordAllocationImpl(&customUsage_[customType], bytes);
}

// Record deallocations
void ResourceBudgetManager::RecordDeallocation(BudgetResourceType type, uint64_t bytes) {
    auto it = usage_.find(type);
    if (it != usage_.end()) {
        RecordDeallocationImpl(&it->second, bytes);
    }
}

void ResourceBudgetManager::RecordDeallocation(const std::string& customType, uint64_t bytes) {
    auto it = customUsage_.find(customType);
    if (it != customUsage_.end()) {
        RecordDeallocationImpl(&it->second, bytes);
    }
}

// Query current state
BudgetResourceUsage ResourceBudgetManager::GetUsage(BudgetResourceType type) const {
    auto it = usage_.find(type);
    return (it != usage_.end()) ? it->second : BudgetResourceUsage{};
}

BudgetResourceUsage ResourceBudgetManager::GetUsage(const std::string& customType) const {
    auto it = customUsage_.find(customType);
    return (it != customUsage_.end()) ? it->second : BudgetResourceUsage{};
}

uint64_t ResourceBudgetManager::GetAvailableBytes(BudgetResourceType type) const {
    auto budgetIt = budgets_.find(type);
    auto usageIt = usage_.find(type);

    if (budgetIt == budgets_.end() || budgetIt->second.maxBytes == 0) {
        return (std::numeric_limits<uint64_t>::max)(); // Unlimited
    }

    uint64_t current = (usageIt != usage_.end()) ? usageIt->second.currentBytes : 0;
    uint64_t max = budgetIt->second.maxBytes;

    return (current < max) ? (max - current) : 0;
}

uint64_t ResourceBudgetManager::GetAvailableBytes(const std::string& customType) const {
    auto budgetIt = customBudgets_.find(customType);
    auto usageIt = customUsage_.find(customType);

    if (budgetIt == customBudgets_.end() || budgetIt->second.maxBytes == 0) {
        return (std::numeric_limits<uint64_t>::max)(); // Unlimited
    }

    uint64_t current = (usageIt != customUsage_.end()) ? usageIt->second.currentBytes : 0;
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
    auto budgetIt = budgets_.find(type);
    auto usageIt = usage_.find(type);

    if (budgetIt == budgets_.end() || budgetIt->second.warningThreshold == 0) {
        return false; // No warning threshold set
    }

    uint64_t current = (usageIt != usage_.end()) ? usageIt->second.currentBytes : 0;
    return current >= budgetIt->second.warningThreshold;
}

bool ResourceBudgetManager::IsNearWarningThreshold(const std::string& customType) const {
    auto budgetIt = customBudgets_.find(customType);
    auto usageIt = customUsage_.find(customType);

    if (budgetIt == customBudgets_.end() || budgetIt->second.warningThreshold == 0) {
        return false; // No warning threshold set
    }

    uint64_t current = (usageIt != customUsage_.end()) ? usageIt->second.currentBytes : 0;
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
    budgets_.clear();
    usage_.clear();
    customBudgets_.clear();
    customUsage_.clear();
}

void ResourceBudgetManager::ResetUsage(BudgetResourceType type) {
    auto it = usage_.find(type);
    if (it != usage_.end()) {
        it->second.Reset();
    }
}

void ResourceBudgetManager::ResetUsage(const std::string& customType) {
    auto it = customUsage_.find(customType);
    if (it != customUsage_.end()) {
        it->second.Reset();
    }
}

// Internal helpers
bool ResourceBudgetManager::TryAllocateImpl(const ResourceBudget* budget, BudgetResourceUsage* usage, uint64_t bytes) {
    if (!budget || budget->maxBytes == 0) {
        return true; // No budget limit - allow
    }

    uint64_t newTotal = usage->currentBytes + bytes;

    if (budget->strict && newTotal > budget->maxBytes) {
        return false; // Strict mode - reject allocation over budget
    }

    return true; // Allow allocation (may exceed budget if not strict)
}

void ResourceBudgetManager::RecordAllocationImpl(BudgetResourceUsage* usage, uint64_t bytes) {
    usage->currentBytes += bytes;
    usage->peakBytes = (std::max)(usage->peakBytes, usage->currentBytes);
    usage->allocationCount++;
}

void ResourceBudgetManager::RecordDeallocationImpl(BudgetResourceUsage* usage, uint64_t bytes) {
    if (usage->currentBytes >= bytes) {
        usage->currentBytes -= bytes;
    } else {
        usage->currentBytes = 0; // Prevent underflow
    }

    if (usage->allocationCount > 0) {
        usage->allocationCount--;
    }
}

} // namespace Vixen::RenderGraph
