#include "Profiler/BenchmarkConfig.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <cstdint>
#include <iostream>

// Platform-specific headers for memory queries
#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#elif __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <vulkan/vulkan.h>

namespace Vixen::Profiler {

namespace {

/// Estimate GPU memory requirements for a test configuration
/// @param resolution Voxel grid resolution (e.g., 512)
/// @param screenWidth Screen width in pixels
/// @param screenHeight Screen height in pixels
/// @param isCompressed Whether shader uses DXT-compressed data
/// @return Estimated GPU memory in bytes
uint64_t EstimateGPUMemory(uint32_t resolution, uint32_t screenWidth, uint32_t screenHeight, bool isCompressed) {
    // Calculate octree structure sizes
    uint64_t voxelCount = static_cast<uint64_t>(resolution) * resolution * resolution;

    // Brick count: Each brick is 8x8x8 = 512 voxels
    // For a fully populated octree, bricks ≈ voxels / 512
    // Add 1.5x safety margin for sparse structure overhead (hierarchy nodes)
    uint64_t brickCount = (voxelCount / 512) * 3 / 2;

    // ChildDescriptor hierarchy: 8 bytes per node
    // Approximate hierarchy size: 1/7 of leaf nodes (octree property)
    uint64_t hierarchyMemory = (brickCount / 7) * 8;

    // Brick data memory depends on compression
    uint64_t brickDataMemory;
    if (isCompressed) {
        // DXT-compressed bricks:
        // - Color: 256 bytes/brick (32 DXT1 blocks × 8 bytes)
        // - Normal: 512 bytes/brick (32 DXT blocks × 16 bytes)
        // - Total: 768 bytes/brick
        brickDataMemory = brickCount * 768;
    } else {
        // Uncompressed bricks:
        // - Material data: 512 voxels × 4 bytes (uint32_t) = 2048 bytes/brick
        // - With safety margin: 3072 bytes/brick
        brickDataMemory = brickCount * 3072;
    }

    uint64_t voxelGridMemory = hierarchyMemory + brickDataMemory;

    // Swapchain images (assume 3 images, RGBA8)
    uint64_t swapchainMemory = static_cast<uint64_t>(screenWidth) * screenHeight * 4 * 3;

    // Additional buffers:
    // - Descriptor sets and uniforms: ~50 MB
    // - Shader counter buffers: ~1 MB
    // - Command buffers and misc: ~50 MB
    uint64_t additionalBuffers = 100 * 1024 * 1024;

    // Staging buffers for data upload (1.2x voxel grid size)
    uint64_t stagingMemory = voxelGridMemory * 12 / 10;

    return voxelGridMemory + swapchainMemory + additionalBuffers + stagingMemory;
}

/// Estimate host memory requirements for a test configuration
/// @param resolution Voxel grid resolution
/// @param screenWidth Screen width in pixels
/// @param screenHeight Screen height in pixels
/// @return Estimated host memory in bytes
uint64_t EstimateHostMemory(uint32_t resolution, uint32_t screenWidth, uint32_t screenHeight) {
    // Voxel grid construction on CPU before upload
    uint64_t voxelCount = static_cast<uint64_t>(resolution) * resolution * resolution;
    uint64_t voxelBuildMemory = voxelCount * 8; // More memory during construction

    // Frame capture buffers
    uint64_t frameCaptureMemory = static_cast<uint64_t>(screenWidth) * screenHeight * 4;

    // Metrics collection and storage
    uint64_t metricsMemory = 50 * 1024 * 1024; // ~50 MB

    return voxelBuildMemory + frameCaptureMemory + metricsMemory;
}

/// Get available GPU memory in bytes
/// @param gpuIndex GPU index to query (0 = first GPU)
/// @return Available GPU memory in bytes, or 0 if query fails
uint64_t GetAvailableGPUMemory(uint32_t gpuIndex) {
    // Create temporary Vulkan instance to query GPU memory
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VIXEN Memory Query";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        return 0;
    }

    // Enumerate physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0 || gpuIndex >= deviceCount) {
        vkDestroyInstance(instance, nullptr);
        return 0;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice physicalDevice = devices[gpuIndex];

    // Query memory properties
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    // Sum up device-local heap sizes
    uint64_t totalVRAM = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalVRAM += memProps.memoryHeaps[i].size;
        }
    }

    vkDestroyInstance(instance, nullptr);

    // Return 80% of total VRAM as available (conservative estimate)
    return totalVRAM * 8 / 10;
}

/// Get available host (system) memory in bytes
/// @return Available system memory in bytes, or 0 if query fails
uint64_t GetAvailableHostMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        // Return 70% of available physical memory (conservative)
        return memStatus.ullAvailPhys * 7 / 10;
    }
#elif __linux__
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        // Return 70% of free memory (conservative)
        return static_cast<uint64_t>(si.freeram) * si.mem_unit * 7 / 10;
    }
#elif __APPLE__
    uint64_t memSize = 0;
    size_t len = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &len, NULL, 0) == 0) {
        // Get free memory - on macOS we use a conservative 50%
        return memSize / 2;
    }
