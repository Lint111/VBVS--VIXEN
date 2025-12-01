#pragma once

#include "Headers.h"
#include <vector>
#include <string>

namespace Vixen::Vulkan::Resources {

class VulkanDevice;

/**
 * @brief GPU timestamp query manager with per-frame query pools
 *
 * Supports multiple frames-in-flight by maintaining separate query pools for each frame.
 * This allows reading results from frame N-1 while recording queries for frame N.
 *
 * Usage:
 * @code
 * GPUTimestampQuery query(device, 3, 4);  // 3 frames-in-flight, 4 timestamps each
 *
 * // Each frame:
 * uint32_t frameIdx = currentFrameIndex % framesInFlight;
 *
 * // Read previous frame's results (after fence wait)
 * if (query.ReadResults(frameIdx)) {
 *     float ms = query.GetElapsedMs(frameIdx, 0, 1);
 * }
 *
 * // Record new queries
 * query.ResetQueries(cmdBuffer, frameIdx);
 * query.WriteTimestamp(cmdBuffer, frameIdx, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
 * vkCmdDispatch(...);
 * query.WriteTimestamp(cmdBuffer, frameIdx, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
 * @endcode
 */
class GPUTimestampQuery {
public:
    /**
     * @brief Construct GPU query manager with per-frame pools
     * @param device Vulkan device (must outlive this object)
     * @param framesInFlight Number of frames-in-flight (typically 2-3)
     * @param maxTimestamps Maximum timestamps per frame (default 4)
     */
    GPUTimestampQuery(VulkanDevice* device, uint32_t framesInFlight, uint32_t maxTimestamps = 4);
    ~GPUTimestampQuery();

    // Non-copyable, movable
    GPUTimestampQuery(const GPUTimestampQuery&) = delete;
    GPUTimestampQuery& operator=(const GPUTimestampQuery&) = delete;
    GPUTimestampQuery(GPUTimestampQuery&&) noexcept;
    GPUTimestampQuery& operator=(GPUTimestampQuery&&) noexcept;

    /**
     * @brief Check if timestamp queries are supported
     */
    bool IsTimestampSupported() const { return timestampSupported_; }

    /**
     * @brief Get timestamp period in nanoseconds per tick
     */
    float GetTimestampPeriod() const { return timestampPeriod_; }

    /**
     * @brief Get number of frames-in-flight
     */
    uint32_t GetFrameCount() const { return framesInFlight_; }

    // ========================================================================
    // COMMAND BUFFER RECORDING (per-frame)
    // ========================================================================

    /**
     * @brief Reset queries for a specific frame (call at start of frame)
     * @param cmdBuffer Command buffer to record reset into
     * @param frameIndex Frame-in-flight index (0 to framesInFlight-1)
     */
    void ResetQueries(VkCommandBuffer cmdBuffer, uint32_t frameIndex);

    /**
     * @brief Write timestamp for a specific frame
     * @param cmdBuffer Command buffer to record into
     * @param frameIndex Frame-in-flight index
     * @param pipelineStage Pipeline stage to write timestamp at
     * @param queryIndex Query index within the frame (0 to maxTimestamps-1)
     */
    void WriteTimestamp(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
                        VkPipelineStageFlagBits pipelineStage, uint32_t queryIndex);

    // ========================================================================
    // RESULT RETRIEVAL (per-frame, after fence wait)
    // ========================================================================

    /**
     * @brief Read results for a specific frame
     * @param frameIndex Frame-in-flight index to read from
     * @return true if results are valid
     */
    bool ReadResults(uint32_t frameIndex);

    /**
     * @brief Get elapsed time in milliseconds for a frame
     * @param frameIndex Frame-in-flight index
     * @param startQuery Start query index
     * @param endQuery End query index
     */
    float GetElapsedMs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const;

    /**
     * @brief Get elapsed time in nanoseconds for a frame
     */
    uint64_t GetElapsedNs(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery) const;

    /**
     * @brief Calculate Mrays/sec
     */
    float CalculateMraysPerSec(uint32_t frameIndex, uint32_t startQuery, uint32_t endQuery,
                                uint32_t width, uint32_t height) const;

private:
    struct PerFrameData {
        VkQueryPool timestampPool = VK_NULL_HANDLE;
        std::vector<uint64_t> results;
        bool resultsValid = false;
        bool hasBeenWritten = false;  // Track if timestamps were written this frame
    };

    void CreateQueryPools();
    void DestroyQueryPools();

    VulkanDevice* device_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t maxTimestamps_ = 4;
    bool timestampSupported_ = false;
    float timestampPeriod_ = 0.0f;

    std::vector<PerFrameData> frameData_;
};

} // namespace Vixen::Vulkan::Resources
