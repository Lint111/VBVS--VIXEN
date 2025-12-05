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

// Data types
#include <Data/CameraData.h>
#include <Data/InputState.h>

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
    bool enableValidation)
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
    ConfigureInfrastructureParams(graph, nodes, width, height, enableValidation);

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
                graph, config, config.screenWidth, config.screenHeight);

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
                graph, config, config.screenWidth, config.screenHeight);

            // TODO: Register fragment shader variant if needed
            // The fragment pipeline uses different shader registration

            return benchGraph;
        }

        case PipelineType::HardwareRT:
            return BuildHardwareRTGraph(graph, config, config.screenWidth, config.screenHeight);

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
    uint32_t height)
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
    result.infra = BuildInfrastructure(graph, width, height, true);
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
    bool /*enableValidation*/)
{
    // Window parameters
    auto* window = static_cast<RG::WindowNode*>(graph->GetInstance(nodes.window));
    if (window) {
        window->SetParameter(RG::WindowNodeConfig::PARAM_WIDTH, width);
        window->SetParameter(RG::WindowNodeConfig::PARAM_HEIGHT, height);
    }

    // Device parameters (GPU index = 0)
    auto* device = static_cast<RG::DeviceNode*>(graph->GetInstance(nodes.device));
    if (device) {
        device->SetParameter(RG::DeviceNodeConfig::PARAM_GPU_INDEX, 0u);
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
    std::cerr << "  [FragPipe] Creating shaderLib..." << std::endl;
    nodes.shaderLib = graph->AddNode<RG::ShaderLibraryNodeType>("benchmark_fragment_shader_lib");
    std::cerr << "  [FragPipe] Creating descriptorGatherer..." << std::endl;
    nodes.descriptorGatherer = graph->AddNode<RG::DescriptorResourceGathererNodeType>("benchmark_fragment_desc_gatherer");
    std::cerr << "  [FragPipe] Creating pushConstantGatherer..." << std::endl;
    nodes.pushConstantGatherer = graph->AddNode<RG::PushConstantGathererNodeType>("benchmark_fragment_pc_gatherer");
    std::cerr << "  [FragPipe] Creating descriptorSet..." << std::endl;
    nodes.descriptorSet = graph->AddNode<RG::DescriptorSetNodeType>("benchmark_fragment_descriptors");
    std::cerr << "  [FragPipe] Creating renderPass..." << std::endl; std::cerr.flush();
    std::cerr << "  [FragPipe] Graph ptr: " << (void*)graph << std::endl;

    std::cerr.flush();
    nodes.renderPass = graph->AddNode<RG::RenderPassNodeType>("benchmark_render_pass");
    std::cerr << "  [FragPipe] RenderPass created!" << std::endl;
    std::cerr << "  [FragPipe] RenderPass created. Creating framebuffer..." << std::endl; std::cerr.flush();
    nodes.framebuffer = graph->AddNode<RG::FramebufferNodeType>("benchmark_framebuffer");
    std::cerr << "  [FragPipe] Creating graphics pipeline..." << std::endl;
    nodes.pipeline = graph->AddNode<RG::GraphicsPipelineNodeType>("benchmark_graphics_pipeline");
    std::cerr << "  [FragPipe] Creating drawCommand (GeometryRenderNode)..." << std::endl;
    nodes.drawCommand = graph->AddNode<RG::GeometryRenderNodeType>("benchmark_draw_command");

    std::cerr << "  [FragPipe] Configuring parameters..." << std::endl;
    // Configure parameters
    ConfigureFragmentPipelineParams(graph, nodes, infra, vertexShaderPath, fragmentShaderPath);
    std::cerr << "  [FragPipe] Done!" << std::endl;

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

    // RenderPass + SwapChain -> Framebuffer (uses FramebufferNodeConfig)
    // Framebuffer needs render pass and swapchain image views
    // Note: Exact connections depend on FramebufferNodeConfig slot definitions

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
    uint32_t height)
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
    std::cerr << "[Fragment] Using shaders: " << vertexShader << " + " << fragmentShader << std::endl;

    // Build all subgraphs
    std::cerr << "[Fragment] Building infrastructure..." << std::endl;
    result.infra = BuildInfrastructure(graph, width, height, true);

    std::cerr << "[Fragment] Building fragment pipeline..." << std::endl;
    result.fragment = BuildFragmentPipeline(
        graph, result.infra,
        vertexShader,           // Full-screen triangle vertex shader
        fragmentShader          // Fragment shader ray marching
    );

    std::cerr << "[Fragment] Building ray march scene..." << std::endl;
    result.rayMarch = BuildRayMarchScene(graph, result.infra, scene);

    std::cerr << "[Fragment] Building output..." << std::endl;
    result.output = BuildOutput(graph, result.infra, false);

    std::cerr << "[Fragment] Registering shaders..." << std::endl;
    // Register shader builder for vertex + fragment shaders
    RegisterFragmentShader(graph, result.fragment, vertexShader, fragmentShader);

    std::cerr << "[Fragment] Wiring variadic resources..." << std::endl;
    // Wire variadic resources (descriptors and push constants)
    WireFragmentVariadicResources(graph, result.infra, result.fragment, result.rayMarch);

    std::cerr << "[Fragment] Connecting subgraphs..." << std::endl;
    // Connect subgraphs
    ConnectFragmentRayMarch(graph, result.infra, result.fragment, result.rayMarch, result.output);

    std::cerr << "[Fragment] Build complete!" << std::endl;
    return result;
}