#endif
    return 0; // Unknown platform or query failed
}

/// Check if a test configuration would exceed available memory
/// @param resolution Voxel grid resolution
/// @param screenWidth Screen width
/// @param screenHeight Screen height
/// @param shaderName Shader file name (to detect compression)
/// @param gpuIndex GPU index
/// @param verbose Print memory info if true
/// @return true if test should be skipped
bool ShouldSkipTestForMemory(uint32_t resolution, uint32_t screenWidth, uint32_t screenHeight,
                              const std::string& shaderName, uint32_t gpuIndex, bool verbose) {
    // Detect if shader uses DXT compression
    bool isCompressed = (shaderName.find("_Compressed") != std::string::npos ||
                         shaderName.find("Compressed") != std::string::npos);

    uint64_t estimatedGPU = EstimateGPUMemory(resolution, screenWidth, screenHeight, isCompressed);
    uint64_t estimatedHost = EstimateHostMemory(resolution, screenWidth, screenHeight);

    uint64_t availableGPU = GetAvailableGPUMemory(gpuIndex);
    uint64_t availableHost = GetAvailableHostMemory();

    // Convert to GB for logging
    double estimatedGPU_GB = estimatedGPU / (1024.0 * 1024.0 * 1024.0);
    double estimatedHost_GB = estimatedHost / (1024.0 * 1024.0 * 1024.0);
    double availableGPU_GB = availableGPU / (1024.0 * 1024.0 * 1024.0);
    double availableHost_GB = availableHost / (1024.0 * 1024.0 * 1024.0);

    bool exceedsGPU = (availableGPU > 0) && (estimatedGPU > availableGPU);
    bool exceedsHost = (availableHost > 0) && (estimatedHost > availableHost);

    if (exceedsGPU || exceedsHost) {
        if (verbose) {
            std::cout << "  [SKIP] Resolution " << resolution << "^3 @ "
                      << screenWidth << "x" << screenHeight
                      << " [" << (isCompressed ? "compressed" : "uncompressed") << "] - ";
            if (exceedsGPU) {
                std::cout << "GPU memory exceeded (need " << estimatedGPU_GB
                          << " GB, have " << availableGPU_GB << " GB)";
            }
            if (exceedsHost) {
                if (exceedsGPU) std::cout << ", ";
                std::cout << "Host memory exceeded (need " << estimatedHost_GB
                          << " GB, have " << availableHost_GB << " GB)";
            }
            std::cout << std::endl;
        }
        return true;
    }

    return false;
}

} // anonymous namespace

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

    // Fragment pipeline - both uncompressed and compressed variants
    PipelineMatrix fragment;
    fragment.enabled = true;
    fragment.shaderGroups = {
        {"Fullscreen.vert", "VoxelRayMarch.frag"},
        {"Fullscreen.vert", "VoxelRayMarch_Compressed.frag"}
    };
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
    uint32_t skippedCount = 0;

    if (verbose) {
        std::cout << "\n[Benchmark] Generating test matrix with memory checks..." << std::endl;
    }

    // Generate tests from matrix configuration
    for (const auto& [pipelineName, pipelineMatrix] : pipelineMatrices) {
        if (!pipelineMatrix.enabled) {
            continue;
        }

        for (uint32_t resolution : globalMatrix.resolutions) {
            for (const auto& renderSize : globalMatrix.renderSizes) {
                for (const auto& sceneName : globalMatrix.scenes) {
                    for (const auto& shaderGroup : pipelineMatrix.shaderGroups) {
                        // Get shader name for memory estimation
                        std::string shaderName = shaderGroup.empty() ? "" : shaderGroup.back();

                        // Check if this test would exceed available memory
                        if (ShouldSkipTestForMemory(resolution, renderSize.width, renderSize.height,
                                                    shaderName, gpuIndex, verbose)) {
                            skippedCount++;
                            continue;
                        }

                        TestConfiguration test;
                        test.pipeline = pipelineName;
                        test.voxelResolution = resolution;
                        test.screenWidth = renderSize.width;
                        test.screenHeight = renderSize.height;
                        test.sceneType = sceneName;
                        test.shaderGroup = shaderGroup;
                        test.shader = shaderName;
                        test.testId = test.GenerateTestId(runNumber++);
                        tests.push_back(test);
                    }
                }
            }
        }
    }

    if (verbose && skippedCount > 0) {
        std::cout << "[Benchmark] Skipped " << skippedCount
                  << " test(s) due to insufficient memory" << std::endl;
    }
    if (verbose) {
        std::cout << "[Benchmark] Generated " << tests.size() << " test(s)" << std::endl;
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

    // Fragment pipeline - both uncompressed and compressed variants
    PipelineMatrix fragment;
    fragment.enabled = true;
    fragment.shaderGroups = {
        {"Fullscreen.vert", "VoxelRayMarch.frag"},
        {"Fullscreen.vert", "VoxelRayMarch_Compressed.frag"}
    };
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
