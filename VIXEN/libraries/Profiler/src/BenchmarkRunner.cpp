#include "Profiler/BenchmarkRunner.h"
#include "Profiler/BenchmarkConfig.h"
#include "Profiler/BenchmarkGraphFactory.h"
#include "Profiler/ProfilerSystem.h"
#include "Profiler/VulkanIntegration.h"
#include "Profiler/TesterPackage.h"
#include "Profiler/MetricsSanityChecker.h"
#include "Profiler/NVMLWrapper.h"
#include <iomanip>
#include <cmath>
#include <chrono>
#include <random>
#include <sstream>
#include <Core/RenderGraph.h>
#include <Core/NodeTypeRegistry.h>
#include <MessageBus.h>
#include <Message.h>
#include <MainCacher.h>
#include <Logger.h>
#include "VulkanDevice.h"

// Node type registrations for graph building
#include <Nodes/InstanceNode.h>
#include <Nodes/WindowNode.h>
#include <Nodes/DeviceNode.h>
#include <Nodes/SwapChainNode.h>
#include <Nodes/CommandPoolNode.h>
#include <Nodes/FrameSyncNode.h>
#include <Nodes/ShaderLibraryNode.h>
#include <Nodes/DescriptorResourceGathererNode.h>
#include <Nodes/PushConstantGathererNode.h>
#include <Nodes/DescriptorSetNode.h>
#include <Nodes/ComputePipelineNode.h>
#include <Nodes/ComputeDispatchNode.h>
#include <Nodes/CameraNode.h>
#include <Nodes/VoxelGridNode.h>
#include <Nodes/InputNode.h>
#include <Nodes/PresentNode.h>
#include <Nodes/DebugBufferReaderNode.h>
#include <InputEvents.h>  // For KeyCode::C (frame capture trigger)
#include <Nodes/ConstantNode.h>
#include <Nodes/ConstantNodeType.h>

// Fragment/graphics pipeline nodes
#include <Nodes/RenderPassNode.h>
#include <Nodes/FramebufferNode.h>
#include <Nodes/GraphicsPipelineNode.h>
#include <Nodes/GeometryRenderNode.h>

// Hardware ray tracing nodes (Phase K)
#include <Nodes/VoxelAABBConverterNode.h>
#include <Nodes/AccelerationStructureNode.h>
#include <Nodes/RayTracingPipelineNode.h>
#include <Nodes/TraceRaysNode.h>

// GPU performance logging
#include <Core/GPUPerformanceLogger.h>

#include <vulkan/vulkan.h>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Vixen::Profiler {

//==============================================================================
// Internal Vulkan Context for Headless Mode
//==============================================================================

namespace {

struct HeadlessVulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    bool rtxEnabled = false;  // Phase K: Hardware RT support
    std::unique_ptr<Vixen::Vulkan::Resources::VulkanDevice> vulkanDevice;  // Capability graph access

    bool IsValid() const {
        return instance != VK_NULL_HANDLE &&
               physicalDevice != VK_NULL_HANDLE &&
               device != VK_NULL_HANDLE &&
               computeQueue != VK_NULL_HANDLE;
    }
};

// Debug callback for validation layers
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    (void)type;
    (void)pUserData;

    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("[Vulkan] " + std::string(pCallbackData->pMessage));
    }
    return VK_FALSE;
}

bool IsLayerAvailable(const char* layerName) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, layerName) == 0) {
            return true;
        }
    }
    return false;
}

VkResult CreateHeadlessInstance(HeadlessVulkanContext& ctx, bool enableValidation) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VIXEN Benchmark";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VIXEN";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions;
    std::vector<const char*> layers;

    if (enableValidation && IsLayerAvailable("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &ctx.instance);
    if (result != VK_SUCCESS) {
        return result;
    }

    if (enableValidation && !layers.empty()) {
        auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT")
        );
        if (createDebugMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
            debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = VulkanDebugCallback;

            createDebugMessenger(ctx.instance, &debugInfo, nullptr, &ctx.debugMessenger);
        }
    }

    return VK_SUCCESS;
}

VkResult SelectPhysicalDevice(HeadlessVulkanContext& ctx, uint32_t gpuIndex, bool verbose) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("No Vulkan-capable GPUs found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());

    if (gpuIndex >= deviceCount) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("GPU index " + std::to_string(gpuIndex) + " out of range (0-" + std::to_string(deviceCount - 1) + ")");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx.physicalDevice = devices[gpuIndex];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    ctx.timestampPeriod = props.limits.timestampPeriod;

    if (verbose) {
        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("Selected GPU: " + std::string(props.deviceName));
    }

    return VK_SUCCESS;
}

uint32_t FindComputeQueueFamily(VkPhysicalDevice device) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            return i;
        }
    }

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }

    return UINT32_MAX;
}

