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

        // Check if it's a new hierarchical matrix configuration
        if (j.contains("matrix") && j["matrix"].contains("global") && j["matrix"].contains("pipelines")) {
            GlobalMatrix global;
            std::map<std::string, PipelineMatrix> pipelineMatrices;

            auto& matrix = j["matrix"];

            // Parse global matrix
            if (matrix["global"].contains("resolutions")) {
                for (const auto& r : matrix["global"]["resolutions"]) {
                    global.resolutions.push_back(r.get<uint32_t>());
                }
            }
            if (matrix["global"].contains("render_sizes")) {
                global.renderSizes.clear();
                for (const auto& size : matrix["global"]["render_sizes"]) {
                    if (size.is_array() && size.size() == 2) {
                        RenderSize rs;
                        rs.width = size[0].get<uint32_t>();
                        rs.height = size[1].get<uint32_t>();
                        global.renderSizes.push_back(rs);
                    }
                }
            }
            if (matrix["global"].contains("scenes")) {
                global.scenes.clear();
                for (const auto& s : matrix["global"]["scenes"]) {
                    global.scenes.push_back(s.get<std::string>());
                }
            }

            // Parse pipeline matrices
            for (auto& [pipelineName, pipelineConfig] : matrix["pipelines"].items()) {
                PipelineMatrix pm;
                if (pipelineConfig.contains("enabled")) {
                    pm.enabled = pipelineConfig["enabled"].get<bool>();
                }
                // New format: shader_groups (array of arrays)
                if (pipelineConfig.contains("shader_groups")) {
                    for (const auto& group : pipelineConfig["shader_groups"]) {
                        std::vector<std::string> shaderGroup;
                        for (const auto& sh : group) {
                            shaderGroup.push_back(sh.get<std::string>());
                        }
                        pm.shaderGroups.push_back(shaderGroup);
                    }
                }
                // Legacy format: shaders (flat array) - convert to single-shader groups
                else if (pipelineConfig.contains("shaders")) {
                    for (const auto& sh : pipelineConfig["shaders"]) {
                        pm.shaderGroups.push_back({sh.get<std::string>()});
                    }
                }
                pipelineMatrices[pipelineName] = pm;
            }

            configs = GenerateTestMatrix(global, pipelineMatrices);
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
    const GlobalMatrix& globalMatrix,
    const std::map<std::string, PipelineMatrix>& pipelineMatrices) {

    std::vector<TestConfiguration> configs;

    // For each pipeline
    for (const auto& [pipelineName, pipelineMatrix] : pipelineMatrices) {
        if (!pipelineMatrix.enabled) {
            continue;
        }

        // For each global resolution
        for (const auto& resolution : globalMatrix.resolutions) {
            // For each screen size
            for (const auto& renderSize : globalMatrix.renderSizes) {
                // For each scene (from global matrix)
                for (const auto& scene : globalMatrix.scenes) {
                    // For each shader group in this pipeline
                    for (const auto& shaderGroup : pipelineMatrix.shaderGroups) {
                        TestConfiguration config;
                        config.pipeline = pipelineName;
                        config.voxelResolution = resolution;
                        config.screenWidth = renderSize.width;
                        config.screenHeight = renderSize.height;
                        config.sceneType = scene;
                        config.shaderGroup = shaderGroup;
                        // Use first shader (or last for fragment) as primary identifier
                        config.shader = shaderGroup.empty() ? "" : shaderGroup.back();
                        configs.push_back(config);
                    }
                }
            }
        }
    }

    return configs;
}

