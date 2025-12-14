#pragma once

#include "FrameMetrics.h"
#include "DeviceCapabilities.h"
#include "MetricsSanityChecker.h"
#include <vector>
#include <string>
#include <map>
#include <filesystem>

namespace Vixen::Profiler {

/// Export format for benchmark results
enum class ExportFormat {
    CSV,    // Comma-separated values (Excel/pandas compatible)
    JSON    // JSON format (programmatic access)
};

/// Exports collected metrics to CSV or JSON files
class MetricsExporter {
public:
    MetricsExporter() = default;

    /// Export frame metrics to CSV file
    /// @param filepath Output file path
    /// @param config Test configuration for metadata header
    /// @param device Device capabilities for context
    /// @param frames Per-frame metrics to export
    /// @param aggregates Aggregate statistics per metric
    void ExportToCSV(
        const std::filesystem::path& filepath,
        const TestConfiguration& config,
        const DeviceCapabilities& device,
        const std::vector<FrameMetrics>& frames,
        const std::map<std::string, AggregateStats>& aggregates);

    /// Export frame metrics to JSON file (without validation)
    void ExportToJSON(
        const std::filesystem::path& filepath,
        const TestConfiguration& config,
        const DeviceCapabilities& device,
        const std::vector<FrameMetrics>& frames,
        const std::map<std::string, AggregateStats>& aggregates);

    /// Export frame metrics to JSON file with validation results
    void ExportToJSON(
        const std::filesystem::path& filepath,
        const TestConfiguration& config,
        const DeviceCapabilities& device,
        const std::vector<FrameMetrics>& frames,
        const std::map<std::string, AggregateStats>& aggregates,
        const ValidationResult& validation);

    /// Export frame metrics to JSON file with validation and AS build timing
    void ExportToJSON(
        const std::filesystem::path& filepath,
        const TestConfiguration& config,
        const DeviceCapabilities& device,
        const std::vector<FrameMetrics>& frames,
        const std::map<std::string, AggregateStats>& aggregates,
        const ValidationResult& validation,
        float blasBuildTimeMs,
        float tlasBuildTimeMs);

    /// Set which columns to include in CSV export
    /// Default: all columns
    void SetEnabledColumns(const std::vector<std::string>& columns);

    /// Get ISO 8601 formatted timestamp for current time
    static std::string GetISO8601Timestamp();

    /// Generate default filename based on configuration
    static std::string GetDefaultFilename(const TestConfiguration& config, ExportFormat format);

    /// Create output directory if it doesn't exist
    static bool EnsureDirectoryExists(const std::filesystem::path& directory);

private:
    std::vector<std::string> enabledColumns_ = {
        "frame", "timestamp_ms", "frame_time_ms", "gpu_time_ms",
        "bandwidth_read_gb", "bandwidth_write_gb", "vram_mb",
        "mrays_per_sec", "fps", "scene_resolution", "screen_width", "screen_height"
    };

    void WriteCSVHeader(std::ofstream& file, const TestConfiguration& config,
                        const DeviceCapabilities& device);
    void WriteCSVDataRows(std::ofstream& file, const std::vector<FrameMetrics>& frames);
    void WriteCSVAggregates(std::ofstream& file, const std::map<std::string, AggregateStats>& aggregates);
};

} // namespace Vixen::Profiler
