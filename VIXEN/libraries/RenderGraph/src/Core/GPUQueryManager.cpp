#include "Core/GPUQueryManager.h"
#include "GPUTimestampQuery.h"
#include <algorithm>
#include <stdexcept>
#include <cstdio>

namespace Vixen::RenderGraph {

GPUQueryManager::GPUQueryManager(VulkanDevice* device, uint32_t framesInFlight, uint32_t maxConsumers)
    : device_(device)
    , framesInFlight_(framesInFlight)
    , maxConsumers_(maxConsumers)
{
    if (!device) {
        throw std::invalid_argument("GPUQueryManager: device cannot be null");
    }

    if (framesInFlight == 0) {
        throw std::invalid_argument("GPUQueryManager: framesInFlight must be > 0");
    }

    if (maxConsumers == 0) {
        throw std::invalid_argument("GPUQueryManager: maxConsumers must be > 0");
    }

    // Initialize slot allocations
    slots_.resize(maxConsumers);
    for (uint32_t i = 0; i < maxConsumers; ++i) {
        slots_[i].allocated = false;
    }

    // Initialize per-frame data
    frameData_.resize(framesInFlight);
    for (auto& frame : frameData_) {
        frame.slots.resize(maxConsumers);
    }

    // Create underlying query pool (2 queries per slot: start + end)
    // NOTE: If device is invalid (e.g., mock device in tests), this will throw
    // and query_ will remain nullptr. GPUQueryManager will still be usable
    // for testing slot allocation logic, but timestamp operations will be no-ops.
    try {
        uint32_t totalQueries = maxConsumers * 2;
        query_ = std::make_unique<GPUTimestampQuery>(device, framesInFlight, totalQueries);
    } catch (const std::runtime_error& e) {
        // GPUTimestampQuery creation failed (likely mock device in tests or unsupported GPU)
        // Log the error so it's not silent, but allow manager to continue for test purposes
        // In production, this indicates timestamp queries are not available

        // Note: Using stderr since we don't have a logger instance in constructor
        std::fprintf(stderr, "[GPUQueryManager] WARNING: Failed to create GPU query pool: %s\n"
                            "GPU timestamp queries will not be available. All timing operations will be no-ops.\n",
                     e.what());

        query_.reset();  // Explicitly set to nullptr - timestamp operations will be no-ops
    }
}

GPUQueryManager::~GPUQueryManager() {
    ReleaseGPUResources();
}

GPUQueryManager::GPUQueryManager(GPUQueryManager&& other) noexcept
    : device_(other.device_)
    , framesInFlight_(other.framesInFlight_)
    , maxConsumers_(other.maxConsumers_)
    , slots_(std::move(other.slots_))
    , frameData_(std::move(other.frameData_))
    , query_(std::move(other.query_))
{
    other.device_ = nullptr;
    other.framesInFlight_ = 0;
    other.maxConsumers_ = 0;
}

GPUQueryManager& GPUQueryManager::operator=(GPUQueryManager&& other) noexcept {
    if (this != &other) {
        ReleaseGPUResources();

        device_ = other.device_;
        framesInFlight_ = other.framesInFlight_;
        maxConsumers_ = other.maxConsumers_;
        slots_ = std::move(other.slots_);
        frameData_ = std::move(other.frameData_);
        query_ = std::move(other.query_);

        other.device_ = nullptr;
        other.framesInFlight_ = 0;
        other.maxConsumers_ = 0;
    }
    return *this;
}

bool GPUQueryManager::IsTimestampSupported() const {
    return query_ && query_->IsTimestampSupported();
}

float GPUQueryManager::GetTimestampPeriod() const {
    return query_ ? query_->GetTimestampPeriod() : 0.0f;
}

// ========================================================================
// SLOT ALLOCATION
// ========================================================================

GPUQueryManager::QuerySlotHandle GPUQueryManager::AllocateQuerySlot(const std::string& consumerName) {
    // Find first free slot
    for (uint32_t i = 0; i < maxConsumers_; ++i) {
        if (!slots_[i].allocated) {
            slots_[i].allocated = true;
            slots_[i].consumerName = consumerName;
            slots_[i].startQueryIndex = i * 2;      // Even indices for start
            slots_[i].endQueryIndex = i * 2 + 1;    // Odd indices for end
            return static_cast<QuerySlotHandle>(i);
        }
    }

    // No free slots available
    return INVALID_SLOT;
}

void GPUQueryManager::FreeQuerySlot(QuerySlotHandle slot) {
    if (!IsSlotValid(slot)) {
        return;
    }

    slots_[slot].allocated = false;
    slots_[slot].consumerName.clear();

    // Clear per-frame tracking for this slot
    for (auto& frame : frameData_) {
        frame.slots[slot].startWritten = false;
        frame.slots[slot].endWritten = false;
    }
}

uint32_t GPUQueryManager::GetAllocatedSlotCount() const {
    return static_cast<uint32_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const SlotAllocation& s) {
            return s.allocated;
        })
    );
}

