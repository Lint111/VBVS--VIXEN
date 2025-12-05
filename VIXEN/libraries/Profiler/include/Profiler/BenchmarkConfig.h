#pragma once

#include "FrameMetrics.h"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <map>
#include <variant>

namespace Vixen::Profiler {

//==============================================================================
// Scene Definition
//==============================================================================

/// Scene source type
enum class SceneSourceType {
    File,       // Load from .vox or similar file
    Procedural  // Generate procedurally
};

/// Procedural scene parameters
struct ProceduralSceneParams {
    std::string generator;  // Generator type: perlin3d, voronoi_caves, buildings
    std::map<std::string, std::variant<int, float, std::string>> params;  // Generator-specific params
};

/// Scene definition for benchmark testing
struct SceneDefinition {
    std::string name;                           // Scene identifier (cornell, noise, tunnels, cityscape)
    SceneSourceType sourceType = SceneSourceType::Procedural;
    std::string filePath;                       // For File type: path to scene file
    ProceduralSceneParams procedural;           // For Procedural type: generator params

    /// Create file-based scene
    static SceneDefinition FromFile(const std::string& name, const std::string& path) {
        SceneDefinition scene;
        scene.name = name;
        scene.sourceType = SceneSourceType::File;
        scene.filePath = path;
        return scene;
    }

    /// Create procedural scene
    static SceneDefinition FromProcedural(const std::string& name, const std::string& generator) {
        SceneDefinition scene;
        scene.name = name;
        scene.sourceType = SceneSourceType::Procedural;
        scene.procedural.generator = generator;
        return scene;
    }
};

//==============================================================================
// Pipeline Matrix Configuration
//==============================================================================

/// Per-pipeline test configuration
struct PipelineMatrix {
    bool enabled = true;                                      // Whether to run tests for this pipeline
    std::vector<std::vector<std::string>> shaderGroups;       // Shader groups to test (each group = one pipeline config)
    // Example: [["VoxelRayMarch.comp"]] for compute
    //          [["Fullscreen.vert", "VoxelRayMarch.frag"]] for graphics
    //          [["ray.rgen", "ray.rmiss", "ray.rchit"]] for RT
};

/// Screen resolution pair
struct RenderSize {
    uint32_t width = 1280;
    uint32_t height = 720;

    bool operator==(const RenderSize& other) const {
        return width == other.width && height == other.height;
    }
};

/// Global matrix parameters (shared across all pipelines)
struct GlobalMatrix {
    std::vector<uint32_t> resolutions = {64, 128, 256};         // SVO resolutions
    std::vector<RenderSize> renderSizes = {{1280, 720}};        // Screen resolutions
    std::vector<std::string> scenes = {"cornell"};              // Scene identifiers (shared by all pipelines)
};

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

    /// List of test configurations to run (generated from matrix)
    std::vector<TestConfiguration> tests;

    /// Number of warmup frames (can override per-test settings)
    std::optional<uint32_t> warmupFramesOverride;

    /// Number of measurement frames (can override per-test settings)
    std::optional<uint32_t> measurementFramesOverride;

    /// Global matrix parameters (resolutions, screen sizes)
    GlobalMatrix globalMatrix;

    /// Per-pipeline matrix configurations
    std::map<std::string, PipelineMatrix> pipelineMatrices;

    /// Scene definitions (name -> definition)
    std::map<std::string, SceneDefinition> sceneDefinitions;

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
     * @brief Generate test configurations from matrix settings
     *
     * Generates all combinations of:
     * - Global: resolutions x renderSizes
     * - Per pipeline: scenes x shaders
     */
    void GenerateTestsFromMatrix();

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

        // Validate global matrix
        for (const auto& res : globalMatrix.resolutions) {
            if (!TestConfiguration::IsValidResolution(res)) {
                errors.push_back("Invalid resolution: " + std::to_string(res));
            }
        }
        for (const auto& size : globalMatrix.renderSizes) {
            if (size.width < 64 || size.width > 8192) {
                errors.push_back("Invalid render width: " + std::to_string(size.width));
            }
            if (size.height < 64 || size.height > 8192) {
                errors.push_back("Invalid render height: " + std::to_string(size.height));
            }
        }

