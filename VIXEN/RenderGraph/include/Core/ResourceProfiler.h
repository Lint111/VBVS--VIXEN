#pragma once

#include "Data/Core/Resource.h"
#include "StackResourceHandle.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <chrono>

namespace Vixen::RenderGraph {

/**
 * @brief Per-node resource usage statistics for a single frame
 */
struct NodeResourceStats {
    uint32_t nodeId = 0;
    std::string nodeName;

    // Allocation counts
    uint32_t stackAllocations = 0;
    uint32_t heapAllocations = 0;
    uint32_t vramAllocations = 0;

    // Bytes used
    size_t stackBytesUsed = 0;
    size_t heapBytesUsed = 0;
    size_t vramBytesUsed = 0;

    // Aliasing statistics
    uint32_t aliasedAllocations = 0;
    size_t bytesSavedViaAliasing = 0;

    // Performance metrics
    double allocationTimeMs = 0.0;
    double releaseTimeMs = 0.0;

    // Helper methods
    size_t GetTotalBytes() const {
        return stackBytesUsed + heapBytesUsed + vramBytesUsed;
    }

    uint32_t GetTotalAllocations() const {
        return stackAllocations + heapAllocations + vramAllocations;
    }

    float GetAliasingEfficiency() const {
        return vramBytesUsed > 0
            ? 100.0f * bytesSavedViaAliasing / (vramBytesUsed + bytesSavedViaAliasing)
            : 0.0f;
    }
};

/**
 * @brief Aggregated statistics across all nodes for a frame
 */
struct FrameResourceStats {
    uint64_t frameNumber = 0;

    // Totals across all nodes
    NodeResourceStats totals;

    // Per-node breakdown
    std::vector<NodeResourceStats> nodeStats;

    // Frame-level metrics
    double frameDurationMs = 0.0;
    size_t peakStackUsage = 0;
    size_t peakHeapUsage = 0;
    size_t peakVramUsage = 0;
};

/**
 * @brief Resource profiler for detailed per-node, per-frame tracking
 *
 * The ResourceProfiler tracks all resource allocations and releases,
 * providing detailed statistics for performance analysis and optimization.
 *
 * Example usage:
 * @code
 * ResourceProfiler profiler;
 * profiler.BeginFrame(frameNumber);
 *
 * // During render graph execution...
 * profiler.RecordAllocation(nodeId, "ShadowPass", ResourceLocation::VRAM, 4096000);
 *
 * profiler.EndFrame();
 *
 * // Query statistics
 * auto stats = profiler.GetCurrentFrameStats();
 * std::cout << profiler.ExportAsText(frameNumber) << std::endl;
 * @endcode
 */
class ResourceProfiler {
public:
    ResourceProfiler();
    ~ResourceProfiler();

    // === Frame Lifecycle ===

    /**
     * @brief Begin tracking a new frame
     * @param frameNumber The current frame number
     */
    void BeginFrame(uint64_t frameNumber);

    /**
     * @brief End tracking the current frame and compute final statistics
     */
    void EndFrame();

    // === Recording ===

    /**
     * @brief Record a resource allocation
     * @param nodeId The ID of the node performing the allocation
     * @param nodeName The name of the node (for reporting)
     * @param location Where the resource was allocated (Stack/Heap/VRAM)
     * @param bytes Size of the allocation in bytes
     * @param wasAliased Whether this allocation reused existing memory
     */
    void RecordAllocation(
        uint32_t nodeId,
        const std::string& nodeName,
        ResourceLocation location,
        size_t bytes,
        bool wasAliased = false
    );

    /**
     * @brief Record a resource release
     * @param nodeId The ID of the node performing the release
     * @param nodeName The name of the node (for reporting)
     * @param resource The resource being released
     * @param bytes Size of the release in bytes
     */
    void RecordRelease(
        uint32_t nodeId,
        const std::string& nodeName,
        Resource* resource,
        size_t bytes
    );

    // === Statistics Queries ===

    /**
     * @brief Get statistics for a specific node in a specific frame
     * @param nodeId The node ID to query
     * @param frameNumber The frame number to query
     * @return Node statistics, or empty stats if not found
     */
    NodeResourceStats GetNodeStats(uint32_t nodeId, uint64_t frameNumber) const;

    /**
     * @brief Get statistics for all nodes in a specific frame
     * @param frameNumber The frame number to query
     * @return Frame statistics, or empty stats if not found
     */
    FrameResourceStats GetFrameStats(uint64_t frameNumber) const;

    /**
     * @brief Get statistics for the current frame
     * @return Current frame statistics
     */
    FrameResourceStats GetCurrentFrameStats() const;

    /**
     * @brief Get average statistics over the last N frames
     * @param frameCount Number of recent frames to average over
     * @return Averaged statistics
     */
    FrameResourceStats GetAverageStats(size_t frameCount) const;

    // === Export ===

    /**
     * @brief Export frame statistics as formatted text
     * @param frameNumber The frame to export
     * @return Human-readable text report
     */
    std::string ExportAsText(uint64_t frameNumber) const;

    /**
     * @brief Export frame statistics as JSON
     * @param frameNumber The frame to export
     * @return JSON-formatted statistics
     */
    std::string ExportAsJSON(uint64_t frameNumber) const;

    // === Configuration ===

    /**
     * @brief Set the maximum number of frames to keep in history
     * @param frames Number of frames (default: 120 = 2 seconds @ 60 FPS)
     */
    void SetMaxFrameHistory(size_t frames) { maxFrameHistory_ = frames; }

    /**
     * @brief Get the maximum frame history size
     */
    size_t GetMaxFrameHistory() const { return maxFrameHistory_; }

    /**
     * @brief Enable or disable detailed logging
     * @param enable True to enable detailed logging to console
     */
    void EnableDetailedLogging(bool enable) { detailedLogging_ = enable; }

    /**
     * @brief Check if detailed logging is enabled
     */
    bool IsDetailedLoggingEnabled() const { return detailedLogging_; }

private:
    // Current frame tracking
    uint64_t currentFrame_ = 0;
    std::chrono::steady_clock::time_point frameStartTime_;
    std::unordered_map<uint32_t, NodeResourceStats> currentFrameStats_;

    // Current frame running totals (for peak tracking)
    size_t currentStackUsage_ = 0;
    size_t currentHeapUsage_ = 0;
    size_t currentVramUsage_ = 0;

    // Peak values for current frame
    size_t peakStackUsage_ = 0;
    size_t peakHeapUsage_ = 0;
    size_t peakVramUsage_ = 0;

    // Historical data (rolling window)
    std::unordered_map<uint64_t, FrameResourceStats> frameHistory_;

    // Configuration
    size_t maxFrameHistory_ = 120;  // ~2 seconds at 60 FPS
    bool detailedLogging_ = false;

    // Helper methods
    void PruneOldFrames();
    NodeResourceStats& GetOrCreateNodeStats(uint32_t nodeId, const std::string& nodeName);
    void UpdatePeakUsage();
    void LogAllocation(uint32_t nodeId, const std::string& nodeName,
                       ResourceLocation location, size_t bytes, bool wasAliased);
    void LogRelease(uint32_t nodeId, const std::string& nodeName, size_t bytes);
};

} // namespace Vixen::RenderGraph
