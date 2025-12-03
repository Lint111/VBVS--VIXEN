#include "Profiler/BenchmarkGraphFactory.h"

// RenderGraph core
#include <Core/RenderGraph.h>
#include <Core/TypedConnection.h>
#include <Core/NodeInstance.h>
#include <Core/GraphLifecycleHooks.h>

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

    // Create scene info from config
    SceneInfo scene = SceneInfo::FromResolutionAndDensity(
        config.voxelResolution,
        config.densityPercent * 100.0f,  // Convert from 0-1 to 0-100 percent
        MapSceneType(config.sceneType),
        config.testId
    );

    // Build all subgraphs
    result.infra = BuildInfrastructure(graph, width, height, true);
    result.compute = BuildComputePipeline(graph, result.infra, "VoxelRayMarch.comp");
    result.rayMarch = BuildRayMarchScene(graph, result.infra, scene);
    result.output = BuildOutput(graph, result.infra, false);

    // Connect subgraphs
    ConnectComputeRayMarch(graph, result.infra, result.compute, result.rayMarch, result.output);

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
    // Map config scene type strings to VoxelGridNode scene type parameter
    if (sceneType == "cornell" || sceneType == "cornell_box") {
        return "cornell";
    }
    if (sceneType == "cave") {
        return "cave";
    }
    if (sceneType == "urban") {
        return "urban";
    }
    if (sceneType == "test") {
        return "test";
    }
    if (sceneType == "sparse_architectural") {
        return "sparse";
    }
    if (sceneType == "dense_organic") {
        return "dense";
    }

    // Default to cornell
    return "cornell";
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

    // Get or create the lifecycle hooks from the graph
    auto* hooks = graph->GetLifecycleHooks();
    if (!hooks) {
        throw std::invalid_argument("BenchmarkGraphFactory::WireProfilerHooks: graph has no lifecycle hooks");
    }

    // Register node-level hooks for dispatch timing
    // PreExecute: Called before node executes (for dispatch begin timing)
    hooks->RegisterNodeHook(
        RG::NodeLifecyclePhase::PreExecute,
        [&adapter, dispatchNodeName](RG::NodeInstance* node) {
            if (node && node->GetName() == dispatchNodeName) {
                adapter.OnDispatchBegin();
            }
            adapter.OnNodePreExecute(node ? node->GetName() : "");
        },
        "ProfilerDispatchBegin"
    );

    // PostExecute: Called after node executes (for dispatch end timing)
    // Note: Dispatch dimensions need to be passed externally since node doesn't expose them
    hooks->RegisterNodeHook(
        RG::NodeLifecyclePhase::PostExecute,
        [&adapter](RG::NodeInstance* node) {
            adapter.OnNodePostExecute(node ? node->GetName() : "");
        },
        "ProfilerDispatchEnd"
    );

    // PreCleanup: Called before cleanup for metrics extraction
    hooks->RegisterNodeHook(
        RG::NodeLifecyclePhase::PreCleanup,
        [&adapter](RG::NodeInstance* node) {
            adapter.OnNodePreCleanup(node ? node->GetName() : "");
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
            dispatchNodeName = dispatchNode->GetName();
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

} // namespace Vixen::Profiler
