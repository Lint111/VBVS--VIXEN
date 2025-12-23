#include "Profiler/BenchmarkGraphFactory.h"

// RenderGraph core
#include <Core/RenderGraph.h>
#include <Core/TypedConnection.h>
#include <Core/NodeInstance.h>
#include <Core/GraphLifecycleHooks.h>
#include <Data/Core/ResourceConfig.h>  // SlotRole

// Shader management
#include <ShaderBundleBuilder.h>

// Node types
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
// Fragment pipeline node types
#include <Nodes/RenderPassNode.h>
#include <Nodes/FramebufferNode.h>
#include <Nodes/GraphicsPipelineNode.h>
#include <Nodes/GeometryRenderNode.h>

// Hardware ray tracing node types (Phase K)
#include <Nodes/VoxelAABBConverterNode.h>
#include <Nodes/AccelerationStructureNode.h>
#include <Nodes/RayTracingPipelineNode.h>
#include <Nodes/TraceRaysNode.h>

// Node configs
#include <Data/Nodes/InstanceNodeConfig.h>
#include <Data/Nodes/WindowNodeConfig.h>
#include <Data/Nodes/DeviceNodeConfig.h>
#include <Data/Nodes/SwapChainNodeConfig.h>
#include <Data/Nodes/CommandPoolNodeConfig.h>
#include <Data/Nodes/FrameSyncNodeConfig.h>
#include <Data/Nodes/ShaderLibraryNodeConfig.h>
#include <Data/Nodes/DescriptorResourceGathererNodeConfig.h>
#include <Data/Nodes/PushConstantGathererNodeConfig.h>
#include <Data/Nodes/DescriptorSetNodeConfig.h>
#include <Data/Nodes/ComputePipelineNodeConfig.h>
#include <Data/Nodes/ComputeDispatchNodeConfig.h>
#include <Data/Nodes/CameraNodeConfig.h>
#include <Data/Nodes/VoxelGridNodeConfig.h>
#include <Data/Nodes/InputNodeConfig.h>
#include <Data/Nodes/PresentNodeConfig.h>
#include <Data/Nodes/DebugBufferReaderNodeConfig.h>
// Fragment pipeline node configs
#include <Data/Nodes/RenderPassNodeConfig.h>
#include <Data/Nodes/FramebufferNodeConfig.h>
#include <Data/Nodes/GraphicsPipelineNodeConfig.h>
#include <Data/Nodes/GeometryRenderNodeConfig.h>

// Hardware ray tracing node configs (Phase K)
#include <Data/Nodes/VoxelAABBConverterNodeConfig.h>
#include <Data/Nodes/AccelerationStructureNodeConfig.h>
#include <Data/Nodes/RayTracingPipelineNodeConfig.h>
#include <Data/Nodes/TraceRaysNodeConfig.h>

// Data types
#include <Data/CameraData.h>
#include <Data/InputState.h>

// SDI (Shader-Driven Interface) generated headers for type-safe binding references
#include "VoxelRTNames.h"

#include <filesystem>
#include <stdexcept>
#include <unordered_set>

// Use namespace alias to avoid RenderGraph class/namespace ambiguity
namespace RG = Vixen::RenderGraph;

// Track which graphs have profiler hooks wired (for HasProfilerHooks)
namespace {
    std::unordered_set<const RG::RenderGraph*> g_graphsWithProfilerHooks;
}

namespace Vixen::Profiler {

//==============================================================================
// Subgraph Builders
//==============================================================================

InfrastructureNodes BenchmarkGraphFactory::BuildInfrastructure(
    RG::RenderGraph* graph,
    uint32_t width,
    uint32_t height,
    bool enableValidation,
    uint32_t gpuIndex)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildInfrastructure: graph is null");
    }

    InfrastructureNodes nodes{};

    // Create infrastructure nodes
    nodes.instance = graph->AddNode<RG::InstanceNodeType>("benchmark_instance");
    nodes.window = graph->AddNode<RG::WindowNodeType>("benchmark_window");
    nodes.device = graph->AddNode<RG::DeviceNodeType>("benchmark_device");
    nodes.swapchain = graph->AddNode<RG::SwapChainNodeType>("benchmark_swapchain");
    nodes.commandPool = graph->AddNode<RG::CommandPoolNodeType>("benchmark_cmd_pool");
    nodes.frameSync = graph->AddNode<RG::FrameSyncNodeType>("benchmark_frame_sync");

    // Configure parameters
    ConfigureInfrastructureParams(graph, nodes, width, height, enableValidation, gpuIndex);

    // Enable critical infrastructure node logging for debugging initialization failures
    // This ensures device/instance creation errors are visible in tester builds
    if (auto* instanceNode = static_cast<RG::InstanceNode*>(graph->GetInstance(nodes.instance))) {
        auto logger = instanceNode->GetLogger();
        if (logger) {
            logger->SetEnabled(true);
            logger->SetTerminalOutput(true);
        }
    }
    if (auto* deviceNode = static_cast<RG::DeviceNode*>(graph->GetInstance(nodes.device))) {
        auto logger = deviceNode->GetLogger();
        if (logger) {
            logger->SetEnabled(true);
            logger->SetTerminalOutput(true);
        }
    }
    if (auto* commandPoolNode = static_cast<RG::CommandPoolNode*>(graph->GetInstance(nodes.commandPool))) {
        auto logger = commandPoolNode->GetLogger();
        if (logger) {
            logger->SetEnabled(true);
            logger->SetTerminalOutput(true);
        }
    }

    return nodes;
}

ComputePipelineNodes BenchmarkGraphFactory::BuildComputePipeline(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const std::string& shaderPath,
    uint32_t workgroupSizeX,
    uint32_t workgroupSizeY)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildComputePipeline: graph is null");
    }

    if (!infra.IsValid()) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildComputePipeline: infrastructure nodes invalid");
    }

    ComputePipelineNodes nodes{};

    // Create compute pipeline nodes
    nodes.shaderLib = graph->AddNode<RG::ShaderLibraryNodeType>("benchmark_shader_lib");
    nodes.descriptorGatherer = graph->AddNode<RG::DescriptorResourceGathererNodeType>("benchmark_desc_gatherer");
    nodes.pushConstantGatherer = graph->AddNode<RG::PushConstantGathererNodeType>("benchmark_pc_gatherer");
    nodes.descriptorSet = graph->AddNode<RG::DescriptorSetNodeType>("benchmark_descriptors");
    nodes.pipeline = graph->AddNode<RG::ComputePipelineNodeType>("benchmark_pipeline");
    nodes.dispatch = graph->AddNode<RG::ComputeDispatchNodeType>("benchmark_dispatch");

    // Configure parameters
    ConfigureComputePipelineParams(graph, nodes, infra, shaderPath, workgroupSizeX, workgroupSizeY);

    return nodes;
}

RayMarchNodes BenchmarkGraphFactory::BuildRayMarchScene(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const SceneInfo& scene)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildRayMarchScene: graph is null");
    }

    if (!infra.IsValid()) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildRayMarchScene: infrastructure nodes invalid");
    }

    RayMarchNodes nodes{};

    // Create ray march scene nodes
    nodes.camera = graph->AddNode<RG::CameraNodeType>("benchmark_camera");
    nodes.voxelGrid = graph->AddNode<RG::VoxelGridNodeType>("benchmark_voxel_grid");
    nodes.input = graph->AddNode<RG::InputNodeType>("benchmark_input");

    // Configure parameters
    ConfigureRayMarchSceneParams(graph, nodes, scene);

    return nodes;
}

OutputNodes BenchmarkGraphFactory::BuildOutput(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    bool enableDebugCapture)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildOutput: graph is null");
    }

    if (!infra.IsValid()) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildOutput: infrastructure nodes invalid");
    }

    OutputNodes nodes{};

    // Create output nodes
    nodes.present = graph->AddNode<RG::PresentNodeType>("benchmark_present");

    if (enableDebugCapture) {
        nodes.debugCapture = graph->AddNode<RG::DebugBufferReaderNodeType>("benchmark_debug_capture");
    }

    // Configure parameters
    ConfigureOutputParams(graph, nodes, enableDebugCapture);

    return nodes;
}

//==============================================================================
// Connection Builders
//==============================================================================

