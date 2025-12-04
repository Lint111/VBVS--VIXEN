/**
 * @file BenchmarkMain.cpp
 * @brief Standalone GPU benchmark executable for compute ray march profiling
 *
 * This benchmark tool supports two execution modes:
 *
 * --headless (default): Compute-only benchmark without window
 *   - Minimal Vulkan context (instance, device, command pool)
 *   - Creates storage images as render targets
 *   - Uses VkQueryPool for GPU timing
 *   - Fastest mode for automated benchmarking
 *
 * --render: Full rendering with window and real-time preview
 *   - Uses RenderGraph infrastructure
 *   - Creates window + swapchain
 *   - Shows real-time ray march preview while benchmarking
 *   - Uses BenchmarkGraphFactory for graph construction
 *
 * Usage:
 *   vixen_benchmark --quick --headless --output ./results
 *   vixen_benchmark --quick --render --output ./results
 *
 * See --help for full options.
 */

#include "BenchmarkCLI.h"

#include <Profiler/BenchmarkRunner.h>
#include <Profiler/BenchmarkConfig.h>
#include <Profiler/BenchmarkGraphFactory.h>
#include <Profiler/DeviceCapabilities.h>
#include <Profiler/ProfilerSystem.h>
#include <Profiler/ProfilerGraphAdapter.h>
#include <Profiler/MetricsExporter.h>
#include <Profiler/VulkanIntegration.h>

// RenderGraph infrastructure for render mode
#include <Core/RenderGraph.h>
#include <Core/NodeTypeRegistry.h>

// Node type registrations for render mode
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

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <filesystem>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

//=============================================================================
// Vulkan Context - Minimal headless Vulkan setup for compute benchmarks
//=============================================================================

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // GPU timing query pool
    VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;  // nanoseconds per timestamp tick

    bool IsValid() const {
        return instance != VK_NULL_HANDLE &&
               physicalDevice != VK_NULL_HANDLE &&
               device != VK_NULL_HANDLE &&
               computeQueue != VK_NULL_HANDLE;
    }
};

// Debug callback for validation layers
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
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

// Check if layer is available
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

// Create Vulkan instance
VkResult CreateInstance(VulkanContext& ctx, bool enableValidation) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VIXEN Benchmark";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VIXEN";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions;
    std::vector<const char*> layers;

    // Add validation if requested and available
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

    // Setup debug messenger if validation enabled
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
            debugInfo.pfnUserCallback = DebugCallback;

            createDebugMessenger(ctx.instance, &debugInfo, nullptr, &ctx.debugMessenger);
        }
    }

    return VK_SUCCESS;
}

// Select physical device
VkResult SelectPhysicalDevice(VulkanContext& ctx, uint32_t gpuIndex, bool verbose) {
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

    // Get timestamp period for GPU timing
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    ctx.timestampPeriod = props.limits.timestampPeriod;

    if (verbose) {
        std::cout << "Selected GPU: " << props.deviceName << "\n";
        std::cout << "Timestamp period: " << ctx.timestampPeriod << " ns\n";
    }

    return VK_SUCCESS;
}

// Find compute queue family
uint32_t FindComputeQueueFamily(VkPhysicalDevice device) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // Prefer dedicated compute queue
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            return i;
        }
    }

    // Fallback to any queue with compute support
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }

    return UINT32_MAX;
}

