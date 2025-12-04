#pragma once

#include "FrameMetrics.h"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Vixen::Profiler {

/**
 * @brief Configuration for an entire benchmark suite
 *
 * This is the primary configuration struct passed to BenchmarkRunner.
 * The benchmark executable creates this struct from CLI arguments and
 * passes it to BenchmarkRunner::RunSuite() - the runner handles ALL
 * Vulkan initialization and execution internally.
 *
 * Usage:
 * @code
 * BenchmarkSuiteConfig config;
 * config.outputDir = "./results";
 * config.tests = BenchmarkConfigLoader::GetQuickTestMatrix();
 * config.headless = true;
 *
 * BenchmarkRunner runner;
 * auto results = runner.RunSuite(config);
 * @endcode
 */
struct BenchmarkSuiteConfig {
    /// Output directory for benchmark results (CSV/JSON files)
    std::filesystem::path outputDir = "./benchmark_results";

    /// List of test configurations to run
    std::vector<TestConfiguration> tests;

    /// Number of warmup frames (can override per-test settings)
    std::optional<uint32_t> warmupFramesOverride;

    /// Number of measurement frames (can override per-test settings)
    std::optional<uint32_t> measurementFramesOverride;

    /// Render target width
    uint32_t renderWidth = 800;

    /// Render target height
    uint32_t renderHeight = 600;

    /// GPU index to use (0 = first GPU)
    uint32_t gpuIndex = 0;

    /// Enable headless mode (no window, compute-only)
    bool headless = true;

    /// Enable verbose logging
    bool verbose = false;

    /// Enable Vulkan validation layers
    bool enableValidation = false;

    /// Export results as CSV
    bool exportCSV = true;

    /// Export results as JSON
    bool exportJSON = true;

    /**
     * @brief Apply warmup/measurement overrides to all tests
     *
     * Call this after populating tests to apply global overrides.
     */
    void ApplyOverrides() {
        for (auto& test : tests) {
            if (warmupFramesOverride) {
                test.warmupFrames = *warmupFramesOverride;
            }
            if (measurementFramesOverride) {
                test.measurementFrames = *measurementFramesOverride;
            }
            test.screenWidth = renderWidth;
            test.screenHeight = renderHeight;
        }
    }

    /**
     * @brief Validate the suite configuration
     *
     * @return Vector of error messages (empty if valid)
     */
    std::vector<std::string> Validate() const {
        std::vector<std::string> errors;

        if (tests.empty()) {
            errors.push_back("No test configurations provided");
        }

        for (size_t i = 0; i < tests.size(); ++i) {
            auto testErrors = tests[i].ValidateWithErrors();
            for (const auto& err : testErrors) {
                errors.push_back("Test[" + std::to_string(i) + "]: " + err);
            }
        }

        if (renderWidth < 64 || renderWidth > 8192) {
            errors.push_back("Invalid render width: " + std::to_string(renderWidth));
        }
        if (renderHeight < 64 || renderHeight > 8192) {
            errors.push_back("Invalid render height: " + std::to_string(renderHeight));
        }

        return errors;
    }

    /**
     * @brief Check if configuration is valid
     */
    bool IsValid() const {
        return Validate().empty();
    }
};

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
