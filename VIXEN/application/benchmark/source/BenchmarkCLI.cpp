#include "BenchmarkCLI.h"
#include <Profiler/BenchmarkConfig.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>  // For SHGetKnownFolderPath
#include <KnownFolders.h>  // For FOLDERID_Downloads
#pragma comment(lib, "Shell32.lib")
#endif

namespace Vixen::Benchmark {

//==============================================================================
// Downloads Folder Detection
//==============================================================================

std::filesystem::path GetDownloadsFolder() {
#ifdef _WIN32
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path);
    if (SUCCEEDED(hr) && path != nullptr) {
        std::filesystem::path downloadsPath(path);
        CoTaskMemFree(path);
        return downloadsPath;
    }

    // Fallback: try environment variable
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        std::filesystem::path fallback = std::filesystem::path(userProfile) / "Downloads";
        if (std::filesystem::exists(fallback)) {
            return fallback;
        }
    }

    // Last resort: use exe directory
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        return std::filesystem::path(exePath).parent_path();
    }

    return std::filesystem::current_path();
#else
    // Linux/Mac: use XDG_DOWNLOAD_DIR or fallback to ~/Downloads
    const char* xdgDownload = std::getenv("XDG_DOWNLOAD_DIR");
    if (xdgDownload && std::filesystem::exists(xdgDownload)) {
        return xdgDownload;
    }

    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path downloadsPath = std::filesystem::path(home) / "Downloads";
        if (std::filesystem::exists(downloadsPath)) {
            return downloadsPath;
        }
    }

    return std::filesystem::current_path();
#endif
}

std::filesystem::path GetDefaultOutputDirectory() {
    return GetDownloadsFolder() / "VIXEN_Benchmarks";
}

namespace {

// Trim whitespace from string
std::string Trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), ::isspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), ::isspace).base();
    return (start < end) ? std::string(start, end) : std::string();
}

// Check if argument matches short or long form
bool ArgMatches(const char* arg, const char* shortForm, const char* longForm) {
    return (shortForm && std::strcmp(arg, shortForm) == 0) ||
           (longForm && std::strcmp(arg, longForm) == 0);
}

// Get next argument value safely
const char* GetNextArg(int argc, char* argv[], int& i, const char* argName) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << argName << " requires a value\n";  // User-facing error - keep as is
        return nullptr;
    }
    return argv[++i];
}

} // anonymous namespace

std::vector<uint32_t> ParseUint32List(const std::string& str) {
    std::vector<uint32_t> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (!token.empty()) {
            try {
                result.push_back(static_cast<uint32_t>(std::stoul(token)));
            } catch (const std::exception&) {
                // Skip invalid values
            }
        }
    }
    return result;
}

std::vector<float> ParseFloatList(const std::string& str) {
    std::vector<float> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (!token.empty()) {
            try {
                result.push_back(std::stof(token));
            } catch (const std::exception&) {
                // Skip invalid values
            }
        }
    }
    return result;
}

std::vector<std::string> ParseStringList(const std::string& str) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

