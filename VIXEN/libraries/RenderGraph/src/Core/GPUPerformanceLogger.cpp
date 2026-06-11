#include "Core/GPUPerformanceLogger.h"
#include <iostream>

namespace Vixen::RenderGraph {

GPUPerformanceLogger::GPUPerformanceLogger(const std::string& name, std::shared_ptr<GPUQueryManager> queryManager,
                                           size_t rollingWindowSize)
    : Logger(name + "_GPUPerf", true)
    , queryManager_(queryManager)
    , rollingWindowSize_(rollingWindowSize)
{
    if (queryManager_) {
        // Allocate a query slot from the shared manager
        querySlot_ = queryManager_->AllocateQuerySlot(name + "_GPUPerf");

        if (querySlot_ == GPUQueryManager::INVALID_SLOT) {
            Warning("Failed to allocate GPU query slot - GPU timing disabled");
        } else {
            frameDispatchInfo_.resize(queryManager_->GetFrameCount());

            if (queryManager_->IsTimestampSupported()) {
                Info("GPU timestamp queries enabled (period: " +
                     std::to_string(queryManager_->GetTimestampPeriod()) + " ns/tick, " +
                     std::to_string(queryManager_->GetFrameCount()) + " frames-in-flight, slot " +
                     std::to_string(querySlot_) + ")");
            } else {
                Warning("GPU timestamp queries NOT supported on this device");
            }
        }
    } else {
        Warning("No GPUQueryManager provided - GPU timing disabled");
    }
}

// ============================================================================
// COMMAND BUFFER RECORDING
// ============================================================================

void GPUPerformanceLogger::BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT) {
        queryManager_->BeginFrame(cmdBuffer, frameIndex);
    }
}

void GPUPerformanceLogger::RecordDispatchStart(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT && queryManager_->IsTimestampSupported()) {
        // Write start timestamp for this slot
        queryManager_->WriteTimestamp(cmdBuffer, frameIndex, querySlot_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }
}

