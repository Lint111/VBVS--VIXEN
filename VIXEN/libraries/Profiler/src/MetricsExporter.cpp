#include "Profiler/MetricsExporter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace Vixen::Profiler {

void MetricsExporter::ExportToCSV(
    const std::filesystem::path& filepath,
    const TestConfiguration& config,
    const DeviceCapabilities& device,
    const std::vector<FrameMetrics>& frames,
    const std::map<std::string, AggregateStats>& aggregates) {

    EnsureDirectoryExists(filepath.parent_path());

    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open CSV file: " + filepath.string());
    }

    file << std::fixed << std::setprecision(3);

    WriteCSVHeader(file, config, device);
    WriteCSVDataRows(file, frames);
    WriteCSVAggregates(file, aggregates);
}

void MetricsExporter::ExportToJSON(
    const std::filesystem::path& filepath,
    const TestConfiguration& config,
    const DeviceCapabilities& device,
    const std::vector<FrameMetrics>& frames,
    const std::map<std::string, AggregateStats>& aggregates) {

    EnsureDirectoryExists(filepath.parent_path());

    nlohmann::json j;

    // Metadata
    j["metadata"]["timestamp"] = GetISO8601Timestamp();
    j["metadata"]["pipeline"] = config.pipeline;
    j["metadata"]["algorithm"] = config.algorithm;
    j["metadata"]["scene_type"] = config.sceneType;
    j["metadata"]["voxel_resolution"] = config.voxelResolution;
    j["metadata"]["density_percent"] = config.densityPercent;
    j["metadata"]["screen_width"] = config.screenWidth;
    j["metadata"]["screen_height"] = config.screenHeight;
    j["metadata"]["warmup_frames"] = config.warmupFrames;
    j["metadata"]["measurement_frames"] = config.measurementFrames;

    // Device info
    j["device"]["name"] = device.deviceName;
    j["device"]["driver_version"] = device.driverVersion;
    j["device"]["vulkan_version"] = device.vulkanVersion;
    j["device"]["vram_mb"] = device.totalVRAM_MB;
    j["device"]["type"] = device.GetDeviceTypeString();

    // Frame data
    nlohmann::json framesArray = nlohmann::json::array();
    for (const auto& frame : frames) {
        nlohmann::json f;
        f["frame"] = frame.frameNumber;
        f["timestamp_ms"] = frame.timestampMs;
        f["frame_time_ms"] = frame.frameTimeMs;
        f["gpu_time_ms"] = frame.gpuTimeMs;
        f["bandwidth_read_gb"] = frame.bandwidthReadGB;
        f["bandwidth_write_gb"] = frame.bandwidthWriteGB;
        f["vram_mb"] = frame.vramUsageMB;
        f["mrays_per_sec"] = frame.mRaysPerSec;
        f["fps"] = frame.fps;
        framesArray.push_back(f);
    }
    j["frames"] = framesArray;

    // Aggregates
    for (const auto& [name, stats] : aggregates) {
        j["aggregates"][name]["min"] = stats.min;
        j["aggregates"][name]["max"] = stats.max;
        j["aggregates"][name]["mean"] = stats.mean;
        j["aggregates"][name]["stddev"] = stats.stddev;
        j["aggregates"][name]["p1"] = stats.p1;
        j["aggregates"][name]["p50"] = stats.p50;
        j["aggregates"][name]["p99"] = stats.p99;
        j["aggregates"][name]["sample_count"] = stats.sampleCount;
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open JSON file: " + filepath.string());
    }

    file << j.dump(2);
}

void MetricsExporter::SetEnabledColumns(const std::vector<std::string>& columns) {
    enabledColumns_ = columns;
}

std::string MetricsExporter::GetISO8601Timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string MetricsExporter::GetDefaultFilename(const TestConfiguration& config, ExportFormat format) {
    std::ostringstream oss;
    oss << config.pipeline << "_"
        << config.algorithm << "_"
        << config.sceneType << "_"
        << config.voxelResolution << "_"
        << static_cast<int>(config.densityPercent * 100) << "pct";

    switch (format) {
        case ExportFormat::CSV: oss << ".csv"; break;
        case ExportFormat::JSON: oss << ".json"; break;
    }

    return oss.str();
}

