#include "GPUTimestampQuery.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>

namespace Vixen::Vulkan::Resources {

GPUTimestampQuery::GPUTimestampQuery(VulkanDevice* device, uint32_t maxTimestamps, bool enablePipelineStats)
    : device_(device)
    , maxTimestamps_(maxTimestamps)
    , pipelineStatsEnabled_(enablePipelineStats)
{
    if (!device_ || device_->device == VK_NULL_HANDLE) {
        throw std::runtime_error("[GPUTimestampQuery] Invalid Vulkan device");
    }

    // Check timestamp support
    timestampPeriod_ = device_->gpuProperties.limits.timestampPeriod;
    timestampSupported_ = (timestampPeriod_ > 0.0f);

    // Check if compute queue supports timestamps
    if (timestampSupported_) {
        uint32_t queueFamily = device_->graphicsQueueIndex;
        if (queueFamily < device_->queueFamilyProperties.size()) {
            uint32_t validBits = device_->queueFamilyProperties[queueFamily].timestampValidBits;
            timestampSupported_ = (validBits > 0);
        }
    }

    // Allocate result storage
    timestampResults_.resize(maxTimestamps_, 0);
    pipelineStatsResults_.resize(static_cast<size_t>(PipelineStatistic::Count), 0);

    CreateQueryPools();
}

GPUTimestampQuery::~GPUTimestampQuery() {
    DestroyQueryPools();
}

GPUTimestampQuery::GPUTimestampQuery(GPUTimestampQuery&& other) noexcept
    : device_(other.device_)
    , timestampPool_(other.timestampPool_)
    , maxTimestamps_(other.maxTimestamps_)
    , timestampSupported_(other.timestampSupported_)
    , timestampPeriod_(other.timestampPeriod_)
    , timestampResults_(std::move(other.timestampResults_))
    , pipelineStatsPool_(other.pipelineStatsPool_)
    , pipelineStatsEnabled_(other.pipelineStatsEnabled_)
    , pipelineStatsResults_(std::move(other.pipelineStatsResults_))
    , resultsValid_(other.resultsValid_)
{
    other.device_ = nullptr;
    other.timestampPool_ = VK_NULL_HANDLE;
    other.pipelineStatsPool_ = VK_NULL_HANDLE;
}

GPUTimestampQuery& GPUTimestampQuery::operator=(GPUTimestampQuery&& other) noexcept {
    if (this != &other) {
        DestroyQueryPools();
        device_ = other.device_;
        timestampPool_ = other.timestampPool_;
        maxTimestamps_ = other.maxTimestamps_;
        timestampSupported_ = other.timestampSupported_;
        timestampPeriod_ = other.timestampPeriod_;
        timestampResults_ = std::move(other.timestampResults_);
        pipelineStatsPool_ = other.pipelineStatsPool_;
        pipelineStatsEnabled_ = other.pipelineStatsEnabled_;
        pipelineStatsResults_ = std::move(other.pipelineStatsResults_);
        resultsValid_ = other.resultsValid_;

        other.device_ = nullptr;
        other.timestampPool_ = VK_NULL_HANDLE;
        other.pipelineStatsPool_ = VK_NULL_HANDLE;
    }
    return *this;
}

void GPUTimestampQuery::CreateQueryPools() {
    // Create timestamp query pool
    if (timestampSupported_ && maxTimestamps_ > 0) {
        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        createInfo.queryCount = maxTimestamps_;

        VkResult result = vkCreateQueryPool(device_->device, &createInfo, nullptr, &timestampPool_);
        if (result != VK_SUCCESS) {
            timestampSupported_ = false;
            timestampPool_ = VK_NULL_HANDLE;
        }
    }

    // Create pipeline statistics query pool
    if (pipelineStatsEnabled_) {
        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        createInfo.queryCount = 1;
        createInfo.pipelineStatistics =
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

        VkResult result = vkCreateQueryPool(device_->device, &createInfo, nullptr, &pipelineStatsPool_);
        if (result != VK_SUCCESS) {
            pipelineStatsEnabled_ = false;
            pipelineStatsPool_ = VK_NULL_HANDLE;
        }
    }
}

void GPUTimestampQuery::DestroyQueryPools() {
    if (device_ && device_->device != VK_NULL_HANDLE) {
        if (timestampPool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_->device, timestampPool_, nullptr);
            timestampPool_ = VK_NULL_HANDLE;
        }
        if (pipelineStatsPool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_->device, pipelineStatsPool_, nullptr);
            pipelineStatsPool_ = VK_NULL_HANDLE;
        }
    }
}

// ============================================================================
// COMMAND BUFFER RECORDING
// ============================================================================