void GPUPerformanceLogger::RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                              uint32_t dispatchWidth, uint32_t dispatchHeight) {
    if (frameIndex < frameDispatchInfo_.size()) {
        frameDispatchInfo_[frameIndex].width = dispatchWidth;
        frameDispatchInfo_[frameIndex].height = dispatchHeight;
    }

    if (queryManager_ && querySlot_ != GPUQueryManager::INVALID_SLOT && queryManager_->IsTimestampSupported()) {
        // Write end timestamp for this slot
        queryManager_->WriteTimestamp(cmdBuffer, frameIndex, querySlot_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
}

// ============================================================================
// RESULT COLLECTION
// ============================================================================

void GPUPerformanceLogger::CollectResults(uint32_t frameIndex) {
    if (!queryManager_ || querySlot_ == GPUQueryManager::INVALID_SLOT || !queryManager_->IsTimestampSupported()) {
        return;
    }

    // Try to read timestamps for this slot
    if (!queryManager_->TryReadTimestamps(frameIndex, querySlot_)) {
        return;  // No results ready yet (first few frames, or timestamps not written)
    }

    // Get dispatch dimensions for this frame
    uint32_t width = 0, height = 0;
    if (frameIndex < frameDispatchInfo_.size()) {
        width = frameDispatchInfo_[frameIndex].width;
        height = frameDispatchInfo_[frameIndex].height;
    }

    // Calculate timing from query manager
    lastDispatchMs_ = queryManager_->GetElapsedMs(frameIndex, querySlot_);

    // Calculate Mrays/sec
    uint64_t elapsedNs = queryManager_->GetElapsedNs(frameIndex, querySlot_);
    if (elapsedNs > 0 && width > 0 && height > 0) {
        uint64_t totalRays = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        double elapsedSec = static_cast<double>(elapsedNs) / 1'000'000'000.0;
        lastMraysPerSec_ = static_cast<float>(totalRays / 1'000'000.0 / elapsedSec);
    } else {
        lastMraysPerSec_ = 0.0f;
    }

    // Update rolling statistics
    UpdateRollingStats();

    // Logging
    ++frameCounter_;
    if (logFrequency_ > 0 && (frameCounter_ % logFrequency_) == 0) {
        std::string summary = GetPerformanceSummary();
        Info(summary);

        if (printToTerminal_) {
            Info("[GPU Perf] " + summary);
        }
    }
}

// ============================================================================
// STATISTICS
// ============================================================================

void GPUPerformanceLogger::UpdateRollingStats() {
    dispatchMsHistory_.push_back(lastDispatchMs_);
    mraysHistory_.push_back(lastMraysPerSec_);

    while (dispatchMsHistory_.size() > rollingWindowSize_) {
        dispatchMsHistory_.pop_front();
    }
    while (mraysHistory_.size() > rollingWindowSize_) {
        mraysHistory_.pop_front();
    }
}

float GPUPerformanceLogger::GetAverageDispatchMs() const {
    if (dispatchMsHistory_.empty()) return 0.0f;
    float sum = std::accumulate(dispatchMsHistory_.begin(), dispatchMsHistory_.end(), 0.0f);
    return sum / static_cast<float>(dispatchMsHistory_.size());
}

float GPUPerformanceLogger::GetAverageMraysPerSec() const {
    if (mraysHistory_.empty()) return 0.0f;
    float sum = std::accumulate(mraysHistory_.begin(), mraysHistory_.end(), 0.0f);
    return sum / static_cast<float>(mraysHistory_.size());
}

float GPUPerformanceLogger::GetMinDispatchMs() const {
    if (dispatchMsHistory_.empty()) return 0.0f;
    return *std::min_element(dispatchMsHistory_.begin(), dispatchMsHistory_.end());
}

float GPUPerformanceLogger::GetMaxDispatchMs() const {
    if (dispatchMsHistory_.empty()) return 0.0f;
    return *std::max_element(dispatchMsHistory_.begin(), dispatchMsHistory_.end());
}

std::string GPUPerformanceLogger::GetPerformanceSummary() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "Dispatch: " << GetAverageDispatchMs() << " ms avg "
        << "(min " << GetMinDispatchMs() << ", max " << GetMaxDispatchMs() << ") | "
        << "Mrays/s: " << GetAverageMraysPerSec() << " avg";

    if (!frameDispatchInfo_.empty()) {
        oss << " | Resolution: " << frameDispatchInfo_[0].width << "x" << frameDispatchInfo_[0].height;
    }

    // Add memory info if tracked
    if (totalTrackedMemory_ > 0) {
        oss << " | Memory: " << std::fixed << std::setprecision(2)
            << (static_cast<float>(totalTrackedMemory_) / (1024.0f * 1024.0f)) << " MB";
    }

    return oss.str();
}

// ============================================================================
// MEMORY TRACKING
// ============================================================================

void GPUPerformanceLogger::RegisterBufferAllocation(const std::string& name, VkDeviceSize sizeBytes) {
    // If buffer already registered, remove old size first
    auto it = bufferAllocations_.find(name);
    if (it != bufferAllocations_.end()) {
        totalTrackedMemory_ -= it->second;
    }

    bufferAllocations_[name] = sizeBytes;
    totalTrackedMemory_ += sizeBytes;

    // Log the allocation
    std::ostringstream oss;
    oss << "Buffer '" << name << "' allocated: "
        << std::fixed << std::setprecision(2)
        << (static_cast<float>(sizeBytes) / 1024.0f) << " KB";
    Debug(oss.str());
}

void GPUPerformanceLogger::UnregisterBufferAllocation(const std::string& name) {
    auto it = bufferAllocations_.find(name);
    if (it != bufferAllocations_.end()) {
        totalTrackedMemory_ -= it->second;
        bufferAllocations_.erase(it);
        Debug("Buffer '" + name + "' deallocated");
    }
}

std::string GPUPerformanceLogger::GetMemorySummary() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "=== GPU Memory Summary ===\n";
    oss << "Total tracked: " << (static_cast<float>(totalTrackedMemory_) / (1024.0f * 1024.0f)) << " MB\n";
    oss << "\nBuffer breakdown:\n";

    // Sort by size (largest first)
    std::vector<std::pair<std::string, VkDeviceSize>> sorted(
        bufferAllocations_.begin(), bufferAllocations_.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [name, size] : sorted) {
        float sizeMB = static_cast<float>(size) / (1024.0f * 1024.0f);
        float sizeKB = static_cast<float>(size) / 1024.0f;

        if (sizeMB >= 1.0f) {
            oss << "  " << name << ": " << sizeMB << " MB\n";
        } else {
            oss << "  " << name << ": " << sizeKB << " KB\n";
        }
    }

    return oss.str();
}

} // namespace Vixen::RenderGraph