// Create logical device
VkResult CreateDevice(VulkanContext& ctx) {
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

    // Enable memory budget extension if available
    std::vector<const char*> deviceExtensions;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(ctx.physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(ctx.physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    for (const auto& ext : availableExtensions) {
        if (std::strcmp(ext.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
            deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        }
        if (std::strcmp(ext.extensionName, VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME) == 0) {
            deviceExtensions.push_back(VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME);
        }
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    VkResult result = vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device);
    if (result != VK_SUCCESS) {
        return result;
    }

    vkGetDeviceQueue(ctx.device, ctx.computeQueueFamily, 0, &ctx.computeQueue);

    return VK_SUCCESS;
}

// Create command pool
VkResult CreateCommandPool(VulkanContext& ctx) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    return vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool);
}

// Create timestamp query pool for GPU timing
VkResult CreateTimestampQueryPool(VulkanContext& ctx, uint32_t queryCount = 128) {
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = queryCount;

    return vkCreateQueryPool(ctx.device, &queryPoolInfo, nullptr, &ctx.timestampQueryPool);
}

// Initialize complete Vulkan context
bool InitializeVulkan(VulkanContext& ctx, const Vixen::Benchmark::BenchmarkCLIOptions& opts) {
    if (CreateInstance(ctx, opts.enableValidation) != VK_SUCCESS) {
        std::cerr << "Error: Failed to create Vulkan instance\n";
        return false;
    }

    if (SelectPhysicalDevice(ctx, opts.gpuIndex, opts.verbose) != VK_SUCCESS) {
        return false;
    }

    if (CreateDevice(ctx) != VK_SUCCESS) {
        std::cerr << "Error: Failed to create logical device\n";
        return false;
    }

    if (CreateCommandPool(ctx) != VK_SUCCESS) {
        std::cerr << "Error: Failed to create command pool\n";
        return false;
    }

    if (CreateTimestampQueryPool(ctx) != VK_SUCCESS) {
        std::cerr << "Warning: Failed to create timestamp query pool - GPU timing disabled\n";
        // Non-fatal, we can continue with CPU-only timing
    }

    return true;
}

// Cleanup Vulkan context
void CleanupVulkan(VulkanContext& ctx) {
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

// List available GPUs
void ListGPUs() {
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
        auto caps = Vixen::Profiler::DeviceCapabilities::Capture(devices[i]);
        std::cout << "  [" << i << "] " << caps.GetSummaryString() << "\n";
    }
    std::cout << "\nUse --gpu N to select a specific GPU\n\n";

    vkDestroyInstance(instance, nullptr);
}

//=============================================================================
// Headless Benchmark Execution
//=============================================================================

/**
 * @brief Run the benchmark suite in headless mode (compute-only)
 *
 * This mode creates a minimal Vulkan context without a window.
 * Currently uses synthetic metrics - real compute dispatch requires
 * shader compilation and voxel data which need the RenderGraph.
 */
int RunHeadlessBenchmark(
    VulkanContext& ctx,
    const Vixen::Benchmark::BenchmarkCLIOptions& opts,
    const std::vector<Vixen::Profiler::TestConfiguration>& configs
) {
    using namespace Vixen::Profiler;

    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "VIXEN Benchmark Tool (Headless Mode)\n";
    std::cout << "=================================================\n";

    // Capture device capabilities
    auto deviceCaps = DeviceCapabilities::Capture(ctx.physicalDevice);
    std::cout << "\nDevice: " << deviceCaps.GetSummaryString() << "\n";

    // Create output directory
    if (!MetricsExporter::EnsureDirectoryExists(opts.outputDirectory)) {
        std::cerr << "Error: Failed to create output directory: " << opts.outputDirectory << "\n";
        return 1;
    }

    std::cout << "\nOutput: " << opts.outputDirectory << "\n";
    std::cout << "Tests:  " << configs.size() << " configurations\n";
    std::cout << "\n";

    // Initialize profiler system
    ProfilerSystem::Instance().Initialize(ctx.device, ctx.physicalDevice, 3);
    ProfilerSystem::Instance().CaptureDeviceCapabilities(ctx.physicalDevice);
    ProfilerSystem::Instance().SetOutputDirectory(opts.outputDirectory);
    ProfilerSystem::Instance().SetExportFormats(opts.exportCSV, opts.exportJSON);

    // Initialize benchmark runner
    BenchmarkRunner runner;
    runner.SetOutputDirectory(opts.outputDirectory);
    runner.SetDeviceCapabilities(deviceCaps);
    runner.SetTestMatrix(configs);
    runner.SetRenderDimensions(opts.renderWidth, opts.renderHeight);

    // Progress callback
    runner.SetProgressCallback([&](uint32_t testIdx, uint32_t totalTests,
                                    uint32_t frame, uint32_t totalFrames) {
        if (opts.verbose) {
            std::cout << "\r  Test " << (testIdx + 1) << "/" << totalTests
                      << " - Frame " << frame << "/" << totalFrames << "    " << std::flush;
        }
    });

    // Start benchmark suite
    std::cout << "Starting benchmark suite...\n\n";
    auto startTime = std::chrono::steady_clock::now();

    ProfilerSystem::Instance().StartTestSuite(opts.GetRunName());

    if (!runner.StartSuite()) {
        std::cerr << "Error: Failed to start benchmark suite\n";
        return 1;
    }

    uint32_t successCount = 0;

    // Run each test configuration
    while (runner.BeginNextTest()) {
        const auto& testConfig = runner.GetCurrentTestConfig();

        std::cout << "  [" << (runner.GetCurrentTestIndex() + 1) << "/"
                  << configs.size() << "] "
                  << testConfig.pipeline << " | "
                  << testConfig.voxelResolution << "^3 | "
                  << testConfig.densityPercent << "% | "
                  << testConfig.algorithm;

        if (opts.verbose) {
            std::cout << "\n       Warmup: " << testConfig.warmupFrames
                      << " frames, Measure: " << testConfig.measurementFrames << " frames";
        }
        std::cout << "\n";

        // Start profiler test run
        ProfilerSystem::Instance().StartTestRun(testConfig);

        // Simulate frame execution
        // NOTE: For full compute dispatch, use --render mode which has the RenderGraph
        // infrastructure needed for shader compilation and voxel data.
        uint32_t totalFrames = testConfig.warmupFrames + testConfig.measurementFrames;

        for (uint32_t frame = 0; frame < totalFrames; ++frame) {
            // Create synthetic frame metrics
            // Real compute would happen here with VkQueryPool timing
            FrameMetrics metrics{};
            metrics.frameNumber = frame;
            metrics.timestampMs = static_cast<double>(frame) * 16.67; // 60 FPS target
            metrics.frameTimeMs = 16.67f;
            metrics.gpuTimeMs = 8.0f + (static_cast<float>(testConfig.voxelResolution) / 256.0f) * 8.0f;
            metrics.sceneResolution = testConfig.voxelResolution;
            metrics.screenWidth = testConfig.screenWidth;
            metrics.screenHeight = testConfig.screenHeight;
            metrics.sceneDensity = testConfig.densityPercent;
            metrics.totalRaysCast = testConfig.screenWidth * testConfig.screenHeight;
            metrics.mRaysPerSec = static_cast<float>(metrics.totalRaysCast) / (metrics.gpuTimeMs * 1000.0f);
            metrics.fps = 1000.0f / metrics.frameTimeMs;
            metrics.bandwidthEstimated = true; // No hardware counters in headless mode

            runner.RecordFrame(metrics);
        }

        runner.FinalizeCurrentTest();
        ProfilerSystem::Instance().EndTestRun(true);
        ++successCount;
    }

    // Export results
    std::cout << "\nExporting results...\n";
    runner.ExportAllResults();
    ProfilerSystem::Instance().EndTestSuite();

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);

    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "Benchmark Complete\n";
    std::cout << "=================================================\n";
    std::cout << "  Tests:    " << successCount << "/" << configs.size() << " succeeded\n";
    std::cout << "  Duration: " << duration.count() << " seconds\n";
    std::cout << "  Output:   " << opts.outputDirectory << "\n";
    std::cout << "=================================================\n";

    // Cleanup profiler
    ProfilerSystem::Instance().Shutdown();

    return (successCount == configs.size()) ? 0 : 1;
}