void GPUTimestampQuery::ResetQueries(VkCommandBuffer cmdBuffer) {
    resultsValid_ = false;

    if (timestampPool_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmdBuffer, timestampPool_, 0, maxTimestamps_);
    }
    if (pipelineStatsPool_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmdBuffer, pipelineStatsPool_, 0, 1);
    }
}

void GPUTimestampQuery::WriteTimestamp(VkCommandBuffer cmdBuffer, VkPipelineStageFlagBits pipelineStage, uint32_t queryIndex) {
    if (timestampPool_ == VK_NULL_HANDLE || queryIndex >= maxTimestamps_) {
        return;
    }
    vkCmdWriteTimestamp(cmdBuffer, pipelineStage, timestampPool_, queryIndex);
}

void GPUTimestampQuery::BeginPipelineStats(VkCommandBuffer cmdBuffer) {
    if (pipelineStatsPool_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBeginQuery(cmdBuffer, pipelineStatsPool_, 0, 0);
}

void GPUTimestampQuery::EndPipelineStats(VkCommandBuffer cmdBuffer) {
    if (pipelineStatsPool_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndQuery(cmdBuffer, pipelineStatsPool_, 0);
}

// ============================================================================
// RESULT RETRIEVAL
// ============================================================================

bool GPUTimestampQuery::ReadResults() {
    resultsValid_ = false;

    // Read timestamp results
    // Note: We use VK_QUERY_RESULT_WAIT_BIT to ensure results are ready.
    // Caller (GPUPerformanceLogger::CollectResults) must ensure this is only
    // called after the command buffer has been submitted and fence waited.
    if (timestampPool_ != VK_NULL_HANDLE) {
        // First check if results are available without blocking
        VkResult result = vkGetQueryPoolResults(
            device_->device,
            timestampPool_,
            0, maxTimestamps_,
            timestampResults_.size() * sizeof(uint64_t),
            timestampResults_.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT  // No WAIT_BIT - return immediately if not ready
        );

        if (result == VK_NOT_READY) {
            // Results not ready yet - try again next frame
            return false;
        }
        if (result != VK_SUCCESS) {
            return false;
        }
    }

    // Read pipeline statistics results
    if (pipelineStatsPool_ != VK_NULL_HANDLE) {
        VkResult result = vkGetQueryPoolResults(
            device_->device,
            pipelineStatsPool_,
            0, 1,
            pipelineStatsResults_.size() * sizeof(uint64_t),
            pipelineStatsResults_.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT  // No WAIT_BIT
        );

        if (result == VK_NOT_READY) {
            return false;
        }
        if (result != VK_SUCCESS) {
            return false;
        }
    }

    resultsValid_ = true;
    return true;
}

uint64_t GPUTimestampQuery::GetTimestamp(uint32_t queryIndex) const {
    if (!resultsValid_ || queryIndex >= maxTimestamps_) {
        return 0;
    }
    return timestampResults_[queryIndex];
}

float GPUTimestampQuery::GetElapsedMs(uint32_t startIndex, uint32_t endIndex) const {
    return static_cast<float>(GetElapsedNs(startIndex, endIndex)) / 1000000.0f;
}

uint64_t GPUTimestampQuery::GetElapsedNs(uint32_t startIndex, uint32_t endIndex) const {
    if (!resultsValid_ || startIndex >= maxTimestamps_ || endIndex >= maxTimestamps_) {
        return 0;
    }

    uint64_t startTs = timestampResults_[startIndex];
    uint64_t endTs = timestampResults_[endIndex];

    if (endTs <= startTs) {
        return 0;  // Handle wraparound or invalid data
    }

    uint64_t deltaTicks = endTs - startTs;
    return static_cast<uint64_t>(static_cast<double>(deltaTicks) * static_cast<double>(timestampPeriod_));
}

uint64_t GPUTimestampQuery::GetStatistic(PipelineStatistic stat) const {
    if (!resultsValid_ || !pipelineStatsEnabled_) {
        return 0;
    }

    uint32_t index = static_cast<uint32_t>(stat);
    if (index >= pipelineStatsResults_.size()) {
        return 0;
    }
    return pipelineStatsResults_[index];
}

float GPUTimestampQuery::CalculateMraysPerSec(uint32_t startIndex, uint32_t endIndex, uint32_t width, uint32_t height) const {
    float elapsedMs = GetElapsedMs(startIndex, endIndex);
    if (elapsedMs <= 0.0f) {
        return 0.0f;
    }

    uint64_t totalRays = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    // rays/ms * 1000 = rays/sec, then / 1,000,000 = Mrays/sec
    // Simplified: rays / (ms * 1000) = Mrays/sec
    float raysPerMs = static_cast<float>(totalRays) / elapsedMs;
    return raysPerMs / 1000.0f;  // rays/ms / 1000 = Mrays/sec (rays/ms = 1000 rays/sec)
}

} // namespace Vixen::Vulkan::Resources