void BenchmarkGraphFactory::ConnectComputeRayMarch(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const ComputePipelineNodes& compute,
    const RayMarchNodes& rayMarch,
    const OutputNodes& output)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::ConnectComputeRayMarch: graph is null");
    }

    RG::ConnectionBatch batch(graph);

    //--------------------------------------------------------------------------
    // Infrastructure Connections
    //--------------------------------------------------------------------------

    // Instance -> Device
    batch.Connect(infra.instance, RG::InstanceNodeConfig::INSTANCE,
                  infra.device, RG::DeviceNodeConfig::INSTANCE_IN);

    // Device -> Window (VkInstance passthrough)
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.window, RG::WindowNodeConfig::INSTANCE);

    // Window -> SwapChain
    batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HWND)
         .Connect(infra.window, RG::WindowNodeConfig::HINSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HINSTANCE)
         .Connect(infra.window, RG::WindowNodeConfig::WIDTH_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::WIDTH)
         .Connect(infra.window, RG::WindowNodeConfig::HEIGHT_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HEIGHT);

    // Window -> Input
    if (rayMarch.input.IsValid()) {
        batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                      rayMarch.input, RG::InputNodeConfig::HWND_IN);
    }

    // Device -> SwapChain
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::INSTANCE)
         .Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // Device -> FrameSync
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.frameSync, RG::FrameSyncNodeConfig::VULKAN_DEVICE);

    // FrameSync -> SwapChain
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  infra.swapchain, RG::SwapChainNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::PRESENT_FENCES_ARRAY);

    // Device -> CommandPool
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.commandPool, RG::CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    //--------------------------------------------------------------------------
    // Compute Pipeline Connections
    //--------------------------------------------------------------------------

    // Device -> Shader Library
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  compute.shaderLib, RG::ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // Device -> Descriptor Set
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  compute.descriptorSet, RG::DescriptorSetNodeConfig::VULKAN_DEVICE_IN);

    // Device -> Compute Pipeline
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  compute.pipeline, RG::ComputePipelineNodeConfig::VULKAN_DEVICE_IN);

    // Shader -> Descriptor Gatherer
    batch.Connect(compute.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  compute.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);

    // Gatherer -> Descriptor Set
    batch.Connect(compute.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  compute.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);

    // Shader -> Push Constant Gatherer
    batch.Connect(compute.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  compute.pushConstantGatherer, RG::PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);

    // Push Constant Gatherer -> Dispatch
    batch.Connect(compute.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(compute.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::PUSH_CONSTANT_RANGES);

    // Shader -> Descriptor Set, Pipeline, Dispatch
    batch.Connect(compute.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  compute.descriptorSet, RG::DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(compute.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  compute.pipeline, RG::ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(compute.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE);

    // Descriptor Set Layout -> Pipeline
    batch.Connect(compute.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  compute.pipeline, RG::ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // Device -> Dispatch
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::VULKAN_DEVICE_IN);

    // Pipeline -> Dispatch
    batch.Connect(compute.pipeline, RG::ComputePipelineNodeConfig::PIPELINE,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::COMPUTE_PIPELINE)
         .Connect(compute.pipeline, RG::ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::PIPELINE_LAYOUT);

    // Descriptor Set -> Dispatch
    batch.Connect(compute.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::DESCRIPTOR_SETS);

    // CommandPool -> Dispatch
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::COMMAND_POOL);

    //--------------------------------------------------------------------------
    // Ray March Scene Connections
    //--------------------------------------------------------------------------

    // Device -> Camera
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.camera, RG::CameraNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Camera
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  rayMarch.camera, RG::CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  rayMarch.camera, RG::CameraNodeConfig::IMAGE_INDEX);

    // Input -> Camera
    if (rayMarch.input.IsValid()) {
        batch.Connect(rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
                      rayMarch.camera, RG::CameraNodeConfig::INPUT_STATE);
    }

    // Device -> VoxelGrid
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> VoxelGrid
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMMAND_POOL);

    //--------------------------------------------------------------------------
    // SwapChain/Sync -> Dispatch Connections
    //--------------------------------------------------------------------------

    // SwapChain -> Descriptor Set (image count metadata)
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  compute.descriptorSet, RG::DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT,
                  &SwapChainPublicVariables::swapChainImageCount);

    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  compute.descriptorSet, RG::DescriptorSetNodeConfig::IMAGE_INDEX);

    // SwapChain -> Dispatch
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::SWAPCHAIN_INFO)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::IMAGE_INDEX);

    // FrameSync -> Dispatch
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::IN_FLIGHT_FENCE)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  compute.dispatch, RG::ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    //--------------------------------------------------------------------------
    // Output Connections
    //--------------------------------------------------------------------------

    // Device -> Present
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  output.present, RG::PresentNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Present
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  output.present, RG::PresentNodeConfig::SWAPCHAIN)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  output.present, RG::PresentNodeConfig::IMAGE_INDEX);

    // Dispatch -> Present (render complete semaphore)
    batch.Connect(compute.dispatch, RG::ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  output.present, RG::PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);

    // FrameSync -> Present (present fences)
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  output.present, RG::PresentNodeConfig::PRESENT_FENCE_ARRAY);

    //--------------------------------------------------------------------------
    // Debug Capture Connections (if enabled)
    //--------------------------------------------------------------------------

    if (output.debugCapture.IsValid()) {
        batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      output.debugCapture, RG::DebugBufferReaderNodeConfig::VULKAN_DEVICE_IN);
        batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                      output.debugCapture, RG::DebugBufferReaderNodeConfig::COMMAND_POOL);
        batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                      output.debugCapture, RG::DebugBufferReaderNodeConfig::IN_FLIGHT_FENCE);

        // Gatherer -> Dispatch -> Debug (passthrough chain)
        batch.Connect(compute.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::DEBUG_CAPTURE,
                      compute.dispatch, RG::ComputeDispatchNodeConfig::DEBUG_CAPTURE);
        batch.Connect(compute.dispatch, RG::ComputeDispatchNodeConfig::DEBUG_CAPTURE_OUT,
                      output.debugCapture, RG::DebugBufferReaderNodeConfig::DEBUG_CAPTURE);
    }

    // Register all connections atomically
    batch.RegisterAll();
}

//==============================================================================
// High-Level Graph Builders
//==============================================================================

BenchmarkGraph BenchmarkGraphFactory::BuildFromConfig(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    const BenchmarkSuiteConfig* suiteConfig)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildFromConfig: graph is null");
    }

    // Parse pipeline type
    PipelineType pipelineType = ParsePipelineType(config.pipeline);

    // Get scene definition if available from suite config
    const SceneDefinition* sceneDef = nullptr;
    if (suiteConfig && suiteConfig->sceneDefinitions.count(config.sceneType) > 0) {
        sceneDef = &suiteConfig->sceneDefinitions.at(config.sceneType);
    }

    // Build appropriate graph based on pipeline type
    switch (pipelineType) {
        case PipelineType::Compute: {
            // Build compute ray march graph with config dimensions
            auto benchGraph = BuildComputeRayMarchGraph(
                graph, config, config.screenWidth, config.screenHeight, suiteConfig);

            // Register shader from config - shader name is used directly as filename
            RegisterComputeShader(graph, benchGraph.compute, config.shader);

            // TODO: If sceneDef is File type, configure VoxelGridNode to load from file
            // This requires extending VoxelGridNode to support file loading
            if (sceneDef && sceneDef->sourceType == SceneSourceType::File) {
                // Future: voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_FILE_PATH, sceneDef->filePath);
                // For now, file loading is not implemented - fall back to procedural
            }

            return benchGraph;
        }

        case PipelineType::Fragment: {
            // Build fragment ray march graph
            auto benchGraph = BuildFragmentRayMarchGraph(
                graph, config, config.screenWidth, config.screenHeight, suiteConfig);

            // TODO: Register fragment shader variant if needed
            // The fragment pipeline uses different shader registration

            return benchGraph;
        }

        case PipelineType::HardwareRT:
            return BuildHardwareRTGraph(graph, config, config.screenWidth, config.screenHeight, suiteConfig);

        case PipelineType::Hybrid:
            throw std::invalid_argument(
                "BenchmarkGraphFactory::BuildFromConfig: Hybrid pipeline not yet implemented");

        default:
            throw std::invalid_argument(
                "BenchmarkGraphFactory::BuildFromConfig: Invalid pipeline type '" +
                config.pipeline + "'. Valid options: compute, fragment, hardware_rt");
    }
}

BenchmarkGraph BenchmarkGraphFactory::BuildComputeRayMarchGraph(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    uint32_t width,
    uint32_t height,
    const BenchmarkSuiteConfig* suiteConfig)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildComputeRayMarchGraph: graph is null");
    }

    BenchmarkGraph result{};
    result.pipelineType = PipelineType::Compute;

    // Create scene info from config
    // Density is 0 - will be computed from actual scene data
    SceneInfo scene = SceneInfo::FromResolutionAndDensity(
        config.voxelResolution,
        0.0f,  // Density computed from scene data, not config
        MapSceneType(config.sceneType),
        config.testId
    );

    // Build all subgraphs
    uint32_t gpuIndex = (suiteConfig ? suiteConfig->gpuIndex : 0);
    result.infra = BuildInfrastructure(graph, width, height, true, gpuIndex);
    result.compute = BuildComputePipeline(graph, result.infra, config.shader);
    result.rayMarch = BuildRayMarchScene(graph, result.infra, scene);
    result.output = BuildOutput(graph, result.infra, false);

    // Connect subgraphs (typed slot connections)
    ConnectComputeRayMarch(graph, result.infra, result.compute, result.rayMarch, result.output);

    // Wire variadic resources (descriptor bindings + push constants)
    WireVariadicResources(graph, result.infra, result.compute, result.rayMarch);

    // Register shader builder (uses config.shader as filename)
    RegisterComputeShader(graph, result.compute, config.shader);

    return result;
}

//==============================================================================
// Internal Helpers - Parameter Configuration
//==============================================================================

void BenchmarkGraphFactory::ConfigureInfrastructureParams(
    RG::RenderGraph* graph,
    const InfrastructureNodes& nodes,
    uint32_t width,
    uint32_t height,
    bool /*enableValidation*/,
    uint32_t gpuIndex)
{
    // Window parameters
    auto* window = static_cast<RG::WindowNode*>(graph->GetInstance(nodes.window));
    if (window) {
        window->SetParameter(RG::WindowNodeConfig::PARAM_WIDTH, width);
        window->SetParameter(RG::WindowNodeConfig::PARAM_HEIGHT, height);
    }

    // Device parameters - use configured GPU index
    auto* device = static_cast<RG::DeviceNode*>(graph->GetInstance(nodes.device));
    if (device) {
        device->SetParameter(RG::DeviceNodeConfig::PARAM_GPU_INDEX, gpuIndex);
    }

    // Note: InstanceNode validation is configured via builder function registration
    // FrameSyncNode uses defaults (MAX_FRAMES_IN_FLIGHT = 4)
}

void BenchmarkGraphFactory::ConfigureComputePipelineParams(
    RG::RenderGraph* graph,
    const ComputePipelineNodes& nodes,
    const InfrastructureNodes& /*infra*/,
    const std::string& /*shaderPath*/,
    uint32_t /*workgroupSizeX*/,
    uint32_t /*workgroupSizeY*/)
{
    // Dispatch parameters
    auto* dispatch = static_cast<RG::ComputeDispatchNode*>(graph->GetInstance(nodes.dispatch));
    if (dispatch) {
        // Default 800x600 / 8 = 100x75 workgroups
        uint32_t dispatchX = 100;  // Will be recalculated during compile
        uint32_t dispatchY = 75;
        dispatch->SetParameter(RG::ComputeDispatchNodeConfig::DISPATCH_X, dispatchX);
        dispatch->SetParameter(RG::ComputeDispatchNodeConfig::DISPATCH_Y, dispatchY);
        dispatch->SetParameter(RG::ComputeDispatchNodeConfig::DISPATCH_Z, 1u);
    }

    // Shader builder registration would be done by the application code
    // since it requires access to ShaderBundleBuilder and file paths
}