// Helper to check if an extension is available
bool HasDeviceExtension(const std::vector<VkExtensionProperties>& available, const char* name) {
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

VkResult CreateHeadlessDevice(HeadlessVulkanContext& ctx) {
    ctx.computeQueueFamily = FindComputeQueueFamily(ctx.physicalDevice);
    if (ctx.computeQueueFamily == UINT32_MAX) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("No compute-capable queue family found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = ctx.computeQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(ctx.physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(ctx.physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    // Enable memory budget if available
    if (HasDeviceExtension(availableExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
        deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }

    // Enable RTX extensions if available (Phase K - Hardware RT)
    bool rtxAvailable =
        HasDeviceExtension(availableExtensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        HasDeviceExtension(availableExtensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
        HasDeviceExtension(availableExtensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        HasDeviceExtension(availableExtensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // Feature chain for RTX
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
    accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeatures.pNext = &bufferDeviceAddressFeatures;
    accelStructFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtPipelineFeatures.pNext = &accelStructFeatures;
    rtPipelineFeatures.rayTracingPipeline = VK_TRUE;

    if (rtxAvailable) {
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

        // Also need SPIRV 1.4 for RT shaders
        if (HasDeviceExtension(availableExtensions, VK_KHR_SPIRV_1_4_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        }
        if (HasDeviceExtension(availableExtensions, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        }

        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("RTX extensions enabled for hardware ray tracing");
        ctx.rtxEnabled = true;
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features = deviceFeatures;
    if (rtxAvailable) {
        deviceFeatures2.pNext = &rtPipelineFeatures;
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (rtxAvailable) {
        createInfo.pNext = &deviceFeatures2;
        createInfo.pEnabledFeatures = nullptr;  // Must be null when using pNext features
    } else {
        createInfo.pEnabledFeatures = &deviceFeatures;
    }

    VkResult result = vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device);
    if (result != VK_SUCCESS) {
        return result;
    }

    vkGetDeviceQueue(ctx.device, ctx.computeQueueFamily, 0, &ctx.computeQueue);

    // Initialize VulkanDevice wrapper for capability graph access
    ctx.vulkanDevice = std::make_unique<Vixen::Vulkan::Resources::VulkanDevice>(&ctx.physicalDevice);
    // Note: VulkanDevice will build its capability graph during construction

    return VK_SUCCESS;
}

VkResult CreateCommandPool(HeadlessVulkanContext& ctx) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    return vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool);
}

VkResult CreateTimestampQueryPool(HeadlessVulkanContext& ctx, uint32_t queryCount = 128) {
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = queryCount;

    return vkCreateQueryPool(ctx.device, &queryPoolInfo, nullptr, &ctx.timestampQueryPool);
}

bool InitializeHeadlessVulkan(HeadlessVulkanContext& ctx, const BenchmarkSuiteConfig& config) {
    if (CreateHeadlessInstance(ctx, config.enableValidation) != VK_SUCCESS) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to create Vulkan instance");
        return false;
    }

    if (SelectPhysicalDevice(ctx, config.gpuIndex, config.verbose) != VK_SUCCESS) {
        return false;
    }

    if (CreateHeadlessDevice(ctx) != VK_SUCCESS) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to create logical device");
        return false;
    }

    if (CreateCommandPool(ctx) != VK_SUCCESS) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to create command pool");
        return false;
    }

    if (CreateTimestampQueryPool(ctx) != VK_SUCCESS) {
        // Non-fatal, continue without GPU timing
    }

    return true;
}

void CleanupHeadlessVulkan(HeadlessVulkanContext& ctx) {
    if (ctx.timestampQueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(ctx.device, ctx.timestampQueryPool, nullptr);
    }
    if (ctx.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
    }
    if (ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx.device, nullptr);
    }
    if (ctx.debugMessenger != VK_NULL_HANDLE) {
        auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT")
        );
        if (destroyDebugMessenger) {
            destroyDebugMessenger(ctx.instance, ctx.debugMessenger, nullptr);
        }
    }
    if (ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx.instance, nullptr);
    }
}

void RegisterAllNodeTypes(Vixen::RenderGraph::NodeTypeRegistry& registry) {
    using namespace Vixen::RenderGraph;

    // Infrastructure nodes
    registry.Register<InstanceNodeType>();
    registry.Register<WindowNodeType>();
    registry.Register<DeviceNodeType>();
    registry.Register<CommandPoolNodeType>();
    registry.Register<FrameSyncNodeType>();
    registry.Register<SwapChainNodeType>();

    // Shader/descriptor nodes (shared by compute and fragment)
    registry.Register<ShaderLibraryNodeType>();
    registry.Register<DescriptorResourceGathererNodeType>();
    registry.Register<PushConstantGathererNodeType>();
    registry.Register<DescriptorSetNodeType>();

    // Compute pipeline nodes
    registry.Register<ComputePipelineNodeType>();
    registry.Register<ComputeDispatchNodeType>();

    // Fragment/graphics pipeline nodes
    registry.Register<RenderPassNodeType>();
    registry.Register<FramebufferNodeType>();
    registry.Register<GraphicsPipelineNodeType>();
    registry.Register<GeometryRenderNodeType>();

    // RTX nodes
    registry.Register<VoxelAABBConverterNodeType>();
    registry.Register<AccelerationStructureNodeType>();
    registry.Register<RayTracingPipelineNodeType>();
    registry.Register<TraceRaysNodeType>();

    // Scene/utility nodes
    registry.Register<CameraNodeType>();
    registry.Register<VoxelGridNodeType>();
    registry.Register<InputNodeType>();
    registry.Register<PresentNodeType>();
    registry.Register<DebugBufferReaderNodeType>();
    registry.Register<ConstantNodeType>();
}

} // anonymous namespace

BenchmarkRunner::BenchmarkRunner() = default;
BenchmarkRunner::~BenchmarkRunner() = default;

//==============================================================================
// High-Level API: RunSuite
//==============================================================================

TestSuiteResults BenchmarkRunner::RunSuite(const BenchmarkSuiteConfig& config) {
    using namespace Vixen::RenderGraph;

    // Validate configuration
    auto errors = config.Validate();
    if (!errors.empty()) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Configuration errors:");
        for (const auto& err : errors) {
            // LOG_ERROR("  - " + err);
        }
        return TestSuiteResults{};
    }

    // Setup internal state from config
    SetOutputDirectory(config.outputDir);
    SetTestMatrix(config.tests);
    // Use first test's render dimensions or default
    if (!config.tests.empty()) {
        SetRenderDimensions(config.tests[0].screenWidth, config.tests[0].screenHeight);
    } else if (!config.globalMatrix.renderSizes.empty()) {
        SetRenderDimensions(config.globalMatrix.renderSizes[0].width, 
                           config.globalMatrix.renderSizes[0].height);
    } else {
        SetRenderDimensions(1280, 720);  // Default
    }

    // Create output directory
    if (!MetricsExporter::EnsureDirectoryExists(config.outputDir)) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to create output directory: " + config.outputDir.string());
        return TestSuiteResults{};
    }

    if (config.verbose) {
        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("");
        // LOG_INFO("=================================================");
        // LOG_INFO("VIXEN Benchmark Tool");
        // LOG_INFO("=================================================");
        // LOG_INFO("Mode:   " + std::string(config.headless ? "Headless" : "Render"));
        // LOG_INFO("Tests:  " + std::to_string(config.tests.size()) + " configurations");
        // LOG_INFO("Output: " + config.outputDir.string());
        // LOG_INFO("");
    }

    // Initialize NVML for GPU utilization monitoring (optional - gracefully fails on AMD)
    auto& nvml = NVMLWrapper::Instance();
    if (nvml.Initialize()) {
        if (config.verbose) {
            std::cout << "[NVML] GPU monitoring enabled: " << nvml.GetDeviceName(0) << std::endl;
        }
    }

    TestSuiteResults results;

    // Multi-GPU iteration: Run entire benchmark suite once per GPU
    if (config.runOnAllGPUs) {
        auto gpuList = EnumerateAvailableGPUs();

        if (gpuList.empty()) {
            std::cerr << "Error: No GPUs found for multi-GPU benchmarking\n";
            return TestSuiteResults{};
        }

        // Generate session UUID (shared across all GPUs in this run)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        uint32_t sessionUuidInt = dis(gen);
        std::ostringstream sessionUuidStream;
        sessionUuidStream << std::hex << std::setfill('0') << std::setw(8) << sessionUuidInt;
        std::string sessionUuid = sessionUuidStream.str();

        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "Multi-GPU Benchmarking Mode\n";
        std::cout << "=================================================\n";
        std::cout << "Running entire benchmark suite on " << gpuList.size() << " GPU(s)\n";
        std::cout << "Each GPU will execute all " << config.tests.size() << " test(s)\n";
        std::cout << "Each GPU will have its own output folder and ZIP package\n";
        std::cout << "Session UUID: " << sessionUuid << " (shared across all GPUs)\n";
        std::cout << "\n";

        uint32_t totalTests = 0;
        uint32_t totalPassed = 0;

        // Run suite on each GPU with separate output directories
        for (size_t i = 0; i < gpuList.size(); ++i) {
            const auto& gpu = gpuList[i];

            std::cout << "\n";
            std::cout << "--------------------------------------------------\n";
            std::cout << "GPU " << gpu.index << ": " << gpu.name << " (" << (i + 1) << " of " << gpuList.size() << ")\n";
            std::cout << "--------------------------------------------------\n";

            // Create unique benchmark folder name with shared session UUID: YYYYMMDD_HHMMSS_GPUName_UUID
            std::string benchmarkFolderName = GenerateBenchmarkFolderName(gpu.name, sessionUuid);
            std::filesystem::path gpuOutputDir = config.outputDir / benchmarkFolderName;

            // Create modified config for this GPU
            BenchmarkSuiteConfig gpuConfig = config;
            gpuConfig.gpuIndex = gpu.index;
            gpuConfig.runOnAllGPUs = false;  // Prevent recursive iteration
            gpuConfig.outputDir = gpuOutputDir;  // Separate output folder per GPU

            std::cout << "Output directory: " << gpuOutputDir.string() << "\n";

            // Run suite for this GPU
            TestSuiteResults gpuResults;
            if (gpuConfig.headless) {
                gpuResults = RunSuiteHeadless(gpuConfig);
            } else {
                gpuResults = RunSuiteWithWindow(gpuConfig);
            }

            // Export results for this GPU
            ExportAllResults();

            // Create ZIP package for this GPU if requested
            if (gpuConfig.createPackage) {
                TesterPackage packager;
                if (!gpuConfig.testerName.empty()) {
                    packager.SetTesterName(gpuConfig.testerName);
                }

                auto packageResult = packager.CreatePackage(
                    gpuConfig.outputDir,
                    gpuConfig.outputDir,
                    deviceCapabilities_
                );

                if (packageResult.success) {
                    std::cout << "  Package: " << packageResult.packagePath.filename().string()
                              << " (" << (packageResult.compressedSizeBytes / 1024) << " KB)\n";
                } else {
                    std::cerr << "  Warning: Failed to create package: " << packageResult.errorMessage << "\n";
                }
            }

            // Track aggregate stats for summary
            totalTests += gpuResults.GetTotalCount();
            totalPassed += gpuResults.GetPassCount();

            std::cout << "GPU " << gpu.index << " (" << gpu.name << ") completed: "
                      << gpuResults.GetPassCount() << "/" << gpuResults.GetTotalCount() << " tests passed\n";
        }

        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "Multi-GPU Benchmarking Complete\n";
        std::cout << "=================================================\n";
        std::cout << "Total tests across all GPUs: " << totalTests << "\n";
        std::cout << "Passed: " << totalPassed << "\n";
        std::cout << "Output format: " << config.outputDir.string() << "/YYYYMMDD_HHMMSS_GPUName_UUID/\n";
        std::cout << "\n";

        // Return empty results (each GPU exported independently)
        return TestSuiteResults{};
    }

    // Single GPU mode - continue with normal flow
    if (config.headless) {
        results = RunSuiteHeadless(config);
    } else {
        results = RunSuiteWithWindow(config);
    }

    // Export final results
    ExportAllResults();

    // Create ZIP package for tester sharing if requested
    if (config.createPackage) {
        TesterPackage packager;
        if (!config.testerName.empty()) {
            packager.SetTesterName(config.testerName);
        }

        // Package to same directory as results
        auto packageResult = packager.CreatePackage(
            config.outputDir,
            config.outputDir,
            deviceCapabilities_
        );

        if (packageResult.success) {
            std::cout << "\n";
            std::cout << "\n";
            std::cout << "##################################################\n";
            std::cout << "##                                              ##\n";
            std::cout << "##            BENCHMARK COMPLETE!               ##\n";
            std::cout << "##                                              ##\n";
            std::cout << "##################################################\n";
            std::cout << "\n";
            std::cout << "  Package: " << packageResult.packagePath.filename().string() << "\n";
            std::cout << "  Size:    " << (packageResult.compressedSizeBytes / 1024) << " KB";
            if (packageResult.originalSizeBytes > 0) {
                float ratio = 100.0f * (1.0f - float(packageResult.compressedSizeBytes) / float(packageResult.originalSizeBytes));
                std::cout << " (" << std::fixed << std::setprecision(0) << ratio << "% smaller)";
            }
            std::cout << "\n";
            std::cout << "  Files:   " << packageResult.filesIncluded << " included\n";
            std::cout << "\n";
            std::cout << "--------------------------------------------------\n";
            std::cout << "  NEXT STEPS:\n";
            std::cout << "--------------------------------------------------\n";
            std::cout << "  1. Find the ZIP file at:\n";
            std::cout << "     " << packageResult.packagePath.string() << "\n";
            std::cout << "\n";
            std::cout << "  2. Send this ZIP to the benchmark coordinator\n";
            std::cout << "\n";
            std::cout << "##################################################\n";
            std::cout << "\n";
        } else {
            std::cerr << "Warning: Failed to create package: " << packageResult.errorMessage << "\n";
        }
    }

    if (config.verbose) {
        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("");
        // LOG_INFO("=================================================");
        // LOG_INFO("Benchmark Complete");
        // LOG_INFO("=================================================");
        // LOG_INFO("  Passed: " + std::to_string(results.GetPassCount()) + "/" + std::to_string(results.GetTotalCount()));
        // LOG_INFO("  Output: " + config.outputDir.string());
        // LOG_INFO("=================================================");
    }

    return results;
}

