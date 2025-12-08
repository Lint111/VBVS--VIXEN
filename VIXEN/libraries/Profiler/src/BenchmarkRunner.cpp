#include "Profiler/BenchmarkRunner.h"
#include "Profiler/BenchmarkConfig.h"
#include "Profiler/BenchmarkGraphFactory.h"
#include "Profiler/ProfilerSystem.h"
#include "Profiler/VulkanIntegration.h"
#include <iomanip>
#include <Core/RenderGraph.h>
#include <Core/NodeTypeRegistry.h>
#include <MessageBus.h>
#include <Message.h>
#include <MainCacher.h>
#include <Logger.h>

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
        std::cerr << "[Vulkan] " << pCallbackData->pMessage << "\n";
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
        std::cerr << "Error: No Vulkan-capable GPUs found\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());

    if (gpuIndex >= deviceCount) {
        std::cerr << "Error: GPU index " << gpuIndex << " out of range (0-"
                  << deviceCount - 1 << ")\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx.physicalDevice = devices[gpuIndex];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    ctx.timestampPeriod = props.limits.timestampPeriod;

    if (verbose) {
        std::cout << "Selected GPU: " << props.deviceName << "\n";
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
        std::cerr << "Error: No compute-capable queue family found\n";
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

        std::cout << "RTX extensions enabled for hardware ray tracing\n";
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
        std::cerr << "Error: Failed to create Vulkan instance\n";
        return false;
    }

    if (SelectPhysicalDevice(ctx, config.gpuIndex, config.verbose) != VK_SUCCESS) {
        return false;
    }

    if (CreateHeadlessDevice(ctx) != VK_SUCCESS) {
        std::cerr << "Error: Failed to create logical device\n";
        return false;
    }

    if (CreateCommandPool(ctx) != VK_SUCCESS) {
        std::cerr << "Error: Failed to create command pool\n";
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
        std::cerr << "Configuration errors:\n";
        for (const auto& err : errors) {
            std::cerr << "  - " << err << "\n";
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
        std::cerr << "Error: Failed to create output directory: " << config.outputDir << "\n";
        return TestSuiteResults{};
    }

    if (config.verbose) {
        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "VIXEN Benchmark Tool\n";
        std::cout << "=================================================\n";
        std::cout << "Mode:   " << (config.headless ? "Headless" : "Render") << "\n";
        std::cout << "Tests:  " << config.tests.size() << " configurations\n";
        std::cout << "Output: " << config.outputDir << "\n";
        std::cout << "\n";
    }

    TestSuiteResults results;

    if (config.headless) {
        results = RunSuiteHeadless(config);
    } else {
        results = RunSuiteWithWindow(config);
    }

    // Export final results
    ExportAllResults();

    if (config.verbose) {
        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "Benchmark Complete\n";
        std::cout << "=================================================\n";
        std::cout << "  Passed: " << results.GetPassCount() << "/" << results.GetTotalCount() << "\n";
        std::cout << "  Output: " << config.outputDir << "\n";
        std::cout << "=================================================\n";
    }

    return results;
}

TestSuiteResults BenchmarkRunner::RunSuiteHeadless(const BenchmarkSuiteConfig& config) {
    // Initialize headless Vulkan context
    HeadlessVulkanContext ctx{};
    if (!InitializeHeadlessVulkan(ctx, config)) {
        std::cerr << "Error: Failed to initialize Vulkan\n";
        return TestSuiteResults{};
    }

    // Capture device capabilities
    auto deviceCaps = DeviceCapabilities::Capture(ctx.physicalDevice);
    SetDeviceCapabilities(deviceCaps);

    if (config.verbose) {
        std::cout << "Device: " << deviceCaps.GetSummaryString() << "\n\n";
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
            std::cout << "\r  [" << bar << ">" << empty << "] "
                      << std::setw(3) << percent << "% | "
                      << "Test " << (testIdx + 1) << "/" << totalTests << " | "
                      << cfg.voxelResolution << "^3 " << cfg.sceneType << " | "
                      << cfg.shader
                      << "     " << std::flush;
        });
    }

    // Start suite
    if (!StartSuite()) {
        std::cerr << "Error: Failed to start benchmark suite\n";
        CleanupHeadlessVulkan(ctx);
        return TestSuiteResults{};
    }

    // Run each test
    while (BeginNextTest()) {
        const auto& testConfig = GetCurrentTestConfig();

        if (config.verbose) {
            std::cout << "  [" << (GetCurrentTestIndex() + 1) << "/" << testMatrix_.size() << "] "
                      << testConfig.pipeline << " | "
                      << testConfig.voxelResolution << "^3 | "
                      << testConfig.sceneType << " | "
                      << testConfig.shader << "\n";
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
            metrics.fps = 1000.0f / metrics.frameTimeMs;
            metrics.bandwidthEstimated = true;

            RecordFrame(metrics);
        }

        FinalizeCurrentTest();
        ProfilerSystem::Instance().EndTestRun(true);
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

    // Create render graph
    auto renderGraph = std::make_unique<RG::RenderGraph>(
        nodeRegistry.get(),
        messageBus.get(),
        graphLogger.get(),
        &mainCacher
    );

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
            std::cout << "\r  [" << bar << ">" << empty << "] "
                      << std::setw(3) << percent << "% | "
                      << "Test " << (testIdx + 1) << "/" << totalTests << " | "
                      << cfg.voxelResolution << "^3 " << cfg.sceneType << " | "
                      << cfg.shader
                      << "     " << std::flush;
        });
    }

    // Start suite
    if (!StartSuite()) {
        std::cerr << "Error: Failed to start benchmark suite\n";
        return TestSuiteResults{};
    }

    // Run each test
    while (BeginNextTest() && !shouldClose) {
        const auto& testConfig = GetCurrentTestConfig();

        if (config.verbose) {
            std::cout << "  [" << (GetCurrentTestIndex() + 1) << "/" << testMatrix_.size() << "] "
                      << testConfig.pipeline << " | "
                      << testConfig.voxelResolution << "^3 | "
                      << testConfig.sceneType << " | "
                      << testConfig.shader << "\n";
        }

        // Create graph for current test
        auto benchGraph = CreateGraphForCurrentTest(renderGraph.get());
        if (!benchGraph.IsValid()) {
            std::cerr << "Error: Failed to create graph for test\n";
            continue;
        }

        // Compile the graph
        renderGraph->Compile();

        // Initialize profiler from graph
        VulkanHandles vulkanHandles;
        bool profilerInitialized = VulkanIntegrationHelper::InitializeProfilerFromGraph(renderGraph.get());

        if (profilerInitialized) {
            vulkanHandles = VulkanIntegrationHelper::ExtractFromGraph(renderGraph.get());
            if (vulkanHandles.IsValid()) {
                auto deviceCaps = DeviceCapabilities::Capture(vulkanHandles.physicalDevice);
                SetDeviceCapabilities(deviceCaps);
                if (config.verbose) {
                    std::cout << "[BenchmarkRunner] Device: " << deviceCaps.deviceName << "\n";
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
                        std::cout << "[BenchmarkRunner] Device from graph: " << deviceCaps.deviceName << "\n";
                    }
                }
            }
        }

        // Update suite results with device capabilities (StartSuite was called before device detection)
        suiteResults_.SetDeviceCapabilities(deviceCapabilities_);

        ProfilerSystem::Instance().SetOutputDirectory(config.outputDir);
        ProfilerSystem::Instance().SetExportFormats(config.exportCSV, config.exportJSON);

        // Get ComputeDispatchNode for GPU timing extraction
        RG::ComputeDispatchNode* dispatchNode = nullptr;
        if (benchGraph.compute.dispatch.IsValid()) {
            dispatchNode = static_cast<RG::ComputeDispatchNode*>(
                renderGraph->GetInstance(benchGraph.compute.dispatch));
        }

        ProfilerSystem::Instance().StartTestRun(testConfig);

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
            renderGraph->RecompileDirtyNodes();

            VkResult result = renderGraph->RenderFrame();
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                // Non-fatal warning
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

            // GPU timing from ComputeDispatchNode's GPUPerformanceLogger (REAL)
            if (dispatchNode) {
                // The GPUPerformanceLogger collects results during Execute()
                // via CollectResults(frameIndex) - results are 1 frame behind
                // due to frames-in-flight, but this gives us actual GPU timing
                auto* gpuLogger = dispatchNode->GetGPUPerformanceLogger();
                if (gpuLogger) {
                    metrics.gpuTimeMs = gpuLogger->GetLastDispatchMs();
                    metrics.mRaysPerSec = gpuLogger->GetLastMraysPerSec();
                }
            }

            // Scene properties from TestConfiguration (REAL)
            metrics.sceneResolution = testConfig.voxelResolution;
            metrics.screenWidth = testConfig.screenWidth;
            metrics.screenHeight = testConfig.screenHeight;
            metrics.sceneDensity = 0.0f;  // Will be computed from actual scene data
            metrics.totalRaysCast = static_cast<uint64_t>(testConfig.screenWidth) * testConfig.screenHeight;

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

        // Save user-initiated close state before graph destruction
        bool userRequestedClose = shouldClose;

        ProfilerSystem::Instance().EndTestRun(!userRequestedClose);
        ClearCurrentGraph();

        // Reset graph for next test (this destroys window, triggering WindowCloseEvent)
        renderGraph.reset();

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

    ProfilerSystem::Instance().Shutdown();

    return suiteResults_;
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
        std::cerr << "Error: Failed to create Vulkan instance for GPU enumeration\n";
        return;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cout << "No Vulkan-capable GPUs found\n";
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::cout << "\nAvailable GPUs:\n";
    std::cout << "===============\n";

    for (uint32_t i = 0; i < deviceCount; ++i) {
        auto caps = DeviceCapabilities::Capture(devices[i]);
        std::cout << "  [" << i << "] " << caps.GetSummaryString() << "\n";
    }
    std::cout << "\nUse --gpu N to select a specific GPU\n\n";

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

    // Export each test result
    for (size_t i = 0; i < suiteResults_.GetAllResults().size(); ++i) {
        const auto& result = suiteResults_.GetAllResults()[i];
        std::string filename = result.config.testId.empty()
            ? result.config.GenerateTestId(static_cast<uint32_t>(i + 1))
            : result.config.testId;

        auto filepath = outputDirectory_ / (filename + ".json");
        exporter.ExportToJSON(filepath, result.config, deviceCapabilities_,
                              result.frames, result.aggregates);
    }

    // Export suite summary
    auto summaryPath = outputDirectory_ / "suite_summary.json";
    suiteResults_.ExportSummary(summaryPath.string());
}

void BenchmarkRunner::ExportTestResults(const TestRunResults& results, const std::string& filename) {
    MetricsExporter exporter;
    auto filepath = outputDirectory_ / filename;
    exporter.ExportToJSON(filepath, results.config, deviceCapabilities_,
                          results.frames, results.aggregates);
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