BenchmarkCLIOptions ParseCommandLine(int argc, char* argv[]) {
    BenchmarkCLIOptions opts;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        // Help
        if (ArgMatches(arg, "-h", "--help")) {
            opts.showHelp = true;
            return opts;
        }

        // Version (treated as help for now)
        if (ArgMatches(arg, "-v", "--version")) {
            opts.showHelp = true;
            return opts;
        }

        // Config file
        if (ArgMatches(arg, "-c", "--config")) {
            const char* val = GetNextArg(argc, argv, i, "--config");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--config requires a file path";
                return opts;
            }
            opts.configPath = val;
            opts.hasConfigFile = true;
            continue;
        }

        // Output directory
        if (ArgMatches(arg, "-o", "--output")) {
            const char* val = GetNextArg(argc, argv, i, "--output");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--output requires a directory path";
                return opts;
            }
            opts.outputDirectory = val;
            continue;
        }

        // Measurement frames
        if (ArgMatches(arg, "-i", "--iterations")) {
            const char* val = GetNextArg(argc, argv, i, "--iterations");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--iterations requires a number";
                return opts;
            }
            try {
                opts.measurementFrames = static_cast<uint32_t>(std::stoul(val));
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --iterations: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Warmup frames
        if (ArgMatches(arg, "-w", "--warmup")) {
            const char* val = GetNextArg(argc, argv, i, "--warmup");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--warmup requires a number";
                return opts;
            }
            try {
                opts.warmupFrames = static_cast<uint32_t>(std::stoul(val));
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --warmup: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Runs per config (for statistical robustness)
        if (ArgMatches(arg, nullptr, "--runs")) {
            const char* val = GetNextArg(argc, argv, i, "--runs");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--runs requires a number";
                return opts;
            }
            try {
                opts.runsPerConfig = static_cast<uint32_t>(std::stoul(val));
                if (opts.runsPerConfig < 1 || opts.runsPerConfig > 100) {
                    opts.hasError = true;
                    opts.parseError = "--runs must be between 1 and 100";
                    return opts;
                }
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --runs: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Resolutions
        if (ArgMatches(arg, "-r", "--resolutions")) {
            const char* val = GetNextArg(argc, argv, i, "--resolutions");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--resolutions requires a comma-separated list";
                return opts;
            }
            opts.resolutions = ParseUint32List(val);
            if (opts.resolutions.empty()) {
                opts.hasError = true;
                opts.parseError = "Invalid resolution list: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Densities
        if (ArgMatches(arg, "-d", "--densities")) {
            const char* val = GetNextArg(argc, argv, i, "--densities");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--densities requires a comma-separated list";
                return opts;
            }
            opts.densities = ParseFloatList(val);
            if (opts.densities.empty()) {
                opts.hasError = true;
                opts.parseError = "Invalid density list: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Pipelines
        if (ArgMatches(arg, "-p", "--pipelines")) {
            const char* val = GetNextArg(argc, argv, i, "--pipelines");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--pipelines requires a comma-separated list";
                return opts;
            }
            opts.pipelines = ParseStringList(val);
            continue;
        }

        // Algorithms
        if (ArgMatches(arg, "-a", "--algorithms")) {
            const char* val = GetNextArg(argc, argv, i, "--algorithms");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--algorithms requires a comma-separated list";
                return opts;
            }
            opts.algorithms = ParseStringList(val);
            continue;
        }

        // GPU index
        if (ArgMatches(arg, "-g", "--gpu")) {
            const char* val = GetNextArg(argc, argv, i, "--gpu");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--gpu requires a number";
                return opts;
            }
            try {
                opts.gpuIndex = static_cast<uint32_t>(std::stoul(val));
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --gpu: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Render dimensions
        if (ArgMatches(arg, nullptr, "--width")) {
            const char* val = GetNextArg(argc, argv, i, "--width");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--width requires a number";
                return opts;
            }
            try {
                opts.renderWidth = static_cast<uint32_t>(std::stoul(val));
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --width: " + std::string(val);
                return opts;
            }
            continue;
        }

        if (ArgMatches(arg, nullptr, "--height")) {
            const char* val = GetNextArg(argc, argv, i, "--height");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--height requires a number";
                return opts;
            }
            try {
                opts.renderHeight = static_cast<uint32_t>(std::stoul(val));
            } catch (const std::exception&) {
                opts.hasError = true;
                opts.parseError = "Invalid value for --height: " + std::string(val);
                return opts;
            }
            continue;
        }

        // Boolean flags
        if (ArgMatches(arg, nullptr, "--list-gpus")) {
            opts.listGpus = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--verbose")) {
            opts.verbose = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--debug")) {
            opts.enableValidation = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--quick")) {
            opts.quickMode = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--full")) {
            opts.fullMode = true;
            continue;
        }

        // Execution mode flags
        if (ArgMatches(arg, nullptr, "--headless")) {
            opts.headlessMode = true;
            opts.renderMode = false;
            opts.headlessExplicitlySet = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--render")) {
            opts.headlessMode = false;
            opts.renderMode = true;
            opts.headlessExplicitlySet = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--csv-only")) {
            opts.exportCSV = true;
            opts.exportJSON = false;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--json-only")) {
            opts.exportCSV = false;
            opts.exportJSON = true;
            continue;
        }

        // Save config option
        if (ArgMatches(arg, nullptr, "--save-config")) {
            const char* val = GetNextArg(argc, argv, i, "--save-config");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--save-config requires a file path";
                return opts;
            }
            opts.saveConfig = true;
            opts.saveConfigPath = val;
            continue;
        }

        // Package output options
        if (ArgMatches(arg, nullptr, "--package")) {
            opts.createPackage = true;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--no-package")) {
            opts.createPackage = false;
            continue;
        }

        if (ArgMatches(arg, nullptr, "--no-open")) {
            opts.openResultsFolder = false;
            continue;
        }

        // Tester name for package
        if (ArgMatches(arg, nullptr, "--tester")) {
            const char* val = GetNextArg(argc, argv, i, "--tester");
            if (!val) {
                opts.hasError = true;
                opts.parseError = "--tester requires a name";
                return opts;
            }
            std::string name = Trim(val);
            if (name.empty()) {
                opts.hasError = true;
                opts.parseError = "--tester name cannot be empty";
                return opts;
            }
            // Warn about characters that might cause filename issues
            bool hasSpecialChars = false;
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != ' ') {
                    hasSpecialChars = true;
                    break;
                }
            }
            if (hasSpecialChars) {
                std::cerr << "Warning: --tester name contains special characters that may cause filename issues.\n";
                std::cerr << "         Recommended format: letters, numbers, spaces, underscores, hyphens only.\n";
            }
            opts.testerName = name;
            continue;
        }

        // Unknown argument
        opts.hasError = true;
        opts.parseError = "Unknown argument: " + std::string(arg);
        return opts;
    }

    return opts;
}

