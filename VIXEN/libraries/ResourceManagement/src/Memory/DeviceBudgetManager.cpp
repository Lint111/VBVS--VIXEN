#include "Memory/DeviceBudgetManager.h"
#include "MessageBus.h"
#include "Message.h"
#include <sstream>

namespace ResourceManagement {

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

    // Subscribe to frame events if MessageBus provided
    messageBus_ = config_.messageBus;
    if (messageBus_) {
        SubscribeToFrameEvents();
    }
}

DeviceBudgetManager::~DeviceBudgetManager() {
    UnsubscribeFromFrameEvents();
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

// ============================================================================
// Frame Boundary Tracking (Sprint 5 Phase 4)
// ============================================================================

AllocationSnapshot DeviceBudgetManager::CaptureSnapshot() const {
    AllocationSnapshot snapshot{};

    // Get usage from budget tracker
    auto usage = budgetTracker_.GetUsage(BudgetResourceType::DeviceMemory);
    snapshot.totalAllocated = usage.currentBytes;
    snapshot.allocationCount = usage.allocationCount;

    // Staging quota
    snapshot.stagingInUse = stagingQuotaUsed_.load(std::memory_order_acquire);

    return snapshot;
}

void DeviceBudgetManager::OnFrameStart() {
    frameStartSnapshot_ = CaptureSnapshot();
}

void DeviceBudgetManager::OnFrameEnd() {
    ++frameNumber_;

    // Capture current state
    AllocationSnapshot currentSnapshot = CaptureSnapshot();

    // Calculate delta (reset all fields including exceededThreshold)
    lastFrameDelta_ = FrameAllocationDelta{};

    // Calculate allocated this frame (only if increased)
    if (currentSnapshot.totalAllocated > frameStartSnapshot_.totalAllocated) {
        lastFrameDelta_.allocatedThisFrame =
            currentSnapshot.totalAllocated - frameStartSnapshot_.totalAllocated;
    }

    // Calculate freed this frame (only if decreased)
    if (currentSnapshot.totalAllocated < frameStartSnapshot_.totalAllocated) {
        lastFrameDelta_.freedThisFrame =
            frameStartSnapshot_.totalAllocated - currentSnapshot.totalAllocated;
    }

    // Net delta (can be negative if more freed than allocated)
    lastFrameDelta_.netDelta =
        static_cast<int64_t>(currentSnapshot.totalAllocated) -
        static_cast<int64_t>(frameStartSnapshot_.totalAllocated);

    // Calculate utilization
    auto budget = budgetTracker_.GetBudget(BudgetResourceType::DeviceMemory);
    if (budget && budget->maxBytes > 0) {
        lastFrameDelta_.utilizationPercent =
            (static_cast<float>(currentSnapshot.totalAllocated) / static_cast<float>(budget->maxBytes)) * 100.0f;
    }

    // Mark if any allocations occurred
    lastFrameDelta_.hadAllocations =
        (currentSnapshot.allocationCount != frameStartSnapshot_.allocationCount) ||
        (lastFrameDelta_.netDelta != 0);

    // Report warning if threshold exceeded
    if (frameDeltaWarningThreshold_ > 0 &&
        lastFrameDelta_.allocatedThisFrame > frameDeltaWarningThreshold_) {

        // Mark that threshold was exceeded (for programmatic checking)
        lastFrameDelta_.exceededThreshold = true;

        // Call warning callback if provided
        if (config_.warningCallback) {
            float allocMB = static_cast<float>(lastFrameDelta_.allocatedThisFrame) / (1024.0f * 1024.0f);
            float thresholdMB = static_cast<float>(frameDeltaWarningThreshold_) / (1024.0f * 1024.0f);

            std::ostringstream msg;
            msg << "Frame " << frameNumber_
                << " allocation exceeded threshold: " << allocMB
                << " MB > " << thresholdMB << " MB limit"
                << " (utilization: " << lastFrameDelta_.utilizationPercent << "%)";

            config_.warningCallback(msg.str());
        }
    }
}

const FrameAllocationDelta& DeviceBudgetManager::GetLastFrameDelta() const {
    return lastFrameDelta_;
}

void DeviceBudgetManager::SetFrameDeltaWarningThreshold(uint64_t threshold) {
    frameDeltaWarningThreshold_ = threshold;
}

// ============================================================================
// Event-Driven Frame Tracking
// ============================================================================

void DeviceBudgetManager::SubscribeToFrameEvents() {
    if (!messageBus_) return;

    // Subscribe to FrameStartEvent
    frameStartSubscription_ = messageBus_->Subscribe(
        Vixen::EventBus::FrameStartEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) {
            return HandleFrameStartEvent(msg);
        }
    );

    // Subscribe to FrameEndEvent
    frameEndSubscription_ = messageBus_->Subscribe(
        Vixen::EventBus::FrameEndEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) {
            return HandleFrameEndEvent(msg);
        }
    );
}

void DeviceBudgetManager::UnsubscribeFromFrameEvents() {
    if (!messageBus_) return;

    if (frameStartSubscription_ != 0) {
        messageBus_->Unsubscribe(frameStartSubscription_);
        frameStartSubscription_ = 0;
    }

    if (frameEndSubscription_ != 0) {
        messageBus_->Unsubscribe(frameEndSubscription_);
        frameEndSubscription_ = 0;
    }
}

bool DeviceBudgetManager::HandleFrameStartEvent(const Vixen::EventBus::BaseEventMessage& /*msg*/) {
    OnFrameStart();
    return false;  // Don't consume, allow other listeners
}

bool DeviceBudgetManager::HandleFrameEndEvent(const Vixen::EventBus::BaseEventMessage& /*msg*/) {
    OnFrameEnd();
    return false;  // Don't consume, allow other listeners
}

} // namespace ResourceManagement