void BenchmarkGraphFactory::ConfigureRayMarchSceneParams(
    RG::RenderGraph* graph,
    const RayMarchNodes& nodes,
    const SceneInfo& scene)
{
    // Camera parameters
    auto* camera = static_cast<RG::CameraNode*>(graph->GetInstance(nodes.camera));
    if (camera) {
        camera->SetParameter(RG::CameraNodeConfig::PARAM_FOV, 45.0f);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_FAR_PLANE, 500.0f);

        // Cornell box camera position (grid spans [0, resolution])
        float center = static_cast<float>(scene.resolution) / 2.0f;
        camera->SetParameter(RG::CameraNodeConfig::PARAM_CAMERA_X, center);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_CAMERA_Y, center);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_CAMERA_Z, static_cast<float>(scene.resolution) * 2.5f);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_YAW, 0.0f);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_PITCH, 0.0f);
        camera->SetParameter(RG::CameraNodeConfig::PARAM_GRID_RESOLUTION, scene.resolution);
    }

    // Voxel grid parameters
    auto* voxelGrid = static_cast<RG::VoxelGridNode*>(graph->GetInstance(nodes.voxelGrid));
    if (voxelGrid) {
        voxelGrid->SetParameter(RG::VoxelGridNodeConfig::PARAM_RESOLUTION, scene.resolution);
        voxelGrid->SetParameter(RG::VoxelGridNodeConfig::PARAM_SCENE_TYPE, scene.sceneType);
    }

    // Input parameters: Disable mouse capture for benchmarks (headless mode)
    auto* input = static_cast<RG::InputNode*>(graph->GetInstance(nodes.input));
    if (input) {
        // Keep keyboard polling enabled for manual frame capture ('C' key)
        input->SetParameter(RG::InputNodeConfig::PARAM_ENABLED, true);
        // Disable mouse capture - not needed for benchmark automated runs
        input->SetParameter(RG::InputNodeConfig::PARAM_MOUSE_CAPTURE_MODE,
                            static_cast<int>(RG::MouseCaptureMode::Disabled));
    }
}

void BenchmarkGraphFactory::ConfigureOutputParams(
    RG::RenderGraph* graph,
    const OutputNodes& nodes,
    bool enableDebugCapture)
{
    // Present parameters
    auto* present = static_cast<RG::PresentNode*>(graph->GetInstance(nodes.present));
    if (present) {
        present->SetParameter(RG::PresentNodeConfig::WAIT_FOR_IDLE, true);
    }

    // Debug capture parameters
    if (enableDebugCapture && nodes.debugCapture.IsValid()) {
        auto* debugCapture = static_cast<RG::DebugBufferReaderNode*>(
            graph->GetInstance(nodes.debugCapture));
        if (debugCapture) {
            debugCapture->SetParameter(RG::DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, 1000u);
            debugCapture->SetParameter(RG::DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, false);
        }
    }
}

std::string BenchmarkGraphFactory::MapSceneType(const std::string& sceneType)
{
    // Scene type names in config match SceneGeneratorFactory names directly
    // Just handle legacy aliases
    if (sceneType == "cornell_box") {
        return "cornell";
    }
    // Pass through directly - VoxelGridNode handles unknown types with fallback
    return sceneType;
}

void BenchmarkGraphFactory::ConfigureFragmentPipelineParams(
    RG::RenderGraph* graph,
    const FragmentPipelineNodes& nodes,
    const InfrastructureNodes& /*infra*/,
    const std::string& /*vertexShaderPath*/,
    const std::string& /*fragmentShaderPath*/)
{
    // RenderPass parameters
    auto* renderPass = static_cast<RG::RenderPassNode*>(graph->GetInstance(nodes.renderPass));
    if (renderPass) {
        // Use AttachmentLoadOp/StoreOp enums from RenderPassNodeConfig
        // Color: Clear on load, store result
        // Note: Parameters are set via SetParameter with string names
    }

    // GraphicsPipeline parameters
    auto* pipeline = static_cast<RG::GraphicsPipelineNode*>(graph->GetInstance(nodes.pipeline));
    if (pipeline) {
        pipeline->SetParameter(RG::GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, false);
        pipeline->SetParameter(RG::GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, false);
        pipeline->SetParameter(RG::GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, false);  // Full-screen triangle
        pipeline->SetParameter(RG::GraphicsPipelineNodeConfig::CULL_MODE, std::string("None"));
        pipeline->SetParameter(RG::GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
    }

    // GeometryRenderNode parameters for fullscreen triangle
    auto* drawCommand = static_cast<RG::GeometryRenderNode*>(graph->GetInstance(nodes.drawCommand));
    if (drawCommand) {
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::VERTEX_COUNT, static_cast<uint32_t>(3));
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::INSTANCE_COUNT, static_cast<uint32_t>(1));
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::FIRST_VERTEX, static_cast<uint32_t>(0));
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::FIRST_INSTANCE, static_cast<uint32_t>(0));
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::USE_INDEX_BUFFER, false);
        // Clear color (dark gray background)
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::CLEAR_COLOR_R, 0.1f);
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::CLEAR_COLOR_G, 0.1f);
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::CLEAR_COLOR_B, 0.1f);
        drawCommand->SetParameter(RG::GeometryRenderNodeConfig::CLEAR_COLOR_A, 1.0f);
    }

    // Shader builder registration would be done by the application code
    // since it requires access to ShaderBundleBuilder and file paths
}

//==============================================================================
// Fragment Pipeline Builders
//==============================================================================

FragmentPipelineNodes BenchmarkGraphFactory::BuildFragmentPipeline(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const std::string& vertexShaderPath,
    const std::string& fragmentShaderPath)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildFragmentPipeline: graph is null");
    }

    if (!infra.IsValid()) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildFragmentPipeline: infrastructure nodes invalid");
    }

    FragmentPipelineNodes nodes{};

    // Create fragment pipeline nodes
    // TODO: Migrate to LOG_DEBUG when BenchmarkGraphFactory inherits from ILoggable
    // LOG_DEBUG("  [FragPipe] Creating shaderLib...");
    nodes.shaderLib = graph->AddNode<RG::ShaderLibraryNodeType>("benchmark_fragment_shader_lib");
    // LOG_DEBUG("  [FragPipe] Creating descriptorGatherer...");
    nodes.descriptorGatherer = graph->AddNode<RG::DescriptorResourceGathererNodeType>("benchmark_fragment_desc_gatherer");
    // LOG_DEBUG("  [FragPipe] Creating pushConstantGatherer...");
    nodes.pushConstantGatherer = graph->AddNode<RG::PushConstantGathererNodeType>("benchmark_fragment_pc_gatherer");
    // LOG_DEBUG("  [FragPipe] Creating descriptorSet...");
    nodes.descriptorSet = graph->AddNode<RG::DescriptorSetNodeType>("benchmark_fragment_descriptors");
    // LOG_DEBUG("  [FragPipe] Creating renderPass...");
    // LOG_DEBUG("  [FragPipe] Graph ptr: " + std::to_string(reinterpret_cast<uintptr_t>(graph)));
    nodes.renderPass = graph->AddNode<RG::RenderPassNodeType>("benchmark_render_pass");
    // LOG_DEBUG("  [FragPipe] RenderPass created!");
    // LOG_DEBUG("  [FragPipe] RenderPass created. Creating framebuffer...");
    nodes.framebuffer = graph->AddNode<RG::FramebufferNodeType>("benchmark_framebuffer");
    // LOG_DEBUG("  [FragPipe] Creating graphics pipeline...");
    nodes.pipeline = graph->AddNode<RG::GraphicsPipelineNodeType>("benchmark_graphics_pipeline");
    // LOG_DEBUG("  [FragPipe] Creating drawCommand (GeometryRenderNode)...");
    nodes.drawCommand = graph->AddNode<RG::GeometryRenderNodeType>("benchmark_draw_command");

    // LOG_DEBUG("  [FragPipe] Configuring parameters...");
    // Configure parameters
    ConfigureFragmentPipelineParams(graph, nodes, infra, vertexShaderPath, fragmentShaderPath);
    // LOG_DEBUG("  [FragPipe] Done!");

    return nodes;
}

