#include "GPUTimestampQuery.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace Vixen::Vulkan::Resources {

GPUTimestampQuery::GPUTimestampQuery(VulkanDevice* device, uint32_t framesInFlight, uint32_t maxTimestamps)
    : device_(device)
    , framesInFlight_(framesInFlight)
    , maxTimestamps_(maxTimestamps)
{
    if (!device_ || device_->device == VK_NULL_HANDLE) {
        throw std::runtime_error("[GPUTimestampQuery] Invalid Vulkan device");
    }

    if (framesInFlight_ == 0) {
        throw std::runtime_error("[GPUTimestampQuery] framesInFlight must be > 0");
    }

    // Check timestamp support
    timestampPeriod_ = device_->gpuProperties.limits.timestampPeriod;
    timestampSupported_ = (timestampPeriod_ > 0.0f);

    // Check if queue supports timestamps
    if (timestampSupported_) {
        uint32_t queueFamily = device_->graphicsQueueIndex;
        if (queueFamily < device_->queueFamilyProperties.size()) {
            uint32_t validBits = device_->queueFamilyProperties[queueFamily].timestampValidBits;
            timestampSupported_ = (validBits > 0);
        }
    }

    // Initialize per-frame data
    frameData_.resize(framesInFlight_);
    for (auto& frame : frameData_) {
        frame.results.resize(maxTimestamps_, 0);
    }

    if (timestampSupported_) {
        CreateQueryPools();
    }
}

GPUTimestampQuery::~GPUTimestampQuery() {
    DestroyQueryPools();
}

GPUTimestampQuery::GPUTimestampQuery(GPUTimestampQuery&& other) noexcept
    : device_(other.device_)
    , framesInFlight_(other.framesInFlight_)
    , maxTimestamps_(other.maxTimestamps_)
    , timestampSupported_(other.timestampSupported_)
    , timestampPeriod_(other.timestampPeriod_)
    , frameData_(std::move(other.frameData_))
{
    other.device_ = nullptr;
    other.frameData_.clear();
}

GPUTimestampQuery& GPUTimestampQuery::operator=(GPUTimestampQuery&& other) noexcept {
    if (this != &other) {
        DestroyQueryPools();
        device_ = other.device_;
        framesInFlight_ = other.framesInFlight_;
        maxTimestamps_ = other.maxTimestamps_;
        timestampSupported_ = other.timestampSupported_;
        timestampPeriod_ = other.timestampPeriod_;
        frameData_ = std::move(other.frameData_);

        other.device_ = nullptr;
        other.frameData_.clear();
    }
    return *this;
}

void GPUTimestampQuery::CreateQueryPools() {
    if (!timestampSupported_ || maxTimestamps_ == 0) {
        return;
    }

    for (auto& frame : frameData_) {
        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        createInfo.queryCount = maxTimestamps_;

        VkResult result = vkCreateQueryPool(device_->device, &createInfo, nullptr, &frame.timestampPool);
        if (result != VK_SUCCESS) {
            // Cleanup already created pools
            DestroyQueryPools();
            timestampSupported_ = false;
            return;
        }
    }
}

void GPUTimestampQuery::DestroyQueryPools() {
    // Safety check: If device_ is null, we cannot destroy query pools.
    // This can happen if the object was moved from, or if cleanup is called
    // after the parent VulkanDevice was destroyed (use-after-free scenario).
    // In the latter case, we must NOT dereference device_ at all.
    if (!device_) {
        // Clear handles to prevent double-destruction attempts
        for (auto& frame : frameData_) {
            frame.timestampPool = VK_NULL_HANDLE;
        }
        return;
    }

    // Validate device handle before attempting destruction
    if (device_->device != VK_NULL_HANDLE) {
        for (auto& frame : frameData_) {
            if (frame.timestampPool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device_->device, frame.timestampPool, nullptr);
                frame.timestampPool = VK_NULL_HANDLE;
            }
        }
    }

    // Clear device pointer to prevent any further use after destruction
    device_ = nullptr;
}

// ============================================================================
// COMMAND BUFFER RECORDING
// ============================================================================

void GPUTimestampQuery::ResetQueries(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!timestampSupported_ || frameIndex >= framesInFlight_) {
        return;
    }

    auto& frame = frameData_[frameIndex];
    if (frame.timestampPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmdBuffer, frame.timestampPool, 0, maxTimestamps_);
        frame.resultsValid = false;
        // NOTE: Don't reset hasBeenWritten here - it tracks if timestamps
        // were written in the PREVIOUS frame's command buffer for this slot.
        // WriteTimestamp will set it true, and we'll read those results
        // after the fence for this slot is waited on.
    }
}

void GPUTimestampQuery::WriteTimestamp(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                        VkPipelineStageFlagBits pipelineStage, uint32_t queryIndex) {
    if (!timestampSupported_ || frameIndex >= framesInFlight_ || queryIndex >= maxTimestamps_) {
        return;
    }

    auto& frame = frameData_[frameIndex];
    if (frame.timestampPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(cmdBuffer, pipelineStage, frame.timestampPool, queryIndex);
        frame.hasBeenWritten = true;
    }
}

// ============================================================================
// RESULT RETRIEVAL
// ============================================================================

bool GPUTimestampQuery::ReadResults(uint32_t frameIndex) {
    if (!timestampSupported_ || frameIndex >= framesInFlight_) {
        return false;
    }

    auto& frame = frameData_[frameIndex];
    frame.resultsValid = false;

    // Only read if timestamps were actually written
    if (!frame.hasBeenWritten) {
        return false;
    }

    if (frame.timestampPool == VK_NULL_HANDLE) {
        return false;
    }

    // Only read queries that were actually written (2: start and end timestamp)
    // We allocate maxTimestamps_ for flexibility, but only use 2 currently
    const uint32_t queriesToRead = 2;
    VkResult result = vkGetQueryPoolResults(
        device_->device,
        frame.timestampPool,
        0, queriesToRead,
        queriesToRead * sizeof(uint64_t),
        frame.results.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT  // No WAIT_BIT - return immediately if not ready
    );

    if (result == VK_SUCCESS) {
        frame.resultsValid = true;
        return true;
    }

    return false;
}

float GPUTimestampQuery::GetElapsedMs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const {
    return static_cast<float>(GetElapsedNs(frameIndex, startQuery, endQuery)) / 1000000.0f;
}

uint64_t GPUTimestampQuery::GetElapsedNs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const {
    if (!timestampSupported_ || frameIndex >= framesInFlight_) {
        return 0;
    }

    const auto& frame = frameData_[frameIndex];
    if (!frame.resultsValid || startQuery >= maxTimestamps_ || endQuery >= maxTimestamps_) {
        return 0;
    }

    uint64_t startTs = frame.results[startQuery];
    uint64_t endTs = frame.results[endQuery];

    if (endTs <= startTs) {
        return 0;  // Handle wraparound or invalid data
    }

    uint64_t deltaTicks = endTs - startTs;
    return static_cast<uint64_t>(static_cast<double>(deltaTicks) * static_cast<double>(timestampPeriod_));
}

float GPUTimestampQuery::CalculateMraysPerSec(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery,
                                              uint32_t width, uint32_t height) const {
    float elapsedMs = GetElapsedMs(frameIndex, startQuery, endQuery);
    if (elapsedMs <= 0.0f) {
        return 0.0f;
    }

    uint64_t totalRays = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    float raysPerMs = static_cast<float>(totalRays) / elapsedMs;
    return raysPerMs / 1000.0f;  // rays/ms / 1000 = Mrays/sec
}

} // namespace Vixen::Vulkan::Resources
