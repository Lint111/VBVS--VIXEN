#include "Core/ResourceManagerBase.h"
#include "Core/ResourceHash.h"
#include <functional>

namespace Vixen::RenderGraph {

ResourceManagerBase::ResourceManagerBase()
    : stackTracker_(std::make_unique<StackResourceTracker>())
    , budgetManager_(std::make_unique<ResourceBudgetManager>())
{
}

ResourceManagerBase::~ResourceManagerBase() = default;

// ============================================================================
// FRAME LIFECYCLE
// ============================================================================

void ResourceManagerBase::BeginFrame(uint64_t frameNumber) {
    currentFrameNumber_ = frameNumber;

    if (stackTracker_) {
        stackTracker_->BeginFrame(frameNumber);
    }
}

void ResourceManagerBase::EndFrame() {
    if (stackTracker_) {
        stackTracker_->EndFrame();
    }
}

// ============================================================================
// BUDGET MANAGEMENT
// ============================================================================

void ResourceManagerBase::SetBudget(BudgetResourceType type, const ResourceBudget& budget) {
    if (budgetManager_) {
        budgetManager_->SetBudget(type, budget);
    }
}

BudgetResourceUsage ResourceManagerBase::GetBudgetUsage(BudgetResourceType type) const {
    if (budgetManager_) {
        return budgetManager_->GetUsage(type);
    }
    return BudgetResourceUsage{};
}

bool ResourceManagerBase::IsBudgetExceeded(BudgetResourceType type) const {
    if (budgetManager_) {
        return budgetManager_->IsOverBudget(type);
    }
    return false;
}

// ============================================================================
// STACK USAGE QUERIES
// ============================================================================

bool ResourceManagerBase::IsStackOverWarningThreshold() const {
    if (stackTracker_) {
        return stackTracker_->IsOverWarningThreshold();
    }
    return false;
}

bool ResourceManagerBase::IsStackOverCriticalThreshold() const {
    if (stackTracker_) {
        return stackTracker_->IsOverCriticalThreshold();
    }
    return false;
}

const StackResourceTracker::FrameStackUsage& ResourceManagerBase::GetCurrentFrameStackUsage() const {
    static const StackResourceTracker::FrameStackUsage empty{};
    if (stackTracker_) {
        return stackTracker_->GetCurrentFrameUsage();
    }
    return empty;
}

StackResourceTracker::UsageStats ResourceManagerBase::GetStackUsageStats() const {
    if (stackTracker_) {
        return stackTracker_->GetStats();
    }
    return StackResourceTracker::UsageStats{};
}

// ============================================================================
// HASH COMPUTATION
// ============================================================================

uint64_t ResourceManagerBase::ComputeResourceHash(uint64_t nodeId, std::string_view name) {
    // Use FNV-1a hash combining node ID and resource name
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis

    // Mix in node ID
    hash ^= nodeId;
    hash *= 1099511628211ULL; // FNV prime

    // Mix in name
    for (char c : name) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }

    return hash;
}

uint64_t ResourceManagerBase::ComputeScopeHash(uint64_t nodeId, uint64_t frameNumber) {
    // Combine node ID and frame number for scope identification
    uint64_t hash = 14695981039346656037ULL;

    hash ^= nodeId;
    hash *= 1099511628211ULL;
    hash ^= frameNumber;
    hash *= 1099511628211ULL;

    return hash;
}

// ============================================================================
// ALLOCATION HELPERS
// ============================================================================

bool ResourceManagerBase::CanAllocateOnStack(size_t bytes) const {
    if (!stackTracker_) {
        return false;  // No tracker means no stack allocation support
    }

    const auto& usage = stackTracker_->GetCurrentFrameUsage();
    const size_t available = StackResourceTracker::MAX_STACK_PER_FRAME - usage.totalStackUsed;

    return bytes <= available;
}

void ResourceManagerBase::TrackAllocationInternal(
    const void* address,
    size_t bytes,
    uint64_t nodeId,
    std::string_view name,
    ResourceLifetime lifetime,
    bool isStack
) {
    uint64_t resourceHash = ComputeResourceHash(nodeId, name);
    uint64_t scopeHash = ComputeScopeHash(nodeId, currentFrameNumber_);

    if (isStack && stackTracker_) {
        stackTracker_->TrackAllocation(
            resourceHash,
            scopeHash,
            address,
            bytes,
            static_cast<uint32_t>(nodeId),
            lifetime == ResourceLifetime::FrameLocal
        );
    }

    // Record allocation in budget manager regardless of stack/heap
    if (budgetManager_) {
        budgetManager_->RecordAllocation(BudgetResourceType::HostMemory, bytes);
    }
}

} // namespace Vixen::RenderGraph
