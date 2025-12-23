/**
 * @file BenchmarkMain.cpp
 * @brief Config-only benchmark executable - all Vulkan handled by BenchmarkRunner
 *
 * This executable is a thin orchestrator that:
 * 1. Parses CLI arguments
 * 2. Creates BenchmarkSuiteConfig from CLI options
 * 3. Passes config to BenchmarkRunner::RunSuite()
 * 4. Reports results
 *
 * ALL Vulkan initialization, graph building, and execution is handled internally
 * by BenchmarkRunner. This file contains ZERO Vulkan API calls.
 *
 * Usage:
 *   vixen_benchmark --quick --output ./results
 *   vixen_benchmark --quick --render --output ./results
 *   vixen_benchmark --list-gpus
 *
 * See --help for full options.
 */

#include "BenchmarkCLI.h"
#include <Profiler/BenchmarkRunner.h>
#include <Profiler/BenchmarkConfig.h>
#include <VulkanGlobalNames.h>
#include <iostream>
#include <Logger.h>

#ifdef _WIN32
#include <shellapi.h>  // For ShellExecuteW
#endif

// Initialize global Vulkan extension/layer lists for windowed benchmark mode.
// These are requested by InstanceNode and DeviceNode when running with a window.
// Headless mode doesn't need these (creates its own minimal instance).
//
// Extension Classification:
// - REQUIRED: Swapchain (VK_KHR_SWAPCHAIN) - absolutely necessary for windowed mode
// - OPTIONAL: Maintenance extensions - provide enhanced features but gracefully degrade if unavailable
//
// DeviceNode validates all extensions and only enables those that are available (see DeviceNode.cpp:252-263).
// Optional extensions are skipped with warnings if not supported by the GPU.
static bool initGlobalNames = []() {
    deviceExtensionNames = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,                // REQUIRED for windowed mode
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,  // OPTIONAL: Enhanced swapchain features
        VK_KHR_MAINTENANCE_6_EXTENSION_NAME,            // OPTIONAL: General maintenance features
    };

    instanceExtensionNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,                          // REQUIRED for surface creation
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,            // OPTIONAL: Enhanced surface features
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,       // OPTIONAL: Extended capability queries
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME                     // REQUIRED on Windows platform
#ifdef _DEBUG
        , VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
    };

    layerNames = {
#ifdef _DEBUG
        "VK_LAYER_KHRONOS_validation"
#endif
    };

    return true;
}();

int main(int argc, char* argv[]) {
    using namespace Vixen::Benchmark;
    using namespace Vixen::Profiler;

    // Create logger for benchmark main
    auto mainLogger = std::make_shared<Vixen::Log::Logger>("BenchmarkMain", true);
    mainLogger->SetTerminalOutput(true);

    // Parse command line arguments
    auto opts = ParseCommandLine(argc, argv);

    // Handle parse errors
    if (opts.hasError) {
        mainLogger->Error("Error: " + opts.parseError);
        mainLogger->Error("Use --help for usage information");
        return 1;
    }

    // Show help
    if (opts.showHelp) {
        PrintHelp();
        return 0;
    }

    // List GPUs (static method, no instance needed)
    if (opts.listGpus) {
        BenchmarkRunner::ListAvailableGPUs();
        return 0;
    }

    // Save config and exit
    if (opts.saveConfig) {
        BenchmarkSuiteConfig config = opts.BuildSuiteConfig();
        if (config.SaveToFile(opts.saveConfigPath)) {
            mainLogger->Info("Configuration saved to: " + opts.saveConfigPath.string());
            std::cout << "Configuration saved to: " << opts.saveConfigPath << "\n";  // User-facing output
            return 0;
        } else {
            mainLogger->Error("Failed to save configuration to: " + opts.saveConfigPath.string());
            std::cerr << "Error: Failed to save configuration to: " << opts.saveConfigPath << "\n";  // User-facing error
            return 1;
        }
    }

    // Validate CLI options
    auto errors = opts.Validate();
    if (!errors.empty()) {
        mainLogger->Error("Configuration errors:");
        for (const auto& error : errors) {
            mainLogger->Error("  - " + error);
        }
        std::cerr << "Configuration errors:\n";  // User-facing error
        for (const auto& error : errors) {
            std::cerr << "  - " << error << "\n";
        }
        return 1;
    }

    // Build suite configuration from CLI options
    // This is the ONLY configuration step - no Vulkan here
    BenchmarkSuiteConfig config = opts.BuildSuiteConfig();

    // Validate suite configuration
    auto configErrors = config.Validate();
    if (!configErrors.empty()) {
        mainLogger->Error("Suite configuration errors:");
        for (const auto& error : configErrors) {
            mainLogger->Error("  - " + error);
        }
        std::cerr << "Suite configuration errors:\n";  // User-facing error
        for (const auto& error : configErrors) {
            std::cerr << "  - " << error << "\n";
        }
        return 1;
    }

    // Run the benchmark suite
    // BenchmarkRunner handles ALL Vulkan internally:
    // - Instance/device creation
    // - RenderGraph setup (headless or windowed)
    // - Test execution with profiler hooks
    // - Results collection and export
    // - Vulkan cleanup
    BenchmarkRunner runner;
    TestSuiteResults results = runner.RunSuite(config);

    // Report final status
    uint32_t passed = results.GetPassCount();
    uint32_t total = results.GetTotalCount();

    if (total == 0) {
        mainLogger->Error("No tests were executed");
        std::cerr << "Error: No tests were executed\n";  // User-facing error
        return 1;
    }

    // Auto-open results folder (Windows only, unless --no-open specified)
#ifdef _WIN32
    if (opts.openResultsFolder) {
        auto outputPath = std::filesystem::absolute(runner.GetOutputDirectory());
        if (std::filesystem::exists(outputPath)) {
            std::wstring pathW = outputPath.wstring();
            ShellExecuteW(nullptr, L"explore", pathW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            mainLogger->Info("Opened results folder: " + outputPath.string());
        }
    }
#endif

    return (passed == total) ? 0 : 1;
}