//==============================================================================
// Hardware RT Pipeline (Stub)
//==============================================================================

BenchmarkGraph BenchmarkGraphFactory::BuildHardwareRTGraph(
    RG::RenderGraph* /*graph*/,
    const TestConfiguration& /*config*/,
    uint32_t /*width*/,
    uint32_t /*height*/)
{
    // TODO: Implement when VK_KHR_ray_tracing_pipeline support is added
    //
    // Implementation requirements:
    // 1. VK_KHR_ray_tracing_pipeline extension
    //    - Check device support via vkGetPhysicalDeviceFeatures2 with
    //      VkPhysicalDeviceRayTracingPipelineFeaturesKHR
    //
    // 2. VK_KHR_acceleration_structure extension
    //    - Required for building bottom-level (BLAS) and top-level (TLAS)
    //      acceleration structures
    //
    // 3. New node types needed:
    //    - AccelerationStructureNode: Builds BLAS from voxel geometry
    //    - TopLevelASNode: Builds TLAS from BLAS instances
    //    - RTShaderLibraryNode: Ray gen, closest hit, miss shaders
    //    - ShaderBindingTableNode: SBT management
    //    - RayTracingPipelineNode: VkRayTracingPipelineCreateInfoKHR
    //    - RayTraceDispatchNode: vkCmdTraceRaysKHR
    //
    // 4. Shader types:
    //    - Ray generation shader (.rgen): Camera ray generation
    //    - Closest hit shader (.rchit): Surface shading
    //    - Miss shader (.rmiss): Background color
    //    - Optional: Any hit, intersection shaders for transparency
    //
    // 5. Memory requirements:
    //    - Scratch buffers for AS building
    //    - AS storage buffers
    //    - SBT buffer
    //
    // 6. Comparison metrics:
    //    - Hardware RT vs software ray marching performance
    //    - Memory overhead of acceleration structures
    //    - Build time for AS vs octree

    throw std::runtime_error(
        "BuildHardwareRTGraph: Hardware ray tracing pipeline not yet implemented. "
        "Requires VK_KHR_ray_tracing_pipeline and VK_KHR_acceleration_structure extensions. "
        "See implementation notes in BenchmarkGraphFactory.cpp for requirements."
    );
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
    // VoxelRayMarch.comp Descriptor Bindings (Set 0)
    //--------------------------------------------------------------------------
    // Binding constants from VoxelRayMarchNames.h (hard-coded for portability)
    constexpr uint32_t BINDING_OUTPUT_IMAGE = 0;
    constexpr uint32_t BINDING_ESVO_NODES = 1;
    constexpr uint32_t BINDING_BRICK_DATA = 2;
    constexpr uint32_t BINDING_MATERIALS = 3;
    constexpr uint32_t BINDING_TRACE_WRITE_INDEX = 4;
    constexpr uint32_t BINDING_OCTREE_CONFIG = 5;

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
        // Extract program name from shader filename (remove extension)
        std::string programName = shaderName;
        auto dotPos = programName.rfind('.');
        if (dotPos != std::string::npos) {
            programName = programName.substr(0, dotPos);
        }

        // Search paths for shader source
        std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
            std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
            std::string("shaders/") + shaderName,
            std::string("../shaders/") + shaderName,
            shaderName
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

        // Extract program name from fragment shader (remove extension)
        std::string programName = fragmentShaderName;
        auto dotPos = programName.rfind('.');
        if (dotPos != std::string::npos) {
            programName = programName.substr(0, dotPos);
        }

        // Helper to find shader file
        auto findShader = [](const std::string& shaderName) -> std::filesystem::path {
            std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
                std::string("shaders/") + shaderName,
                std::string("../shaders/") + shaderName,
                shaderName
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

    // Descriptor binding indices (matching VoxelRayMarch.frag layout)
    // binding 0: outputImage (handled by swapchain, not via variadic)
    constexpr uint32_t BINDING_ESVO_NODES = 1;
    constexpr uint32_t BINDING_BRICK_DATA = 2;
    constexpr uint32_t BINDING_MATERIALS = 3;
    constexpr uint32_t BINDING_DEBUG_CAPTURE = 4;
    constexpr uint32_t BINDING_OCTREE_CONFIG = 5;

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

} // namespace Vixen::Profiler
