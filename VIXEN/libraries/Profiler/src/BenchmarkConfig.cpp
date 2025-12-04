#include "Profiler/BenchmarkConfig.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <set>

namespace Vixen::Profiler {

std::optional<TestConfiguration> BenchmarkConfigLoader::LoadFromFile(const std::filesystem::path& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return std::nullopt;
    }

    try {
        nlohmann::json j;
        file >> j;
        return ParseConfigObject(&j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<TestConfiguration> BenchmarkConfigLoader::LoadBatchFromFile(const std::filesystem::path& filepath) {
    std::vector<TestConfiguration> configs;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return configs;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Check if it's a matrix configuration
        if (j.contains("matrix")) {
            auto& matrix = j["matrix"];
            std::vector<std::string> pipelines;
            std::vector<uint32_t> resolutions;
            std::vector<float> densities;
            std::vector<std::string> algorithms;

            if (matrix.contains("pipelines")) {
                for (const auto& p : matrix["pipelines"]) {
                    pipelines.push_back(p.get<std::string>());
                }
            }
            if (matrix.contains("resolutions")) {
                for (const auto& r : matrix["resolutions"]) {
                    resolutions.push_back(r.get<uint32_t>());
                }
            }
            if (matrix.contains("densities")) {
                for (const auto& d : matrix["densities"]) {
                    densities.push_back(d.get<float>());
                }
            }
            if (matrix.contains("algorithms")) {
                for (const auto& a : matrix["algorithms"]) {
                    algorithms.push_back(a.get<std::string>());
                }
            }

            configs = GenerateTestMatrix(pipelines, resolutions, densities, algorithms);

            // Apply common settings
            if (j.contains("common")) {
                auto& common = j["common"];
                for (auto& config : configs) {
                    if (common.contains("scene")) {
                        config.sceneType = common["scene"].get<std::string>();
                    }
                    if (common.contains("render")) {
                        if (common["render"].contains("width")) {
                            config.screenWidth = common["render"]["width"].get<uint32_t>();
                        }
                        if (common["render"].contains("height")) {
                            config.screenHeight = common["render"]["height"].get<uint32_t>();
                        }
                    }
                    if (common.contains("profiling")) {
                        if (common["profiling"].contains("warmupFrames")) {
                            config.warmupFrames = common["profiling"]["warmupFrames"].get<uint32_t>();
                        }
                        if (common["profiling"].contains("measurementFrames")) {
                            config.measurementFrames = common["profiling"]["measurementFrames"].get<uint32_t>();
                        }
                    }
                }
            }
        }
        // Check if it's a batch of individual configs
        else if (j.contains("benchmarks") && j["benchmarks"].is_array()) {
            for (const auto& item : j["benchmarks"]) {
                auto config = ParseConfigObject(&item);
                if (config.Validate()) {
                    configs.push_back(config);
                }
            }
        }
        // Single config
        else {
            auto config = ParseConfigObject(&j);
            if (config.Validate()) {
                configs.push_back(config);
            }
        }
    } catch (const std::exception&) {
        configs.clear();
    }

    return configs;
}

std::vector<TestConfiguration> BenchmarkConfigLoader::GenerateTestMatrix(
    const std::vector<std::string>& pipelines,
    const std::vector<uint32_t>& resolutions,
    const std::vector<float>& densities,
    const std::vector<std::string>& algorithms) {

    std::vector<TestConfiguration> configs;

    for (const auto& pipeline : pipelines) {
        for (const auto& resolution : resolutions) {
            for (const auto& density : densities) {
                for (const auto& algorithm : algorithms) {
                    TestConfiguration config;
                    config.pipeline = pipeline;
                    config.voxelResolution = resolution;
                    config.densityPercent = density;
                    config.algorithm = algorithm;
                    configs.push_back(config);
                }
            }
        }
    }

    return configs;
}

bool BenchmarkConfigLoader::SaveToFile(const TestConfiguration& config, const std::filesystem::path& filepath) {
    nlohmann::json j;
    j["pipeline"] = config.pipeline;
    j["algorithm"] = config.algorithm;
    j["scene"]["type"] = config.sceneType;
    j["scene"]["resolution"] = config.voxelResolution;
    j["scene"]["density"] = config.densityPercent;
    j["render"]["width"] = config.screenWidth;
    j["render"]["height"] = config.screenHeight;
    j["profiling"]["warmupFrames"] = config.warmupFrames;
    j["profiling"]["measurementFrames"] = config.measurementFrames;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(2);
    return true;
}

bool BenchmarkConfigLoader::SaveBatchToFile(const std::vector<TestConfiguration>& configs,
                                            const std::filesystem::path& filepath) {
    nlohmann::json j;
    j["benchmarks"] = nlohmann::json::array();

    for (const auto& config : configs) {
        nlohmann::json c;
        c["pipeline"] = config.pipeline;
        c["algorithm"] = config.algorithm;
        c["scene"]["type"] = config.sceneType;
        c["scene"]["resolution"] = config.voxelResolution;
        c["scene"]["density"] = config.densityPercent;
        c["render"]["width"] = config.screenWidth;
        c["render"]["height"] = config.screenHeight;
        c["profiling"]["warmupFrames"] = config.warmupFrames;
        c["profiling"]["measurementFrames"] = config.measurementFrames;
        j["benchmarks"].push_back(c);
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(2);
    return true;
}

std::vector<TestConfiguration> BenchmarkConfigLoader::GetResearchTestMatrix() {
    return GenerateTestMatrix(
        {"compute", "fragment", "hardware_rt", "hybrid"},
        {32, 64, 128, 256, 512},
        {0.2f, 0.5f, 0.8f},
        {"baseline", "empty_skip", "blockwalk"}
    );
}

std::vector<TestConfiguration> BenchmarkConfigLoader::GetQuickTestMatrix() {
    return GenerateTestMatrix(
        {"compute"},
        {64, 128},
        {0.2f, 0.5f},
        {"baseline", "empty_skip"}
    );
}

std::optional<TestConfiguration> BenchmarkConfigLoader::ParseFromString(const std::string& jsonString) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonString);
        return ParseConfigObject(&j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string BenchmarkConfigLoader::SerializeToString(const TestConfiguration& config) {
    nlohmann::json j;
    j["pipeline"] = config.pipeline;
    j["algorithm"] = config.algorithm;
    j["scene"]["type"] = config.sceneType;
    j["scene"]["resolution"] = config.voxelResolution;
    j["scene"]["density"] = config.densityPercent;
    j["render"]["width"] = config.screenWidth;
    j["render"]["height"] = config.screenHeight;
    j["profiling"]["warmupFrames"] = config.warmupFrames;
    j["profiling"]["measurementFrames"] = config.measurementFrames;
    return j.dump(2);
}

TestConfiguration BenchmarkConfigLoader::ParseConfigObject(const void* jsonObject) {
    const nlohmann::json& j = *static_cast<const nlohmann::json*>(jsonObject);
    TestConfiguration config;

    if (j.contains("pipeline")) config.pipeline = j["pipeline"].get<std::string>();
    if (j.contains("algorithm")) config.algorithm = j["algorithm"].get<std::string>();

    if (j.contains("scene")) {
        auto& scene = j["scene"];
        if (scene.contains("type")) config.sceneType = scene["type"].get<std::string>();
        if (scene.contains("resolution")) config.voxelResolution = scene["resolution"].get<uint32_t>();
        if (scene.contains("density")) config.densityPercent = scene["density"].get<float>();
    }

    if (j.contains("render")) {
        auto& render = j["render"];
        if (render.contains("width")) config.screenWidth = render["width"].get<uint32_t>();
        if (render.contains("height")) config.screenHeight = render["height"].get<uint32_t>();
    }

    if (j.contains("profiling")) {
        auto& profiling = j["profiling"];
        if (profiling.contains("warmupFrames")) config.warmupFrames = profiling["warmupFrames"].get<uint32_t>();
        if (profiling.contains("measurementFrames")) config.measurementFrames = profiling["measurementFrames"].get<uint32_t>();
    }

    return config;
}

//==============================================================================
// BenchmarkSuiteConfig Static Methods
//==============================================================================

BenchmarkSuiteConfig BenchmarkSuiteConfig::LoadFromFile(const std::filesystem::path& filepath) {
    BenchmarkSuiteConfig config;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return config;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Parse suite settings
        if (j.contains("suite")) {
            auto& suite = j["suite"];
            if (suite.contains("name")) config.suiteName = suite["name"].get<std::string>();
            if (suite.contains("output_dir")) config.outputDir = suite["output_dir"].get<std::string>();
            if (suite.contains("gpu_index")) config.gpuIndex = suite["gpu_index"].get<uint32_t>();
            if (suite.contains("headless")) config.headless = suite["headless"].get<bool>();
            if (suite.contains("verbose")) config.verbose = suite["verbose"].get<bool>();
            if (suite.contains("validation")) config.enableValidation = suite["validation"].get<bool>();

            if (suite.contains("render")) {
                if (suite["render"].contains("width")) config.renderWidth = suite["render"]["width"].get<uint32_t>();
                if (suite["render"].contains("height")) config.renderHeight = suite["render"]["height"].get<uint32_t>();
            }

            if (suite.contains("export")) {
                if (suite["export"].contains("csv")) config.exportCSV = suite["export"]["csv"].get<bool>();
                if (suite["export"].contains("json")) config.exportJSON = suite["export"]["json"].get<bool>();
            }
        }

        // Parse profiling settings
        if (j.contains("profiling")) {
            auto& profiling = j["profiling"];
            if (profiling.contains("warmup_frames")) {
                config.warmupFramesOverride = profiling["warmup_frames"].get<uint32_t>();
            }
            if (profiling.contains("measurement_frames")) {
                config.measurementFramesOverride = profiling["measurement_frames"].get<uint32_t>();
            }
        }

        // Parse test matrix
        if (j.contains("matrix")) {
            auto& matrix = j["matrix"];
            std::vector<std::string> pipelines;
            std::vector<uint32_t> resolutions;
            std::vector<float> densities;
            std::vector<std::string> algorithms;

            if (matrix.contains("pipelines")) {
                for (const auto& p : matrix["pipelines"]) {
                    pipelines.push_back(p.get<std::string>());
                }
            }
            if (matrix.contains("resolutions")) {
                for (const auto& r : matrix["resolutions"]) {
                    resolutions.push_back(r.get<uint32_t>());
                }
            }
            if (matrix.contains("densities")) {
                for (const auto& d : matrix["densities"]) {
                    densities.push_back(d.get<float>());
                }
            }
            if (matrix.contains("algorithms")) {
                for (const auto& a : matrix["algorithms"]) {
                    algorithms.push_back(a.get<std::string>());
                }
            }

            config.tests = BenchmarkConfigLoader::GenerateTestMatrix(pipelines, resolutions, densities, algorithms);
        }

        // Parse common settings for all tests
        if (j.contains("common")) {
            auto& common = j["common"];
            for (auto& test : config.tests) {
                if (common.contains("scene")) {
                    test.sceneType = common["scene"].get<std::string>();
                }
            }
        }

        // Apply profiling overrides to all tests
        config.ApplyOverrides();

    } catch (const std::exception&) {
        // Return empty config on parse error
        return BenchmarkSuiteConfig{};
    }

    return config;
}

bool BenchmarkSuiteConfig::SaveToFile(const std::filesystem::path& filepath) const {
    nlohmann::json j;

    // Suite settings
    j["suite"]["name"] = suiteName;
    j["suite"]["output_dir"] = outputDir.string();
    j["suite"]["render"]["width"] = renderWidth;
    j["suite"]["render"]["height"] = renderHeight;
    j["suite"]["gpu_index"] = gpuIndex;
    j["suite"]["headless"] = headless;
    j["suite"]["verbose"] = verbose;
    j["suite"]["validation"] = enableValidation;
    j["suite"]["export"]["csv"] = exportCSV;
    j["suite"]["export"]["json"] = exportJSON;

    // Profiling settings
    if (warmupFramesOverride) {
        j["profiling"]["warmup_frames"] = *warmupFramesOverride;
    } else if (!tests.empty()) {
        j["profiling"]["warmup_frames"] = tests[0].warmupFrames;
    }
    if (measurementFramesOverride) {
        j["profiling"]["measurement_frames"] = *measurementFramesOverride;
    } else if (!tests.empty()) {
        j["profiling"]["measurement_frames"] = tests[0].measurementFrames;
    }

    // Extract unique values from tests to build matrix
    std::set<std::string> pipelines, algorithms, scenes;
    std::set<uint32_t> resolutions;
    std::set<float> densities;

    for (const auto& test : tests) {
        pipelines.insert(test.pipeline);
        algorithms.insert(test.algorithm);
        scenes.insert(test.sceneType);
        resolutions.insert(test.voxelResolution);
        densities.insert(test.densityPercent);
    }

    j["matrix"]["pipelines"] = std::vector<std::string>(pipelines.begin(), pipelines.end());
    j["matrix"]["resolutions"] = std::vector<uint32_t>(resolutions.begin(), resolutions.end());
    j["matrix"]["densities"] = std::vector<float>(densities.begin(), densities.end());
    j["matrix"]["algorithms"] = std::vector<std::string>(algorithms.begin(), algorithms.end());

    // Common settings (use first scene if all same)
    if (scenes.size() == 1) {
        j["common"]["scene"] = *scenes.begin();
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(2);
    return true;
}

BenchmarkSuiteConfig BenchmarkSuiteConfig::GetQuickConfig() {
    BenchmarkSuiteConfig config;
    config.suiteName = "Quick Validation Suite";
    config.tests = BenchmarkConfigLoader::GetQuickTestMatrix();
    config.warmupFramesOverride = 10;
    config.measurementFramesOverride = 100;
    config.ApplyOverrides();
    return config;
}

BenchmarkSuiteConfig BenchmarkSuiteConfig::GetResearchConfig() {
    BenchmarkSuiteConfig config;
    config.suiteName = "Research Benchmark Suite";
    config.tests = BenchmarkConfigLoader::GetResearchTestMatrix();
    config.warmupFramesOverride = 60;
    config.measurementFramesOverride = 300;
    config.ApplyOverrides();
    return config;
}

} // namespace Vixen::Profiler