void BenchmarkGraphFactory::ConnectFragmentRayMarch(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const FragmentPipelineNodes& fragment,
    const RayMarchNodes& rayMarch,
    const OutputNodes& output)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::ConnectFragmentRayMarch: graph is null");
    }

    RG::ConnectionBatch batch(graph);

    //--------------------------------------------------------------------------
    // Infrastructure Connections (same as compute pipeline)
    //--------------------------------------------------------------------------

    // Instance -> Device
    batch.Connect(infra.instance, RG::InstanceNodeConfig::INSTANCE,
                  infra.device, RG::DeviceNodeConfig::INSTANCE_IN);

    // Device -> Window (VkInstance passthrough)
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.window, RG::WindowNodeConfig::INSTANCE);

    // Window -> SwapChain
    batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HWND)
         .Connect(infra.window, RG::WindowNodeConfig::HINSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HINSTANCE)
         .Connect(infra.window, RG::WindowNodeConfig::WIDTH_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::WIDTH)
         .Connect(infra.window, RG::WindowNodeConfig::HEIGHT_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HEIGHT);

    // Window -> Input
    if (rayMarch.input.IsValid()) {
        batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                      rayMarch.input, RG::InputNodeConfig::HWND_IN);
    }

    // Device -> SwapChain
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::INSTANCE)
         .Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // Device -> FrameSync
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.frameSync, RG::FrameSyncNodeConfig::VULKAN_DEVICE);

    // FrameSync -> SwapChain
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  infra.swapchain, RG::SwapChainNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::PRESENT_FENCES_ARRAY);

    // Device -> CommandPool
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.commandPool, RG::CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    //--------------------------------------------------------------------------
    // Fragment Pipeline Connections
    //--------------------------------------------------------------------------

    // Device -> Shader Library
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  fragment.shaderLib, RG::ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // Device -> Descriptor Set
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  fragment.descriptorSet, RG::DescriptorSetNodeConfig::VULKAN_DEVICE_IN);

    // Shader -> Descriptor Gatherer
    batch.Connect(fragment.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  fragment.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);

    // Gatherer -> Descriptor Set
    batch.Connect(fragment.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  fragment.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);

    // Shader -> Push Constant Gatherer
    batch.Connect(fragment.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  fragment.pushConstantGatherer, RG::PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);

    // Shader -> Descriptor Set
    batch.Connect(fragment.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  fragment.descriptorSet, RG::DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);

    // SwapChain -> Descriptor Set (image count and image index)
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  fragment.descriptorSet, RG::DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT,
                  &SwapChainPublicVariables::swapChainImageCount);
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  fragment.descriptorSet, RG::DescriptorSetNodeConfig::IMAGE_INDEX);

    // Device -> RenderPass
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  fragment.renderPass, RG::RenderPassNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> RenderPass (for color format)
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  fragment.renderPass, RG::RenderPassNodeConfig::SWAPCHAIN_INFO);

    // RenderPass -> GraphicsPipeline
    batch.Connect(fragment.renderPass, RG::RenderPassNodeConfig::RENDER_PASS,
                  fragment.pipeline, RG::GraphicsPipelineNodeConfig::RENDER_PASS);

    // Device -> GraphicsPipeline
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  fragment.pipeline, RG::GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

    // Shader -> GraphicsPipeline
    batch.Connect(fragment.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  fragment.pipeline, RG::GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE);

    // Descriptor Set Layout -> GraphicsPipeline
    batch.Connect(fragment.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  fragment.pipeline, RG::GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // Device -> Framebuffer
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  fragment.framebuffer, RG::FramebufferNodeConfig::VULKAN_DEVICE_IN);

    // RenderPass -> Framebuffer
    batch.Connect(fragment.renderPass, RG::RenderPassNodeConfig::RENDER_PASS,
                  fragment.framebuffer, RG::FramebufferNodeConfig::RENDER_PASS);

    // SwapChain -> Framebuffer (swapchain info for image views and dimensions)
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  fragment.framebuffer, RG::FramebufferNodeConfig::SWAPCHAIN_INFO);

    //--------------------------------------------------------------------------
    // Ray March Scene Connections
    //--------------------------------------------------------------------------

    // Device -> Camera
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.camera, RG::CameraNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Camera
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  rayMarch.camera, RG::CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  rayMarch.camera, RG::CameraNodeConfig::IMAGE_INDEX);

    // Input -> Camera
    if (rayMarch.input.IsValid()) {
        batch.Connect(rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
                      rayMarch.camera, RG::CameraNodeConfig::INPUT_STATE);
    }

    // Device -> VoxelGrid
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> VoxelGrid
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMMAND_POOL);

    //--------------------------------------------------------------------------
    // Output Connections
    //--------------------------------------------------------------------------

    // Device -> Present
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  output.present, RG::PresentNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Present
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  output.present, RG::PresentNodeConfig::SWAPCHAIN)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  output.present, RG::PresentNodeConfig::IMAGE_INDEX);

    // FrameSync -> Present (present fences)
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  output.present, RG::PresentNodeConfig::PRESENT_FENCE_ARRAY);

    //--------------------------------------------------------------------------
    // GeometryRenderNode (Draw Command) Connections
    //--------------------------------------------------------------------------

    if (fragment.drawCommand.IsValid()) {
        // RenderPass -> GeometryRenderNode
        batch.Connect(fragment.renderPass, RG::RenderPassNodeConfig::RENDER_PASS,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::RENDER_PASS);

        // Framebuffer -> GeometryRenderNode
        batch.Connect(fragment.framebuffer, RG::FramebufferNodeConfig::FRAMEBUFFERS,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::FRAMEBUFFERS);

        // GraphicsPipeline -> GeometryRenderNode
        batch.Connect(fragment.pipeline, RG::GraphicsPipelineNodeConfig::PIPELINE,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::PIPELINE)
             .Connect(fragment.pipeline, RG::GraphicsPipelineNodeConfig::PIPELINE_LAYOUT,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::PIPELINE_LAYOUT);

        // DescriptorSet -> GeometryRenderNode
        batch.Connect(fragment.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::DESCRIPTOR_SETS);

        // PushConstantGatherer -> GeometryRenderNode (camera data for ray marching)
        batch.Connect(fragment.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::PUSH_CONSTANT_DATA)
             .Connect(fragment.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::PUSH_CONSTANT_RANGES);

        // SwapChain -> GeometryRenderNode
        batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::SWAPCHAIN_INFO)
             .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::IMAGE_INDEX);

        // Device -> GeometryRenderNode
        batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::VULKAN_DEVICE);

        // CommandPool -> GeometryRenderNode
        batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::COMMAND_POOL);

        // FrameSync -> GeometryRenderNode
        batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::IN_FLIGHT_FENCE)
             .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
             .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                      fragment.drawCommand, RG::GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

        // GeometryRenderNode -> Present (render complete semaphore for presentation sync)
        batch.Connect(fragment.drawCommand, RG::GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      output.present, RG::PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);
    }

    // Register all connections atomically
    batch.RegisterAll();
}

BenchmarkGraph BenchmarkGraphFactory::BuildFragmentRayMarchGraph(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    uint32_t width,
    uint32_t height,
    const BenchmarkSuiteConfig* suiteConfig)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildFragmentRayMarchGraph: graph is null");
    }

    BenchmarkGraph result{};
    result.pipelineType = PipelineType::Fragment;

    // Create scene info from config
    // Density is 0 - will be computed from actual scene data
    SceneInfo scene = SceneInfo::FromResolutionAndDensity(
        config.voxelResolution,
        0.0f,  // Density computed from scene data, not config
        MapSceneType(config.sceneType),
        config.testId
    );

    // Extract shader names from config.shaderGroup (expected: [vertex, fragment])
    std::string vertexShader = "Fullscreen.vert";    // Default vertex shader
    std::string fragmentShader = "VoxelRayMarch.frag"; // Default fragment shader
    if (config.shaderGroup.size() >= 2) {
        vertexShader = config.shaderGroup[0];
        fragmentShader = config.shaderGroup[1];
    } else if (config.shaderGroup.size() == 1) {
        fragmentShader = config.shaderGroup[0];  // Only fragment specified
    }
    // TODO: Migrate to LOG_DEBUG when BenchmarkGraphFactory inherits from ILoggable
    // LOG_DEBUG("[Fragment] Using shaders: " + vertexShader + " + " + fragmentShader);

    // Build all subgraphs
    // LOG_DEBUG("[Fragment] Building infrastructure...");
    uint32_t gpuIndex = (suiteConfig ? suiteConfig->gpuIndex : 0);
    result.infra = BuildInfrastructure(graph, width, height, true, gpuIndex);

    // LOG_DEBUG("[Fragment] Building fragment pipeline...");
    result.fragment = BuildFragmentPipeline(
        graph, result.infra,
        vertexShader,           // Full-screen triangle vertex shader
        fragmentShader          // Fragment shader ray marching
    );

    // LOG_DEBUG("[Fragment] Building ray march scene...");
    result.rayMarch = BuildRayMarchScene(graph, result.infra, scene);

    // LOG_DEBUG("[Fragment] Building output...");
    result.output = BuildOutput(graph, result.infra, false);

    // LOG_DEBUG("[Fragment] Registering shaders...");
    // Register shader builder for vertex + fragment shaders
    RegisterFragmentShader(graph, result.fragment, vertexShader, fragmentShader);

    // LOG_DEBUG("[Fragment] Wiring variadic resources...");
    // Wire variadic resources (descriptors and push constants)
    WireFragmentVariadicResources(graph, result.infra, result.fragment, result.rayMarch);

    // LOG_DEBUG("[Fragment] Connecting subgraphs...");
    // Connect subgraphs
    ConnectFragmentRayMarch(graph, result.infra, result.fragment, result.rayMarch, result.output);

    // LOG_DEBUG("[Fragment] Build complete!");
    return result;
}

//==============================================================================
// Hardware RT Pipeline (Phase K)
//==============================================================================

HardwareRTNodes BenchmarkGraphFactory::BuildHardwareRT(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildHardwareRT: graph is null");
    }

    if (!infra.IsValid()) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildHardwareRT: infrastructure nodes invalid");
    }

    HardwareRTNodes nodes{};

    // Create hardware RT nodes (following same pattern as compute/fragment pipelines)

    // ShaderLibraryNode: Compiles and manages RT shaders
    nodes.shaderLib = graph->AddNode<RG::ShaderLibraryNodeType>("benchmark_rt_shader_lib");

    // DescriptorResourceGathererNode: Gathers descriptor resources from variadic connections
    nodes.descriptorGatherer = graph->AddNode<RG::DescriptorResourceGathererNodeType>("benchmark_rt_desc_gatherer");

    // PushConstantGathererNode: Gathers camera push constants for ray tracing
    nodes.pushConstantGatherer = graph->AddNode<RG::PushConstantGathererNodeType>("benchmark_rt_pc_gatherer");

    // DescriptorSetNode: Creates descriptor set layout and descriptor sets
    nodes.descriptorSet = graph->AddNode<RG::DescriptorSetNodeType>("benchmark_rt_descriptors");

    // VoxelAABBConverterNode: Extracts AABBs from voxel grid for BLAS
    nodes.aabbConverter = graph->AddNode<RG::VoxelAABBConverterNodeType>("benchmark_aabb_converter");

    // AccelerationStructureNode: Builds BLAS + TLAS
    nodes.accelerationStructure = graph->AddNode<RG::AccelerationStructureNodeType>("benchmark_accel_structure");

    // RayTracingPipelineNode: Creates RT pipeline + SBT
    nodes.rtPipeline = graph->AddNode<RG::RayTracingPipelineNodeType>("benchmark_rt_pipeline");

    // TraceRaysNode: Dispatches vkCmdTraceRaysKHR
    nodes.traceRays = graph->AddNode<RG::TraceRaysNodeType>("benchmark_trace_rays");

    // DEBUG: Logging for RT descriptor nodes disabled for clean benchmark output
    // Re-enable for debugging RT issues by uncommenting below
    // auto* descSetInst = graph->GetInstance(nodes.descriptorSet);
    // if (descSetInst && descSetInst->GetLogger()) {
    //     descSetInst->GetLogger()->SetEnabled(true);
    //     descSetInst->GetLogger()->SetTerminalOutput(true);
    // }
    // auto* descGathererInst = graph->GetInstance(nodes.descriptorGatherer);
    // if (descGathererInst && descGathererInst->GetLogger()) {
    //     descGathererInst->GetLogger()->SetEnabled(true);
    //     descGathererInst->GetLogger()->SetTerminalOutput(true);
    // }

    return nodes;
}