std::string GPUQueryManager::GetSlotConsumerName(QuerySlotHandle slot) const {
    if (!IsSlotValid(slot) || !slots_[slot].allocated) {
        return "";
    }
    return slots_[slot].consumerName;
}

// ========================================================================
// COMMAND BUFFER RECORDING
// ========================================================================

void GPUQueryManager::BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (frameIndex >= framesInFlight_) {
        throw std::out_of_range("GPUQueryManager::BeginFrame: frameIndex out of range");
    }

    if (!query_) {
        return;  // Queries not supported or released
    }

    // Reset all queries for this frame
    query_->ResetQueries(cmdBuffer, frameIndex);

    // Clear per-frame tracking
    auto& frame = frameData_[frameIndex];
    frame.resultsRead = false;
    for (auto& slot : frame.slots) {
        slot.startWritten = false;
        slot.endWritten = false;
    }
}

void GPUQueryManager::WriteTimestamp(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                                     QuerySlotHandle slot, VkPipelineStageFlagBits pipelineStage) {
    if (frameIndex >= framesInFlight_) {
        throw std::out_of_range("GPUQueryManager::WriteTimestamp: frameIndex out of range");
    }

    if (!IsSlotAllocated(slot)) {
        throw std::invalid_argument("GPUQueryManager::WriteTimestamp: invalid or unallocated slot");
    }

    if (!query_) {
        return;  // Queries not supported or released
    }

    auto& frame = frameData_[frameIndex];
    auto& slotData = frame.slots[slot];

    // Determine which timestamp to write (start or end)
    uint32_t queryIndex;
    if (!slotData.startWritten) {
        // Write start timestamp
        queryIndex = slots_[slot].startQueryIndex;
        slotData.startWritten = true;
    } else if (!slotData.endWritten) {
        // Write end timestamp
        queryIndex = slots_[slot].endQueryIndex;
        slotData.endWritten = true;
    } else {
        // Both timestamps already written - this is likely a bug in consumer code
        throw std::logic_error("GPUQueryManager::WriteTimestamp: slot already has both timestamps written");
    }

    query_->WriteTimestamp(cmdBuffer, frameIndex, pipelineStage, queryIndex);
}

// ========================================================================
// RESULT RETRIEVAL
// ========================================================================

bool GPUQueryManager::ReadAllResults(uint32_t frameIndex) {
    if (frameIndex >= framesInFlight_) {
        throw std::out_of_range("GPUQueryManager::ReadAllResults: frameIndex out of range");
    }

    if (!query_) {
        return false;  // Queries not supported or released
    }

    auto& frame = frameData_[frameIndex];
    if (frame.resultsRead) {
        return true;  // Already read this frame
    }

    frame.resultsRead = query_->ReadResults(frameIndex);
    return frame.resultsRead;
}

bool GPUQueryManager::TryReadTimestamps(uint32_t frameIndex, QuerySlotHandle slot) {
    if (frameIndex >= framesInFlight_) {
        return false;
    }

    if (!IsSlotAllocated(slot)) {
        return false;
    }

    if (!query_) {
        return false;
    }

    // Ensure results have been read for this frame
    if (!frameData_[frameIndex].resultsRead) {
        if (!ReadAllResults(frameIndex)) {
            return false;
        }
    }

    // Check if both timestamps were written
    const auto& slotData = frameData_[frameIndex].slots[slot];
    return slotData.startWritten && slotData.endWritten;
}

uint64_t GPUQueryManager::GetElapsedNs(uint32_t frameIndex, QuerySlotHandle slot) const {
    if (frameIndex >= framesInFlight_) {
        return 0;
    }

    if (!IsSlotAllocated(slot)) {
        return 0;
    }

    if (!query_) {
        return 0;
    }

    const auto& frame = frameData_[frameIndex];
    if (!frame.resultsRead) {
        return 0;  // Results not available yet
    }

    const auto& slotData = frame.slots[slot];
    if (!slotData.startWritten || !slotData.endWritten) {
        return 0;  // Timestamps not written
    }

    // Get elapsed time from underlying query
    uint32_t startIndex = slots_[slot].startQueryIndex;
    uint32_t endIndex = slots_[slot].endQueryIndex;
    return query_->GetElapsedNs(frameIndex, startIndex, endIndex);
}

float GPUQueryManager::GetElapsedMs(uint32_t frameIndex, QuerySlotHandle slot) const {
    uint64_t ns = GetElapsedNs(frameIndex, slot);
    return static_cast<float>(ns) / 1'000'000.0f;
}

void GPUQueryManager::ReleaseGPUResources() {
    query_.reset();
}

// ========================================================================
// PRIVATE HELPERS
// ========================================================================

bool GPUQueryManager::IsSlotValid(QuerySlotHandle slot) const {
    return slot < maxConsumers_;
}

bool GPUQueryManager::IsSlotAllocated(QuerySlotHandle slot) const {
    return IsSlotValid(slot) && slots_[slot].allocated;
}

} // namespace Vixen::RenderGraph
