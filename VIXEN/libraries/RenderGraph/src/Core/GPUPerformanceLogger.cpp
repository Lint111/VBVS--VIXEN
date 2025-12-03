#include "Core/GPUPerformanceLogger.h"
#include <iostream>

namespace Vixen::RenderGraph {

GPUPerformanceLogger::GPUPerformanceLogger(const std::string& name, VulkanDevice* device,
                                           uint32_t framesInFlight, size_t rollingWindowSize)
    : Logger(name + "_GPUPerf", true)
    , rollingWindowSize_(rollingWindowSize)
{
    if (device && framesInFlight > 0) {
        query_ = std::make_unique<GPUTimestampQuery>(device, framesInFlight, 4);
        frameDispatchInfo_.resize(framesInFlight);

        if (query_->IsTimestampSupported()) {
            Info("GPU timestamp queries enabled (period: " +
                 std::to_string(query_->GetTimestampPeriod()) + " ns/tick, " +
                 std::to_string(framesInFlight) + " frames-in-flight)");
        } else {
            Warning("GPU timestamp queries NOT supported on this device");
        }
    } else {
        Warning("No Vulkan device provided - GPU timing disabled");
    }
}

// ============================================================================
// COMMAND BUFFER RECORDING
// ============================================================================

void GPUPerformanceLogger::BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (query_) {
        query_->ResetQueries(cmdBuffer, frameIndex);
    }
}

void GPUPerformanceLogger::RecordDispatchStart(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (query_ && query_->IsTimestampSupported()) {
        query_->WriteTimestamp(cmdBuffer, frameIndex, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
    }
}

void GPUPerformanceLogger::RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                              uint32_t dispatchWidth, uint32_t dispatchHeight) {
    if (frameIndex < frameDispatchInfo_.size()) {
        frameDispatchInfo_[frameIndex].width = dispatchWidth;
        frameDispatchInfo_[frameIndex].height = dispatchHeight;
    }

    if (query_ && query_->IsTimestampSupported()) {
        query_->WriteTimestamp(cmdBuffer, frameIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
    }
}

// ============================================================================
// RESULT COLLECTION
// ============================================================================

void GPUPerformanceLogger::CollectResults(uint32_t frameIndex) {
    if (!query_ || !query_->IsTimestampSupported()) {
        return;
    }

    // Read results for this frame (timestamps were written in previous submission)
    if (!query_->ReadResults(frameIndex)) {
        return;  // No results ready yet (first few frames, or query unavailable)
    }

    // Get dispatch dimensions for this frame
    uint32_t width = 0, height = 0;
    if (frameIndex < frameDispatchInfo_.size()) {
        width = frameDispatchInfo_[frameIndex].width;
        height = frameDispatchInfo_[frameIndex].height;
    }

    // Calculate timing
    lastDispatchMs_ = query_->GetElapsedMs(frameIndex, 0, 1);
    lastMraysPerSec_ = query_->CalculateMraysPerSec(frameIndex, 0, 1, width, height);

    // Update rolling statistics
    UpdateRollingStats();

    // Logging
    ++frameCounter_;
    if (logFrequency_ > 0 && (frameCounter_ % logFrequency_) == 0) {
        std::string summary = GetPerformanceSummary();
        Info(summary);

        if (printToTerminal_) {
            std::cout << "[GPU Perf] " << summary << std::endl;
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