TestSuiteResults BenchmarkRunner::RunSuiteHeadless(const BenchmarkSuiteConfig& config) {
    // Initialize headless Vulkan context
    HeadlessVulkanContext ctx{};
    if (!InitializeHeadlessVulkan(ctx, config)) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to initialize Vulkan");
        return TestSuiteResults{};
    }

    // Capture device capabilities
    auto deviceCaps = DeviceCapabilities::Capture(ctx.physicalDevice);
    SetDeviceCapabilities(deviceCaps);

    if (config.verbose) {
        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("Device: " + deviceCaps.GetSummaryString());
    }

    // Initialize profiler system
    ProfilerSystem::Instance().Initialize(ctx.device, ctx.physicalDevice, 3);
    ProfilerSystem::Instance().CaptureDeviceCapabilities(ctx.physicalDevice);
    ProfilerSystem::Instance().SetOutputDirectory(config.outputDir);
    ProfilerSystem::Instance().SetExportFormats(config.exportCSV, config.exportJSON);

    // Set progress callback if verbose - show progress bar with test params
    if (config.verbose) {
        SetProgressCallback([this](uint32_t testIdx, uint32_t totalTests,
                                uint32_t frame, uint32_t totalFrames) {
            const auto& cfg = GetCurrentTestConfig();

            // Build progress bar (20 chars wide)
            int progress = (frame * 20) / totalFrames;
            std::string bar(progress, '=');
            std::string empty(20 - progress, ' ');

            // Format: [=====>              ] 25% | Test 1/4 | 64^3 cornell | shader.comp
            int percent = (frame * 100) / totalFrames;
            // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
            // LOG_INFO("\r  [" + bar + ">" + empty + "] " +
            //          std::to_string(percent) + "% | " +
            //          "Test " + std::to_string(testIdx + 1) + "/" + std::to_string(totalTests) + " | " +
            //          std::to_string(cfg.voxelResolution) + "^3 " + cfg.sceneType + " | " + cfg.shader);
        });
    }

    // Start suite
    if (!StartSuite()) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to start benchmark suite");
        CleanupHeadlessVulkan(ctx);
        return TestSuiteResults{};
    }

    // Run each test
    while (BeginNextTest()) {
        const auto& testConfig = GetCurrentTestConfig();

        // Check GPU capabilities before running test
        if (ctx.vulkanDevice && !testConfig.CanRunOnDevice(ctx.vulkanDevice.get())) {
            // GPU doesn't support required capabilities - skip this test
            std::cout << "Test " << (GetCurrentTestIndex() + 1) << "/" << testMatrix_.size()
                      << " - " << testConfig.pipeline << "... SKIPPED" << std::endl;
            if (config.verbose) {
                std::cout << "  [Capability Check] Test requires capabilities not available on this GPU:" << std::endl;
                for (const auto& cap : testConfig.requiredCapabilities) {
                    if (!ctx.vulkanDevice->HasCapability(cap)) {
                        std::cout << "    ✗ " << cap << " (not available)" << std::endl;
                    }
                }
            }

            // Skip to next test without running this one
            FinalizeCurrentTest();  // Mark as complete (will show in results as skipped)
            continue;  // Move to next test
        }

        // Always show minimal progress (Test X/Y) so testers know it's running
        std::cout << "Test " << (GetCurrentTestIndex() + 1) << "/" << testMatrix_.size()
                  << " - " << testConfig.pipeline << "..." << std::flush;

        if (config.verbose) {
            // Verbose mode: show full details on new line
            std::cout << "\n  " << testConfig.sceneType << " | "
                      << testConfig.voxelResolution << "^3 | " << testConfig.shader << std::endl;
        }

        ProfilerSystem::Instance().StartTestRun(testConfig);

        // Run frames (synthetic metrics in headless mode)
        uint32_t totalFrames = testConfig.warmupFrames + testConfig.measurementFrames;
        for (uint32_t frame = 0; frame < totalFrames; ++frame) {
            FrameMetrics metrics{};
            metrics.frameNumber = frame;
            metrics.timestampMs = static_cast<double>(frame) * 16.67;
            metrics.frameTimeMs = 16.67f;
            metrics.gpuTimeMs = 8.0f + (static_cast<float>(testConfig.voxelResolution) / 256.0f) * 8.0f;
            metrics.sceneResolution = testConfig.voxelResolution;
            metrics.screenWidth = testConfig.screenWidth;
            metrics.screenHeight = testConfig.screenHeight;
            metrics.sceneDensity = 0.0f;  // Will be computed from actual scene data
            metrics.totalRaysCast = testConfig.screenWidth * testConfig.screenHeight;
            metrics.mRaysPerSec = static_cast<float>(metrics.totalRaysCast) / (metrics.gpuTimeMs * 1000.0f);

            // Estimate avgVoxelsPerRay based on octree depth
            // For ESVO traversal: ~log2(resolution) * 3 base iterations + density factor
            // Typical values: 64->18, 128->21, 256->24 voxels/ray for ~25% density
            if (testConfig.voxelResolution > 0) {
                float octreeDepth = std::log2(static_cast<float>(testConfig.voxelResolution));
                metrics.avgVoxelsPerRay = octreeDepth * 3.0f;
            } else if (metrics.sceneResolution > 0) {
                float octreeDepth = std::log2(static_cast<float>(metrics.sceneResolution));
                metrics.avgVoxelsPerRay = octreeDepth * 3.0f;
            }
            metrics.fps = 1000.0f / metrics.frameTimeMs;
            metrics.bandwidthEstimated = true;

            RecordFrame(metrics);
        }

        FinalizeCurrentTest();
        ProfilerSystem::Instance().EndTestRun(true);

        // Complete progress line (only newline if not verbose, since verbose already printed newline)
        if (!config.verbose) {
            std::cout << " Done" << std::endl;
        }
    }

    // Cleanup
    ProfilerSystem::Instance().Shutdown();
    CleanupHeadlessVulkan(ctx);

    return suiteResults_;
}