bool BenchmarkConfigLoader::SaveToFile(const TestConfiguration& config, const std::filesystem::path& filepath) {
    nlohmann::json j;
    j["pipeline"] = config.pipeline;
    j["shader"] = config.shader;
    j["scene"] = config.sceneType;
    j["resolution"] = config.voxelResolution;
    j["render"]["width"] = config.screenWidth;
    j["render"]["height"] = config.screenHeight;
    j["profiling"]["warmup_frames"] = config.warmupFrames;
    j["profiling"]["measurement_frames"] = config.measurementFrames;

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
        c["shader"] = config.shader;
        c["scene"] = config.sceneType;
        c["resolution"] = config.voxelResolution;
        c["render"]["width"] = config.screenWidth;
        c["render"]["height"] = config.screenHeight;
        c["profiling"]["warmup_frames"] = config.warmupFrames;
        c["profiling"]["measurement_frames"] = config.measurementFrames;
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
    GlobalMatrix global;
    global.resolutions = {64, 128, 256, 512};
    global.renderSizes = {{1280, 720}, {1920, 1080}};
    global.scenes = {"cornell", "noise", "tunnels", "cityscape"};

    std::map<std::string, PipelineMatrix> pipelines;

    // Compute pipeline - full matrix
    PipelineMatrix compute;
    compute.enabled = true;
    compute.shaderGroups = {{"VoxelRayMarch.comp"}, {"VoxelRayMarch_Compressed.comp"}};
    pipelines["compute"] = compute;

    // Fragment pipeline
    PipelineMatrix fragment;
    fragment.enabled = true;
    fragment.shaderGroups = {{"Fullscreen.vert", "VoxelRayMarch.frag"}};
    pipelines["fragment"] = fragment;

    // Hardware RT pipeline - disabled by default
    PipelineMatrix hardware_rt;
    hardware_rt.enabled = false;
    hardware_rt.shaderGroups = {{"VoxelRayMarch_RT.rgen", "VoxelRayMarch_RT.rmiss", "VoxelRayMarch_RT.rchit"}};
    pipelines["hardware_rt"] = hardware_rt;

    return GenerateTestMatrix(global, pipelines);
}

std::vector<TestConfiguration> BenchmarkConfigLoader::GetQuickTestMatrix() {
    GlobalMatrix global;
    global.resolutions = {64, 128};
    global.renderSizes = {{800, 600}};
    global.scenes = {"cornell"};

    std::map<std::string, PipelineMatrix> pipelines;

    PipelineMatrix compute;
    compute.enabled = true;
    compute.shaderGroups = {{"VoxelRayMarch.comp"}};
    pipelines["compute"] = compute;

    return GenerateTestMatrix(global, pipelines);
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
    j["shader"] = config.shader;
    j["scene"] = config.sceneType;
    j["resolution"] = config.voxelResolution;
    j["render"]["width"] = config.screenWidth;
    j["render"]["height"] = config.screenHeight;
    j["profiling"]["warmup_frames"] = config.warmupFrames;
    j["profiling"]["measurement_frames"] = config.measurementFrames;
    return j.dump(2);
}

TestConfiguration BenchmarkConfigLoader::ParseConfigObject(const void* jsonObject) {
    const nlohmann::json& j = *static_cast<const nlohmann::json*>(jsonObject);
    TestConfiguration config;

    if (j.contains("pipeline")) config.pipeline = j["pipeline"].get<std::string>();
    if (j.contains("shader")) config.shader = j["shader"].get<std::string>();
    // Legacy support for "algorithm" field
    if (j.contains("algorithm")) config.shader = j["algorithm"].get<std::string>();

    if (j.contains("scene")) config.sceneType = j["scene"].get<std::string>();
    if (j.contains("resolution")) config.voxelResolution = j["resolution"].get<uint32_t>();

    // Legacy scene object format
    if (j.contains("scene") && j["scene"].is_object()) {
        auto& scene = j["scene"];
        if (scene.contains("type")) config.sceneType = scene["type"].get<std::string>();
        if (scene.contains("resolution")) config.voxelResolution = scene["resolution"].get<uint32_t>();
    }

    if (j.contains("render")) {
        auto& render = j["render"];
        if (render.contains("width")) config.screenWidth = render["width"].get<uint32_t>();
        if (render.contains("height")) config.screenHeight = render["height"].get<uint32_t>();
    }

    if (j.contains("profiling")) {
        auto& profiling = j["profiling"];
        if (profiling.contains("warmup_frames")) config.warmupFrames = profiling["warmup_frames"].get<uint32_t>();
        if (profiling.contains("measurement_frames")) config.measurementFrames = profiling["measurement_frames"].get<uint32_t>();
        // Legacy camelCase support
        if (profiling.contains("warmupFrames")) config.warmupFrames = profiling["warmupFrames"].get<uint32_t>();
        if (profiling.contains("measurementFrames")) config.measurementFrames = profiling["measurementFrames"].get<uint32_t>();
    }

    return config;
}

//==============================================================================
// BenchmarkSuiteConfig Methods
//==============================================================================

