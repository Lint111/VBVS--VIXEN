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
#include <iostream>

int main(int argc, char* argv[]) {
    using namespace Vixen::Benchmark;
    using namespace Vixen::Profiler;

    // Parse command line arguments
    auto opts = ParseCommandLine(argc, argv);

    // Handle parse errors
    if (opts.hasError) {
        std::cerr << "Error: " << opts.parseError << "\n";
        std::cerr << "Use --help for usage information\n";
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

    // Validate CLI options
    auto errors = opts.Validate();
    if (!errors.empty()) {
        std::cerr << "Configuration errors:\n";
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
        std::cerr << "Suite configuration errors:\n";
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
        std::cerr << "Error: No tests were executed\n";
        return 1;
    }

    return (passed == total) ? 0 : 1;
}