bool MetricsExporter::EnsureDirectoryExists(const std::filesystem::path& directory) {
    if (directory.empty()) return true;

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return std::filesystem::create_directories(directory, ec);
    }
    return true;
}

void MetricsExporter::WriteCSVHeader(std::ofstream& file, const TestConfiguration& config,
                                     const DeviceCapabilities& device) {
    // Metadata comments
    file << "# VIXEN Voxel Ray Tracing Benchmark Results\n";
    file << "# Timestamp: " << GetISO8601Timestamp() << "\n";
    file << "# Pipeline: " << config.pipeline << "\n";
    file << "# Algorithm: " << config.algorithm << "\n";
    file << "# Scene Type: " << config.sceneType << "\n";
    file << "# Voxel Resolution: " << config.voxelResolution << "\n";
    file << "# Density: " << config.densityPercent << "\n";
    file << "# Screen: " << config.screenWidth << "x" << config.screenHeight << "\n";
    file << "# Warmup Frames: " << config.warmupFrames << "\n";
    file << "# Measurement Frames: " << config.measurementFrames << "\n";
    file << "# GPU: " << device.deviceName << "\n";
    file << "# Driver: " << device.driverVersion << "\n";
    file << "# Vulkan: " << device.vulkanVersion << "\n";
    file << "#\n";

    // Column headers
    bool first = true;
    for (const auto& col : enabledColumns_) {
        if (!first) file << ",";
        file << col;
        first = false;
    }
    file << "\n";
}

void MetricsExporter::WriteCSVDataRows(std::ofstream& file, const std::vector<FrameMetrics>& frames) {
    for (const auto& frame : frames) {
        bool first = true;
        for (const auto& col : enabledColumns_) {
            if (!first) file << ",";

            if (col == "frame") file << frame.frameNumber;
            else if (col == "timestamp_ms") file << frame.timestampMs;
            else if (col == "frame_time_ms") file << frame.frameTimeMs;
            else if (col == "gpu_time_ms") file << frame.gpuTimeMs;
            else if (col == "bandwidth_read_gb") file << frame.bandwidthReadGB;
            else if (col == "bandwidth_write_gb") file << frame.bandwidthWriteGB;
            else if (col == "vram_mb") file << frame.vramUsageMB;
            else if (col == "mrays_per_sec") file << frame.mRaysPerSec;
            else if (col == "fps") file << frame.fps;
            else if (col == "scene_resolution") file << frame.sceneResolution;
            else if (col == "screen_width") file << frame.screenWidth;
            else if (col == "screen_height") file << frame.screenHeight;

            first = false;
        }
        file << "\n";
    }
}

void MetricsExporter::WriteCSVAggregates(std::ofstream& file,
                                         const std::map<std::string, AggregateStats>& aggregates) {
    file << "#\n";
    file << "# Aggregate Statistics (" << (aggregates.empty() ? 0 : aggregates.begin()->second.sampleCount) << " frames)\n";
    file << "# metric,min,max,mean,stddev,p1,p50,p99\n";

    for (const auto& [name, stats] : aggregates) {
        file << "# " << name << ","
             << stats.min << ","
             << stats.max << ","
             << stats.mean << ","
             << stats.stddev << ","
             << stats.p1 << ","
             << stats.p50 << ","
             << stats.p99 << "\n";
    }
}

// FrameMetrics/TestConfiguration implementations
std::string TestConfiguration::GetDefaultFilename() const {
    return MetricsExporter::GetDefaultFilename(*this, ExportFormat::CSV);
}

bool TestConfiguration::Validate() const {
    if (pipeline.empty() || algorithm.empty() || sceneType.empty()) return false;
    if (voxelResolution == 0 || voxelResolution > 4096) return false;
    if (densityPercent < 0.0f || densityPercent > 1.0f) return false;
    if (screenWidth == 0 || screenHeight == 0) return false;
    if (measurementFrames == 0) return false;
    return true;
}

} // namespace Vixen::Profiler