TestSuiteResults BenchmarkRunner::RunSuiteWithWindow(const BenchmarkSuiteConfig& config) {
    namespace RG = Vixen::RenderGraph;
    using namespace Vixen::EventBus;

    // Create node registry and register all node types
    auto nodeRegistry = std::make_unique<RG::NodeTypeRegistry>();
    RegisterAllNodeTypes(*nodeRegistry);

    // Create message bus for event coordination
    auto messageBus = std::make_unique<Vixen::EventBus::MessageBus>();

    // Get global MainCacher instance for cache persistence
    auto& mainCacher = CashSystem::MainCacher::Instance();
    mainCacher.Initialize(messageBus.get());

    // Create logger for RenderGraph (kept as unique_ptr for lifetime management)
    // Use global ::Logger (exposed via 'using Vixen::Log::Logger;' in Logger.h)
    auto graphLogger = std::make_unique<::Logger>("BenchmarkGraph", false);
    graphLogger->SetTerminalOutput(false);  // Set to true for debugging

    // Subscribe to window close events
    bool shouldClose = false;
    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [&shouldClose](const BaseEventMessage& /*msg*/) -> bool {
            shouldClose = true;
            return true;
        }
    );

    // Set graph factory function - dispatch based on pipeline type in config
    SetGraphFactory([](RG::RenderGraph* graph, const TestConfiguration& testConfig,
                       uint32_t width, uint32_t height) {
        return BenchmarkGraphFactory::BuildFromConfig(graph, testConfig);
    });

    // Set progress callback if verbose - show progress bar with test params
    if (config.verbose) {
        SetProgressCallback([this](uint32_t testIdx, uint32_t totalTests,
                                uint32_t frame, uint32_t totalFrames) {
            const auto& cfg = GetCurrentTestConfig();

            // Build progress bar (20 chars wide)
            int progress = (frame * 20) / totalFrames;
            std::string bar(progress, '=');
            std::string empty(20 - progress, ' ');

            // Format: [=====>              ] 25% | Test 1/4 | 64^3 cornell | shader.comp
            int percent = (frame * 100) / totalFrames;
            // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
            // LOG_INFO("\r  [" + bar + ">" + empty + "] " +
            //          std::to_string(percent) + "% | " +
            //          "Test " + std::to_string(testIdx + 1) + "/" + std::to_string(totalTests) + " | " +
            //          std::to_string(cfg.voxelResolution) + "^3 " + cfg.sceneType + " | " + cfg.shader);
        });
    }

    // Start suite
    if (!StartSuite()) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to start benchmark suite");
        return TestSuiteResults{};
    }

    // Run each test
    while (BeginNextTest() && !shouldClose) {
        const auto& testConfig = GetCurrentTestConfig();

        // Create render graph for this test iteration
        auto renderGraph = std::make_unique<RG::RenderGraph>(
            nodeRegistry.get(),
            messageBus.get(),
            graphLogger.get(),
            &mainCacher
        );

        // Always show minimal progress (Test X/Y) so testers know it's running
        std::cout << "Test " << (GetCurrentTestIndex() + 1) << "/" << testMatrix_.size()
                  << " - " << testConfig.pipeline << "..." << std::flush;

        if (config.verbose) {
            // Verbose mode: show full details on new line
            std::cout << "\n  " << testConfig.sceneType << " | "
                      << testConfig.voxelResolution << "^3 | " << testConfig.shader << std::endl;
        }

        // Create graph for current test
        auto benchGraph = CreateGraphForCurrentTest(renderGraph.get());
        if (!benchGraph.IsValid()) {
            // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
            // LOG_ERROR("Failed to create graph for test");
            continue;
        }

        // Compile the graph
        renderGraph->Compile();

        // Check GPU capabilities before running test
        auto* capCheckDeviceNode = dynamic_cast<RG::DeviceNode*>(
            renderGraph->GetInstanceByName("benchmark_device"));
        if (capCheckDeviceNode) {
            auto* vulkanDevice = capCheckDeviceNode->GetVulkanDevice();
            if (vulkanDevice && !testConfig.CanRunOnDevice(vulkanDevice)) {
                // GPU doesn't support required capabilities - skip this test
                std::cout << " SKIPPED" << std::endl;
                if (config.verbose) {
                    std::cout << "  [Capability Check] Test requires capabilities not available on this GPU:" << std::endl;
                    for (const auto& cap : testConfig.requiredCapabilities) {
                        if (!vulkanDevice->HasCapability(cap)) {
                            std::cout << "    ✗ " << cap << " (not available)" << std::endl;
                        }
                    }
                }

                // Skip to next test without running this one
                FinalizeCurrentTest();  // Mark as complete (will show in results as skipped)
                renderGraph.reset();    // Clean up graph
                continue;  // Move to next test
            }
        }

        // Capture BLAS/TLAS build timing for hardware_rt pipeline
        if (testConfig.pipeline == "hardware_rt") {
            auto* asNode = dynamic_cast<RG::AccelerationStructureNode*>(
                renderGraph->GetInstanceByName("benchmark_accel_structure"));
            if (asNode) {
                const auto& asData = asNode->GetAccelData();
                currentBlasBuildTimeMs_ = asData.blasBuildTimeMs;
                currentTlasBuildTimeMs_ = asData.tlasBuildTimeMs;
                if (config.verbose) {
                    std::cout << "  [AS Build] BLAS: " << std::fixed << std::setprecision(2)
                              << currentBlasBuildTimeMs_ << "ms, TLAS: "
                              << currentTlasBuildTimeMs_ << "ms" << std::endl;
                }
            }
        }

        // Initialize profiler from graph
        VulkanHandles vulkanHandles;
        bool profilerInitialized = VulkanIntegrationHelper::InitializeProfilerFromGraph(renderGraph.get());

        if (profilerInitialized) {
            vulkanHandles = VulkanIntegrationHelper::ExtractFromGraph(renderGraph.get());
            if (vulkanHandles.IsValid()) {
                auto deviceCaps = DeviceCapabilities::Capture(vulkanHandles.physicalDevice);
                SetDeviceCapabilities(deviceCaps);
                if (config.verbose) {
                    // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
                    // LOG_INFO("[BenchmarkRunner] Device: " + std::string(deviceCaps.deviceName));
                }
            }
        }

        // Fallback: capture device info via DeviceNode if profiler init failed
        if (!vulkanHandles.IsValid()) {
            auto* deviceNode = dynamic_cast<RG::DeviceNode*>(
                renderGraph->GetInstanceByName("benchmark_device"));
            if (deviceNode) {
                auto* vulkanDevice = deviceNode->GetVulkanDevice();
                if (vulkanDevice && vulkanDevice->device != VK_NULL_HANDLE) {
                    vulkanHandles.device = vulkanDevice->device;
                    vulkanHandles.physicalDevice = vulkanDevice->gpu ? *vulkanDevice->gpu : VK_NULL_HANDLE;
                    vulkanHandles.graphicsQueue = vulkanDevice->queue;
                    vulkanHandles.graphicsQueueFamily = vulkanDevice->graphicsQueueIndex;

                    // Use VulkanDevice's cached properties instead of Vulkan API calls
                    DeviceCapabilities deviceCaps;
                    deviceCaps.deviceName = vulkanDevice->gpuProperties.deviceName;
                    deviceCaps.vendorID = vulkanDevice->gpuProperties.vendorID;
                    deviceCaps.deviceID = vulkanDevice->gpuProperties.deviceID;
                    deviceCaps.deviceType = vulkanDevice->gpuProperties.deviceType;
                    deviceCaps.driverVersion = DeviceCapabilities::FormatDriverVersion(
                        vulkanDevice->gpuProperties.driverVersion,
                        vulkanDevice->gpuProperties.vendorID);
                    deviceCaps.timestampPeriod = vulkanDevice->gpuProperties.limits.timestampPeriod;
                    deviceCaps.timestampSupported = vulkanDevice->gpuProperties.limits.timestampComputeAndGraphics;

                    // Vulkan API version
                    uint32_t apiVersion = vulkanDevice->gpuProperties.apiVersion;
                    deviceCaps.vulkanVersion = std::to_string(VK_VERSION_MAJOR(apiVersion)) + "." +
                                               std::to_string(VK_VERSION_MINOR(apiVersion)) + "." +
                                               std::to_string(VK_VERSION_PATCH(apiVersion));

                    // Get VRAM from memory properties
                    for (uint32_t i = 0; i < vulkanDevice->gpuMemoryProperties.memoryHeapCount; ++i) {
                        if (vulkanDevice->gpuMemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                            deviceCaps.totalVRAM_MB += vulkanDevice->gpuMemoryProperties.memoryHeaps[i].size / (1024 * 1024);
                        }
                    }

                    SetDeviceCapabilities(deviceCaps);
                    if (config.verbose) {
                        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
                        // LOG_INFO("[BenchmarkRunner] Device from graph: " + std::string(deviceCaps.deviceName));
                    }
                }
            }
        }

        // Update suite results with device capabilities (StartSuite was called before device detection)
        suiteResults_.SetDeviceCapabilities(deviceCapabilities_);

        ProfilerSystem::Instance().SetOutputDirectory(config.outputDir);
        ProfilerSystem::Instance().SetExportFormats(config.exportCSV, config.exportJSON);

        // Get dispatch/render nodes for GPU timing extraction based on pipeline type
        RG::ComputeDispatchNode* dispatchNode = nullptr;
        RG::GeometryRenderNode* geometryRenderNode = nullptr;
        RG::TraceRaysNode* traceRaysNode = nullptr;

        if (benchGraph.compute.dispatch.IsValid()) {
            dispatchNode = static_cast<RG::ComputeDispatchNode*>(
                renderGraph->GetInstance(benchGraph.compute.dispatch));
        }
        if (benchGraph.fragment.drawCommand.IsValid()) {
            geometryRenderNode = static_cast<RG::GeometryRenderNode*>(
                renderGraph->GetInstance(benchGraph.fragment.drawCommand));
        }
        if (benchGraph.hardwareRT.traceRays.IsValid()) {
            traceRaysNode = static_cast<RG::TraceRaysNode*>(
                renderGraph->GetInstance(benchGraph.hardwareRT.traceRays));
        }

        ProfilerSystem::Instance().StartTestRun(testConfig);

        // Initialize frame capture if not already done
        if (!frameCapture_ && vulkanHandles.IsValid()) {
            frameCapture_ = std::make_shared<FrameCapture>();
            bool captureInitialized = frameCapture_->Initialize(
                vulkanHandles.device,
                vulkanHandles.physicalDevice,
                vulkanHandles.graphicsQueue,
                vulkanHandles.graphicsQueueFamily,
                testConfig.screenWidth,
                testConfig.screenHeight
            );
            if (!captureInitialized) {
                std::cerr << "[BenchmarkRunner] Warning: Frame capture initialization failed" << std::endl;
                frameCapture_.reset();
            } else {
                // Register cleanup with graph dependency system
                // Shared ownership ensures FrameCapture lives until cleanup executes
                renderGraph->RegisterExternalCleanup(
                    "benchmark_device",
                    [capture = frameCapture_]() {
                        if (capture) {
                            capture->Cleanup();
                        }
                    },
                    "FrameCapture"
                );
            }
        }
        midFrameCaptured_ = false;  // Reset for this test

        // Frame timing variables
        auto profilingStartTime = std::chrono::high_resolution_clock::now();

        // Render loop
        uint32_t totalFrames = testConfig.warmupFrames + testConfig.measurementFrames;
        for (uint32_t frame = 0; frame < totalFrames && !shouldClose; ++frame) {
            // Process window messages
#ifdef _WIN32
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    shouldClose = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
#endif
            if (shouldClose) break;

            // CPU frame timing start
            auto frameStart = std::chrono::high_resolution_clock::now();

            renderGraph->UpdateTime();
            renderGraph->ProcessEvents();

            // Check for ESC/close request after processing events (before rendering)
            // WindowCloseEvent from InputNode sets shouldClose via subscription callback
            if (shouldClose) {
                // Wait for GPU before breaking out of frame loop
                auto* deviceNode = dynamic_cast<RG::DeviceNode*>(
                    renderGraph->GetInstanceByName("benchmark_device"));
                if (deviceNode && deviceNode->GetDevice()) {
                    VkDevice device = deviceNode->GetDevice()->device;
                    if (device != VK_NULL_HANDLE) {
                        vkDeviceWaitIdle(device);
                    }
                }
                break;
            }

            renderGraph->RecompileDirtyNodes();

            VkResult result = renderGraph->RenderFrame();
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                // Non-fatal warning
            }

            // ============================================================
            // Frame capture for debugging (before timing measurement)
            // ============================================================
            if (frameCapture_ && frameCapture_->IsInitialized()) {
                // Get swapchain node for image access
                auto* swapchainNode = dynamic_cast<RG::SwapChainNode*>(
                    renderGraph->GetInstanceByName("benchmark_swapchain"));
                
                // Get input node for 'C' key capture trigger
                auto* inputNode = dynamic_cast<RG::InputNode*>(
                    renderGraph->GetInstanceByName("benchmark_input"));

                if (swapchainNode) {
                    const auto* swapchainVars = swapchainNode->GetSwapchainPublic();
                    uint32_t imageIndex = swapchainNode->GetCurrentImageIndex();

                    // Automatic mid-frame capture (quarter resolution)
                    uint32_t midFrame = totalFrames / 2;
                    if (frame == midFrame && !midFrameCaptured_) {
                        CaptureConfig captureConfig;
                        captureConfig.outputPath = outputDirectory_;
                        captureConfig.testName = testConfig.testId;
                        captureConfig.frameNumber = frame;
                        captureConfig.resolution = CaptureResolution::Quarter;

                        auto captureResult = frameCapture_->Capture(swapchainVars, imageIndex, captureConfig);
                        midFrameCaptured_ = true;
                        if (config.verbose && captureResult.success) {
                            std::cout << "  [Capture] Mid-frame saved: " << captureResult.savedPath << std::endl;
                        }
                    }

                    // Manual capture on 'C' key (full resolution)
                    if (inputNode) {
                        if (inputNode->GetInputState().IsKeyPressed(Vixen::EventBus::KeyCode::C)) {
                            CaptureConfig captureConfig;
                            captureConfig.outputPath = outputDirectory_;
                            captureConfig.testName = testConfig.testId;
                            captureConfig.frameNumber = frame;
                            captureConfig.resolution = CaptureResolution::Full;

                            auto captureResult = frameCapture_->Capture(swapchainVars, imageIndex, captureConfig);
                            if (captureResult.success) {
                                std::cout << "  [Capture] Manual capture saved: " << captureResult.savedPath << std::endl;
                            }
                        }
                    }
                }
            }

            // CPU frame timing end
            auto frameEnd = std::chrono::high_resolution_clock::now();
            auto frameDuration = std::chrono::duration<float, std::milli>(frameEnd - frameStart);
            auto sinceStart = std::chrono::duration<double, std::milli>(frameEnd - profilingStartTime);

            // ============================================================
            // Collect REAL frame metrics (not synthetic!)
            // ============================================================
            FrameMetrics metrics{};

            // Frame identification
            metrics.frameNumber = frame;
            metrics.timestampMs = sinceStart.count();

            // CPU timing (REAL)
            metrics.frameTimeMs = frameDuration.count();
            metrics.fps = (metrics.frameTimeMs > 0.0f) ? (1000.0f / metrics.frameTimeMs) : 0.0f;

            // GPU timing from pipeline-specific node's GPUPerformanceLogger (REAL)
            // The GPUPerformanceLogger collects results during Execute()
            // via CollectResults(frameIndex) - results are 1 frame behind
            // due to frames-in-flight, but this gives us actual GPU timing
            RG::GPUPerformanceLogger* gpuLogger = nullptr;

            if (dispatchNode) {
                gpuLogger = dispatchNode->GetGPUPerformanceLogger();
            } else if (geometryRenderNode) {
                gpuLogger = geometryRenderNode->GetGPUPerformanceLogger();
            } else if (traceRaysNode) {
                gpuLogger = traceRaysNode->GetGPUPerformanceLogger();
            }

            if (gpuLogger) {
                metrics.gpuTimeMs = gpuLogger->GetLastDispatchMs();
                metrics.mRaysPerSec = gpuLogger->GetLastMraysPerSec();
            }

            // Fallback: estimate GPU time from frame time if logger unavailable
            if (metrics.gpuTimeMs == 0.0f && metrics.frameTimeMs > 0.0f) {
                metrics.gpuTimeMs = metrics.frameTimeMs * 0.9f;
            }

            // Scene properties from TestConfiguration (REAL)
            metrics.sceneResolution = testConfig.voxelResolution;
            metrics.screenWidth = testConfig.screenWidth;
            metrics.screenHeight = testConfig.screenHeight;
            metrics.sceneDensity = 0.0f;  // Will be computed from actual scene data
            metrics.totalRaysCast = static_cast<uint64_t>(testConfig.screenWidth) * testConfig.screenHeight;

            // Try to get real avgVoxelsPerRay from GPU shader counters
            // If shader counters are available, use real data; otherwise estimate
            bool gotRealCounters = false;
            auto* voxelGridNode = dynamic_cast<RG::VoxelGridNode*>(
                renderGraph->GetInstanceByName("benchmark_voxel_grid"));
            if (voxelGridNode) {
                // Wait for GPU to finish before reading counter buffer
                // Uses the same pattern as DebugBufferReaderNode: wait on in-flight fence
                // This ensures the compute shader has finished writing to the counter buffer
                auto* frameSyncNode = dynamic_cast<RG::FrameSyncNode*>(
                    renderGraph->GetInstanceByName("benchmark_frame_sync"));
                auto* deviceNode = dynamic_cast<RG::DeviceNode*>(
                    renderGraph->GetInstanceByName("benchmark_device"));

                if (frameSyncNode && deviceNode && deviceNode->GetDevice()) {
                    VkDevice device = deviceNode->GetDevice()->device;
                    VkFence inFlightFence = frameSyncNode->GetCurrentInFlightFence();
                    if (device != VK_NULL_HANDLE && inFlightFence != VK_NULL_HANDLE) {
                        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
                    }
                }

                // Read shader counters from GPU - uses mapped memory
                const auto* shaderCounters = voxelGridNode->ReadShaderCounters();
                if (shaderCounters && shaderCounters->HasData()) {
                    metrics.avgVoxelsPerRay = shaderCounters->GetAvgVoxelsPerRay();
                    metrics.shaderCounters.totalVoxelsTraversed = shaderCounters->totalVoxelsTraversed;
                    metrics.shaderCounters.totalRaysCast = shaderCounters->totalRaysCast;
                    metrics.shaderCounters.totalNodesVisited = shaderCounters->totalNodesVisited;
                    metrics.shaderCounters.totalLeafNodesVisited = shaderCounters->totalLeafNodesVisited;
                    metrics.shaderCounters.totalEmptySpaceSkipped = shaderCounters->totalEmptySpaceSkipped;
                    metrics.shaderCounters.rayHitCount = shaderCounters->rayHitCount;
                    metrics.shaderCounters.rayMissCount = shaderCounters->rayMissCount;
                    metrics.shaderCounters.earlyTerminations = shaderCounters->earlyTerminations;
                    // Copy per-level cache statistics
                    for (size_t i = 0; i < Vixen::Profiler::ShaderCounters::MAX_SVO_LEVELS; ++i) {
                        metrics.shaderCounters.nodeVisitsPerLevel[i] = shaderCounters->nodeVisitsPerLevel[i];
                        metrics.shaderCounters.cacheHitsPerLevel[i] = shaderCounters->cacheHitsPerLevel[i];
                        metrics.shaderCounters.cacheMissesPerLevel[i] = shaderCounters->cacheMissesPerLevel[i];
                    }
                    gotRealCounters = true;
                }
            }

            // Fallback: Estimate avgVoxelsPerRay based on octree depth
            if (!gotRealCounters) {
                uint32_t resolution = testConfig.voxelResolution > 0
                    ? testConfig.voxelResolution
                    : metrics.sceneResolution;
                if (resolution > 0) {
                    float octreeDepth = std::log2(static_cast<float>(resolution));
                    metrics.avgVoxelsPerRay = octreeDepth * 3.0f;
                }
            }

            // Calculate mRays/sec from GPU time if not available from logger
            if (metrics.mRaysPerSec == 0.0f && metrics.gpuTimeMs > 0.0f) {
                metrics.mRaysPerSec = (static_cast<float>(metrics.totalRaysCast) / 1e6f) /
                                      (metrics.gpuTimeMs / 1000.0f);
            }

            // VRAM usage via VK_EXT_memory_budget
            if (vulkanHandles.IsValid() && vulkanHandles.physicalDevice != VK_NULL_HANDLE) {
                CollectVRAMUsage(vulkanHandles.physicalDevice, metrics);
            }

            // Bandwidth estimation (if GPU timing available but no HW counters)
            if (metrics.gpuTimeMs > 0.0f && !HasHardwarePerformanceCounters()) {
                float estimatedBW = EstimateBandwidth(metrics.totalRaysCast, metrics.gpuTimeMs / 1000.0f);
                metrics.bandwidthReadGB = estimatedBW;
                // Write bandwidth: framebuffer output (4 bytes RGBA8 per pixel)
                float writeBytes = static_cast<float>(metrics.totalRaysCast) * 4.0f;
                metrics.bandwidthWriteGB = (writeBytes / 1e9f) / (metrics.gpuTimeMs / 1000.0f);
                metrics.bandwidthEstimated = true;
            }

            RecordFrame(metrics);
        }

        FinalizeCurrentTest();

        // Complete progress line (only newline if not verbose, since verbose already printed newline)
        if (!config.verbose) {
            std::cout << " Done" << std::endl;
        }

        // Save user-initiated close state before graph destruction
        bool userRequestedClose = shouldClose;

        ProfilerSystem::Instance().EndTestRun(!userRequestedClose);
        ClearCurrentGraph();

        // Wait for GPU to finish all pending work before destroying resources
        // This prevents crashes when ESC is pressed during frame rendering
        auto* deviceNode = dynamic_cast<RG::DeviceNode*>(
            renderGraph->GetInstanceByName("benchmark_device"));
        if (deviceNode && deviceNode->GetDevice()) {
            VkDevice device = deviceNode->GetDevice()->device;
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
        }

        // FrameCapture cleanup happens automatically via RegisterExternalCleanup
        // Reset graph for next test (this destroys window, triggering WindowCloseEvent)
        if (config.verbose) std::cout << "[BenchmarkRunner] Resetting RenderGraph..." << std::endl;
        renderGraph.reset();
        if (config.verbose) std::cout << "[BenchmarkRunner] RenderGraph reset." << std::endl;

        // Drain any pending window messages from graph destruction
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_QUIT) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
#endif

        // Reset shouldClose - programmatic window destruction is not a user close request
        // Only preserve if user actually clicked close button during the test
        shouldClose = userRequestedClose;

        if (shouldClose) break;  // User requested exit, don't start next test

        renderGraph = std::make_unique<RG::RenderGraph>(
            nodeRegistry.get(),
            messageBus.get(),
            graphLogger.get(),
            &mainCacher
        );
    }

    // FrameCapture cleanup is handled automatically by RenderGraph dependency system
    // No manual cleanup needed here

    // Shutdown mainCacher BEFORE messageBus destructs
    // mainCacher is a global singleton that destructs during static deinitialization
    // messageBus is local and destructs when this function returns
    CashSystem::MainCacher::Instance().Shutdown();

    ProfilerSystem::Instance().Shutdown();

    return suiteResults_;
}