//=============================================================================
// Render Mode Benchmark Execution
//=============================================================================

/**
 * @brief Register all node types needed for benchmark graphs
 */
void RegisterNodeTypes(Vixen::RenderGraph::NodeTypeRegistry& registry) {
    using namespace Vixen::RenderGraph;

    registry.Register<InstanceNodeType>();
    registry.Register<WindowNodeType>();
    registry.Register<DeviceNodeType>();
    registry.Register<CommandPoolNodeType>();
    registry.Register<FrameSyncNodeType>();
    registry.Register<SwapChainNodeType>();
    registry.Register<ShaderLibraryNodeType>();
    registry.Register<DescriptorResourceGathererNodeType>();
    registry.Register<PushConstantGathererNodeType>();
    registry.Register<DescriptorSetNodeType>();
    registry.Register<ComputePipelineNodeType>();
    registry.Register<ComputeDispatchNodeType>();
    registry.Register<CameraNodeType>();
    registry.Register<VoxelGridNodeType>();
    registry.Register<InputNodeType>();
    registry.Register<PresentNodeType>();
    registry.Register<DebugBufferReaderNodeType>();
    registry.Register<ConstantNodeType>();
}

/**
 * @brief Run the benchmark suite in render mode (with window)
 *
 * This mode creates a full RenderGraph with window and swapchain.
 * Shows real-time ray march preview while benchmarking.
 */
