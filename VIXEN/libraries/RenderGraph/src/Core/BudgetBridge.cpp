#include "Core/BudgetBridge.h"

namespace Vixen::RenderGraph {

BudgetBridge::BudgetBridge(
    HostBudgetManager* hostBudget,
    DeviceBudgetManager* deviceBudget,
    const Config& config)
    : config_(config)
    , hostBudget_(hostBudget)
    , deviceBudget_(deviceBudget)
{
    // Sync device staging quota with our config
    if (deviceBudget_) {
        deviceBudget_->SetStagingQuota(config_.maxStagingQuota);
    }
}

bool BudgetBridge::ReserveStagingQuota(uint64_t bytes) {
    // First check device budget (authoritative source)
    if (deviceBudget_) {
        if (!deviceBudget_->TryReserveStagingQuota(bytes)) {
            return false;
        }
    }

    // Track locally as well
    uint64_t current = stagingQuotaUsed_.load(std::memory_order_acquire);
    while (true) {
        if (current + bytes > config_.maxStagingQuota) {
            // Rollback device reservation
            if (deviceBudget_) {
                deviceBudget_->ReleaseStagingQuota(bytes);
            }
            return false;
        }

        if (stagingQuotaUsed_.compare_exchange_weak(
                current, current + bytes,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

void BudgetBridge::ReleaseStagingQuota(uint64_t bytes) {
    // Release from device
    if (deviceBudget_) {
        deviceBudget_->ReleaseStagingQuota(bytes);
    }

    // Release locally (saturating subtract)
    uint64_t current = stagingQuotaUsed_.load(std::memory_order_acquire);
    while (true) {
        uint64_t newValue = (current >= bytes) ? (current - bytes) : 0;
        if (stagingQuotaUsed_.compare_exchange_weak(
                current, newValue,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
    }
}

void BudgetBridge::RecordUpload(uint64_t stagingBytes, uint64_t fenceValue) {
    PendingUpload upload{};
    upload.stagingBytes = stagingBytes;
    upload.frameSubmitted = currentFrame_.load(std::memory_order_relaxed);
    upload.fenceValue = fenceValue;

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);

        // Enforce max pending limit
        if (pendingUploads_.size() >= config_.maxPendingUploads) {
            // Drop oldest (assume completed)
            auto& oldest = pendingUploads_.front();
            pendingBytes_.fetch_sub(oldest.stagingBytes, std::memory_order_relaxed);
            ReleaseStagingQuota(oldest.stagingBytes);
            pendingUploads_.pop();
        }

        pendingUploads_.push(upload);
    }

    pendingBytes_.fetch_add(stagingBytes, std::memory_order_relaxed);
}

uint64_t BudgetBridge::ProcessCompletedUploads(uint64_t completedFenceValue) {
    uint64_t reclaimedBytes = 0;

    std::lock_guard<std::mutex> lock(pendingMutex_);

    while (!pendingUploads_.empty()) {
        auto& front = pendingUploads_.front();

        // Check if this upload is complete (fence passed)
        if (front.fenceValue > completedFenceValue) {
            break;  // This and all subsequent are still pending
        }

        // Upload complete - reclaim staging
        reclaimedBytes += front.stagingBytes;
        pendingBytes_.fetch_sub(front.stagingBytes, std::memory_order_relaxed);
        ReleaseStagingQuota(front.stagingBytes);

        if (onUploadComplete_) {
            onUploadComplete_(front.stagingBytes);
        }

        pendingUploads_.pop();
    }

    return reclaimedBytes;
}

uint64_t BudgetBridge::ProcessCompletedUploads(uint64_t currentFrame, bool useFrameTracking) {
    if (!useFrameTracking) {
        return ProcessCompletedUploads(currentFrame);
    }

    // Update current frame
    currentFrame_.store(currentFrame, std::memory_order_relaxed);

    uint64_t reclaimedBytes = 0;
    uint64_t frameThreshold = (currentFrame > config_.framesToKeepPending)
        ? (currentFrame - config_.framesToKeepPending)
        : 0;

    std::lock_guard<std::mutex> lock(pendingMutex_);

    while (!pendingUploads_.empty()) {
        auto& front = pendingUploads_.front();

        // Check if this upload is old enough to assume complete
        if (front.frameSubmitted > frameThreshold) {
            break;  // Too recent, keep waiting
        }

        // Upload assumed complete - reclaim staging
        reclaimedBytes += front.stagingBytes;
        pendingBytes_.fetch_sub(front.stagingBytes, std::memory_order_relaxed);
        ReleaseStagingQuota(front.stagingBytes);

        if (onUploadComplete_) {
            onUploadComplete_(front.stagingBytes);
        }

        pendingUploads_.pop();
    }

    return reclaimedBytes;
}

uint64_t BudgetBridge::GetStagingQuotaUsed() const {
    return stagingQuotaUsed_.load(std::memory_order_acquire);
}

uint64_t BudgetBridge::GetAvailableStagingQuota() const {
    uint64_t used = stagingQuotaUsed_.load(std::memory_order_acquire);
    return (used < config_.maxStagingQuota) ? (config_.maxStagingQuota - used) : 0;
}

size_t BudgetBridge::GetPendingUploadCount() const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    return pendingUploads_.size();
}

uint64_t BudgetBridge::GetPendingUploadBytes() const {
    return pendingBytes_.load(std::memory_order_acquire);
}

bool BudgetBridge::IsStagingNearLimit() const {
    return stagingQuotaUsed_.load(std::memory_order_acquire) >= config_.stagingWarningThreshold;
}

void BudgetBridge::SetUploadCompleteCallback(UploadCompleteCallback callback) {
    onUploadComplete_ = std::move(callback);
}

void BudgetBridge::SetStagingQuotaLimit(uint64_t newLimit) {
    config_.maxStagingQuota = newLimit;
    if (deviceBudget_) {
        deviceBudget_->SetStagingQuota(newLimit);
    }
}

} // namespace Vixen::RenderGraph
