#pragma once

#include "Headers.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace Vixen::Vulkan::Resources {
class VulkanDevice;
class GPUTimestampQuery;
} // namespace Vixen::Vulkan::Resources

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

/**
 * @brief Shared GPU query pool coordinator for multiple consumers
 *
 * Prevents query slot conflicts between ProfilerSystem, TimelineCapacityTracker,
 * and other systems that need GPU timestamp queries. Manages query slot allocation,
 * timestamp writes, and result reads across multiple consumers.
 *
 * Each consumer receives a unique QuerySlotHandle that maps to physical query indices
 * in the underlying per-frame query pools.
 *
 * Thread-safety: Not thread-safe. All methods must be called from the same thread.
 *
 * Usage:
 * @code
 * // Create manager for device with 3 frames-in-flight
 * auto queryMgr = std::make_shared<GPUQueryManager>(device, 3);
 *
 * // Consumer 1 (e.g., ProfilerSystem)
 * auto profilerSlot = queryMgr->AllocateQuerySlot("Profiler");
 *
 * // Consumer 2 (e.g., TimelineCapacityTracker)
 * auto trackerSlot = queryMgr->AllocateQuerySlot("CapacityTracker");
 *
 * // Each frame:
 * uint32_t frameIdx = currentFrameIndex % framesInFlight;
 *
 * queryMgr->BeginFrame(cmdBuffer, frameIdx);
 * queryMgr->WriteTimestamp(cmdBuffer, frameIdx, profilerSlot, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
 * // ... GPU work ...
 * queryMgr->WriteTimestamp(cmdBuffer, frameIdx, profilerSlot, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
 *
 * // After fence wait, read results
 * if (queryMgr->TryReadTimestamps(frameIdx, profilerSlot)) {
 *     uint64_t elapsedNs = queryMgr->GetElapsedNs(frameIdx, profilerSlot);
 * }
 * @endcode
 */
class GPUQueryManager {
public:
    /**
     * @brief Opaque handle to a consumer's query slot pair (start + end timestamp)
     *
     * Each slot reserves 2 physical query indices: one for start, one for end.
     * Valid values are 0+. Invalid handle is represented by value 0xFFFFFFFF.
     */
    using QuerySlotHandle = uint32_t;
    static constexpr QuerySlotHandle INVALID_SLOT = 0xFFFFFFFF;

    /**
     * @brief Construct query manager with per-frame pools
     * @param device Vulkan device (must outlive this object)
     * @param framesInFlight Number of frames-in-flight (typically 2-3)
     * @param maxConsumers Maximum number of consumers that can allocate slots (default 8)
     */
    GPUQueryManager(VulkanDevice* device, uint32_t framesInFlight, uint32_t maxConsumers = 8);
    ~GPUQueryManager();

    // Non-copyable, movable
    GPUQueryManager(const GPUQueryManager&) = delete;
    GPUQueryManager& operator=(const GPUQueryManager&) = delete;
    GPUQueryManager(GPUQueryManager&&) noexcept;
    GPUQueryManager& operator=(GPUQueryManager&&) noexcept;

    /**
     * @brief Check if timestamp queries are supported on this device
     */
    [[nodiscard]] bool IsTimestampSupported() const;

    /**
     * @brief Get timestamp period in nanoseconds per tick
     */
    [[nodiscard]] float GetTimestampPeriod() const;

    /**
     * @brief Get number of frames-in-flight
     */
    [[nodiscard]] uint32_t GetFrameCount() const { return framesInFlight_; }

    // ========================================================================
    // SLOT ALLOCATION
    // ========================================================================

    /**
     * @brief Allocate a query slot for a consumer
     *
     * Each slot reserves 2 query indices (start + end timestamp).
     * Call once during initialization, not every frame.
     *
     * @param consumerName Name for debugging (e.g., "ProfilerSystem")
     * @return Query slot handle, or INVALID_SLOT if allocation fails
     */
    [[nodiscard]] QuerySlotHandle AllocateQuerySlot(const std::string& consumerName);

    /**
     * @brief Free a previously allocated query slot
     *
     * Allows slot reuse. Call during cleanup or when consumer no longer needs queries.
     *
     * @param slot Query slot handle to free
     */
    void FreeQuerySlot(QuerySlotHandle slot);

    /**
     * @brief Get number of currently allocated slots
     */
    [[nodiscard]] uint32_t GetAllocatedSlotCount() const;