void BenchmarkGraphFactory::ConfigureHardwareRTParams(
    RG::RenderGraph* graph,
    const HardwareRTNodes& nodes,
    const SceneInfo& scene,
    uint32_t width,
    uint32_t height)
{
    // Configure AABB converter parameters
    auto* aabbConverter = static_cast<RG::VoxelAABBConverterNode*>(
        graph->GetInstance(nodes.aabbConverter));
    if (aabbConverter) {
        // Set scene type and resolution to match VoxelGridNode (critical for correct AABB generation)
        aabbConverter->SetParameter("scene_type", scene.sceneType);
        aabbConverter->SetParameter(RG::VoxelAABBConverterNodeConfig::PARAM_GRID_RESOLUTION, static_cast<uint32_t>(scene.resolution));

        // Calculate voxel size to match world space [0, worldGridSize]
        // VoxelRT.rgen traces rays in world space, so AABBs must be in world space too
        // worldGridSize = 10.0 (from VoxelSceneCacher), resolution varies (64, 128, 256)
        constexpr float WORLD_GRID_SIZE = 10.0f;
        const float voxelWorldSize = WORLD_GRID_SIZE / static_cast<float>(scene.resolution);
        aabbConverter->SetParameter(RG::VoxelAABBConverterNodeConfig::PARAM_VOXEL_SIZE, voxelWorldSize);
    }

    // Configure acceleration structure parameters
    auto* accelStructure = static_cast<RG::AccelerationStructureNode*>(
        graph->GetInstance(nodes.accelerationStructure));
    if (accelStructure) {
        accelStructure->SetParameter(RG::AccelerationStructureNodeConfig::PARAM_PREFER_FAST_TRACE, true);
        accelStructure->SetParameter(RG::AccelerationStructureNodeConfig::PARAM_ALLOW_UPDATE, false);
        accelStructure->SetParameter(RG::AccelerationStructureNodeConfig::PARAM_ALLOW_COMPACTION, false);
    }

    // Configure RT pipeline parameters
    auto* rtPipeline = static_cast<RG::RayTracingPipelineNode*>(
        graph->GetInstance(nodes.rtPipeline));
    if (rtPipeline) {
        rtPipeline->SetParameter(RG::RayTracingPipelineNodeConfig::PARAM_MAX_RAY_RECURSION, 1u);
        rtPipeline->SetParameter(RG::RayTracingPipelineNodeConfig::PARAM_OUTPUT_WIDTH, width);
        rtPipeline->SetParameter(RG::RayTracingPipelineNodeConfig::PARAM_OUTPUT_HEIGHT, height);
        // Shaders are now provided via ShaderLibraryNode -> SHADER_DATA_BUNDLE connection
    }

    // Configure trace rays parameters
    auto* traceRays = static_cast<RG::TraceRaysNode*>(
        graph->GetInstance(nodes.traceRays));
    if (traceRays) {
        traceRays->SetParameter(RG::TraceRaysNodeConfig::PARAM_WIDTH, width);
        traceRays->SetParameter(RG::TraceRaysNodeConfig::PARAM_HEIGHT, height);
        traceRays->SetParameter(RG::TraceRaysNodeConfig::PARAM_DEPTH, 1u);
    }
}

void BenchmarkGraphFactory::ConnectHardwareRT(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const HardwareRTNodes& hardwareRT,
    const RayMarchNodes& rayMarch,
    const OutputNodes& output)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::ConnectHardwareRT: graph is null");
    }

    RG::ConnectionBatch batch(graph);

    //--------------------------------------------------------------------------
    // Infrastructure Connections (same as compute/fragment pipelines)
    //--------------------------------------------------------------------------

    // Instance -> Device
    batch.Connect(infra.instance, RG::InstanceNodeConfig::INSTANCE,
                  infra.device, RG::DeviceNodeConfig::INSTANCE_IN);

    // Device -> Window (VkInstance passthrough)
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.window, RG::WindowNodeConfig::INSTANCE);

    // Window -> SwapChain
    batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HWND)
         .Connect(infra.window, RG::WindowNodeConfig::HINSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HINSTANCE)
         .Connect(infra.window, RG::WindowNodeConfig::WIDTH_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::WIDTH)
         .Connect(infra.window, RG::WindowNodeConfig::HEIGHT_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::HEIGHT);

    // Window -> Input
    if (rayMarch.input.IsValid()) {
        batch.Connect(infra.window, RG::WindowNodeConfig::HWND_OUT,
                      rayMarch.input, RG::InputNodeConfig::HWND_IN);
    }

    // Device -> SwapChain
    batch.Connect(infra.device, RG::DeviceNodeConfig::INSTANCE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::INSTANCE)
         .Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.swapchain, RG::SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // Device -> FrameSync
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.frameSync, RG::FrameSyncNodeConfig::VULKAN_DEVICE);

    // FrameSync -> SwapChain
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  infra.swapchain, RG::SwapChainNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  infra.swapchain, RG::SwapChainNodeConfig::PRESENT_FENCES_ARRAY);

    // Device -> CommandPool
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  infra.commandPool, RG::CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    //--------------------------------------------------------------------------
    // Ray March Scene Connections (VoxelGrid + Camera)
    //--------------------------------------------------------------------------

    // Device -> Camera
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.camera, RG::CameraNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Camera
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  rayMarch.camera, RG::CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  rayMarch.camera, RG::CameraNodeConfig::IMAGE_INDEX);

    // Input -> Camera
    if (rayMarch.input.IsValid()) {
        batch.Connect(rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
                      rayMarch.camera, RG::CameraNodeConfig::INPUT_STATE);
    }

    // Device -> VoxelGrid
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> VoxelGrid
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMMAND_POOL);

    //--------------------------------------------------------------------------
    // Hardware RT Pipeline Connections
    //--------------------------------------------------------------------------

    // Device -> VoxelAABBConverterNode
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> VoxelAABBConverterNode
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::COMMAND_POOL);

    // VoxelGrid -> VoxelAABBConverterNode (octree nodes buffer for AABB extraction)
    batch.Connect(rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_NODES_BUFFER,
                  hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::OCTREE_NODES_BUFFER);

    // VoxelGrid -> VoxelAABBConverterNode (brick grid lookup for compressed color access)
    // Optional: only needed for compressed RTX shaders to map grid coords to brick indices
    batch.Connect(rayMarch.voxelGrid, RG::VoxelGridNodeConfig::BRICK_GRID_LOOKUP_BUFFER,
                  hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::BRICK_GRID_LOOKUP_BUFFER);

    // VoxelGrid -> VoxelAABBConverterNode (cached scene data for VoxelAABBCacher)
    batch.Connect(rayMarch.voxelGrid, RG::VoxelGridNodeConfig::VOXEL_SCENE_DATA,
                  hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::VOXEL_SCENE_DATA);

    // Device -> AccelerationStructureNode
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> AccelerationStructureNode
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::COMMAND_POOL);

    // VoxelAABBConverterNode -> AccelerationStructureNode
    batch.Connect(hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::AABB_DATA,
                  hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::AABB_DATA);

    // Device -> ShaderLibraryNode (for RT shaders)
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // Device -> DescriptorSetNode
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::VULKAN_DEVICE_IN);

    // Device -> RayTracingPipelineNode
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.rtPipeline, RG::RayTracingPipelineNodeConfig::VULKAN_DEVICE_IN);

    // Shader -> DescriptorResourceGatherer
    batch.Connect(hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  hardwareRT.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);

    // DescriptorResourceGatherer -> DescriptorSetNode
    batch.Connect(hardwareRT.descriptorGatherer, RG::DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);

    // Shader -> DescriptorSetNode
    batch.Connect(hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);

    // SwapChain -> DescriptorSetNode (image count and image index)
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT,
                  &SwapChainPublicVariables::swapChainImageCount);
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::IMAGE_INDEX);

    // DescriptorSetNode -> RayTracingPipelineNode (descriptor set layout)
    batch.Connect(hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  hardwareRT.rtPipeline, RG::RayTracingPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // AccelerationStructureNode -> RayTracingPipelineNode
    batch.Connect(hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA,
                  hardwareRT.rtPipeline, RG::RayTracingPipelineNodeConfig::ACCELERATION_STRUCTURE_DATA);

    // ShaderLibraryNode -> RayTracingPipelineNode (compiled RT shader bundle)
    batch.Connect(hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  hardwareRT.rtPipeline, RG::RayTracingPipelineNodeConfig::SHADER_DATA_BUNDLE);

    // ShaderLibraryNode -> PushConstantGathererNode (for reflection data)
    batch.Connect(hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  hardwareRT.pushConstantGatherer, RG::PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);

    // PushConstantGathererNode -> TraceRaysNode (camera push constants and ranges)
    batch.Connect(hardwareRT.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::PUSH_CONSTANT_DATA);

    batch.Connect(hardwareRT.pushConstantGatherer, RG::PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::PUSH_CONSTANT_RANGES);

    // ShaderLibraryNode -> TraceRaysNode (shader data bundle for reflection metadata)
    batch.Connect(hardwareRT.shaderLib, RG::ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::SHADER_DATA_BUNDLE);

    // DescriptorSetNode -> TraceRaysNode (descriptor sets for binding)
    batch.Connect(hardwareRT.descriptorSet, RG::DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::DESCRIPTOR_SETS);

    // Device -> TraceRaysNode
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::VULKAN_DEVICE_IN);

    // CommandPool -> TraceRaysNode
    batch.Connect(infra.commandPool, RG::CommandPoolNodeConfig::COMMAND_POOL,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::COMMAND_POOL);

    // RayTracingPipelineNode -> TraceRaysNode
    batch.Connect(hardwareRT.rtPipeline, RG::RayTracingPipelineNodeConfig::RT_PIPELINE_DATA,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::RT_PIPELINE_DATA);

    // AccelerationStructureNode -> TraceRaysNode
    batch.Connect(hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::ACCELERATION_STRUCTURE_DATA);

    // SwapChain -> TraceRaysNode
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::SWAPCHAIN_INFO)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::IMAGE_INDEX);

    // FrameSync -> TraceRaysNode
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::IN_FLIGHT_FENCE)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(infra.frameSync, RG::FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  hardwareRT.traceRays, RG::TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // Camera -> TraceRaysNode (push constants for camera data)
    // Push constant data would be wired via variadic resources or direct connection
    // For now, the CameraNode outputs CAMERA_DATA which TraceRaysNode can use
    // Note: This may need variadic wiring similar to compute pipeline

    //--------------------------------------------------------------------------
    // Output Connections
    //--------------------------------------------------------------------------

    // Device -> Present
    batch.Connect(infra.device, RG::DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  output.present, RG::PresentNodeConfig::VULKAN_DEVICE_IN);

    // SwapChain -> Present
    batch.Connect(infra.swapchain, RG::SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  output.present, RG::PresentNodeConfig::SWAPCHAIN)
         .Connect(infra.swapchain, RG::SwapChainNodeConfig::IMAGE_INDEX,
                  output.present, RG::PresentNodeConfig::IMAGE_INDEX);

    // TraceRaysNode -> Present (render complete semaphore)
    batch.Connect(hardwareRT.traceRays, RG::TraceRaysNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  output.present, RG::PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);

    // FrameSync -> Present (present fences)
    batch.Connect(infra.frameSync, RG::FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  output.present, RG::PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // Register all connections atomically
    batch.RegisterAll();
}

BenchmarkGraph BenchmarkGraphFactory::BuildHardwareRTGraph(
    RG::RenderGraph* graph,
    const TestConfiguration& config,
    uint32_t width,
    uint32_t height,
    const BenchmarkSuiteConfig* suiteConfig)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::BuildHardwareRTGraph: graph is null");
    }

    BenchmarkGraph result{};
    result.pipelineType = PipelineType::HardwareRT;

    // Create scene info from config
    SceneInfo scene = SceneInfo::FromResolutionAndDensity(
        config.voxelResolution,
        0.0f,  // Density computed from scene data, not config
        MapSceneType(config.sceneType),
        config.testId
    );

    // Build all subgraphs
    uint32_t gpuIndex = (suiteConfig ? suiteConfig->gpuIndex : 0);
    result.infra = BuildInfrastructure(graph, width, height, true, gpuIndex);

    // Check RTX capability before building hardware RT pipeline
    auto* deviceNode = static_cast<RG::DeviceNode*>(graph->GetInstance(result.infra.device));
    if (deviceNode) {
        auto* vulkanDevice = deviceNode->GetVulkanDevice();
        if (vulkanDevice && !vulkanDevice->HasCapability("RTXSupport")) {
            throw std::runtime_error(
                "Cannot build hardware ray tracing graph: GPU does not support RTX. "
                "Required capability 'RTXSupport' is not available on this device. "
                "Use 'compute' or 'fragment' pipeline type instead."
            );
        }
    }

    result.rayMarch = BuildRayMarchScene(graph, result.infra, scene);
    result.hardwareRT = BuildHardwareRT(graph, result.infra);
    result.output = BuildOutput(graph, result.infra, false);

    // Configure hardware RT parameters (including scene type for AABB converter)
    ConfigureHardwareRTParams(graph, result.hardwareRT, scene, width, height);

    // Determine if compressed RTX shaders should be used
    // Check config.shader OR config.shaderGroup for "Compressed" suffix
    bool useCompressed = false;
    if (!config.shader.empty() &&
        config.shader.find("Compressed") != std::string::npos) {
        useCompressed = true;
    }
    // Also check shaderGroup (array of RT shaders: [rgen, rmiss, rchit, rint])
    for (const auto& shaderName : config.shaderGroup) {
        if (shaderName.find("Compressed") != std::string::npos) {
            useCompressed = true;
            break;
        }
    }

    // Register RT shaders (raygen, miss, closest hit, intersection)
    // Only the closest-hit shader differs between compressed/uncompressed
    // since it's the only one that reads color/normal data
    std::string closestHitShader = useCompressed ? "VoxelRT_Compressed.rchit" : "VoxelRT.rchit";

    RegisterRTXShader(graph, result.hardwareRT,
        "VoxelRT.rgen",      // Ray generation shader (shared)
        "VoxelRT.rmiss",     // Miss shader (shared)
        closestHitShader,    // Closest hit shader (compressed or uncompressed)
        "VoxelRT.rint");     // Intersection shader (shared)

    // Wire variadic resources (camera -> push constants, swapchain -> output image)
    // Pass useCompressed to conditionally wire bindings 6-7 for compressed shaders
    WireHardwareRTVariadicResources(graph, result.infra, result.hardwareRT, result.rayMarch, useCompressed);

    // Connect all subgraphs
    ConnectHardwareRT(graph, result.infra, result.hardwareRT, result.rayMarch, result.output);

    return result;
}