std::string BenchmarkRunner::GenerateBenchmarkFolderName(const std::string& gpuName, const std::string& sessionUuid) {
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;

#ifdef _WIN32
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif

    // Format: YYYYMMDD_HHMMSS
    std::ostringstream timestamp;
    timestamp << std::setfill('0')
              << std::setw(4) << (tm_now.tm_year + 1900)
              << std::setw(2) << (tm_now.tm_mon + 1)
              << std::setw(2) << tm_now.tm_mday
              << "_"
              << std::setw(2) << tm_now.tm_hour
              << std::setw(2) << tm_now.tm_min
              << std::setw(2) << tm_now.tm_sec;

    // Use provided session UUID or generate a new one
    std::string uuidStr;
    if (sessionUuid.empty()) {
        // Generate short UUID (8 hex characters for readability)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        uint32_t uuid = dis(gen);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(8) << uuid;
        uuidStr = oss.str();
    } else {
        uuidStr = sessionUuid;
    }

    // Combine: YYYYMMDD_HHMMSS_GPUName_UUID
    return timestamp.str() + "_" + gpuName + "_" + uuidStr;
}

std::vector<BenchmarkRunner::GPUInfo> BenchmarkRunner::EnumerateAvailableGPUs() {
    VkInstance instance = VK_NULL_HANDLE;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GPU Enumeration";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        // Failed to create instance - return empty list
        return {};
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    std::vector<GPUInfo> gpuList;
    if (deviceCount > 0) {
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        gpuList.reserve(deviceCount);
        for (uint32_t i = 0; i < deviceCount; ++i) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(devices[i], &props);

            // Sanitize device name for filesystem (replace spaces with underscores, remove special chars)
            std::string deviceName = props.deviceName;
            for (char& c : deviceName) {
                if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                    c = '_';
                }
            }

            gpuList.push_back({i, deviceName});
        }
    }

    vkDestroyInstance(instance, nullptr);
    return gpuList;
}

