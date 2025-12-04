#include "BenchmarkCLI.h"
#include <Profiler/BenchmarkConfig.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace Vixen::Benchmark {

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
        std::cerr << "Error: " << argName << " requires a value\n";
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
            continue;
        }

        if (ArgMatches(arg, nullptr, "--render")) {
            opts.headlessMode = false;
            opts.renderMode = true;
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

    // Generate from CLI parameters
    std::vector<std::string> pipelineList = pipelines.empty()
        ? std::vector<std::string>{"compute"}
        : pipelines;

    std::vector<uint32_t> resolutionList = resolutions.empty()
        ? std::vector<uint32_t>{64, 128, 256}
        : resolutions;

    std::vector<float> densityList = densities.empty()
        ? std::vector<float>{30.0f, 50.0f, 70.0f}
        : densities;

    std::vector<std::string> algorithmList = algorithms.empty()
        ? std::vector<std::string>{"baseline"}
        : algorithms;

    auto configs = BenchmarkConfigLoader::GenerateTestMatrix(
        pipelineList, resolutionList, densityList, algorithmList);

    // Apply CLI overrides
    for (auto& cfg : configs) {
        if (measurementFrames) cfg.measurementFrames = *measurementFrames;
        if (warmupFrames) cfg.warmupFrames = *warmupFrames;
        cfg.screenWidth = renderWidth;
        cfg.screenHeight = renderHeight;
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
  -o, --output DIR        Output directory for results (default: ./benchmark_results)

Test Parameters:
  -i, --iterations N      Measurement frames per test (default: 100)
  -w, --warmup N          Warmup frames before measurement (default: 10)
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
  Results are exported to the output directory in CSV and/or JSON format.
  Each test configuration generates a separate file with metrics including:
  - Frame time (CPU/GPU)
  - Memory bandwidth
  - Rays per second
  - VRAM usage

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

    // Copy output settings
    config.outputDir = outputDirectory;
    config.exportCSV = exportCSV;
    config.exportJSON = exportJSON;

    // Copy render dimensions
    config.renderWidth = renderWidth;
    config.renderHeight = renderHeight;

    // Copy GPU selection
    config.gpuIndex = gpuIndex;

    // Copy execution mode
    config.headless = headlessMode;
    config.verbose = verbose;
    config.enableValidation = enableValidation;

    // Copy frame overrides
    config.warmupFramesOverride = warmupFrames;
    config.measurementFramesOverride = measurementFrames;

    // Generate test configurations
    config.tests = GenerateTestConfigurations();

    // Apply overrides to all tests
    config.ApplyOverrides();

    return config;
}

} // namespace Vixen::Benchmark