std::vector<Vixen::Profiler::TestConfiguration> BenchmarkCLIOptions::GenerateTestConfigurations() const {
    using namespace Vixen::Profiler;

    // Quick mode: minimal test matrix
    if (quickMode) {
        return BenchmarkConfigLoader::GetQuickTestMatrix();
    }

    // Full mode: research test matrix
    if (fullMode) {
        return BenchmarkConfigLoader::GetResearchTestMatrix();
    }

    // Load from config file if specified
    if (hasConfigFile && std::filesystem::exists(configPath)) {
        auto configs = BenchmarkConfigLoader::LoadBatchFromFile(configPath);
        if (!configs.empty()) {
            // Apply CLI overrides
            for (auto& cfg : configs) {
                if (measurementFrames) cfg.measurementFrames = *measurementFrames;
                if (warmupFrames) cfg.warmupFrames = *warmupFrames;
                cfg.screenWidth = renderWidth;
                cfg.screenHeight = renderHeight;
            }
            return configs;
        }
    }

    // Generate from CLI parameters using new matrix structure
    GlobalMatrix globalMatrix;
    globalMatrix.resolutions = resolutions.empty()
        ? std::vector<uint32_t>{64, 128, 256}
        : resolutions;
    globalMatrix.renderSizes = {{renderWidth, renderHeight}};
    globalMatrix.scenes = {"cornell"};  // Default scene (global)

    std::vector<std::string> pipelineList = pipelines.empty()
        ? std::vector<std::string>{"compute"}
        : pipelines;

    std::vector<std::string> shaderList = algorithms.empty()
        ? std::vector<std::string>{"VoxelRayMarch.comp"}
        : algorithms;

    // Build pipeline matrices
    // Convert flat shader list to shader groups (each shader becomes its own group)
    std::map<std::string, PipelineMatrix> pipelineMatrices;
    for (const auto& pipeline : pipelineList) {
        PipelineMatrix pm;
        pm.enabled = true;
        // Each shader from CLI becomes a single-shader group
        for (const auto& shader : shaderList) {
            pm.shaderGroups.push_back({shader});
        }
        pipelineMatrices[pipeline] = pm;
    }

    auto configs = BenchmarkConfigLoader::GenerateTestMatrix(globalMatrix, pipelineMatrices);

    // Apply CLI overrides
    for (auto& cfg : configs) {
        if (measurementFrames) cfg.measurementFrames = *measurementFrames;
        if (warmupFrames) cfg.warmupFrames = *warmupFrames;
    }

    return configs;
}

