#pragma once

#include <Core/RenderGraph.h>
#include <CleanupStack.h>
#include "BenchmarkConfig.h"
#include "SceneInfo.h"
#include <string>
#include <optional>
#include <functional>

// Namespace alias to avoid RenderGraph class/namespace ambiguity
namespace RG = Vixen::RenderGraph;

namespace Vixen::Profiler {

// Forward declarations
class Logger;

/// Node handle collections for subgraph sections
/// Each struct groups related nodes for a logical pipeline stage

/// Infrastructure nodes: Device setup, window, swapchain, synchronization
struct InfrastructureNodes {
    RG::NodeHandle instance;
    RG::NodeHandle window;
    RG::NodeHandle device;
    RG::NodeHandle swapchain;
    RG::NodeHandle commandPool;
    RG::NodeHandle frameSync;

    /// Check if all required nodes are valid
    bool IsValid() const {
        return instance.IsValid() && window.IsValid() && device.IsValid() &&
               swapchain.IsValid() && commandPool.IsValid() && frameSync.IsValid();
    }
};

/// Compute pipeline nodes: Shader, descriptors, pipeline, dispatch
struct ComputePipelineNodes {
    RG::NodeHandle shaderLib;
    RG::NodeHandle descriptorGatherer;
    RG::NodeHandle pushConstantGatherer;
    RG::NodeHandle descriptorSet;
    RG::NodeHandle pipeline;
    RG::NodeHandle dispatch;

    /// Check if all required nodes are valid
    bool IsValid() const {
        return shaderLib.IsValid() && descriptorGatherer.IsValid() &&
               pushConstantGatherer.IsValid() && descriptorSet.IsValid() &&
               pipeline.IsValid() && dispatch.IsValid();
    }
};

/// Ray marching scene nodes: Camera and voxel data
struct RayMarchNodes {
    RG::NodeHandle camera;
    RG::NodeHandle voxelGrid;
    RG::NodeHandle input;  // Input handling for camera control

    /// Check if all required nodes are valid
    bool IsValid() const {
        return camera.IsValid() && voxelGrid.IsValid();
    }
};

/// Output/presentation nodes
struct OutputNodes {
    RG::NodeHandle present;
    RG::NodeHandle framebuffer;  // Optional for fragment shader path
    RG::NodeHandle debugCapture; // Optional debug buffer reader

    /// Check if present node is valid (framebuffer and debug are optional)
    bool IsValid() const {
        return present.IsValid();
    }
};

/// Complete benchmark graph structure
struct BenchmarkGraph {
    InfrastructureNodes infra;
    ComputePipelineNodes compute;
    RayMarchNodes rayMarch;
    OutputNodes output;

    /// Check if all subgraphs are valid
    bool IsValid() const {
        return infra.IsValid() && compute.IsValid() && rayMarch.IsValid() && output.IsValid();
    }
};

/// Shader builder function type for registering custom shader configurations
/// Returns a ShaderBundleBuilder configured for the specific shader
using ShaderBuilderFunc = std::function<void(void* shaderLibNode, int vulkanVer, int spirvVer)>;

/**
 * @brief Factory for creating switchable benchmark render graphs
 *
 * Extracts the monolithic BuildRenderGraph() into reusable subgraph builders.
 * Each Build* method creates a logical group of nodes without connecting them.
 * Connect* methods wire the subgraphs together.
 *
 * Usage:
 *   RenderGraph graph(&registry);
 *   auto infra = BenchmarkGraphFactory::BuildInfrastructure(&graph, 1920, 1080);
 *   auto compute = BenchmarkGraphFactory::BuildComputePipeline(&graph, infra, "VoxelRayMarch.comp");
 *   auto scene = BenchmarkGraphFactory::BuildRayMarchScene(&graph, infra, sceneInfo);
 *   auto output = BenchmarkGraphFactory::BuildOutput(&graph, infra);
 *   BenchmarkGraphFactory::ConnectComputeRayMarch(&graph, infra, compute, scene, output);
 */
class BenchmarkGraphFactory {
public:
    //==========================================================================
    // Subgraph Builders
    //==========================================================================

    /**
     * @brief Build infrastructure subgraph (device, window, swapchain, sync)
     *
     * Creates core Vulkan infrastructure nodes:
     * - InstanceNode: VkInstance with optional validation layers
     * - WindowNode: Platform window with specified dimensions
     * - DeviceNode: Physical/logical device selection
     * - SwapChainNode: Presentation swapchain
     * - CommandPoolNode: Command buffer allocation
     * - FrameSyncNode: Fences and semaphores for synchronization
     *
     * @param graph Target render graph
     * @param width Window width in pixels
     * @param height Window height in pixels
     * @param enableValidation Enable Vulkan validation layers (default: true)
     * @return InfrastructureNodes with handles to all created nodes
     */
    static InfrastructureNodes BuildInfrastructure(
        RG::RenderGraph* graph,
        uint32_t width,
        uint32_t height,
        bool enableValidation = true
    );