        return errors;
    }

    /**
     * @brief Check if configuration is valid
     */
    bool IsValid() const {
        return Validate().empty();
    }

    /**
     * @brief Load suite configuration from JSON file
     *
     * JSON schema:
     * @code
     * {
     *   "suite": {
     *     "name": "My Benchmark Suite",
     *     "output_dir": "./results",
     *     "gpu_index": 0,
     *     "headless": true,
     *     "verbose": false,
     *     "validation": false,
     *     "export": { "csv": true, "json": true }
     *   },
     *   "profiling": {
     *     "warmup_frames": 100,
     *     "measurement_frames": 300
     *   },
     *   "matrix": {
     *     "global": {
     *       "resolutions": [64, 128, 256],
     *       "render_sizes": [[1280, 720], [1920, 1080]]
     *     },
     *     "pipelines": {
     *       "compute": {
     *         "enabled": true,
     *         "scenes": ["cornell", "noise", "tunnels", "cityscape"],
     *         "shaders": ["ray_march_base", "ray_march_esvo", "ray_march_compressed"]
     *       },
     *       "fragment": {
     *         "enabled": false,
     *         "scenes": ["cornell"],
     *         "shaders": ["ray_march_frag"]
     *       }
     *     }
     *   },
     *   "scenes": {
     *     "cornell": { "type": "file", "path": "assets/cornell.vox" },
     *     "noise": { "type": "procedural", "generator": "perlin3d" },
     *     "tunnels": { "type": "procedural", "generator": "voronoi_caves" },
     *     "cityscape": { "type": "procedural", "generator": "buildings" }
     *   }
     * }
     * @endcode
     *
     * @param filepath Path to JSON config file
     * @return Loaded config, or empty config on error
     */
    static BenchmarkSuiteConfig LoadFromFile(const std::filesystem::path& filepath);

    /**
     * @brief Save suite configuration to JSON file
     * @param filepath Path to save JSON file
     * @return true if saved successfully
     */
    bool SaveToFile(const std::filesystem::path& filepath) const;

    /**
     * @brief Get default quick test suite configuration
     * @return Pre-configured suite with minimal test matrix
     */
    static BenchmarkSuiteConfig GetQuickConfig();

    /**
     * @brief Get default research test suite configuration
     * @return Pre-configured suite with full research test matrix
     */
    static BenchmarkSuiteConfig GetResearchConfig();

    /// Suite name for reports (optional)
    std::string suiteName = "Benchmark Suite";
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

    /// Generate test matrix from hierarchical configuration
    /// @param globalMatrix Global parameters (resolutions, render sizes)
    /// @param pipelineMatrices Per-pipeline configurations
    /// @return Vector of all configuration combinations
    static std::vector<TestConfiguration> GenerateTestMatrix(
        const GlobalMatrix& globalMatrix,
        const std::map<std::string, PipelineMatrix>& pipelineMatrices);

    /// Save configuration to JSON file
    static bool SaveToFile(const TestConfiguration& config, const std::filesystem::path& filepath);

    /// Save batch of configurations to JSON file (as test matrix)
    static bool SaveBatchToFile(const std::vector<TestConfiguration>& configs,
                                const std::filesystem::path& filepath);

    /// Get default test matrix for research
    /// All pipelines x multiple resolutions x all scenes x all shaders
    static std::vector<TestConfiguration> GetResearchTestMatrix();

    /// Get minimal test matrix for quick validation
    static std::vector<TestConfiguration> GetQuickTestMatrix();

    /// Parse configuration from JSON string
    static std::optional<TestConfiguration> ParseFromString(const std::string& jsonString);

    /// Serialize configuration to JSON string
    static std::string SerializeToString(const TestConfiguration& config);

private:
    static TestConfiguration ParseConfigObject(const void* jsonObject);
};

} // namespace Vixen::Profiler