std::string BenchmarkCLIOptions::GetRunName() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << "benchmark_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::vector<std::string> BenchmarkCLIOptions::Validate() const {
    std::vector<std::string> errors;

    // Check for conflicting modes
    if (quickMode && fullMode) {
        errors.push_back("Cannot use both --quick and --full modes");
    }

    // Validate resolutions
    for (auto res : resolutions) {
        if (!Vixen::Profiler::TestConfiguration::IsValidResolution(res)) {
            errors.push_back("Invalid resolution: " + std::to_string(res) +
                           " (must be power of 2: 32, 64, 128, 256, 512)");
        }
    }

    // Validate densities
    for (auto density : densities) {
        if (density < 0.0f || density > 100.0f) {
            errors.push_back("Invalid density: " + std::to_string(density) +
                           " (must be 0-100)");
        }
    }

    // Validate render dimensions
    if (renderWidth < 64 || renderWidth > 8192) {
        errors.push_back("Invalid width: " + std::to_string(renderWidth) +
                        " (must be 64-8192)");
    }
    if (renderHeight < 64 || renderHeight > 8192) {
        errors.push_back("Invalid height: " + std::to_string(renderHeight) +
                        " (must be 64-8192)");
    }

    // Validate pipelines
    for (const auto& pipeline : pipelines) {
        auto type = Vixen::Profiler::ParsePipelineType(pipeline);
        if (type == Vixen::Profiler::PipelineType::Invalid) {
            errors.push_back("Invalid pipeline type: " + pipeline +
                           " (valid: compute, fragment, hardware_rt, hybrid)");
        }
    }

    return errors;
}

void PrintHelp() {
    std::cout << R"(
VIXEN Benchmark Tool - GPU Ray Marching Performance Profiler

Usage: vixen_benchmark [options]

Configuration:
  -c, --config FILE       JSON configuration file (default: benchmark_config.json)
  -o, --output DIR        Output directory for results (default: Downloads/VIXEN_Benchmarks)

Test Parameters:
  -i, --iterations N      Measurement frames per test (default: 100)
  -w, --warmup N          Warmup frames before measurement (default: 10)
      --runs N            Run each config N times for statistical robustness (default: 1)
  -r, --resolutions LIST  Comma-separated voxel resolutions (e.g., 32,64,128,256)
  -d, --densities LIST    Comma-separated scene densities 0-100 (e.g., 10,30,50,70,90)
  -p, --pipelines LIST    Comma-separated pipeline types: compute,fragment,hardware_rt
  -a, --algorithms LIST   Comma-separated algorithms: baseline,empty_skip,blockwalk
      --width N           Render width in pixels (default: 800)
      --height N          Render height in pixels (default: 600)

GPU Selection:
  -g, --gpu N             GPU index to use (default: 0)
      --list-gpus         List available GPUs and exit

Preset Modes:
      --quick             Run minimal test matrix (12 configurations)
      --full              Run full research test matrix (180 configurations)

Execution Modes:
      --headless          Compute-only benchmark, no window (default)
      --render            Full rendering with window and real-time preview

Output Format:
      --csv-only          Export only CSV format
      --json-only         Export only JSON format
      --save-config FILE  Save current config to JSON file and exit
      --no-package        Skip ZIP package creation (package is created by default)
      --no-open           Don't auto-open results folder after completion
      --tester NAME       Tester name for package (e.g., "John_Doe" or "TeamAlpha")

Debug Options:
      --verbose           Enable detailed logging
      --debug             Enable Vulkan validation layers

General:
  -h, --help              Show this help message
  -v, --version           Show version information

Examples:
  # Run default benchmark suite
  vixen_benchmark

  # Run quick validation test
  vixen_benchmark --quick --output ./results

  # Custom configuration
  vixen_benchmark -r 64,128,256 -d 30,50,70 --iterations 200

  # Select specific GPU
  vixen_benchmark --list-gpus
  vixen_benchmark --gpu 1

  # Full research benchmark
  vixen_benchmark --full --output ./research_results --verbose

Output:
  By default, results are saved to your Downloads folder:
    Downloads/VIXEN_Benchmarks/VIXEN_benchmark_<date>_<time>_<gpu>.zip

  The ZIP package contains:
  - benchmark_results.json (detailed per-frame metrics)
  - system_info.json (hardware details)
  - debug_images/ (screenshots if captured)

  Metrics include: frame time, GPU time, rays/sec, VRAM usage, bandwidth.

)";
}

