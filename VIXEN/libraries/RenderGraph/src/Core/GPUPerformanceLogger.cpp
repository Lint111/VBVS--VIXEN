#include "Core/GPUPerformanceLogger.h"
#include <iostream>

namespace Vixen::RenderGraph {

GPUPerformanceLogger::GPUPerformanceLogger(const std::string& name, VulkanDevice* device, size_t rollingWindowSize)
    : Logger(name + "_GPUPerf", true)
    , rollingWindowSize_(rollingWindowSize)
{
    if (device) {
        // Disable pipeline statistics - requires pipelineStatisticsQuery feature which is not enabled
        // Timestamp queries work without additional device features
        query_ = std::make_unique<GPUTimestampQuery>(device, 4, false);

        if (query_->IsTimestampSupported()) {
            Info("GPU timestamp queries enabled (period: " +
                 std::to_string(query_->GetTimestampPeriod()) + " ns/tick)");
        } else {
            Warning("GPU timestamp queries NOT supported on this device");
        }

        if (query_->IsPipelineStatsEnabled()) {
            Info("Pipeline statistics queries enabled");
        }
    } else {
        Warning("No Vulkan device provided - GPU timing disabled");
    }
}

// ============================================================================
// COMMAND BUFFER RECORDING
// ============================================================================

void GPUPerformanceLogger::BeginFrame(VkCommandBuffer cmdBuffer) {
    if (query_) {
        query_->ResetQueries(cmdBuffer);
    }
}

void GPUPerformanceLogger::RecordDispatchStart(VkCommandBuffer cmdBuffer) {
    if (query_ && query_->IsTimestampSupported()) {
        query_->WriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

        if (query_->IsPipelineStatsEnabled()) {
            query_->BeginPipelineStats(cmdBuffer);
        }
    }
}

void GPUPerformanceLogger::RecordDispatchEnd(VkCommandBuffer cmdBuffer, uint32_t dispatchWidth, uint32_t dispatchHeight) {
    currentWidth_ = dispatchWidth;
    currentHeight_ = dispatchHeight;

    if (query_ && query_->IsTimestampSupported()) {
        if (query_->IsPipelineStatsEnabled()) {
            query_->EndPipelineStats(cmdBuffer);
        }

        query_->WriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
        hasRecordedTimestamps_ = true;  // Mark that we've recorded timestamps for collection
    }
}

// ============================================================================
// RESULT COLLECTION
// ============================================================================

void GPUPerformanceLogger::CollectResults() {
    if (!query_ || !query_->IsTimestampSupported()) {
        return;
    }

    // Skip collection if we haven't recorded any timestamps yet (first frame)
    if (!hasRecordedTimestamps_) {
        return;
    }

    if (!query_->ReadResults()) {
        return;
    }

    // Calculate timing
    lastDispatchMs_ = query_->GetElapsedMs(0, 1);
    lastMraysPerSec_ = query_->CalculateMraysPerSec(0, 1, currentWidth_, currentHeight_);

    // Get pipeline statistics
    if (query_->IsPipelineStatsEnabled()) {
        lastComputeInvocations_ = query_->GetStatistic(GPUTimestampQuery::PipelineStatistic::ComputeShaderInvocations);
    }

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
    // Add to history
    dispatchMsHistory_.push_back(lastDispatchMs_);
    mraysHistory_.push_back(lastMraysPerSec_);

    // Trim to window size
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

    if (lastComputeInvocations_ > 0) {
        oss << " | Invocations: " << lastComputeInvocations_;
    }

    oss << " | Resolution: " << currentWidth_ << "x" << currentHeight_;

    return oss.str();
}

} // namespace Vixen::RenderGraph
