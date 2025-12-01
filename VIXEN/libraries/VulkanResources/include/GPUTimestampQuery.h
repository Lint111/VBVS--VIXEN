#pragma once

#include "Headers.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace Vixen::Vulkan::Resources {

class VulkanDevice;

/**
 * @brief GPU timestamp and pipeline statistics query manager
 *
 * Provides accurate GPU-side timing measurements for compute/graphics operations.
 * Supports both timestamp queries (nanosecond precision) and pipeline statistics.
 *
 * Usage:
 * @code
 * GPUTimestampQuery query(device, 4, true);  // 4 timestamps, with pipeline stats
 *
 * // In command buffer recording:
 * query.ResetQueries(cmdBuffer);
 * query.WriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);  // Start
 * query.BeginPipelineStats(cmdBuffer);
 * vkCmdDispatch(cmdBuffer, ...);
 * query.EndPipelineStats(cmdBuffer);
 * query.WriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);  // End
 *
 * // After queue submission + fence wait:
 * query.ReadResults();
 * float dispatchMs = query.GetElapsedMs(0, 1);
 * uint64_t invocations = query.GetStatistic(PipelineStatistic::ComputeShaderInvocations);
 * @endcode
 */
class GPUTimestampQuery {
public:
    /**
     * @brief Pipeline statistics that can be queried
     */
    enum class PipelineStatistic : uint32_t {
        InputAssemblyVertices = 0,
        InputAssemblyPrimitives = 1,
        VertexShaderInvocations = 2,
        ClippingInvocations = 3,
        ClippingPrimitives = 4,
        FragmentShaderInvocations = 5,
        ComputeShaderInvocations = 6,
        Count = 7
    };

    /**
     * @brief Construct GPU query manager
     * @param device Vulkan device (must outlive this object)
     * @param maxTimestamps Maximum number of timestamp queries (default 8)
     * @param enablePipelineStats Enable pipeline statistics queries
     */
    GPUTimestampQuery(VulkanDevice* device, uint32_t maxTimestamps = 8, bool enablePipelineStats = true);
    ~GPUTimestampQuery();

    // Non-copyable, movable
    GPUTimestampQuery(const GPUTimestampQuery&) = delete;
    GPUTimestampQuery& operator=(const GPUTimestampQuery&) = delete;
    GPUTimestampQuery(GPUTimestampQuery&&) noexcept;
    GPUTimestampQuery& operator=(GPUTimestampQuery&&) noexcept;

    /**
     * @brief Check if timestamp queries are supported on this device
     */
    bool IsTimestampSupported() const { return timestampSupported_; }

    /**
     * @brief Check if pipeline statistics are enabled and supported
     */
    bool IsPipelineStatsEnabled() const { return pipelineStatsEnabled_; }

    /**
     * @brief Get the timestamp period in nanoseconds per tick
     */
    float GetTimestampPeriod() const { return timestampPeriod_; }

    // ========================================================================
    // COMMAND BUFFER RECORDING
    // ========================================================================

    /**
     * @brief Reset query pools before use (must be called outside render pass)
     * @param cmdBuffer Command buffer to record reset into
     */
    void ResetQueries(VkCommandBuffer cmdBuffer);

    /**
     * @brief Write a timestamp at the specified pipeline stage
     * @param cmdBuffer Command buffer to record into
     * @param pipelineStage Pipeline stage to write timestamp at
     * @param queryIndex Index of the timestamp query (0 to maxTimestamps-1)
     */
    void WriteTimestamp(VkCommandBuffer cmdBuffer, VkPipelineStageFlagBits pipelineStage, uint32_t queryIndex);

    /**
     * @brief Begin pipeline statistics query
     * @param cmdBuffer Command buffer to record into
     */
    void BeginPipelineStats(VkCommandBuffer cmdBuffer);

    /**
     * @brief End pipeline statistics query
     * @param cmdBuffer Command buffer to record into
     */
    void EndPipelineStats(VkCommandBuffer cmdBuffer);

    // ========================================================================
    // RESULT RETRIEVAL (after queue submission + fence wait)
    // ========================================================================

    /**
     * @brief Read all query results from GPU
     * @return true if results are available, false if not ready
     */
    bool ReadResults();

    /**
     * @brief Get raw timestamp value at index
     * @param queryIndex Timestamp query index
     * @return Timestamp in GPU ticks (0 if invalid)
     */
    uint64_t GetTimestamp(uint32_t queryIndex) const;

    /**
     * @brief Get elapsed time between two timestamps in milliseconds
     * @param startIndex Start timestamp query index
     * @param endIndex End timestamp query index
     * @return Elapsed time in milliseconds
     */
    float GetElapsedMs(uint32_t startIndex, uint32_t endIndex) const;

    /**
     * @brief Get elapsed time between two timestamps in nanoseconds
     * @param startIndex Start timestamp query index
     * @param endIndex End timestamp query index
     * @return Elapsed time in nanoseconds
     */
    uint64_t GetElapsedNs(uint32_t startIndex, uint32_t endIndex) const;

    /**
     * @brief Get pipeline statistic value
     * @param stat Statistic to retrieve
     * @return Statistic value (0 if not available)
     */
    uint64_t GetStatistic(PipelineStatistic stat) const;

    /**
     * @brief Calculate Mrays/sec given dispatch dimensions
     * @param startIndex Start timestamp query index
     * @param endIndex End timestamp query index
     * @param width Dispatch width in pixels
     * @param height Dispatch height in pixels
     * @return Mrays/sec (millions of rays per second)
     */
    float CalculateMraysPerSec(uint32_t startIndex, uint32_t endIndex, uint32_t width, uint32_t height) const;

private:
    void CreateQueryPools();
    void DestroyQueryPools();

    VulkanDevice* device_ = nullptr;

    // Timestamp queries
    VkQueryPool timestampPool_ = VK_NULL_HANDLE;
    uint32_t maxTimestamps_ = 8;
    bool timestampSupported_ = false;
    float timestampPeriod_ = 0.0f;  // nanoseconds per tick
    std::vector<uint64_t> timestampResults_;

    // Pipeline statistics
    VkQueryPool pipelineStatsPool_ = VK_NULL_HANDLE;
    bool pipelineStatsEnabled_ = false;
    std::vector<uint64_t> pipelineStatsResults_;

    // State tracking
    bool resultsValid_ = false;
};

} // namespace Vixen::Vulkan::Resources
