#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace Vixen::Profiler {

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
    std::string testId;                     // Unique test identifier (e.g., "HW_RT_256_SPARSE_BASELINE_RUN1")
    std::string pipeline = "compute";       // compute, fragment, hardware_rt, hybrid
    std::string algorithm = "baseline";     // baseline, empty_skip, blockwalk
    std::string sceneType = "cornell";      // cornell, cave, urban, test
    uint32_t voxelResolution = 128;
    float densityPercent = 0.5f;            // 0-100 (percent)
    uint32_t screenWidth = 800;
    uint32_t screenHeight = 600;
    uint32_t warmupFrames = 60;
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
