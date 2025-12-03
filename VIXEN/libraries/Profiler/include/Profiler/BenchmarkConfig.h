#pragma once

#include "FrameMetrics.h"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Vixen::Profiler {

/// Load and manage benchmark configurations from JSON files
class BenchmarkConfigLoader {
public:
    /// Load single benchmark configuration from JSON file
    /// @param filepath Path to JSON config file
    /// @return Configuration if successful, empty optional on error
    static std::optional<TestConfiguration> LoadFromFile(const std::filesystem::path& filepath);

    /// Load batch of benchmark configurations from JSON file
    /// Supports both single config and test matrix format
    static std::vector<TestConfiguration> LoadBatchFromFile(const std::filesystem::path& filepath);

    /// Generate test matrix from parameter arrays
    /// @param pipelines List of pipeline types to test
    /// @param resolutions List of voxel resolutions
    /// @param densities List of scene densities
    /// @param algorithms List of traversal algorithms
    /// @return Vector of all configuration combinations
    static std::vector<TestConfiguration> GenerateTestMatrix(
        const std::vector<std::string>& pipelines,
        const std::vector<uint32_t>& resolutions,
        const std::vector<float>& densities,
        const std::vector<std::string>& algorithms);

    /// Save configuration to JSON file
    static bool SaveToFile(const TestConfiguration& config, const std::filesystem::path& filepath);

    /// Save batch of configurations to JSON file (as test matrix)
    static bool SaveBatchToFile(const std::vector<TestConfiguration>& configs,
                                const std::filesystem::path& filepath);

    /// Get default test matrix for research (180 configurations)
    /// 4 pipelines x 5 resolutions x 3 densities x 3 algorithms
    static std::vector<TestConfiguration> GetResearchTestMatrix();

    /// Get minimal test matrix for quick validation (12 configurations)
    static std::vector<TestConfiguration> GetQuickTestMatrix();

    /// Parse configuration from JSON string
    static std::optional<TestConfiguration> ParseFromString(const std::string& jsonString);

    /// Serialize configuration to JSON string
    static std::string SerializeToString(const TestConfiguration& config);

private:
    static TestConfiguration ParseConfigObject(const void* jsonObject);
};

} // namespace Vixen::Profiler