    /**
     * @brief Build compute pipeline subgraph
     *
     * Creates compute shader pipeline nodes:
     * - ShaderLibraryNode: Shader loading and compilation
     * - DescriptorResourceGathererNode: Collects descriptor bindings
     * - PushConstantGathererNode: Collects push constant data
     * - DescriptorSetNode: Descriptor set management
     * - ComputePipelineNode: Pipeline state object
     * - ComputeDispatchNode: Command recording and dispatch
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes (for device connection)
     * @param shaderPath Path to compute shader file
     * @param workgroupSizeX Workgroup size X dimension (default: 8)
     * @param workgroupSizeY Workgroup size Y dimension (default: 8)
     * @return ComputePipelineNodes with handles to all created nodes
     */
    static ComputePipelineNodes BuildComputePipeline(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const std::string& shaderPath,
        uint32_t workgroupSizeX = 8,
        uint32_t workgroupSizeY = 8
    );

    /**
     * @brief Build ray marching scene subgraph
     *
     * Creates scene-specific nodes:
     * - CameraNode: Orbit camera with ray generation
     * - VoxelGridNode: Procedural voxel scene generation
     * - InputNode: Keyboard/mouse input handling (optional)
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes (for device connection)
     * @param scene Scene configuration (resolution, density, type)
     * @return RayMarchNodes with handles to created nodes
     */
    static RayMarchNodes BuildRayMarchScene(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const SceneInfo& scene
    );

    /**
     * @brief Build output/presentation subgraph
     *
     * Creates presentation nodes:
     * - PresentNode: Swapchain presentation
     * - DebugBufferReaderNode: Optional debug capture (if enabled)
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes (for swapchain connection)
     * @param enableDebugCapture Enable debug buffer capture (default: false)
     * @return OutputNodes with handles to created nodes
     */
    static OutputNodes BuildOutput(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        bool enableDebugCapture = false
    );

    //==========================================================================
    // Connection Builders
    //==========================================================================

    /**
     * @brief Connect all subgraphs for compute ray march pipeline
     *
     * Wires infrastructure, compute pipeline, ray march scene, and output together.
     * Uses ConnectionBatch for atomic registration.
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes
     * @param compute Compute pipeline nodes
     * @param rayMarch Ray marching scene nodes
     * @param output Output/presentation nodes
     */
    static void ConnectComputeRayMarch(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const ComputePipelineNodes& compute,
        const RayMarchNodes& rayMarch,
        const OutputNodes& output
    );

    //==========================================================================
    // High-Level Graph Builders
    //==========================================================================

    /**
     * @brief Build complete compute ray march benchmark graph
     *
     * High-level convenience method that creates all subgraphs and connects them.
     * Equivalent to calling Build* and Connect* methods individually.
     *
     * @param graph Target render graph
     * @param config Benchmark test configuration
     * @param width Screen width in pixels
     * @param height Screen height in pixels
     * @return BenchmarkGraph with all node handles
     */
    static BenchmarkGraph BuildComputeRayMarchGraph(
        RG::RenderGraph* graph,
        const TestConfiguration& config,
        uint32_t width,
        uint32_t height
    );

    //==========================================================================
    // Future Pipeline Types (Stubs)
    //==========================================================================

    // Fragment shader ray marching pipeline
    // static BenchmarkGraph BuildFragmentRayMarchGraph(...);

    // Hardware RT pipeline (VK_KHR_ray_tracing_pipeline)
    // static BenchmarkGraph BuildHardwareRTGraph(...);

    // Hybrid pipeline (compute + fragment stages)
    // static BenchmarkGraph BuildHybridGraph(...);

private:
    //==========================================================================
    // Internal Helpers
    //==========================================================================

    /// Configure node parameters after creation
    static void ConfigureInfrastructureParams(
        RG::RenderGraph* graph,
        const InfrastructureNodes& nodes,
        uint32_t width,
        uint32_t height,
        bool enableValidation
    );

    static void ConfigureComputePipelineParams(
        RG::RenderGraph* graph,
        const ComputePipelineNodes& nodes,
        const InfrastructureNodes& infra,
        const std::string& shaderPath,
        uint32_t workgroupSizeX,
        uint32_t workgroupSizeY
    );

    static void ConfigureRayMarchSceneParams(
        RG::RenderGraph* graph,
        const RayMarchNodes& nodes,
        const SceneInfo& scene
    );

    static void ConfigureOutputParams(
        RG::RenderGraph* graph,
        const OutputNodes& nodes,
        bool enableDebugCapture
    );

    /// Map scene type string to VoxelGridNode scene parameter
    static std::string MapSceneType(const std::string& sceneType);
};

} // namespace Vixen::Profiler