void BenchmarkSuiteConfig::GenerateTestsFromMatrix() {
    tests.clear();
    uint32_t runNumber = 1;

    // Generate tests from matrix configuration
    for (const auto& [pipelineName, pipelineMatrix] : pipelineMatrices) {
        if (!pipelineMatrix.enabled) {
            continue;
        }

        for (uint32_t resolution : globalMatrix.resolutions) {
            for (const auto& renderSize : globalMatrix.renderSizes) {
                for (const auto& sceneName : globalMatrix.scenes) {
                    for (const auto& shaderGroup : pipelineMatrix.shaderGroups) {
                        TestConfiguration test;
                        test.pipeline = pipelineName;
                        test.voxelResolution = resolution;
                        test.screenWidth = renderSize.width;
                        test.screenHeight = renderSize.height;
                        test.sceneType = sceneName;
                        test.shaderGroup = shaderGroup;
                        test.shader = shaderGroup.empty() ? "" : shaderGroup.back();
                        test.testId = test.GenerateTestId(runNumber++);
                        tests.push_back(test);
                    }
                }
            }
        }
    }

    ApplyOverrides();
}

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

        // Parse matrix configuration
        if (j.contains("matrix")) {
            auto& matrix = j["matrix"];

            // Parse global matrix
            if (matrix.contains("global")) {
                auto& global = matrix["global"];
                if (global.contains("resolutions")) {
                    config.globalMatrix.resolutions.clear();
                    for (const auto& r : global["resolutions"]) {
                        config.globalMatrix.resolutions.push_back(r.get<uint32_t>());
                    }
                }
                if (global.contains("render_sizes")) {
                    config.globalMatrix.renderSizes.clear();
                    for (const auto& size : global["render_sizes"]) {
                        if (size.is_array() && size.size() == 2) {
                            RenderSize rs;
                            rs.width = size[0].get<uint32_t>();
                            rs.height = size[1].get<uint32_t>();
                            config.globalMatrix.renderSizes.push_back(rs);
                        }
                    }
                }
                if (global.contains("scenes")) {
                    config.globalMatrix.scenes.clear();
                    for (const auto& s : global["scenes"]) {
                        config.globalMatrix.scenes.push_back(s.get<std::string>());
                    }
                }
            }

            // Parse pipeline matrices
            if (matrix.contains("pipelines")) {
                for (auto& [pipelineName, pipelineConfig] : matrix["pipelines"].items()) {
                    PipelineMatrix pm;
                    if (pipelineConfig.contains("enabled")) {
                        pm.enabled = pipelineConfig["enabled"].get<bool>();
                    }
                    // New format: shader_groups (array of arrays)
                    if (pipelineConfig.contains("shader_groups")) {
                        for (const auto& group : pipelineConfig["shader_groups"]) {
                            std::vector<std::string> shaderGroup;
                            for (const auto& sh : group) {
                                shaderGroup.push_back(sh.get<std::string>());
                            }
                            pm.shaderGroups.push_back(shaderGroup);
                        }
                    }
                    // Legacy format: shaders (flat array) - convert to single-shader groups
                    else if (pipelineConfig.contains("shaders")) {
                        for (const auto& sh : pipelineConfig["shaders"]) {
                            pm.shaderGroups.push_back({sh.get<std::string>()});
                        }
                    }
                    config.pipelineMatrices[pipelineName] = pm;
                }
            }
        }

        // Parse scene definitions
        if (j.contains("scenes")) {
            for (auto& [sceneName, sceneConfig] : j["scenes"].items()) {
                SceneDefinition scene;
                scene.name = sceneName;

                if (sceneConfig.contains("type")) {
                    std::string typeStr = sceneConfig["type"].get<std::string>();
                    if (typeStr == "file") {
                        scene.sourceType = SceneSourceType::File;
                        if (sceneConfig.contains("path")) {
                            scene.filePath = sceneConfig["path"].get<std::string>();
                        }
                    } else if (typeStr == "procedural") {
                        scene.sourceType = SceneSourceType::Procedural;
                        if (sceneConfig.contains("generator")) {
                            scene.procedural.generator = sceneConfig["generator"].get<std::string>();
                        }
                        if (sceneConfig.contains("params")) {
                            for (auto& [paramName, paramValue] : sceneConfig["params"].items()) {
                                if (paramValue.is_number_integer()) {
                                    scene.procedural.params[paramName] = paramValue.get<int>();
                                } else if (paramValue.is_number_float()) {
                                    scene.procedural.params[paramName] = paramValue.get<float>();
                                } else if (paramValue.is_string()) {
                                    scene.procedural.params[paramName] = paramValue.get<std::string>();
                                }
                            }
                        }
                    }
                }
                config.sceneDefinitions[sceneName] = scene;
            }
        }

        // Generate tests from matrix
        config.GenerateTestsFromMatrix();

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
    } else {
        j["profiling"]["warmup_frames"] = 100;
    }
    if (measurementFramesOverride) {
        j["profiling"]["measurement_frames"] = *measurementFramesOverride;
    } else if (!tests.empty()) {
        j["profiling"]["measurement_frames"] = tests[0].measurementFrames;
    } else {
        j["profiling"]["measurement_frames"] = 300;
    }

    // Global matrix
    j["matrix"]["global"]["resolutions"] = globalMatrix.resolutions;
    nlohmann::json renderSizesJson = nlohmann::json::array();
    for (const auto& size : globalMatrix.renderSizes) {
        renderSizesJson.push_back({size.width, size.height});
    }
    j["matrix"]["global"]["render_sizes"] = renderSizesJson;
    j["matrix"]["global"]["scenes"] = globalMatrix.scenes;

    // Pipeline matrices
    for (const auto& [pipelineName, pm] : pipelineMatrices) {
        j["matrix"]["pipelines"][pipelineName]["enabled"] = pm.enabled;
        j["matrix"]["pipelines"][pipelineName]["shader_groups"] = pm.shaderGroups;
    }

    // Scene definitions
    for (const auto& [sceneName, scene] : sceneDefinitions) {
        if (scene.sourceType == SceneSourceType::File) {
            j["scenes"][sceneName]["type"] = "file";
            j["scenes"][sceneName]["path"] = scene.filePath;
        } else {
            j["scenes"][sceneName]["type"] = "procedural";
            j["scenes"][sceneName]["generator"] = scene.procedural.generator;
            // Note: params would need more complex serialization
        }
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

    // Global matrix
    config.globalMatrix.resolutions = {64, 128};
    config.globalMatrix.renderSizes = {{800, 600}};
    config.globalMatrix.scenes = {"cornell"};

    // Compute pipeline only
    PipelineMatrix compute;
    compute.enabled = true;
    compute.shaderGroups = {{"VoxelRayMarch.comp"}};
    config.pipelineMatrices["compute"] = compute;

    // Default scene definitions
    config.sceneDefinitions["cornell"] = SceneDefinition::FromFile("cornell", "assets/cornell.vox");

    config.warmupFramesOverride = 10;
    config.measurementFramesOverride = 100;
    config.GenerateTestsFromMatrix();

    return config;
}