//==============================================================================
// Profiler Hook Wiring
//==============================================================================

void BenchmarkGraphFactory::WireProfilerHooks(
    RG::RenderGraph* graph,
    ProfilerGraphAdapter& adapter,
    const std::string& dispatchNodeName)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireProfilerHooks: graph is null");
    }

    // Get lifecycle hooks from the graph (returns reference)
    RG::GraphLifecycleHooks& hooks = graph->GetLifecycleHooks();

    // TODO: OPTIMIZATION - Current implementation is O(N) per frame where N = node count.
    // RegisterNodeHook fires for ALL nodes, then we filter by name inside callback.
    // Same pattern used in TypedConnection::ConnectVariadic (RegisterVariadicResourcePopulationHook).
    //
    // Future improvement: Extend GraphLifecycleHooks with node-specific registration:
    //   void RegisterNodeHook(phase, NodeHandle nodeHandle, callback, debugName);
    // This would enable O(1) dispatch instead of O(N) filtering.
    // See: RenderGraph/include/Core/GraphLifecycleHooks.h

    // Register node-level hooks for dispatch timing
    // PreExecute: Called before node executes (for dispatch begin timing)
    hooks.RegisterNodeHook(
        RG::NodeLifecyclePhase::PreExecute,
        [&adapter, dispatchNodeName](RG::NodeInstance* node) {
            if (node && node->GetInstanceName() == dispatchNodeName) {
                adapter.OnDispatchBegin();
            }
            adapter.OnNodePreExecute(node ? node->GetInstanceName() : "");
        },
        "ProfilerDispatchBegin"
    );

    // PostExecute: Called after node executes (for dispatch end timing)
    // Note: Dispatch dimensions need to be passed externally since node doesn't expose them
    hooks.RegisterNodeHook(
        RG::NodeLifecyclePhase::PostExecute,
        [&adapter](RG::NodeInstance* node) {
            adapter.OnNodePostExecute(node ? node->GetInstanceName() : "");
        },
        "ProfilerDispatchEnd"
    );

    // PreCleanup: Called before cleanup for metrics extraction
    hooks.RegisterNodeHook(
        RG::NodeLifecyclePhase::PreCleanup,
        [&adapter](RG::NodeInstance* node) {
            adapter.OnNodePreCleanup(node ? node->GetInstanceName() : "");
        },
        "ProfilerPreCleanup"
    );

    // Track that this graph has profiler hooks
    g_graphsWithProfilerHooks.insert(graph);
}

void BenchmarkGraphFactory::WireProfilerHooks(
    RG::RenderGraph* graph,
    ProfilerGraphAdapter& adapter,
    const BenchmarkGraph& benchGraph)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireProfilerHooks: graph is null");
    }

    // Get dispatch node name from the graph via handle lookup
    std::string dispatchNodeName = "benchmark_dispatch";  // Default name used by factory

    // If compute dispatch handle is valid, get actual name
    if (benchGraph.compute.dispatch.IsValid()) {
        auto* dispatchNode = graph->GetInstance(benchGraph.compute.dispatch);
        if (dispatchNode) {
            dispatchNodeName = dispatchNode->GetInstanceName();
        }
    }

    WireProfilerHooks(graph, adapter, dispatchNodeName);
}

bool BenchmarkGraphFactory::HasProfilerHooks(const RG::RenderGraph* graph)
{
    if (!graph) {
        return false;
    }
    return g_graphsWithProfilerHooks.count(graph) > 0;
}

//==============================================================================
// Variadic Resource Wiring
//==============================================================================

