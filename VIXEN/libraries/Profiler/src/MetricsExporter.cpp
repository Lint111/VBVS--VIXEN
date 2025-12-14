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
    j["configuration"]["shader"] = config.shader;
    j["configuration"]["resolution"] = config.voxelResolution;
    j["configuration"]["scene_type"] = config.sceneType;
    j["configuration"]["screen_width"] = config.screenWidth;
    j["configuration"]["screen_height"] = config.screenHeight;
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
        // NOTE: This is actually iterations (ESVO node visits), not voxels
        // Kept as avg_voxels_per_ray for backward compatibility
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

void MetricsExporter::ExportToJSON(
    const std::filesystem::path& filepath,
    const TestConfiguration& config,
    const DeviceCapabilities& device,
    const std::vector<FrameMetrics>& frames,
    const std::map<std::string, AggregateStats>& aggregates,
    const ValidationResult& validation) {

    EnsureDirectoryExists(filepath.parent_path());

    nlohmann::json j;

    // Test identification (Section 5.2 schema)
    j["test_id"] = config.testId.empty() ? config.GenerateTestId() : config.testId;
    j["timestamp"] = GetISO8601Timestamp();

    // Configuration block
    j["configuration"]["pipeline"] = config.pipeline;
    j["configuration"]["shader"] = config.shader;
    j["configuration"]["resolution"] = config.voxelResolution;
    j["configuration"]["scene_type"] = config.sceneType;
    j["configuration"]["screen_width"] = config.screenWidth;
    j["configuration"]["screen_height"] = config.screenHeight;
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
        // NVML GPU utilization metrics (when available)
        if (frame.nvmlAvailable) {
            f["gpu_utilization"] = frame.gpuUtilization;
            f["memory_utilization"] = frame.memoryUtilization;
            f["gpu_temperature_c"] = frame.gpuTemperature;
            f["gpu_power_w"] = frame.gpuPowerW;
        }
        // SVO cache hit rate (when shader counters available)
        if (frame.HasShaderCounters()) {
            float cacheHitRate = frame.shaderCounters.GetOverallCacheHitRate();
            if (cacheHitRate > 0.0f) {
                f["cache_hit_rate"] = cacheHitRate;
            }
        }
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

    // Validation block - sanity check results
    j["validation"]["valid"] = validation.valid;
    j["validation"]["error_count"] = validation.errorCount;
    j["validation"]["warning_count"] = validation.warningCount;

    if (!validation.checks.empty()) {
        nlohmann::json checksArray = nlohmann::json::array();
        for (const auto& check : validation.checks) {
            nlohmann::json c;
            c["check"] = check.checkName;
            c["metric"] = check.metric;
            c["message"] = check.message;
            c["severity"] = SeverityToString(check.severity);
            if (check.affectedFrames > 0) {
                c["affected_frames"] = check.affectedFrames;
                c["failure_rate"] = check.failureRate;
            }
            checksArray.push_back(c);
        }
        j["validation"]["checks"] = checksArray;
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open JSON file: " + filepath.string());
    }

    file << j.dump(2);
}

void MetricsExporter::ExportToJSON(
    const std::filesystem::path& filepath,
    const TestConfiguration& config,
    const DeviceCapabilities& device,
    const std::vector<FrameMetrics>& frames,
    const std::map<std::string, AggregateStats>& aggregates,
    const ValidationResult& validation,
    float blasBuildTimeMs,
    float tlasBuildTimeMs) {

    EnsureDirectoryExists(filepath.parent_path());

    nlohmann::json j;

    // Test identification (Section 5.2 schema)
    j["test_id"] = config.testId.empty() ? config.GenerateTestId() : config.testId;
    j["timestamp"] = GetISO8601Timestamp();

    // Configuration block
    j["configuration"]["pipeline"] = config.pipeline;
    j["configuration"]["shader"] = config.shader;
    j["configuration"]["resolution"] = config.voxelResolution;
    j["configuration"]["scene_type"] = config.sceneType;
    j["configuration"]["screen_width"] = config.screenWidth;
    j["configuration"]["screen_height"] = config.screenHeight;
    j["configuration"]["optimizations"] = config.optimizations;

    // BLAS/TLAS build timing (hardware_rt only)
    if (blasBuildTimeMs > 0.0f || tlasBuildTimeMs > 0.0f) {
        j["configuration"]["blas_build_time_ms"] = blasBuildTimeMs;
        j["configuration"]["tlas_build_time_ms"] = tlasBuildTimeMs;
    }

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
        // NVML GPU utilization metrics (when available)
        if (frame.nvmlAvailable) {
            f["gpu_utilization"] = frame.gpuUtilization;
            f["memory_utilization"] = frame.memoryUtilization;
            f["gpu_temperature_c"] = frame.gpuTemperature;
            f["gpu_power_w"] = frame.gpuPowerW;
        }
        // SVO cache hit rate (when shader counters available)
        if (frame.HasShaderCounters()) {
            float cacheHitRate = frame.shaderCounters.GetOverallCacheHitRate();
            if (cacheHitRate > 0.0f) {
                f["cache_hit_rate"] = cacheHitRate;
            }
        }
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

    // Validation block - sanity check results
    j["validation"]["valid"] = validation.valid;
    j["validation"]["error_count"] = validation.errorCount;
    j["validation"]["warning_count"] = validation.warningCount;

    if (!validation.checks.empty()) {
        nlohmann::json checksArray = nlohmann::json::array();
        for (const auto& check : validation.checks) {
            nlohmann::json c;
            c["check"] = check.checkName;
            c["metric"] = check.metric;
            c["message"] = check.message;
            c["severity"] = SeverityToString(check.severity);
            if (check.affectedFrames > 0) {
                c["affected_frames"] = check.affectedFrames;
                c["failure_rate"] = check.failureRate;
            }
            checksArray.push_back(c);
        }
        j["validation"]["checks"] = checksArray;
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
        << config.shader << "_"
        << config.sceneType << "_"
        << config.voxelResolution << "_"
        << config.screenWidth << "x" << config.screenHeight;

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
    file << "# Shader: " << config.shader << "\n";
    file << "# Scene Type: " << config.sceneType << "\n";
    file << "# Voxel Resolution: " << config.voxelResolution << "\n";
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

    // Note: shader validation is already done above

    // Scene type validation
    if (sceneType.empty()) {
        errors.push_back("sceneType: must not be empty");
    }

    // Resolution validation (must be power of 2: 32, 64, 128, 256, 512)
    if (!IsValidResolution(voxelResolution)) {
        errors.push_back("voxelResolution: must be power of 2 (32, 64, 128, 256, or 512)");
    }

    // Shader validation
    if (shader.empty()) {
        errors.push_back("shader: must not be empty");
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

    // Shader (uppercase)
    std::string shaderUpper = shader;
    for (char& c : shaderUpper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    oss << "_" << shaderUpper;

    oss << "_RUN" << runNumber;

    return oss.str();
}

bool TestConfiguration::IsValidResolution(uint32_t resolution) {
    // Valid resolutions are powers of 2: 32, 64, 128, 256, 512
    return resolution == 32 || resolution == 64 || resolution == 128 ||
           resolution == 256 || resolution == 512;
}

} // namespace Vixen::Profiler
