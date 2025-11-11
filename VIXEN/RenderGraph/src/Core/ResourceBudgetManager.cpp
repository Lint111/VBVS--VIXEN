#include "Core/ResourceBudgetManager.h"
#include "Core/NodeInstance.h"
#include "Core/ResourceLifetimeAnalyzer.h"
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

// ============================================================================
// PHASE H: UNIFIED RESOURCE REGISTRY IMPLEMENTATION
// ============================================================================

const ResourceBudgetManager::ResourceMetadata* ResourceBudgetManager::GetResourceMetadata(
    Resource* resource
) const {
    auto it = resourceRegistry_.find(resource);
    if (it != resourceRegistry_.end()) {
        return &it->second;
    }

    return nullptr;
}

void ResourceBudgetManager::UpdateResourceSize(Resource* resource, size_t newSize) {
    auto it = resourceRegistry_.find(resource);
    if (it == resourceRegistry_.end()) {
        return; // Resource not tracked
    }

    auto& metadata = it->second;
    size_t oldSize = metadata.allocatedBytes;

    // Update metadata
    metadata.allocatedBytes = newSize;

    // Update budget tracking
    BudgetResourceType budgetType = (metadata.location == ResourceManagement::MemoryLocation::DeviceLocal)
        ? BudgetResourceType::DeviceMemory
        : BudgetResourceType::HostMemory;

    // Adjust budget (deallocate old, allocate new)
    if (oldSize > 0) {
        RecordDeallocation(budgetType, oldSize);
    }
    if (newSize > 0) {
        RecordAllocation(budgetType, newSize);
    }
}

void ResourceBudgetManager::UpdateAliasingPoolsFromTopology(
    const ResourceLifetimeAnalyzer& analyzer
) {
    // Clear existing pools
    aliasingPools_.clear();

    // Get computed aliasing groups from lifetime analyzer
    auto groups = analyzer.ComputeAliasingGroups();

    for (size_t groupIdx = 0; groupIdx < groups.size(); ++groupIdx) {
        const auto& group = groups[groupIdx];

        if (group.size() < 2) continue; // No aliasing benefit for single resource

        // Create aliasing pool
        AliasingPool pool;
        pool.poolID = "auto_alias_" + std::to_string(groupIdx);

        // Find maximum size needed in this group
        size_t maxSize = 0;
        for (auto* resource : group) {
            const auto* metadata = GetResourceMetadata(resource);
            if (metadata) {
                maxSize = (std::max)(maxSize, metadata->allocatedBytes);
            }
        }

        // TODO: Allocate shared device memory
        // For now, just track the pool structure
        pool.totalSize = maxSize;
        pool.sharedMemory = nullptr;  // Will be allocated when needed

        // Register resources in this pool
        for (auto* resource : group) {
            const auto* timeline = analyzer.GetTimeline(resource);
            if (timeline) {
                pool.aliasedResources.push_back(resource);
                pool.lifetimes.push_back({timeline->birthIndex, timeline->deathIndex});

                // TODO: Mark resource as aliased in metadata
                // (Could add aliasingPoolID field to ResourceMetadata)
            }
        }

        aliasingPools_[pool.poolID] = pool;
    }
}

void ResourceBudgetManager::PrintResourceReport() const {
    // TODO: Implement resource tracking report
    // Will print all tracked resources, their sizes, and memory locations
}

void ResourceBudgetManager::PrintAliasingReport() const {
    if (aliasingPools_.empty()) {
        return;
    }

    size_t totalAliased = 0;
    size_t totalMemory = 0;
    size_t memoryIfNoAliasing = 0;

    for (const auto& [poolID, pool] : aliasingPools_) {
        totalAliased += pool.aliasedResources.size();
        totalMemory += pool.totalSize;

        for (auto* resource : pool.aliasedResources) {
            const auto* metadata = GetResourceMetadata(resource);
            if (metadata) {
                memoryIfNoAliasing += metadata->allocatedBytes;
            }
        }
    }

    if (memoryIfNoAliasing > 0) {
        float savings = 100.0f * (1.0f -
            static_cast<float>(totalMemory) / static_cast<float>(memoryIfNoAliasing));

        // TODO: Use proper logging system
        // For now, basic output structure
        // LOG_INFO("=== Automatic Aliasing Report ===");
        // LOG_INFO("Aliasing Pools: " + std::to_string(aliasingPools_.size()));
        // LOG_INFO("Aliased Resources: " + std::to_string(totalAliased));
        // LOG_INFO("Memory Allocated: " + FormatBytes(totalMemory));
        // LOG_INFO("Memory If No Aliasing: " + FormatBytes(memoryIfNoAliasing));
        // LOG_INFO("Savings: " + std::to_string(static_cast<int>(savings)) + "%");
    }
}

// ============================================================================
// PHASE H: STACK RESOURCE TRACKING
// ============================================================================

void ResourceBudgetManager::BeginFrameStackTracking(uint64_t frameNumber) {
    stackTracker_.BeginFrame(frameNumber);
}

void ResourceBudgetManager::EndFrameStackTracking() {
    stackTracker_.EndFrame();
}

} // namespace Vixen::RenderGraph
