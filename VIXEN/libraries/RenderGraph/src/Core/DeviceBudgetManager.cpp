#include "Core/DeviceBudgetManager.h"

namespace Vixen::RenderGraph {

DeviceBudgetManager::DeviceBudgetManager(
    std::shared_ptr<IMemoryAllocator> allocator,
    VkPhysicalDevice physicalDevice,
    const Config& config)
    : config_(config)
    , allocator_(std::move(allocator))
{
    // Auto-detect device memory if not specified
    uint64_t deviceMemory = config_.deviceMemoryBudget;
    if (deviceMemory == 0 && physicalDevice != VK_NULL_HANDLE) {
        deviceMemory = ResourceBudgetManager::DetectDeviceMemoryBytes(physicalDevice);
        // Use 80% of detected memory as budget (leave headroom)
        deviceMemory = static_cast<uint64_t>(deviceMemory * 0.8);
    }

    // Configure device memory budget
    if (deviceMemory > 0) {
        uint64_t warning = config_.deviceMemoryWarning;
        if (warning == 0) {
            warning = static_cast<uint64_t>(deviceMemory * 0.75);  // Warn at 75%
        }

        ResourceBudget budget(deviceMemory, warning, config_.strictBudget);
        budgetTracker_.SetBudget(BudgetResourceType::DeviceMemory, budget);
    }

    // Configure staging quota as custom budget
    ResourceBudget stagingBudget(config_.stagingQuota, 0, true);  // Strict staging quota
    budgetTracker_.SetBudget("StagingQuota", stagingBudget);

    // Link allocator to our budget tracker
    if (allocator_) {
        allocator_->SetBudgetManager(&budgetTracker_);
    }
}

std::expected<BufferAllocation, AllocationError>
DeviceBudgetManager::AllocateBuffer(const BufferAllocationRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Budget check is done by allocator via SetBudgetManager
    return allocator_->AllocateBuffer(request);
}

void DeviceBudgetManager::FreeBuffer(BufferAllocation& allocation) {
    if (allocator_) {
        allocator_->FreeBuffer(allocation);
    }
}

std::expected<ImageAllocation, AllocationError>
DeviceBudgetManager::AllocateImage(const ImageAllocationRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    return allocator_->AllocateImage(request);
}

void DeviceBudgetManager::FreeImage(ImageAllocation& allocation) {
    if (allocator_) {
        allocator_->FreeImage(allocation);
    }
}

// ============================================================================
// Aliased Allocations (Sprint 4 Phase B+)
// ============================================================================

std::expected<BufferAllocation, AllocationError>
DeviceBudgetManager::CreateAliasedBuffer(const AliasedBufferRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    // Aliased allocations do NOT consume additional budget
    // They share memory with the source allocation
    auto result = allocator_->CreateAliasedBuffer(request);

    if (result) {
        aliasedAllocationCount_.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

std::expected<ImageAllocation, AllocationError>
DeviceBudgetManager::CreateAliasedImage(const AliasedImageRequest& request) {
    if (!allocator_) {
        return std::unexpected(AllocationError::InvalidParameters);
    }

    auto result = allocator_->CreateAliasedImage(request);

    if (result) {
        aliasedAllocationCount_.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

void DeviceBudgetManager::FreeAliasedBuffer(BufferAllocation& allocation) {
    if (!allocator_ || !allocation.buffer) {
        return;
    }

    // Only destroy the buffer, not the underlying memory
    // The memory is owned by the source allocation
    if (allocation.isAliased) {
        // Get device from allocator (VMA stores it internally)
        // For now, we assume the caller manages VkBuffer destruction
        // The allocator's FreeBuffer handles this correctly for aliased buffers
        aliasedAllocationCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    // Invalidate but don't free memory
    allocation.buffer = VK_NULL_HANDLE;
    allocation.size = 0;
}

void DeviceBudgetManager::FreeAliasedImage(ImageAllocation& allocation) {
    if (!allocator_ || !allocation.image) {
        return;
    }

    if (allocation.isAliased) {
        aliasedAllocationCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    allocation.image = VK_NULL_HANDLE;
    allocation.size = 0;
}

bool DeviceBudgetManager::SupportsAliasing(AllocationHandle allocation) const {
    if (!allocator_) {
        return false;
    }
    return allocator_->SupportsAliasing(allocation);
}

uint32_t DeviceBudgetManager::GetAliasedAllocationCount() const {
    return aliasedAllocationCount_.load(std::memory_order_relaxed);
}

bool DeviceBudgetManager::TryReserveStagingQuota(uint64_t bytes) {
    // Atomic check-and-reserve
    uint64_t current = stagingQuotaUsed_.load(std::memory_order_acquire);

    while (true) {
        if (current + bytes > config_.stagingQuota) {
            return false;  // Would exceed quota
        }

        if (stagingQuotaUsed_.compare_exchange_weak(
                current, current + bytes,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;  // Reserved successfully
        }
        // CAS failed, current was updated, retry
    }
}

void DeviceBudgetManager::ReleaseStagingQuota(uint64_t bytes) {
    // Saturating subtract
    uint64_t current = stagingQuotaUsed_.load(std::memory_order_acquire);

    while (true) {
        uint64_t newValue = (current >= bytes) ? (current - bytes) : 0;

        if (stagingQuotaUsed_.compare_exchange_weak(
                current, newValue,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
        // CAS failed, retry
    }
}

uint64_t DeviceBudgetManager::GetStagingQuotaUsed() const {
    return stagingQuotaUsed_.load(std::memory_order_acquire);
}

uint64_t DeviceBudgetManager::GetAvailableStagingQuota() const {
    uint64_t used = stagingQuotaUsed_.load(std::memory_order_acquire);
    return (used < config_.stagingQuota) ? (config_.stagingQuota - used) : 0;
}

DeviceMemoryStats DeviceBudgetManager::GetStats() const {
    DeviceMemoryStats stats{};

    // Get budget info
    auto budget = budgetTracker_.GetBudget(BudgetResourceType::DeviceMemory);
    if (budget) {
        stats.totalDeviceMemory = budget->maxBytes;
    }

    // Get usage from budget tracker
    auto usage = budgetTracker_.GetUsage(BudgetResourceType::DeviceMemory);
    stats.usedDeviceMemory = usage.currentBytes;
    stats.availableDeviceMemory = budgetTracker_.GetAvailableBytes(BudgetResourceType::DeviceMemory);

    // Staging quota
    stats.stagingQuotaUsed = stagingQuotaUsed_.load(std::memory_order_acquire);
    stats.stagingQuotaMax = config_.stagingQuota;

    // Get fragmentation from allocator
    if (allocator_) {
        auto allocStats = allocator_->GetStats();
        stats.fragmentationRatio = allocStats.fragmentationRatio;
    }

    return stats;
}

BudgetResourceUsage DeviceBudgetManager::GetHeapUsage(DeviceHeapType heapType) const {
    // Map heap type to budget resource type
    BudgetResourceType budgetType = HeapTypeToBudgetType(heapType);
    return budgetTracker_.GetUsage(budgetType);
}

AllocationStats DeviceBudgetManager::GetAllocatorStats() const {
    if (allocator_) {
        return allocator_->GetStats();
    }
    return AllocationStats{};
}

bool DeviceBudgetManager::IsNearBudgetLimit() const {
    return budgetTracker_.IsNearWarningThreshold(BudgetResourceType::DeviceMemory);
}

bool DeviceBudgetManager::IsOverBudget() const {
    return budgetTracker_.IsOverBudget(BudgetResourceType::DeviceMemory);
}

void DeviceBudgetManager::SetStagingQuota(uint64_t quota) {
    config_.stagingQuota = quota;

    ResourceBudget stagingBudget(quota, 0, true);
    budgetTracker_.SetBudget("StagingQuota", stagingBudget);
}

BudgetResourceType DeviceBudgetManager::HeapTypeToBudgetType(DeviceHeapType heapType) {
    switch (heapType) {
        case DeviceHeapType::DeviceLocal:
        case DeviceHeapType::HostVisible:
        case DeviceHeapType::HostCached:
            return BudgetResourceType::DeviceMemory;
        case DeviceHeapType::Staging:
            // Staging is tracked separately via custom budget
            return BudgetResourceType::UserDefined;
    }
    return BudgetResourceType::DeviceMemory;
}

DeviceHeapType DeviceBudgetManager::MemoryLocationToHeapType(MemoryLocation location) const {
    switch (location) {
        case MemoryLocation::DeviceLocal:
            return DeviceHeapType::DeviceLocal;
        case MemoryLocation::HostVisible:
            return DeviceHeapType::HostVisible;
        case MemoryLocation::HostCached:
            return DeviceHeapType::HostCached;
        case MemoryLocation::Auto:
        default:
            return DeviceHeapType::DeviceLocal;
    }
}

} // namespace Vixen::RenderGraph
