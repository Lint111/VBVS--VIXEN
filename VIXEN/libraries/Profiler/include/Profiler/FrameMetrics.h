#pragma once

#include <cstdint>
#include <string>
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

/// Test configuration for a single benchmark run
struct TestConfiguration {
    std::string pipeline = "compute";       // compute, fragment, hardware_rt, hybrid
    std::string algorithm = "baseline";     // baseline, empty_skip, blockwalk
    std::string sceneType = "cornell";      // cornell, cave, urban, test
    uint32_t voxelResolution = 128;
    float densityPercent = 0.5f;
    uint32_t screenWidth = 800;
    uint32_t screenHeight = 600;
    uint32_t warmupFrames = 60;
    uint32_t measurementFrames = 300;

    /// Generate default output filename
    std::string GetDefaultFilename() const;

    /// Validate configuration
    bool Validate() const;
};

} // namespace Vixen::Profiler