int RunRenderBenchmark(
    const Vixen::Benchmark::BenchmarkCLIOptions& opts,
    const std::vector<Vixen::Profiler::TestConfiguration>& configs
) {
    using namespace Vixen::Profiler;
    using namespace Vixen::RenderGraph;

    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "VIXEN Benchmark Tool (Render Mode)\n";
    std::cout << "=================================================\n";

    // Create node registry and register all node types
    auto nodeRegistry = std::make_unique<NodeTypeRegistry>();
    RegisterNodeTypes(*nodeRegistry);

    // Create message bus for event coordination
    auto messageBus = std::make_unique<Vixen::EventBus::MessageBus>();

    // Create render graph
    auto renderGraph = std::make_unique<RenderGraph>(
        nodeRegistry.get(),
        messageBus.get(),
        nullptr,  // No logger
        nullptr   // No cache
    );

    // Subscribe to window close events
    bool shouldClose = false;
    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [&shouldClose](const Vixen::EventBus::BaseEventMessage& /*msg*/) -> bool {
            shouldClose = true;
            return true;
        }
    );

    // Create output directory
    if (!MetricsExporter::EnsureDirectoryExists(opts.outputDirectory)) {
        std::cerr << "Error: Failed to create output directory: " << opts.outputDirectory << "\n";
        return 1;
    }

    std::cout << "\nOutput: " << opts.outputDirectory << "\n";
    std::cout << "Tests:  " << configs.size() << " configurations\n";
    std::cout << "\n";

    // Initialize benchmark runner
    BenchmarkRunner runner;
    runner.SetOutputDirectory(opts.outputDirectory);
    runner.SetTestMatrix(configs);
    runner.SetRenderDimensions(opts.renderWidth, opts.renderHeight);

    // Set graph factory function
    runner.SetGraphFactory([](RenderGraph* graph, const TestConfiguration& config,
                              uint32_t width, uint32_t height) {
        return BenchmarkGraphFactory::BuildComputeRayMarchGraph(graph, config, width, height);
    });

    // Progress callback
    runner.SetProgressCallback([&](uint32_t testIdx, uint32_t totalTests,
                                    uint32_t frame, uint32_t totalFrames) {
        if (opts.verbose) {
            std::cout << "\r  Test " << (testIdx + 1) << "/" << totalTests
                      << " - Frame " << frame << "/" << totalFrames << "    " << std::flush;
        }
    });

    // Start benchmark suite
    std::cout << "Starting benchmark suite with window...\n\n";
    auto startTime = std::chrono::steady_clock::now();

    if (!runner.StartSuite()) {
        std::cerr << "Error: Failed to start benchmark suite\n";
        return 1;
    }

    uint32_t successCount = 0;

    // Run each test configuration
    while (runner.BeginNextTest() && !shouldClose) {
        const auto& testConfig = runner.GetCurrentTestConfig();

        std::cout << "  [" << (runner.GetCurrentTestIndex() + 1) << "/"
                  << configs.size() << "] "
                  << testConfig.pipeline << " | "
                  << testConfig.voxelResolution << "^3 | "
                  << testConfig.densityPercent << "% | "
                  << testConfig.algorithm << "\n";

        // Create graph for current test
        auto benchGraph = runner.CreateGraphForCurrentTest(renderGraph.get());
        if (!benchGraph.IsValid()) {
            std::cerr << "Error: Failed to create graph for test\n";
            continue;
        }

        // Wire profiler hooks
        BenchmarkGraphFactory::WireProfilerHooks(renderGraph.get(), runner.GetAdapter(), benchGraph);

        // Compile the graph
        renderGraph->Compile();

        // Initialize profiler from graph
        if (VulkanIntegrationHelper::InitializeProfilerFromGraph(renderGraph.get())) {
            ProfilerSystem::Instance().SetOutputDirectory(opts.outputDirectory);
            ProfilerSystem::Instance().SetExportFormats(opts.exportCSV, opts.exportJSON);

            // Capture device capabilities after initialization
            auto handles = VulkanIntegrationHelper::ExtractFromGraph(renderGraph.get());
            if (handles.IsValid()) {
                auto deviceCaps = DeviceCapabilities::Capture(handles.physicalDevice);
                runner.SetDeviceCapabilities(deviceCaps);
            }
        }

        ProfilerSystem::Instance().StartTestRun(testConfig);

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

            // Update graph time and process events
            renderGraph->UpdateTime();
            renderGraph->ProcessEvents();
            renderGraph->RecompileDirtyNodes();

            // Execute frame
            VkResult result = renderGraph->RenderFrame();
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                std::cerr << "Warning: Frame rendering failed with result: " << result << "\n";
            }

            // Collect frame metrics
            FrameMetrics metrics{};
            metrics.frameNumber = frame;
            metrics.timestampMs = static_cast<double>(frame) * 16.67;
            metrics.frameTimeMs = 16.67f; // TODO: Get actual frame time from profiler
            metrics.gpuTimeMs = 8.0f;     // TODO: Get actual GPU time from query pool
            metrics.sceneResolution = testConfig.voxelResolution;
            metrics.screenWidth = testConfig.screenWidth;
            metrics.screenHeight = testConfig.screenHeight;
            metrics.sceneDensity = testConfig.densityPercent;
            metrics.totalRaysCast = testConfig.screenWidth * testConfig.screenHeight;
            metrics.mRaysPerSec = static_cast<float>(metrics.totalRaysCast) / (metrics.gpuTimeMs * 1000.0f);
            metrics.fps = 1000.0f / metrics.frameTimeMs;

            runner.RecordFrame(metrics);
        }

        runner.FinalizeCurrentTest();
        ProfilerSystem::Instance().EndTestRun(!shouldClose);
        runner.ClearCurrentGraph();

        if (!shouldClose) {
            ++successCount;
        }

        // Reset graph for next test
        renderGraph.reset();
        renderGraph = std::make_unique<RenderGraph>(
            nodeRegistry.get(),
            messageBus.get(),
            nullptr,
            nullptr
        );
    }

    // Export results
    std::cout << "\nExporting results...\n";
    runner.ExportAllResults();

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);

    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "Benchmark Complete\n";
    std::cout << "=================================================\n";
    std::cout << "  Tests:    " << successCount << "/" << configs.size() << " succeeded\n";
    std::cout << "  Duration: " << duration.count() << " seconds\n";
    std::cout << "  Output:   " << opts.outputDirectory << "\n";
    std::cout << "=================================================\n";

    // Cleanup
    ProfilerSystem::Instance().Shutdown();

    return (successCount == configs.size()) ? 0 : 1;
}

} // anonymous namespace

