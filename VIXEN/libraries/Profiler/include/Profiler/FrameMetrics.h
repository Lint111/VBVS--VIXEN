#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace Vixen::Profiler {

/**
 * @brief GPU-side shader counters for detailed ray marching metrics
 *
 * These counters require GPU-side atomic operations and readback to CPU.
 * The shader writes to atomic counters in a storage buffer, which is then
 * read back after frame completion.
 *
 * Implementation requirements:
 * 1. Storage buffer with atomic counters (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
 * 2. Shader writes via atomicAdd() in GLSL/HLSL
 * 3. Readback via vkCmdCopyBuffer or mapped staging buffer
 * 4. Double/triple buffering to avoid stalls
 *
 * GPU-side GLSL example:
 * @code
 * layout(set = 0, binding = X, std430) buffer ShaderCountersBuffer {
 *     uint totalVoxelsTraversed;
 *     uint totalRaysCast;
 *     uint totalNodesVisited;
 *     uint totalLeafNodesVisited;
 *     uint totalEmptySpaceSkipped;
 *     uint rayHitCount;
 *     uint rayMissCount;
 *     uint earlyTerminations;
 * } counters;
 *
 * // In ray march loop:
 * atomicAdd(counters.totalVoxelsTraversed, 1);
 * atomicAdd(counters.totalNodesVisited, 1);
 * @endcode
 *
 * TODO: Integration with MetricsCollector
 * - Add CreateCounterBuffer() method
 * - Add ResetCounters() for per-frame reset
 * - Add ReadbackCounters() with fence synchronization
 * - Connect to DescriptorResourceGatherer for binding
 */
struct ShaderCounters {
    /// Total voxels traversed across all rays this frame
    /// Divide by totalRaysCast to get avgVoxelsPerRay
    uint64_t totalVoxelsTraversed = 0;

    /// Total rays cast this frame (screen_width * screen_height for full-screen)
    uint64_t totalRaysCast = 0;

    /// Total octree/SVO nodes visited during traversal
    /// High values indicate deep traversal or inefficient skip logic
    uint64_t totalNodesVisited = 0;

    /// Leaf nodes (actual voxel data) visited
    /// totalNodesVisited - totalLeafNodesVisited = internal node visits
    uint64_t totalLeafNodesVisited = 0;

    /// Voxels skipped via empty space optimization (e.g., ESVO empty-skip)
    /// Higher is better - indicates efficient empty space culling
    uint64_t totalEmptySpaceSkipped = 0;

    /// Rays that hit geometry (found a voxel)
    uint64_t rayHitCount = 0;

    /// Rays that missed all geometry (background)
    uint64_t rayMissCount = 0;

    /// Rays terminated early (e.g., max depth, max iterations)
    uint64_t earlyTerminations = 0;

    /// Compute derived metrics
    float GetAvgVoxelsPerRay() const {
        return totalRaysCast > 0 ? static_cast<float>(totalVoxelsTraversed) / totalRaysCast : 0.0f;
    }

    float GetAvgNodesPerRay() const {
        return totalRaysCast > 0 ? static_cast<float>(totalNodesVisited) / totalRaysCast : 0.0f;
    }

    float GetHitRate() const {
        return totalRaysCast > 0 ? static_cast<float>(rayHitCount) / totalRaysCast : 0.0f;
    }

    float GetEmptySpaceSkipRatio() const {
        uint64_t totalPotential = totalVoxelsTraversed + totalEmptySpaceSkipped;
        return totalPotential > 0 ? static_cast<float>(totalEmptySpaceSkipped) / totalPotential : 0.0f;
    }

    /// Reset all counters to zero (call at frame start)
    void Reset() {
        totalVoxelsTraversed = 0;
        totalRaysCast = 0;
        totalNodesVisited = 0;
        totalLeafNodesVisited = 0;
        totalEmptySpaceSkipped = 0;
        rayHitCount = 0;
        rayMissCount = 0;
        earlyTerminations = 0;
    }

    /// Check if counters contain valid data (at least one ray was cast)
    bool HasData() const {
        return totalRaysCast > 0;
    }
};

