#pragma once

#include <Profiler/FrameMetrics.h>
#include <Profiler/BenchmarkConfig.h>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace Vixen::Benchmark {

/**
 * @brief Command line argument parser for benchmark executable
 *
 * Parses command line arguments and provides configuration for benchmark runs.
 * Supports both command-line overrides and JSON configuration files.
 *
 * Usage:
 *   vixen_benchmark [options]
 *     --config FILE       JSON config file (default: benchmark_config.json)
 *     --output DIR        Output directory for results (default: ./benchmark_results)
 *     --iterations N      Measurement frames per test (default: 100)
 *     --warmup N          Warmup frames before measurement (default: 10)
 *     --resolutions LIST  Comma-separated voxel resolutions: 32,64,128,256
 *     --densities LIST    Comma-separated densities (0-100): 10,30,50,70,90
 *     --gpu N             GPU index to use (default: 0)
 *     --list-gpus         List available GPUs and exit
 *     --verbose           Enable detailed logging
 *     --debug             Enable Vulkan validation layers
 *     --help              Show help message
 *     --quick             Run minimal test matrix (12 configs)
 *     --full              Run research test matrix (180 configs)
 */
struct BenchmarkCLIOptions {
    // Configuration file
    std::filesystem::path configPath = "benchmark_config.json";
    bool hasConfigFile = false;

    // Output settings
    std::filesystem::path outputDirectory = "./benchmark_results";
    bool exportCSV = true;
    bool exportJSON = true;

    // Test parameters (command-line overrides)
    std::optional<uint32_t> measurementFrames;
    std::optional<uint32_t> warmupFrames;
    std::vector<uint32_t> resolutions;
    std::vector<float> densities;
    std::vector<std::string> pipelines;
    std::vector<std::string> algorithms;

    // GPU selection
    uint32_t gpuIndex = 0;
    bool listGpus = false;

    // Render dimensions (headless)
    uint32_t renderWidth = 800;
    uint32_t renderHeight = 600;

    // Logging and debug
    bool verbose = false;
    bool enableValidation = false;

    // Preset modes
    bool quickMode = false;   // Minimal test matrix
    bool fullMode = false;    // Full research matrix

    // Execution modes
    bool headlessMode = true;   // Default: headless compute-only (no window)
    bool renderMode = false;    // Full rendering with window
    bool headlessExplicitlySet = false;  // True if --headless or --render was passed

    // Config save/load
    bool saveConfig = false;                    // Save current config to file and exit
    std::filesystem::path saveConfigPath;       // Path to save config to

    // Package output
    bool createPackage = true;                  // Create ZIP package for tester sharing (default: ON)
    std::string testerName;                     // Optional tester name for package

    // Post-run behavior
    bool openResultsFolder = true;              // Auto-open results folder after completion (default: ON)

    // Help flag
    bool showHelp = false;

    // Parse error (if any)
    std::string parseError;
    bool hasError = false;

    /**
     * @brief Generate TestConfiguration list from CLI options
     *
     * Creates test matrix based on CLI arguments, or loads from config file.
     * CLI arguments override config file settings.
     *
     * @return Vector of test configurations
     */
    std::vector<Vixen::Profiler::TestConfiguration> GenerateTestConfigurations() const;

    /**
     * @brief Get descriptive name for this benchmark run
     *
     * @return String like "benchmark_20250104_143052" for result naming
     */
    std::string GetRunName() const;

    /**
     * @brief Validate options and return any errors
     *
     * @return Empty vector if valid, error messages otherwise
     */
    std::vector<std::string> Validate() const;

    /**
     * @brief Build a BenchmarkSuiteConfig from CLI options
     *
     * This is the primary entry point for creating suite configuration.
     * Converts all CLI options into a single config struct that can be
     * passed to BenchmarkRunner::RunSuite().
     *
     * @return Complete suite configuration
     */
    Vixen::Profiler::BenchmarkSuiteConfig BuildSuiteConfig() const;
};

/**
 * @brief Parse command line arguments into options struct
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return Parsed options (check hasError for parse failures)
 */
BenchmarkCLIOptions ParseCommandLine(int argc, char* argv[]);

/**
 * @brief Print usage help message to stdout
 */
void PrintHelp();

/**
 * @brief Print version information
 */
void PrintVersion();

/**
 * @brief Get the user's Downloads folder path
 *
 * On Windows: Uses SHGetKnownFolderPath(FOLDERID_Downloads)
 * Fallback: Returns executable directory if detection fails
 *
 * @return Path to Downloads folder
 */
std::filesystem::path GetDownloadsFolder();

/**
 * @brief Get default output directory for benchmark results
 *
 * Returns: Downloads/VIXEN_Benchmarks/ (or fallback)
 *
 * @return Path to default output directory
 */
std::filesystem::path GetDefaultOutputDirectory();

/**
 * @brief Parse comma-separated list of uint32_t values
 *
 * @param str Input string like "32,64,128,256"
 * @return Vector of parsed values
 */
std::vector<uint32_t> ParseUint32List(const std::string& str);

/**
 * @brief Parse comma-separated list of float values
 *
 * @param str Input string like "10.0,30.0,50.0"
 * @return Vector of parsed values
 */
std::vector<float> ParseFloatList(const std::string& str);

/**
 * @brief Parse comma-separated list of strings
 *
 * @param str Input string like "compute,fragment"
 * @return Vector of parsed strings
 */
std::vector<std::string> ParseStringList(const std::string& str);

} // namespace Vixen::Benchmark