    /**
     * @brief Get maximum number of allocatable slots
     */
    [[nodiscard]] uint32_t GetMaxSlotCount() const { return maxConsumers_; }

    /**
     * @brief Get consumer name for a slot (for debugging)
     * @return Consumer name, or empty string if slot is invalid/freed
     */
    [[nodiscard]] std::string GetSlotConsumerName(QuerySlotHandle slot) const;

    // ========================================================================
    // COMMAND BUFFER RECORDING (per-frame)
    // ========================================================================

    /**
     * @brief Begin frame - reset queries for all slots
     *
     * Call at start of frame before any WriteTimestamp calls.
     *
     * @param cmdBuffer Command buffer to record reset into
     * @param frameIndex Frame-in-flight index (0 to framesInFlight-1)
     */
    void BeginFrame(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /**
     * @brief Write timestamp for a specific consumer slot
     *
     * Call once for start timestamp, once for end timestamp.
     * BeginFrame must be called first in this frame.
     *
     * @param cmdBuffer Command buffer to record into
     * @param frameIndex Frame-in-flight index
     * @param slot Query slot handle
     * @param pipelineStage Pipeline stage to write timestamp at
     */
    void WriteTimestamp(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                        QuerySlotHandle slot, VkPipelineStageFlagBits pipelineStage);

    // ========================================================================
    // RESULT RETRIEVAL (per-frame, after fence wait)
    // ========================================================================

    /**
     * @brief Read results for all slots in this frame
     *
     * Call after fence wait, before reading individual slot results.
     * Required before calling GetElapsedNs or GetElapsedMs.
     *
     * @param frameIndex Frame-in-flight index to read from
     * @return true if results are valid
     */
    bool ReadAllResults(uint32_t frameIndex);

    /**
     * @brief Try to read timestamps for a specific slot
     *
     * Checks if timestamps were written and results are available.
     * Automatically calls ReadAllResults if not already called this frame.
     *
     * @param frameIndex Frame-in-flight index
     * @param slot Query slot handle
     * @return true if timestamps are available for this slot
     */
    [[nodiscard]] bool TryReadTimestamps(uint32_t frameIndex, QuerySlotHandle slot);

    /**
     * @brief Get elapsed time in nanoseconds for a slot
     *
     * TryReadTimestamps must have returned true for this frame/slot.
     *
     * @param frameIndex Frame-in-flight index
     * @param slot Query slot handle
     * @return Elapsed nanoseconds, or 0 if timestamps not available
     */
    [[nodiscard]] uint64_t GetElapsedNs(uint32_t frameIndex, QuerySlotHandle slot) const;

    /**
     * @brief Get elapsed time in milliseconds for a slot
     *
     * TryReadTimestamps must have returned true for this frame/slot.
     *
     * @param frameIndex Frame-in-flight index
     * @param slot Query slot handle
     * @return Elapsed milliseconds, or 0.0f if timestamps not available
     */
    [[nodiscard]] float GetElapsedMs(uint32_t frameIndex, QuerySlotHandle slot) const;

    /**
     * @brief Release GPU resources (QueryPools) while device is still valid
     *
     * Call during cleanup phase BEFORE the VkDevice is destroyed.
     * The manager object remains valid for queries, but timing will no longer function.
     */
    void ReleaseGPUResources();

private:
    struct SlotAllocation {
        std::string consumerName;
        uint32_t startQueryIndex = 0;  // Physical query index for start timestamp
        uint32_t endQueryIndex = 0;    // Physical query index for end timestamp
        bool allocated = false;
    };

    struct PerFrameSlotData {
        bool startWritten = false;  // Track if start timestamp was written
        bool endWritten = false;    // Track if end timestamp was written
    };

    struct PerFrameData {
        std::vector<PerFrameSlotData> slots;
        bool resultsRead = false;  // Track if ReadAllResults was called this frame
    };

    VulkanDevice* device_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t maxConsumers_ = 0;

    std::vector<SlotAllocation> slots_;
    std::vector<PerFrameData> frameData_;  // One per frame-in-flight

    std::unique_ptr<GPUTimestampQuery> query_;

    [[nodiscard]] bool IsSlotValid(QuerySlotHandle slot) const;
    [[nodiscard]] bool IsSlotAllocated(QuerySlotHandle slot) const;
};

} // namespace Vixen::RenderGraph
