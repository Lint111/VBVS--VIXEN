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

    // Test identification (Section 5.2 schema)
    j["test_id"] = config.testId.empty() ? config.GenerateTestId() : config.testId;
    j["timestamp"] = GetISO8601Timestamp();

    // Configuration block
    j["configuration"]["pipeline"] = config.pipeline;
    j["configuration"]["algorithm"] = config.algorithm;
    j["configuration"]["resolution"] = config.voxelResolution;
    j["configuration"]["density_percent"] = static_cast<int>(config.densityPercent * 100.0f);
    j["configuration"]["scene_type"] = config.sceneType;
    j["configuration"]["optimizations"] = config.optimizations;

    // Device info block
    j["device"]["gpu"] = device.deviceName;
    j["device"]["driver"] = device.driverVersion;
    j["device"]["vram_gb"] = device.totalVRAM_MB / 1024.0;

    // Check if any frame has estimated bandwidth
    bool anyBandwidthEstimated = false;
    for (const auto& frame : frames) {
        if (frame.bandwidthEstimated) {
            anyBandwidthEstimated = true;
            break;
        }
    }
    if (anyBandwidthEstimated) {
        j["device"]["bandwidth_estimated"] = true;
    }

    // Frame data array (Section 5.2 schema)
    nlohmann::json framesArray = nlohmann::json::array();
    for (const auto& frame : frames) {
        nlohmann::json f;
        f["frame_num"] = frame.frameNumber;
        f["frame_time_ms"] = frame.frameTimeMs;
        f["fps"] = frame.fps;
        f["bandwidth_read_gbps"] = frame.bandwidthReadGB;
        f["bandwidth_write_gbps"] = frame.bandwidthWriteGB;
        f["ray_throughput_mrays"] = frame.mRaysPerSec;
        f["vram_mb"] = frame.vramUsageMB;
        f["avg_voxels_per_ray"] = frame.avgVoxelsPerRay;
        framesArray.push_back(f);
    }
    j["frames"] = framesArray;

    // Statistics block (Section 5.2 schema)
    if (aggregates.count("frame_time_ms")) {
        const auto& ft = aggregates.at("frame_time_ms");
        j["statistics"]["frame_time_mean"] = ft.mean;
        j["statistics"]["frame_time_stddev"] = ft.stddev;
        j["statistics"]["frame_time_p99"] = ft.p99;
    }
    if (aggregates.count("fps")) {
        j["statistics"]["fps_mean"] = aggregates.at("fps").mean;
    }
    if (aggregates.count("bandwidth_read_gb")) {
        j["statistics"]["bandwidth_mean"] = aggregates.at("bandwidth_read_gb").mean;
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
    return ValidateWithErrors().empty();
}

std::vector<std::string> TestConfiguration::ValidateWithErrors() const {
    std::vector<std::string> errors;

    // Pipeline validation
    if (pipeline.empty()) {
        errors.push_back("pipeline: must not be empty");
    } else if (ParsePipelineType(pipeline) == PipelineType::Invalid) {
        errors.push_back("pipeline: must be one of: compute, fragment, hardware_rt, hybrid");
    }

    // Algorithm validation
    if (algorithm.empty()) {
        errors.push_back("algorithm: must not be empty");
    }

    // Scene type validation
    if (sceneType.empty()) {
        errors.push_back("sceneType: must not be empty");
    }

    // Resolution validation (must be power of 2: 32, 64, 128, 256, 512)
    if (!IsValidResolution(voxelResolution)) {
        errors.push_back("voxelResolution: must be power of 2 (32, 64, 128, 256, or 512)");
    }

    // Density validation: 0-1 range (0% to 100%)
    if (densityPercent < 0.0f) {
        errors.push_back("densityPercent: must be >= 0");
    } else if (densityPercent > 1.0f) {
        errors.push_back("densityPercent: must be <= 1.0 (representing 0-100%)");
    }

    // Screen dimensions validation
    if (screenWidth == 0) {
        errors.push_back("screenWidth: must be > 0");
    }
    if (screenHeight == 0) {
        errors.push_back("screenHeight: must be > 0");
    }

    // Frame count validation
    if (warmupFrames < 10) {
        errors.push_back("warmupFrames: must be >= 10");
    }
    if (measurementFrames < 100) {
        errors.push_back("measurementFrames: must be >= 100");
    }

    return errors;
}

std::string TestConfiguration::GenerateTestId(uint32_t runNumber) const {
    std::ostringstream oss;

    // Pipeline prefix
    if (pipeline == "hardware_rt") {
        oss << "HW_RT";
    } else if (pipeline == "compute") {
        oss << "COMPUTE";
    } else if (pipeline == "fragment") {
        oss << "FRAGMENT";
    } else if (pipeline == "hybrid") {
        oss << "HYBRID";
    } else {
        oss << pipeline;
    }

    oss << "_" << voxelResolution;

    // Scene type (uppercase)
    std::string sceneUpper = sceneType;
    for (char& c : sceneUpper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    oss << "_" << sceneUpper;

    // Algorithm (uppercase)
    std::string algoUpper = algorithm;
    for (char& c : algoUpper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    oss << "_" << algoUpper;

    oss << "_RUN" << runNumber;

    return oss.str();
}

bool TestConfiguration::IsValidResolution(uint32_t resolution) {
    // Valid resolutions are powers of 2: 32, 64, 128, 256, 512
    return resolution == 32 || resolution == 64 || resolution == 128 ||
           resolution == 256 || resolution == 512;
}

} // namespace Vixen::Profiler