BenchmarkSuiteConfig BenchmarkSuiteConfig::GetResearchConfig() {
    BenchmarkSuiteConfig config;
    config.suiteName = "Research Benchmark Suite";

    // Global matrix
    config.globalMatrix.resolutions = {64, 128, 256, 512};
    config.globalMatrix.renderSizes = {{1280, 720}, {1920, 1080}};
    config.globalMatrix.scenes = {"cornell", "noise", "tunnels", "cityscape"};

    // Compute pipeline - full test
    PipelineMatrix compute;
    compute.enabled = true;
    compute.shaderGroups = {{"VoxelRayMarch.comp"}, {"VoxelRayMarch_Compressed.comp"}};
    config.pipelineMatrices["compute"] = compute;

    // Fragment pipeline - limited
    PipelineMatrix fragment;
    fragment.enabled = true;
    fragment.shaderGroups = {{"Fullscreen.vert", "VoxelRayMarch.frag"}};
    config.pipelineMatrices["fragment"] = fragment;

    // Hardware RT - disabled by default
    PipelineMatrix hardware_rt;
    hardware_rt.enabled = false;
    hardware_rt.shaderGroups = {{"VoxelRayMarch_RT.rgen", "VoxelRayMarch_RT.rmiss", "VoxelRayMarch_RT.rchit"}};
    config.pipelineMatrices["hardware_rt"] = hardware_rt;

    // Scene definitions
    config.sceneDefinitions["cornell"] = SceneDefinition::FromFile("cornell", "assets/cornell.vox");
    config.sceneDefinitions["noise"] = SceneDefinition::FromProcedural("noise", "perlin3d");
    config.sceneDefinitions["tunnels"] = SceneDefinition::FromProcedural("tunnels", "voronoi_caves");
    config.sceneDefinitions["cityscape"] = SceneDefinition::FromProcedural("cityscape", "buildings");

    config.warmupFramesOverride = 100;
    config.measurementFramesOverride = 300;
    config.GenerateTestsFromMatrix();

    return config;
}

} // namespace Vixen::Profiler