void BenchmarkRunner::ListAvailableGPUs() {
    VkInstance instance = VK_NULL_HANDLE;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GPU List";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        // TODO: Migrate to LOG_ERROR when BenchmarkRunner inherits from ILoggable
        // LOG_ERROR("Failed to create Vulkan instance for GPU enumeration");
        return;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
        // LOG_INFO("No Vulkan-capable GPUs found");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // TODO: Migrate to LOG_INFO when BenchmarkRunner inherits from ILoggable
    // LOG_INFO("");
    // LOG_INFO("Available GPUs:");
    // LOG_INFO("===============");

    for (uint32_t i = 0; i < deviceCount; ++i) {
        auto caps = DeviceCapabilities::Capture(devices[i]);
        // LOG_INFO("  [" + std::to_string(i) + "] " + caps.GetSummaryString());
    }
    // LOG_INFO("");
    // LOG_INFO("Use --gpu N to select a specific GPU");
    // LOG_INFO("");

    vkDestroyInstance(instance, nullptr);
}

//==============================================================================
// Low-Level API Implementation
//==============================================================================

bool BenchmarkRunner::LoadConfig(const std::filesystem::path& configPath) {
    configPath_ = configPath;
    testMatrix_ = BenchmarkConfigLoader::LoadBatchFromFile(configPath);
    return !testMatrix_.empty();
}