void BenchmarkGraphFactory::WireVariadicResources(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const ComputePipelineNodes& compute,
    const RayMarchNodes& rayMarch)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireVariadicResources: graph is null");
    }

    RG::ConnectionBatch batch(graph);

    // Use RenderGraph namespace types
    using RG::SlotRole;
    using RG::CameraData;
    using RG::InputState;

    //--------------------------------------------------------------------------
    // VoxelRayMarch.comp / VoxelRayMarch_Compressed.comp Descriptor Bindings (Set 0)
    //--------------------------------------------------------------------------
    // Binding constants from VoxelRayMarchNames.h (hard-coded for portability)
    constexpr uint32_t BINDING_OUTPUT_IMAGE = 0;
    constexpr uint32_t BINDING_ESVO_NODES = 1;
    constexpr uint32_t BINDING_BRICK_DATA = 2;
    constexpr uint32_t BINDING_MATERIALS = 3;
    constexpr uint32_t BINDING_TRACE_WRITE_INDEX = 4;
    constexpr uint32_t BINDING_OCTREE_CONFIG = 5;
    // Compressed shader bindings (VoxelRayMarch_Compressed.comp)
    constexpr uint32_t BINDING_COMPRESSED_COLOR = 6;
    constexpr uint32_t BINDING_COMPRESSED_NORMAL = 7;
    // Both uncompressed and compressed shaders use binding 8 for ShaderCounters
    constexpr uint32_t BINDING_SHADER_COUNTERS = 8;

    // Binding 0: outputImage (swapchain storage image) - Execute only (changes per frame)
    batch.ConnectVariadic(
        infra.swapchain, RG::SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW,
        compute.descriptorGatherer, BINDING_OUTPUT_IMAGE,
        SlotRole::Execute);

    // Binding 1: esvoNodes (SSBO) - Dependency + Execute
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_NODES_BUFFER,
        compute.descriptorGatherer, BINDING_ESVO_NODES,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 2: brickData (SSBO) - Dependency + Execute
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER,
        compute.descriptorGatherer, BINDING_BRICK_DATA,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 3: materials (SSBO) - Dependency + Execute
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER,
        compute.descriptorGatherer, BINDING_MATERIALS,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 4: traceWriteIndex (SSBO) - Debug capture buffer
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
        compute.descriptorGatherer, BINDING_TRACE_WRITE_INDEX,
        SlotRole::Dependency | SlotRole::Execute | SlotRole::Debug);

    // Binding 5: octreeConfig (UBO) - Scale and depth parameters
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER,
        compute.descriptorGatherer, BINDING_OCTREE_CONFIG,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 6-7: Compressed buffers (optional - only used by VoxelRayMarch_Compressed.comp)
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER,
        compute.descriptorGatherer, BINDING_COMPRESSED_COLOR,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER,
        compute.descriptorGatherer, BINDING_COMPRESSED_NORMAL,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 8: ShaderCounters for avgVoxelsPerRay metrics
    // Both VoxelRayMarch.comp and VoxelRayMarch_Compressed.comp use binding 8 for shader counters
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER,
        compute.descriptorGatherer, BINDING_SHADER_COUNTERS,
        SlotRole::Dependency | SlotRole::Execute);

    //--------------------------------------------------------------------------
    // VoxelRayMarch.comp Push Constants
    //--------------------------------------------------------------------------
    // Push constant layout (64 bytes total):
    //   offset 0:  cameraPos (vec3, 12 bytes) + time (float, 4 bytes)
    //   offset 16: cameraDir (vec3, 12 bytes) + fov (float, 4 bytes)
    //   offset 32: cameraUp (vec3, 12 bytes) + aspect (float, 4 bytes)
    //   offset 48: cameraRight (vec3, 12 bytes) + debugMode (int, 4 bytes)

    // Push constant binding indices (from VoxelRayMarchNames.h)
    constexpr uint32_t PC_CAMERA_POS = 0;
    constexpr uint32_t PC_TIME = 1;
    constexpr uint32_t PC_CAMERA_DIR = 2;
    constexpr uint32_t PC_FOV = 3;
    constexpr uint32_t PC_CAMERA_UP = 4;
    constexpr uint32_t PC_ASPECT = 5;
    constexpr uint32_t PC_CAMERA_RIGHT = 6;
    constexpr uint32_t PC_DEBUG_MODE = 7;

    // Suppress unused variable warning for PC_TIME
    (void)PC_TIME;

    // Connect camera data to push constants using field extraction
    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_CAMERA_POS,
        &CameraData::cameraPos, SlotRole::Execute);

    // Note: time field (PC_TIME) not connected - will be zero (no animation)
    // TODO: Connect actual time source when animation is needed

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_CAMERA_DIR,
        &CameraData::cameraDir, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_FOV,
        &CameraData::fov, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_CAMERA_UP,
        &CameraData::cameraUp, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_ASPECT,
        &CameraData::aspect, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        compute.pushConstantGatherer, PC_CAMERA_RIGHT,
        &CameraData::cameraRight, SlotRole::Execute);

    // Connect debugMode from input node (if valid)
    if (rayMarch.input.IsValid()) {
        batch.ConnectVariadic(
            rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
            compute.pushConstantGatherer, PC_DEBUG_MODE,
            &InputState::debugMode, SlotRole::Execute);
    }

    // Register all connections atomically
    batch.RegisterAll();
}

//==============================================================================
// Shader Builder Registration
//==============================================================================

void BenchmarkGraphFactory::RegisterComputeShader(
    RG::RenderGraph* graph,
    const ComputePipelineNodes& compute,
    const std::string& shaderName)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::RegisterComputeShader: graph is null");
    }

    // Get the shader library node
    auto* shaderLibNode = static_cast<RG::ShaderLibraryNode*>(
        graph->GetInstance(compute.shaderLib));

    if (!shaderLibNode) {
        throw std::runtime_error("BenchmarkGraphFactory::RegisterComputeShader: "
                                "shader library node not found");
    }

    // Register shader builder callback - captures shaderName by value
    shaderLibNode->RegisterShaderBuilder([shaderName](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Shader name is used directly as filename
        // Extract program name with pipeline suffix for unique SDI naming
        // VoxelRayMarch.comp -> VoxelRayMarch_Compute_ (SDI generates VoxelRayMarch_Compute_Names.h)
        std::string programName = shaderName;
        auto dotPos = programName.rfind('.');
        if (dotPos != std::string::npos) {
            programName = programName.substr(0, dotPos) + "_Compute_";
        }

        // Search paths for shader source
        // Check relative paths FIRST for portability, fall back to compile-time path
        std::vector<std::filesystem::path> possiblePaths = {
            std::string("shaders/") + shaderName,
            std::string("../shaders/") + shaderName,
            shaderName,
#ifdef VIXEN_SHADER_SOURCE_DIR
            std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
        };

        std::filesystem::path shaderPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                shaderPath = path;
                break;
            }
        }

        if (shaderPath.empty()) {
            throw std::runtime_error(
                std::string("Compute shader not found: ") + shaderName);
        }

        // Configure builder with include paths for #include support
        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, shaderPath, "main");

        return builder;
    });
}

void BenchmarkGraphFactory::RegisterFragmentShader(
    RG::RenderGraph* graph,
    const FragmentPipelineNodes& fragment,
    const std::string& vertexShaderName,
    const std::string& fragmentShaderName)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::RegisterFragmentShader: graph is null");
    }

    // Get the shader library node
    auto* shaderLibNode = static_cast<RG::ShaderLibraryNode*>(
        graph->GetInstance(fragment.shaderLib));

    if (!shaderLibNode) {
        throw std::runtime_error("BenchmarkGraphFactory::RegisterFragmentShader: "
                                "shader library node not found");
    }

    // Capture shader names by value for the lambda
    shaderLibNode->RegisterShaderBuilder(
        [vertexShaderName, fragmentShaderName](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Extract program name with pipeline suffix for unique SDI naming
        // VoxelRayMarch.frag -> VoxelRayMarch_Fragment_ (SDI generates VoxelRayMarch_Fragment_Names.h)
        std::string programName = fragmentShaderName;
        auto dotPos = programName.rfind('.');
        if (dotPos != std::string::npos) {
            programName = programName.substr(0, dotPos) + "_Fragment_";
        }

        // Helper to find shader file
        // Check relative paths FIRST for portability, fall back to compile-time path
        auto findShader = [](const std::string& shaderName) -> std::filesystem::path {
            std::vector<std::filesystem::path> possiblePaths = {
                std::string("shaders/") + shaderName,
                std::string("../shaders/") + shaderName,
                shaderName,
#ifdef VIXEN_SHADER_SOURCE_DIR
                std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
            };

            for (const auto& path : possiblePaths) {
                if (std::filesystem::exists(path)) {
                    return path;
                }
            }
            return {};
        };

        auto vertexPath = findShader(vertexShaderName);
        auto fragmentPath = findShader(fragmentShaderName);

        if (vertexPath.empty()) {
            throw std::runtime_error(
                std::string("Vertex shader not found: ") + vertexShaderName);
        }
        if (fragmentPath.empty()) {
            throw std::runtime_error(
                std::string("Fragment shader not found: ") + fragmentShaderName);
        }

        // Configure builder with include paths and both stages
        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Graphics)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Vertex, vertexPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, fragmentPath, "main");

        return builder;
    });
}

void BenchmarkGraphFactory::RegisterRTXShader(
    RG::RenderGraph* graph,
    const HardwareRTNodes& hardwareRT,
    const std::string& raygenShader,
    const std::string& missShader,
    const std::string& closestHitShader,
    const std::string& intersectionShader)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::RegisterRTXShader: graph is null");
    }

    // Get the shader library node
    auto* shaderLibNode = static_cast<RG::ShaderLibraryNode*>(
        graph->GetInstance(hardwareRT.shaderLib));

    if (!shaderLibNode) {
        throw std::runtime_error("BenchmarkGraphFactory::RegisterRTXShader: "
                                "shader library node not found");
    }

    // Capture shader names by value for the lambda
    shaderLibNode->RegisterShaderBuilder(
        [raygenShader, missShader, closestHitShader, intersectionShader](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Extract program name with pipeline suffix for unique SDI naming
        // VoxelRT.rgen -> VoxelRT_RayTracing_ (SDI generates VoxelRT_RayTracing_Names.h)
        std::string programName = raygenShader;
        auto dotPos = programName.rfind('.');
        if (dotPos != std::string::npos) {
            programName = programName.substr(0, dotPos) + "_RayTracing_";
        }

        // Helper to find shader file
        // Check relative paths FIRST for portability, fall back to compile-time path
        auto findShader = [](const std::string& shaderName) -> std::filesystem::path {
            std::vector<std::filesystem::path> possiblePaths = {
                std::string("shaders/") + shaderName,
                std::string("../shaders/") + shaderName,
                shaderName,
#ifdef VIXEN_SHADER_SOURCE_DIR
                std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
            };

            for (const auto& path : possiblePaths) {
                if (std::filesystem::exists(path)) {
                    return path;
                }
            }
            return {};
        };

        auto raygenPath = findShader(raygenShader);
        auto missPath = findShader(missShader);
        auto closestHitPath = findShader(closestHitShader);
        auto intersectionPath = findShader(intersectionShader);

        if (raygenPath.empty()) {
            throw std::runtime_error(
                std::string("Raygen shader not found: ") + raygenShader);
        }
        if (missPath.empty()) {
            throw std::runtime_error(
                std::string("Miss shader not found: ") + missShader);
        }
        if (closestHitPath.empty()) {
            throw std::runtime_error(
                std::string("Closest hit shader not found: ") + closestHitShader);
        }
        if (intersectionPath.empty()) {
            throw std::runtime_error(
                std::string("Intersection shader not found: ") + intersectionShader);
        }

        // Configure builder with include paths and all RT stages
        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::RayTracing)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::RayGen, raygenPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Miss, missPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::ClosestHit, closestHitPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Intersection, intersectionPath, "main");

        return builder;
    });
}