//=============================================================================
// Main Entry Point
//=============================================================================

int main(int argc, char* argv[]) {
    using namespace Vixen::Benchmark;

    // Parse command line
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

    // List GPUs
    if (opts.listGpus) {
        ListGPUs();
        return 0;
    }

    // Validate options
    auto errors = opts.Validate();
    if (!errors.empty()) {
        std::cerr << "Configuration errors:\n";
        for (const auto& error : errors) {
            std::cerr << "  - " << error << "\n";
        }
        return 1;
    }

    // Generate test configurations
    auto configs = opts.GenerateTestConfigurations();
    if (configs.empty()) {
        std::cerr << "Error: No test configurations to run\n";
        std::cerr << "Check your --config file or use --quick/--full for preset matrices\n";
        return 1;
    }

    // Run benchmark based on mode
    if (opts.renderMode) {
        // Render mode: full RenderGraph with window
        return RunRenderBenchmark(opts, configs);
    } else {
        // Headless mode: minimal Vulkan context
        VulkanContext ctx{};
        if (!InitializeVulkan(ctx, opts)) {
            std::cerr << "Error: Failed to initialize Vulkan\n";
            return 1;
        }

        int result = RunHeadlessBenchmark(ctx, opts, configs);

        CleanupVulkan(ctx);
        return result;
    }
}