void BenchmarkRunner::SetOutputDirectory(const std::filesystem::path& path) {
    outputDirectory_ = path;
}

void BenchmarkRunner::SetDeviceCapabilities(const DeviceCapabilities& caps) {
    deviceCapabilities_ = caps;
}

void BenchmarkRunner::SetBandwidthEstimationConfig(const BandwidthEstimationConfig& config) {
    bandwidthConfig_ = config;
}

std::vector<TestConfiguration> BenchmarkRunner::GenerateTestMatrix() const {
    return testMatrix_;
}

void BenchmarkRunner::SetTestMatrix(const std::vector<TestConfiguration>& matrix) {
    testMatrix_ = matrix;
}

void BenchmarkRunner::SetFrameCallback(FrameCallback callback) {
    frameCallback_ = std::move(callback);
}

void BenchmarkRunner::SetProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

bool BenchmarkRunner::StartSuite() {
    if (testMatrix_.empty()) {
        state_ = BenchmarkState::Error;
        return false;
    }

    // Validate all configurations
    for (const auto& config : testMatrix_) {
        auto errors = config.ValidateWithErrors();
        if (!errors.empty()) {
            state_ = BenchmarkState::Error;
            return false;
        }
    }

    // Ensure output directory exists
    MetricsExporter::EnsureDirectoryExists(outputDirectory_);

    // Initialize suite
    suiteResults_.Clear();
    suiteResults_.SetDeviceCapabilities(deviceCapabilities_);
    suiteResults_.SetStartTime(std::chrono::system_clock::now());
    suiteStartTime_ = std::chrono::system_clock::now();

    currentTestIndex_ = 0;
    state_ = BenchmarkState::Idle;

    return true;
}

bool BenchmarkRunner::BeginNextTest() {
    if (currentTestIndex_ >= testMatrix_.size()) {
        state_ = BenchmarkState::Completed;
        suiteResults_.SetEndTime(std::chrono::system_clock::now());
        return false;
    }

    // Setup current test
    currentConfig_ = testMatrix_[currentTestIndex_];
    currentFrames_.clear();
    currentFrame_ = 0;
    testStartTime_ = std::chrono::system_clock::now();

    // Reset AS timing (will be populated during graph compilation for hardware_rt)
    currentBlasBuildTimeMs_ = 0.0f;
    currentTlasBuildTimeMs_ = 0.0f;

    // Initialize stats trackers
    InitializeStatsTrackers();

    // Start in warmup phase
    state_ = BenchmarkState::Warmup;

    return true;
}

void BenchmarkRunner::RecordFrame(const FrameMetrics& metrics) {
    if (state_ == BenchmarkState::Idle || state_ == BenchmarkState::Completed ||
        state_ == BenchmarkState::Error) {
        return;
    }

    // Handle warmup phase
    if (state_ == BenchmarkState::Warmup) {
        currentFrame_++;
        if (currentFrame_ >= currentConfig_.warmupFrames) {
            // Transition to measurement
            state_ = BenchmarkState::Measuring;
            currentFrame_ = 0;
        }
        ReportProgress();
        return;
    }

    // Measurement phase
    if (state_ == BenchmarkState::Measuring) {
        // Apply bandwidth estimation if needed
        FrameMetrics adjustedMetrics = metrics;
        if (!HasHardwarePerformanceCounters() && bandwidthConfig_.useEstimation) {
            float estimatedBW = EstimateBandwidth(metrics.totalRaysCast, metrics.frameTimeMs / 1000.0f);
            adjustedMetrics.bandwidthReadGB = estimatedBW;
            // Write bandwidth: framebuffer output (4 bytes RGBA8 per pixel)
            float writeBytes = static_cast<float>(metrics.totalRaysCast) * 4.0f;
            adjustedMetrics.bandwidthWriteGB = (writeBytes / 1e9f) / (metrics.frameTimeMs / 1000.0f);
            adjustedMetrics.bandwidthEstimated = true;
        }

        // Sample NVML GPU utilization if available
        auto& nvml = NVMLWrapper::Instance();
        if (nvml.IsAvailable()) {
            auto util = nvml.GetUtilization(0);
            if (util.valid) {
                adjustedMetrics.gpuUtilization = util.gpuUtilization;
                adjustedMetrics.memoryUtilization = util.memoryUtilization;
                adjustedMetrics.gpuTemperature = util.temperature;
                adjustedMetrics.gpuPowerW = util.powerUsageW;
                adjustedMetrics.nvmlAvailable = true;
            }
        }

        currentFrames_.push_back(adjustedMetrics);
        UpdateStats(adjustedMetrics);

        // Invoke frame callback
        if (frameCallback_) {
            frameCallback_(currentFrame_);
        }

        currentFrame_++;
        ReportProgress();
    }
}

bool BenchmarkRunner::IsCurrentTestComplete() const {
    return state_ == BenchmarkState::Measuring &&
           currentFrame_ >= currentConfig_.measurementFrames;
}

void BenchmarkRunner::FinalizeCurrentTest() {
    if (currentFrames_.empty()) {
        currentTestIndex_++;
        state_ = BenchmarkState::Idle;
        return;
    }

    // Compute aggregates
    auto aggregates = ComputeAggregates();

    // Create test results
    TestRunResults results;
    results.config = currentConfig_;
    results.frames = std::move(currentFrames_);
    results.aggregates = std::move(aggregates);
    results.startTime = testStartTime_;
    results.endTime = std::chrono::system_clock::now();
    results.blasBuildTimeMs = currentBlasBuildTimeMs_;
    results.tlasBuildTimeMs = currentTlasBuildTimeMs_;

    // Run sanity checks on collected data
    MetricsSanityChecker checker;
    results.validation = checker.Validate(results.frames, results.config);

    // Also validate aggregates
    auto aggregateValidation = checker.ValidateAggregates(results.aggregates);
    for (const auto& check : aggregateValidation.checks) {
        results.validation.checks.push_back(check);
        switch (check.severity) {
            case SanityCheckSeverity::Info: results.validation.infoCount++; break;
            case SanityCheckSeverity::Warning: results.validation.warningCount++; break;
            case SanityCheckSeverity::Error:
                results.validation.errorCount++;
                results.validation.valid = false;
                break;
        }
    }

    // Log validation warnings/errors
    if (results.validation.warningCount > 0 || results.validation.errorCount > 0) {
        std::cout << "  [Validation] " << results.validation.errorCount << " errors, "
                  << results.validation.warningCount << " warnings" << std::endl;
    }

    // Add to suite
    suiteResults_.AddTestRun(results);

    // Export individual test results
    std::string filename = currentConfig_.testId.empty()
        ? currentConfig_.GenerateTestId(currentTestIndex_ + 1)
        : currentConfig_.testId;
    ExportTestResults(results, filename + ".json");

    // Prepare for next test
    currentFrames_.clear();
    currentTestIndex_++;
    state_ = BenchmarkState::Idle;
}