/// Per-frame metrics collected during profiling
struct FrameMetrics {
    uint64_t frameNumber = 0;
    double timestampMs = 0.0;           // Time since profiling started
    float frameTimeMs = 0.0f;           // CPU frame time
    float gpuTimeMs = 0.0f;             // GPU dispatch time
    float bandwidthReadGB = 0.0f;       // Memory read bandwidth (GB/s)
    float bandwidthWriteGB = 0.0f;      // Memory write bandwidth (GB/s)
    uint64_t vramUsageMB = 0;           // VRAM usage in MB (from VK_EXT_memory_budget)
    uint64_t vramBudgetMB = 0;          // VRAM budget in MB (from VK_EXT_memory_budget)
    float mRaysPerSec = 0.0f;           // Million rays per second
    float fps = 0.0f;                   // Frames per second

    // Scene-specific metrics (extracted from nodes)
    uint32_t sceneResolution = 0;       // Voxel grid resolution (e.g., 128)
    uint32_t screenWidth = 0;           // Render target width
    uint32_t screenHeight = 0;          // Render target height
    float sceneDensity = 0.0f;          // Scene fill percentage

    // Voxel traversal metrics
    float avgVoxelsPerRay = 0.0f;       // Average voxels traversed per ray
    uint64_t totalRaysCast = 0;         // Total rays cast this frame

    // Bandwidth estimation info
    bool bandwidthEstimated = false;    // True if bandwidth is estimated (no HW counters)

    // GPU shader counters (when available)
    // These require GPU-side atomic counters and readback - see ShaderCounters documentation
    ShaderCounters shaderCounters;

    /// Check if shader counters contain valid data
    bool HasShaderCounters() const {
        return shaderCounters.HasData();
    }
};

/// Aggregate statistics for a metric
struct AggregateStats {
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    float stddev = 0.0f;
    float p1 = 0.0f;                    // 1st percentile
    float p50 = 0.0f;                   // 50th percentile (median)
    float p99 = 0.0f;                   // 99th percentile
    uint32_t sampleCount = 0;
};

/// Valid pipeline types for benchmarks
enum class PipelineType {
    Compute,
    Fragment,
    HardwareRT,
    Hybrid,
    Invalid
};

/// Convert pipeline type to string
inline std::string PipelineTypeToString(PipelineType type) {
    switch (type) {
        case PipelineType::Compute: return "compute";
        case PipelineType::Fragment: return "fragment";
        case PipelineType::HardwareRT: return "hardware_rt";
        case PipelineType::Hybrid: return "hybrid";
        default: return "invalid";
    }
}

/// Parse pipeline type from string
inline PipelineType ParsePipelineType(const std::string& str) {
    if (str == "compute") return PipelineType::Compute;
    if (str == "fragment") return PipelineType::Fragment;
    if (str == "hardware_rt") return PipelineType::HardwareRT;
    if (str == "hybrid") return PipelineType::Hybrid;
    return PipelineType::Invalid;
}

/// Test configuration for a single benchmark run
struct TestConfiguration {
    std::string testId;                     // Unique test identifier (e.g., "COMPUTE_256_CORNELL_ESVO_RUN1")
    std::string pipeline = "compute";       // compute, fragment, hardware_rt, hybrid
    std::string shader = "ray_march_base";  // Primary shader name (for identification/logging)
    std::vector<std::string> shaderGroup;   // All shaders in group (e.g., [vert, frag] for graphics)
    std::string sceneType = "cornell";      // Scene identifier (cornell, noise, tunnels, cityscape)
    uint32_t voxelResolution = 128;         // SVO resolution (64, 128, 256, 512)
    uint32_t screenWidth = 800;             // Render target width
    uint32_t screenHeight = 600;            // Render target height
    uint32_t warmupFrames = 100;    // Filter frame 75 spike observed in data
    uint32_t measurementFrames = 300;
    std::vector<std::string> optimizations; // List of enabled optimizations

    /// Generate default output filename
    std::string GetDefaultFilename() const;

    /// Validate configuration (simple bool return)
    bool Validate() const;

    /// Validate configuration with detailed error messages
    /// @return Vector of validation error messages (empty if valid)
    std::vector<std::string> ValidateWithErrors() const;

    /// Generate test ID from configuration if not set
    std::string GenerateTestId(uint32_t runNumber = 1) const;

    /// Check if resolution is a valid power of 2 (32, 64, 128, 256, 512)
    static bool IsValidResolution(uint32_t resolution);
};

} // namespace Vixen::Profiler