void PrintVersion() {
    std::cout << "VIXEN Benchmark Tool v1.0.0\n";
    std::cout << "GPU Ray Marching Performance Profiler\n";
    std::cout << "Built with Vulkan 1.4\n";
}

Vixen::Profiler::BenchmarkSuiteConfig BenchmarkCLIOptions::BuildSuiteConfig() const {
    using namespace Vixen::Profiler;

    BenchmarkSuiteConfig config;

    // Try to load from config file first (either explicit or default)
    // Search paths: explicit path, CWD, exe directory (for running from binaries/)
    std::vector<std::filesystem::path> searchPaths;
    if (hasConfigFile) {
        searchPaths.push_back(configPath);
    }

    // Get exe directory for relative searches
    std::filesystem::path exeDir;
#ifdef _WIN32
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        exeDir = std::filesystem::path(exePath).parent_path();
    }
#endif

    // Search relative to CWD
    searchPaths.push_back("benchmark_config.json");
    searchPaths.push_back("./application/benchmark/benchmark_config.json");

    // Search relative to exe directory (handles running from binaries/)
    if (!exeDir.empty()) {
        searchPaths.push_back(exeDir / "benchmark_config.json");
        searchPaths.push_back(exeDir / "../application/benchmark/benchmark_config.json");
    }

    std::filesystem::path loadedConfigPath;
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            config = BenchmarkSuiteConfig::LoadFromFile(path);
            if (!config.tests.empty() || !config.pipelineMatrices.empty()) {
                // Successfully loaded config, generate tests from matrix
                config.GenerateTestsFromMatrix();
                loadedConfigPath = std::filesystem::absolute(path);
                break;
            }
        }
    }

    // Show config source feedback
    if (!loadedConfigPath.empty()) {
        std::cout << "[Config] Loaded: " << loadedConfigPath.string() << "\n";
    } else {
        std::cout << "[Config] WARNING: No config file found, using built-in defaults\n";
        std::cout << "         Searched: ";
        for (size_t i = 0; i < searchPaths.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << searchPaths[i].string();
        }
        std::cout << "\n";
    }

    // Set output directory: CLI override > config file > Downloads folder default
    if (!outputDirectory.empty() && outputDirectory != "./benchmark_results") {
        // CLI explicitly provided output directory
        config.outputDir = outputDirectory;
    } else if (config.outputDir == "./benchmark_results" || config.outputDir.empty()) {
        // No override and config uses default - switch to Downloads folder
        config.outputDir = GetDefaultOutputDirectory();
    }
    // else: config file specified a custom path, keep it

    // Print output directory for user visibility
    std::cout << "[Output] Results will be saved to: " << std::filesystem::absolute(config.outputDir).string() << "\n";

    config.exportCSV = exportCSV;
    config.exportJSON = exportJSON;

    // Copy render dimensions to global matrix (CLI override)
    if (renderWidth != 800 || renderHeight != 600) {
        config.globalMatrix.renderSizes = {{renderWidth, renderHeight}};
    }

    // Copy GPU selection (but don't override runOnAllGPUs from config file)
    // When runOnAllGPUs is true, gpuIndex is ignored anyway
    config.gpuIndex = gpuIndex;
    // NOTE: config.runOnAllGPUs is preserved from LoadFromFile() - don't override it

    // Copy execution mode - only override if explicitly set via CLI
    if (headlessExplicitlySet) {
        config.headless = headlessMode;
    }
    if (verbose) config.verbose = verbose;
    if (enableValidation) config.enableValidation = enableValidation;

    // Copy frame overrides (only if provided via CLI)
    if (warmupFrames) config.warmupFramesOverride = warmupFrames;
    if (measurementFrames) config.measurementFramesOverride = measurementFrames;

    // Copy multi-run setting
    config.runsPerConfig = runsPerConfig;

    // Generate test configurations (if not already loaded from file)
    if (config.tests.empty()) {
        config.tests = GenerateTestConfigurations();
    }

    // Apply overrides to all tests
    config.ApplyOverrides();

    // Copy package settings
    config.createPackage = createPackage;
    config.testerName = testerName;

    return config;
}

} // namespace Vixen::Benchmark