TestRunResults BenchmarkRunner::CollectCurrentTestResults() {
    TestRunResults results;

    if (currentFrames_.empty()) {
        return results;
    }

    // Compute aggregates
    auto aggregates = ComputeAggregates();

    // Create test results
    results.config = currentConfig_;
    results.frames = currentFrames_;  // Copy, don't move
    results.aggregates = std::move(aggregates);
    results.startTime = testStartTime_;
    results.endTime = std::chrono::system_clock::now();
    results.blasBuildTimeMs = currentBlasBuildTimeMs_;
    results.tlasBuildTimeMs = currentTlasBuildTimeMs_;

    // Run sanity checks on collected data
    MetricsSanityChecker checker;
    results.validation = checker.Validate(results.frames, results.config);

    // Also validate aggregates
    auto aggregateValidation = checker.ValidateAggregates(results.aggregates);
    for (const auto& check : aggregateValidation.checks) {
        results.validation.checks.push_back(check);
        switch (check.severity) {
            case SanityCheckSeverity::Info: results.validation.infoCount++; break;
            case SanityCheckSeverity::Warning: results.validation.warningCount++; break;
            case SanityCheckSeverity::Error:
                results.validation.errorCount++;
                results.validation.valid = false;
                break;
        }
    }

    return results;
}

void BenchmarkRunner::ResetCurrentTestForRerun() {
    currentFrames_.clear();
    currentFrame_ = 0;
    testStartTime_ = std::chrono::system_clock::now();
    midFrameCaptured_ = false;
    InitializeStatsTrackers();
    state_ = BenchmarkState::Warmup;
}

void BenchmarkRunner::AbortSuite() {
    state_ = BenchmarkState::Idle;
    currentFrames_.clear();
}

const TestConfiguration& BenchmarkRunner::GetCurrentTestConfig() const {
    static TestConfiguration empty;
    if (currentTestIndex_ < testMatrix_.size()) {
        return currentConfig_;
    }
    return empty;
}

void BenchmarkRunner::ExportAllResults() {
    MetricsExporter exporter;

    // Export each test result with validation
    for (size_t i = 0; i < suiteResults_.GetAllResults().size(); ++i) {
        const auto& result = suiteResults_.GetAllResults()[i];
        std::string filename = result.config.testId.empty()
            ? result.config.GenerateTestId(static_cast<uint32_t>(i + 1))
            : result.config.testId;

        auto filepath = outputDirectory_ / (filename + ".json");
        exporter.ExportToJSON(filepath, result.config, deviceCapabilities_,
                              result.frames, result.aggregates, result.validation,
                              result.blasBuildTimeMs, result.tlasBuildTimeMs);
    }

    // Export suite summary
    auto summaryPath = outputDirectory_ / "suite_summary.json";
    suiteResults_.ExportSummary(summaryPath.string());
}

void BenchmarkRunner::ExportTestResults(const TestRunResults& results, const std::string& filename) {
    MetricsExporter exporter;
    auto filepath = outputDirectory_ / filename;
    exporter.ExportToJSON(filepath, results.config, deviceCapabilities_,
                          results.frames, results.aggregates, results.validation,
                          results.blasBuildTimeMs, results.tlasBuildTimeMs);
}

float BenchmarkRunner::EstimateBandwidth(uint64_t raysCast, float frameTimeSeconds) const {
    if (frameTimeSeconds <= 0.0f || raysCast == 0) {
        return 0.0f;
    }

    // Formula: bandwidth_estimate = rays_cast * avg_bytes_per_ray / frame_time_s
    // Result in bytes/second, convert to GB/s
    double bytesTransferred = static_cast<double>(raysCast) * bandwidthConfig_.avgBytesPerRay;
    double bytesPerSecond = bytesTransferred / static_cast<double>(frameTimeSeconds);
    double gbPerSecond = bytesPerSecond / (1024.0 * 1024.0 * 1024.0);

    return static_cast<float>(gbPerSecond);
}

bool BenchmarkRunner::HasHardwarePerformanceCounters() const {
    return deviceCapabilities_.performanceQuerySupported;
}

void BenchmarkRunner::InitializeStatsTrackers() {
    currentStats_.clear();

    // Initialize rolling stats for each metric
    const uint32_t windowSize = currentConfig_.measurementFrames;
    currentStats_["frame_time_ms"] = RollingStats(windowSize);
    currentStats_["fps"] = RollingStats(windowSize);
    currentStats_["bandwidth_read_gb"] = RollingStats(windowSize);
    currentStats_["bandwidth_write_gb"] = RollingStats(windowSize);
    currentStats_["vram_mb"] = RollingStats(windowSize);
    currentStats_["mrays_per_sec"] = RollingStats(windowSize);
}

void BenchmarkRunner::UpdateStats(const FrameMetrics& metrics) {
    currentStats_["frame_time_ms"].AddSample(metrics.frameTimeMs);
    currentStats_["fps"].AddSample(metrics.fps);
    currentStats_["bandwidth_read_gb"].AddSample(metrics.bandwidthReadGB);
    currentStats_["bandwidth_write_gb"].AddSample(metrics.bandwidthWriteGB);
    currentStats_["vram_mb"].AddSample(static_cast<float>(metrics.vramUsageMB));
    currentStats_["mrays_per_sec"].AddSample(metrics.mRaysPerSec);
}

std::map<std::string, AggregateStats> BenchmarkRunner::ComputeAggregates() const {
    std::map<std::string, AggregateStats> aggregates;

    for (const auto& [name, stats] : currentStats_) {
        aggregates[name] = const_cast<RollingStats&>(stats).GetAggregateStats();
    }

    return aggregates;
}

void BenchmarkRunner::ReportProgress() {
    if (progressCallback_) {
        uint32_t totalFrames = currentConfig_.warmupFrames + currentConfig_.measurementFrames;
        uint32_t absoluteFrame = (state_ == BenchmarkState::Warmup)
            ? currentFrame_
            : currentConfig_.warmupFrames + currentFrame_;

        progressCallback_(currentTestIndex_,
                          static_cast<uint32_t>(testMatrix_.size()),
                          absoluteFrame,
                          totalFrames);
    }
}

//==============================================================================
// Graph Management Implementation
//==============================================================================

void BenchmarkRunner::SetGraphFactory(GraphFactoryFunc factory) {
    graphFactory_ = std::move(factory);
}

void BenchmarkRunner::SetRenderDimensions(uint32_t width, uint32_t height) {
    renderWidth_ = width;
    renderHeight_ = height;
}

BenchmarkGraph BenchmarkRunner::CreateGraphForCurrentTest(Vixen::RenderGraph::RenderGraph* graph) {
    if (!graph) {
        return BenchmarkGraph{};
    }

    // Clear any previous graph state
    currentGraph_ = BenchmarkGraph{};

    // Use custom factory if set, otherwise use default
    if (graphFactory_) {
        currentGraph_ = graphFactory_(graph, currentConfig_, renderWidth_, renderHeight_);
    } else {
        // Default: use BenchmarkGraphFactory with pipeline dispatch
        currentGraph_ = BenchmarkGraphFactory::BuildFromConfig(graph, currentConfig_);
    }

    // Wire profiler hooks if graph was created successfully
    if (currentGraph_.IsValid()) {
        BenchmarkGraphFactory::WireProfilerHooks(graph, adapter_, currentGraph_);
    }

    return currentGraph_;
}

void BenchmarkRunner::ClearCurrentGraph() {
    currentGraph_ = BenchmarkGraph{};
}

//==============================================================================
// VRAM Collection via VK_EXT_memory_budget
//==============================================================================

void BenchmarkRunner::CollectVRAMUsage(VkPhysicalDevice physicalDevice, FrameMetrics& metrics) const {
    if (physicalDevice == VK_NULL_HANDLE) {
        metrics.vramUsageMB = 0;
        metrics.vramBudgetMB = 0;
        return;
    }

    // Check for VK_EXT_memory_budget support
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

    bool memoryBudgetSupported = false;
    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
            memoryBudgetSupported = true;
            break;
        }
    }

    if (!memoryBudgetSupported) {
        metrics.vramUsageMB = 0;
        metrics.vramBudgetMB = 0;
        return;
    }

    // Query memory budget using VK_EXT_memory_budget
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
    budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 memProps2{};
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memProps2.pNext = &budgetProps;

    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProps2);

    // Sum up device-local heap usage and budget
    uint64_t totalUsage = 0;
    uint64_t totalBudget = 0;

    for (uint32_t i = 0; i < memProps2.memoryProperties.memoryHeapCount; ++i) {
        if (memProps2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalUsage += budgetProps.heapUsage[i];
            totalBudget += budgetProps.heapBudget[i];
        }
    }

    metrics.vramUsageMB = totalUsage / (1024 * 1024);
    metrics.vramBudgetMB = totalBudget / (1024 * 1024);
}

} // namespace Vixen::Profiler