void BenchmarkGraphFactory::WireFragmentVariadicResources(
    RG::RenderGraph* graph,
    const InfrastructureNodes& /*infra*/,
    const FragmentPipelineNodes& fragment,
    const RayMarchNodes& rayMarch)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireFragmentVariadicResources: graph is null");
    }

    // Local using declarations for cleaner code
    using RG::SlotRole;
    using Vixen::RenderGraph::CameraData;
    using Vixen::RenderGraph::InputState;

    RG::ConnectionBatch batch(graph);

    // Descriptor binding indices (matching VoxelRayMarch.frag / VoxelRayMarch_Compressed.frag layout)
    // binding 0: outputImage (handled by swapchain, not via variadic)
    constexpr uint32_t BINDING_ESVO_NODES = 1;
    constexpr uint32_t BINDING_BRICK_DATA = 2;
    constexpr uint32_t BINDING_MATERIALS = 3;
    constexpr uint32_t BINDING_DEBUG_CAPTURE = 4;
    constexpr uint32_t BINDING_OCTREE_CONFIG = 5;
    // Compressed shader bindings (VoxelRayMarch_Compressed.frag only)
    constexpr uint32_t BINDING_COMPRESSED_COLOR = 6;
    constexpr uint32_t BINDING_COMPRESSED_NORMAL = 7;

    // Connect voxel grid buffers to descriptor gatherer
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_NODES_BUFFER,
        fragment.descriptorGatherer, BINDING_ESVO_NODES,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_BRICKS_BUFFER,
        fragment.descriptorGatherer, BINDING_BRICK_DATA,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_MATERIALS_BUFFER,
        fragment.descriptorGatherer, BINDING_MATERIALS,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
        fragment.descriptorGatherer, BINDING_DEBUG_CAPTURE,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER,
        fragment.descriptorGatherer, BINDING_OCTREE_CONFIG,
        SlotRole::Dependency | SlotRole::Execute);

    // Compressed buffer bindings (optional - only used by VoxelRayMarch_Compressed.frag)
    // These are marked Optional so uncompressed shader works without them
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER,
        fragment.descriptorGatherer, BINDING_COMPRESSED_COLOR,
        SlotRole::Dependency | SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER,
        fragment.descriptorGatherer, BINDING_COMPRESSED_NORMAL,
        SlotRole::Dependency | SlotRole::Execute);

    // Push constant binding indices
    constexpr uint32_t PC_CAMERA_POS = 0;
    constexpr uint32_t PC_TIME = 1;
    constexpr uint32_t PC_CAMERA_DIR = 2;
    constexpr uint32_t PC_FOV = 3;
    constexpr uint32_t PC_CAMERA_UP = 4;
    constexpr uint32_t PC_ASPECT = 5;
    constexpr uint32_t PC_CAMERA_RIGHT = 6;
    constexpr uint32_t PC_DEBUG_MODE = 7;

    (void)PC_TIME;  // Suppress unused warning

    // Connect camera data to push constants using field extraction
    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_CAMERA_POS,
        &CameraData::cameraPos, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_CAMERA_DIR,
        &CameraData::cameraDir, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_FOV,
        &CameraData::fov, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_CAMERA_UP,
        &CameraData::cameraUp, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_ASPECT,
        &CameraData::aspect, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        fragment.pushConstantGatherer, PC_CAMERA_RIGHT,
        &CameraData::cameraRight, SlotRole::Execute);

    // Connect debugMode from input node (if valid)
    if (rayMarch.input.IsValid()) {
        batch.ConnectVariadic(
            rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
            fragment.pushConstantGatherer, PC_DEBUG_MODE,
            &InputState::debugMode, SlotRole::Execute);
    }

    // Register all connections atomically
    batch.RegisterAll();
}

void BenchmarkGraphFactory::WireHardwareRTVariadicResources(
    RG::RenderGraph* graph,
    const InfrastructureNodes& infra,
    const HardwareRTNodes& hardwareRT,
    const RayMarchNodes& rayMarch,
    bool useCompressed)
{
    if (!graph) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireHardwareRTVariadicResources: graph is null");
    }

    // Local using declarations for cleaner code
    using RG::SlotRole;
    using RG::CameraData;
    using RG::InputState;

    RG::ConnectionBatch batch(graph);


    // Connect camera data to push constants using field extraction
    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::cameraPos::BINDING,
        &CameraData::cameraPos, SlotRole::Execute);

    // Note: time field (PC_TIME) not connected - will be zero (no animation)

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::cameraDir::BINDING,
        &CameraData::cameraDir, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::fov::BINDING,
        &CameraData::fov, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::cameraUp::BINDING,
        &CameraData::cameraUp, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::aspect::BINDING,
        &CameraData::aspect, SlotRole::Execute);

    batch.ConnectVariadic(
        rayMarch.camera, RG::CameraNodeConfig::CAMERA_DATA,
        hardwareRT.pushConstantGatherer, VoxelRT::cameraRight::BINDING,
        &CameraData::cameraRight, SlotRole::Execute);

    // Connect debugMode from input node (if valid)
    if (rayMarch.input.IsValid()) {
        batch.ConnectVariadic(
            rayMarch.input, RG::InputNodeConfig::INPUT_STATE,
            hardwareRT.pushConstantGatherer, VoxelRT::debugMode::BINDING,
            &InputState::debugMode, SlotRole::Execute);
    }

    //--------------------------------------------------------------------------
    // VoxelRT.rgen Descriptor Bindings (SDI-driven)
    //--------------------------------------------------------------------------
    // Bindings are derived from VoxelRTNames.h (generated from shader reflection)
    // This ensures compile-time validation that bindings match the shader.
    //
    // NOTE: RT shader descriptor bindings require special handling:
    // - Output image: Bound via DescriptorResourceGatherer from swapchain
    // - TLAS: Bound via DescriptorResourceGatherer from AccelerationStructureNode

    // Binding 0: outputImage (swapchain storage image) - Execute only (changes per frame)
    // SDI reference: VoxelRT::outputImage::BINDING
    batch.ConnectVariadic(
        infra.swapchain, RG::SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW,
        hardwareRT.descriptorGatherer, VoxelRT::outputImage::BINDING,
        SlotRole::Execute);

    // Binding 1: topLevelAS (acceleration structure) - Dependency + Execute
    // SDI reference: VoxelRT::topLevelAS::BINDING
    // TLAS is static (built once during compile), so Dependency is primary role
    batch.ConnectVariadic(
        hardwareRT.accelerationStructure, RG::AccelerationStructureNodeConfig::TLAS_HANDLE,
        hardwareRT.descriptorGatherer, VoxelRT::topLevelAS::BINDING,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 2: aabbBuffer (SSBO) - Dependency + Execute
    // AABB buffer for intersection shader to look up actual voxel bounds
    // SDI reference: VoxelRT::aabbBuffer::BINDING
    batch.ConnectVariadic(
        hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::AABB_BUFFER,
        hardwareRT.descriptorGatherer, VoxelRT::aabbBuffer::BINDING,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 3: materialIdBuffer (SSBO) - Dependency + Execute
    // Material ID buffer for closest-hit shader to get voxel colors
    // SDI reference: VoxelRT::materialIdBuffer::BINDING
    batch.ConnectVariadic(
        hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::MATERIAL_ID_BUFFER,
        hardwareRT.descriptorGatherer, VoxelRT::materialIdBuffer::BINDING,
        SlotRole::Dependency | SlotRole::Execute);

    // Binding 5: OctreeConfigUBO (UBO) - Dependency + Execute
    // Contains world<->local transformation matrices for coordinate space alignment
    // This matches compute shader binding 5, ensuring RTX rays are in the same
    // coordinate space as the AABB geometry.
    // SDI reference: VoxelRT::octreeConfig::BINDING
    batch.ConnectVariadic(
        rayMarch.voxelGrid, RG::VoxelGridNodeConfig::OCTREE_CONFIG_BUFFER,
        hardwareRT.descriptorGatherer, VoxelRT::octreeConfig::BINDING,
        SlotRole::Dependency | SlotRole::Execute);

    //--------------------------------------------------------------------------
    // Compressed Buffer Bindings (VoxelRT_Compressed.rchit only)
    //--------------------------------------------------------------------------
    // Only wire compressed bindings when using compressed shader.
    // The uncompressed shader doesn't declare bindings 6-8, and wiring them
    // would cause DescriptorResourceGathererNode::Execute to fail with
    // "Binding X out of range" errors.
    if (useCompressed) {
        constexpr uint32_t BINDING_COMPRESSED_COLOR = 6;
        constexpr uint32_t BINDING_COMPRESSED_NORMAL = 7;
        constexpr uint32_t BINDING_BRICK_MAPPING = 8;

        // Binding 6: compressedColors (DXT1 color blocks) - Dependency + Execute
        batch.ConnectVariadic(
            rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_COLOR_BUFFER,
            hardwareRT.descriptorGatherer, BINDING_COMPRESSED_COLOR,
            SlotRole::Dependency | SlotRole::Execute);

        // Binding 7: compressedNormals (DXT normal blocks) - Dependency + Execute
        batch.ConnectVariadic(
            rayMarch.voxelGrid, RG::VoxelGridNodeConfig::COMPRESSED_NORMAL_BUFFER,
            hardwareRT.descriptorGatherer, BINDING_COMPRESSED_NORMAL,
            SlotRole::Dependency | SlotRole::Execute);

        // Binding 8: brickMapping (SSBO) - Maps gl_PrimitiveID to (brickIndex, localVoxelIdx)
        // Required for compressed RTX shaders to access DXT-compressed color/normal buffers
        batch.ConnectVariadic(
            hardwareRT.aabbConverter, RG::VoxelAABBConverterNodeConfig::BRICK_MAPPING_BUFFER,
            hardwareRT.descriptorGatherer, BINDING_BRICK_MAPPING,
            SlotRole::Dependency | SlotRole::Execute);
    }

    // Register all connections atomically
    batch.RegisterAll();
}

} // namespace Vixen::Profiler
