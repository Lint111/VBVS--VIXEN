#pragma once

#include <Core/RenderGraph.h>
#include <Core/GraphLifecycleHooks.h>
#include <CleanupStack.h>
#include "BenchmarkConfig.h"
#include "SceneInfo.h"
#include "ProfilerGraphAdapter.h"
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

/// Fragment (graphics) pipeline nodes: Vertex shader, fragment shader, render pass, pipeline
struct FragmentPipelineNodes {
    RG::NodeHandle shaderLib;
    RG::NodeHandle descriptorGatherer;
    RG::NodeHandle pushConstantGatherer;
    RG::NodeHandle descriptorSet;
    RG::NodeHandle renderPass;
    RG::NodeHandle framebuffer;
    RG::NodeHandle pipeline;        // GraphicsPipelineNode
    RG::NodeHandle drawCommand;     // GeometryRenderNode for fullscreen triangle rendering

    /// Check if all required nodes are valid
    bool IsValid() const {
        return shaderLib.IsValid() && descriptorGatherer.IsValid() &&
               pushConstantGatherer.IsValid() && descriptorSet.IsValid() &&
               renderPass.IsValid() && framebuffer.IsValid() && pipeline.IsValid() &&
               drawCommand.IsValid();
    }
};

/// Hardware ray tracing pipeline nodes (Phase K)
struct HardwareRTNodes {
    RG::NodeHandle shaderLib;            // ShaderLibraryNode for RT shaders
    RG::NodeHandle aabbConverter;        // VoxelAABBConverterNode
    RG::NodeHandle accelerationStructure; // AccelerationStructureNode (BLAS + TLAS)
    RG::NodeHandle rtPipeline;           // RayTracingPipelineNode
    RG::NodeHandle traceRays;            // TraceRaysNode

    /// Check if all required nodes are valid
    bool IsValid() const {
        return shaderLib.IsValid() && aabbConverter.IsValid() &&
               accelerationStructure.IsValid() && rtPipeline.IsValid() &&
               traceRays.IsValid();
    }
};

/// Complete benchmark graph structure
struct BenchmarkGraph {
    InfrastructureNodes infra;
    ComputePipelineNodes compute;
    FragmentPipelineNodes fragment;   // Used for fragment pipeline type
    HardwareRTNodes hardwareRT;       // Used for hardware RT pipeline type (Phase K)
    RayMarchNodes rayMarch;
    OutputNodes output;

    /// The pipeline type this graph was built for
    PipelineType pipelineType = PipelineType::Invalid;

    /// Check if all required subgraphs are valid based on pipeline type
    bool IsValid() const {
        if (!infra.IsValid() || !rayMarch.IsValid() || !output.IsValid()) {
            return false;
        }

        switch (pipelineType) {
            case PipelineType::Compute:
                return compute.IsValid();
            case PipelineType::Fragment:
                return fragment.IsValid();
            case PipelineType::HardwareRT:
                return hardwareRT.IsValid();
            case PipelineType::Hybrid:
                // Not yet implemented - return false
                return false;
            default:
                return false;
        }
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
     * @brief Build benchmark graph from test configuration
     *
     * Unified entry point that dispatches to appropriate pipeline builder based
     * on config.pipeline. Configures shader, scene, and dimensions from
     * TestConfiguration.
     *
     * Supported pipelines:
     * - "compute": Builds compute shader ray march graph
     * - "fragment": Builds fragment shader ray march graph
     * - "hardware_rt": Not yet implemented (throws)
     *
     * @param graph Target render graph
     * @param config Test configuration (pipeline, shader, scene, resolution)
     * @param suiteConfig Suite config for scene definitions (optional, used for
     *                    file-based scenes)
     * @return BenchmarkGraph with configured pipeline
     * @throws std::invalid_argument if graph is null or pipeline is unsupported
     */
    static BenchmarkGraph BuildFromConfig(
        RG::RenderGraph* graph,
        const TestConfiguration& config,
        const BenchmarkSuiteConfig* suiteConfig = nullptr
    );

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
    // Profiler Hook Wiring
    //==========================================================================

    /**
     * @brief Wire profiler adapter hooks to graph lifecycle events
     *
     * Registers the ProfilerGraphAdapter callbacks with the graph's lifecycle hooks
     * for automatic frame and dispatch profiling. Call after graph construction
     * but before compilation.
     *
     * Registers hooks for:
     * - Frame begin/end (via graph-level PostCompilation hook)
     * - Node pre/post execute (for dispatch timing)
     * - Pre-cleanup (for metrics extraction)
     *
     * @param graph Target render graph (must have a valid GraphLifecycleHooks)
     * @param adapter ProfilerGraphAdapter to wire callbacks to
     * @param dispatchNodeName Name of the compute dispatch node for GPU timing hooks
     *        (default: "benchmark_dispatch")
     *
     * Usage:
     * @code
     * ProfilerGraphAdapter adapter;
     * auto benchGraph = BenchmarkGraphFactory::BuildComputeRayMarchGraph(graph, config, w, h);
     * BenchmarkGraphFactory::WireProfilerHooks(graph, adapter);
     * // In render loop:
     * adapter.SetFrameContext(cmdBuffer, frameIndex);
     * adapter.OnFrameBegin();
     * // ... render ...
     * adapter.OnDispatchEnd(dispatchWidth, dispatchHeight);
     * adapter.OnFrameEnd();
     * @endcode
     */
    static void WireProfilerHooks(
        RG::RenderGraph* graph,
        ProfilerGraphAdapter& adapter,
        const std::string& dispatchNodeName = "benchmark_dispatch"
    );

    /**
     * @brief Wire profiler hooks using BenchmarkGraph node handles
     *
     * Convenience overload that extracts the dispatch node name from the
     * BenchmarkGraph structure.
     *
     * @param graph Target render graph
     * @param adapter ProfilerGraphAdapter to wire callbacks to
     * @param benchGraph BenchmarkGraph containing node handles
     */
    static void WireProfilerHooks(
        RG::RenderGraph* graph,
        ProfilerGraphAdapter& adapter,
        const BenchmarkGraph& benchGraph
    );

    /**
     * @brief Check if profiler hooks have been wired for a graph
     *
     * @param graph Graph to check
     * @return true if WireProfilerHooks has been called on this graph
     */
    static bool HasProfilerHooks(const RG::RenderGraph* graph);

    //==========================================================================
    // Additional Pipeline Types
    //==========================================================================

    /**
     * @brief Build fragment pipeline subgraph
     *
     * Creates fragment shader pipeline nodes for full-screen ray marching:
     * - ShaderLibraryNode: Vertex + Fragment shader loading
     * - DescriptorResourceGathererNode: Collects descriptor bindings
     * - PushConstantGathererNode: Collects push constant data
     * - DescriptorSetNode: Descriptor set management
     * - RenderPassNode: Single-pass render pass
     * - FramebufferNode: Framebuffer for each swapchain image
     * - GraphicsPipelineNode: Graphics pipeline state object
     *
     * Note: This creates a full-screen triangle approach - the vertex shader
     * generates a full-screen triangle and the fragment shader performs ray marching.
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes (for device/swapchain connection)
     * @param vertexShaderPath Path to vertex shader (default: fullscreen pass-through)
     * @param fragmentShaderPath Path to fragment shader (ray marching)
     * @return FragmentPipelineNodes with handles to all created nodes
     */
    static FragmentPipelineNodes BuildFragmentPipeline(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const std::string& vertexShaderPath,
        const std::string& fragmentShaderPath
    );

    /**
     * @brief Connect all subgraphs for fragment ray march pipeline
     *
     * Wires infrastructure, fragment pipeline, ray march scene, and output together.
     * Uses ConnectionBatch for atomic registration.
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes
     * @param fragment Fragment pipeline nodes
     * @param rayMarch Ray marching scene nodes
     * @param output Output/presentation nodes
     */
    static void ConnectFragmentRayMarch(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const FragmentPipelineNodes& fragment,
        const RayMarchNodes& rayMarch,
        const OutputNodes& output
    );

    /**
     * @brief Build hardware ray tracing pipeline subgraph
     *
     * Creates hardware RT nodes for VK_KHR_ray_tracing_pipeline:
     * - VoxelAABBConverterNode: Extracts AABBs from voxel grid
     * - AccelerationStructureNode: Builds BLAS + TLAS
     * - RayTracingPipelineNode: Creates RT pipeline + SBT
     * - TraceRaysNode: Dispatches vkCmdTraceRaysKHR
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes (for device connection)
     * @return HardwareRTNodes with handles to all created nodes
     */
    static HardwareRTNodes BuildHardwareRT(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra
    );

    /**
     * @brief Connect all subgraphs for hardware ray tracing pipeline
     *
     * Wires infrastructure, hardware RT, ray march scene, and output together.
     * Uses ConnectionBatch for atomic registration.
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes
     * @param hardwareRT Hardware RT pipeline nodes
     * @param rayMarch Ray marching scene nodes (provides voxel data)
     * @param output Output/presentation nodes
     */
    static void ConnectHardwareRT(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const HardwareRTNodes& hardwareRT,
        const RayMarchNodes& rayMarch,
        const OutputNodes& output
    );

    /**
     * @brief Build complete fragment ray march benchmark graph
     *
     * High-level convenience method that creates all subgraphs and connects them
     * for fragment shader-based ray marching (full-screen triangle approach).
     *
     * @param graph Target render graph
     * @param config Benchmark test configuration
     * @param width Screen width in pixels
     * @param height Screen height in pixels
     * @return BenchmarkGraph with all node handles (pipelineType = Fragment)
     */
    static BenchmarkGraph BuildFragmentRayMarchGraph(
        RG::RenderGraph* graph,
        const TestConfiguration& config,
        uint32_t width,
        uint32_t height
    );

    /**
     * @brief Build complete hardware ray tracing benchmark graph
     *
     * High-level convenience method that creates all subgraphs and connects them
     * for VK_KHR_ray_tracing_pipeline based rendering.
     *
     * Pipeline structure:
     * - VoxelGridNode: Generates voxel data (shared with compute/fragment)
     * - VoxelAABBConverterNode: Extracts AABBs from voxel grid
     * - AccelerationStructureNode: Builds BLAS + TLAS
     * - RayTracingPipelineNode: Creates RT pipeline + SBT
     * - TraceRaysNode: Dispatches vkCmdTraceRaysKHR
     *
     * Requires RTX support (checked via VulkanDevice::CheckRTXSupport).
     *
     * @param graph Target render graph
     * @param config Benchmark test configuration
     * @param width Screen width in pixels
     * @param height Screen height in pixels
     * @return BenchmarkGraph with all node handles (pipelineType = HardwareRT)
     * @throws std::runtime_error if RTX extensions are not available
     */
    static BenchmarkGraph BuildHardwareRTGraph(
        RG::RenderGraph* graph,
        const TestConfiguration& config,
        uint32_t width,
        uint32_t height
    );

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

    static void ConfigureFragmentPipelineParams(
        RG::RenderGraph* graph,
        const FragmentPipelineNodes& nodes,
        const InfrastructureNodes& infra,
        const std::string& vertexShaderPath,
        const std::string& fragmentShaderPath
    );

    /// Map scene type string to VoxelGridNode scene parameter
    static std::string MapSceneType(const std::string& sceneType);

    //==========================================================================
    // Variadic Resource Wiring
    //==========================================================================

    /**
     * @brief Wire descriptor resources and push constants using ConnectVariadic
     *
     * This connects the ray march scene nodes (VoxelGrid, Camera) to the
     * descriptor gatherer and push constant gatherer using ConnectVariadic
     * for proper binding-indexed resource population.
     *
     * Uses VoxelRayMarch.comp binding layout:
     * - Descriptor Set 0:
     *   - Binding 0: outputImage (storage image, swapchain)
     *   - Binding 1: esvoNodes (SSBO, octree node buffer)
     *   - Binding 2: brickData (SSBO, voxel brick data)
     *   - Binding 3: materials (SSBO, material palette)
     *   - Binding 4: traceWriteIndex (SSBO, debug capture)
     *   - Binding 5: octreeConfig (UBO, scale/depth params)
     *
     * - Push Constants:
     *   - cameraPos (vec3), time (float)
     *   - cameraDir (vec3), fov (float)
     *   - cameraUp (vec3), aspect (float)
     *   - cameraRight (vec3), debugMode (int)
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes
     * @param compute Compute pipeline nodes
     * @param rayMarch Ray march scene nodes
     */
    static void WireVariadicResources(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const ComputePipelineNodes& compute,
        const RayMarchNodes& rayMarch
    );

    //==========================================================================
    // Shader Builder Registration
    //==========================================================================

    /**
     * @brief Register compute shader builder
     *
     * Registers a ShaderBundleBuilder callback that loads the specified shader
     * with proper include paths for the shader preprocessor.
     *
     * The shader name is used directly as the filename - it will be searched
     * in the shader directories (shaders/, ../shaders/, VIXEN_SHADER_SOURCE_DIR).
     *
     * @param graph Target render graph
     * @param compute Compute pipeline nodes (shader library node)
     * @param shaderName Shader filename (e.g., "VoxelRayMarch.comp")
     */
    static void RegisterComputeShader(
        RG::RenderGraph* graph,
        const ComputePipelineNodes& compute,
        const std::string& shaderName
    );

    /**
     * @brief Register fragment shader builder (vertex + fragment pair)
     *
     * Registers a ShaderBundleBuilder callback that loads the specified vertex
     * and fragment shaders with proper include paths for the shader preprocessor.
     *
     * @param graph Target render graph
     * @param fragment Fragment pipeline nodes (shader library node)
     * @param vertexShaderName Vertex shader filename (e.g., "Fullscreen.vert")
     * @param fragmentShaderName Fragment shader filename (e.g., "VoxelRayMarch.frag")
     */
    static void RegisterFragmentShader(
        RG::RenderGraph* graph,
        const FragmentPipelineNodes& fragment,
        const std::string& vertexShaderName,
        const std::string& fragmentShaderName
    );

    /**
     * @brief Wire descriptor resources and push constants for fragment pipeline
     *
     * Similar to WireVariadicResources but for fragment/graphics pipeline.
     * Connects VoxelGrid buffers and Camera data to the fragment pipeline's
     * descriptor and push constant gatherers.
     *
     * @param graph Target render graph
     * @param infra Infrastructure nodes
     * @param fragment Fragment pipeline nodes
     * @param rayMarch Ray march scene nodes
     */
    static void WireFragmentVariadicResources(
        RG::RenderGraph* graph,
        const InfrastructureNodes& infra,
        const FragmentPipelineNodes& fragment,
        const RayMarchNodes& rayMarch
    );

    /**
     * @brief Register RTX shader builder (raygen, miss, closesthit, intersection)
     *
     * Registers a ShaderBundleBuilder callback that loads all RT shader stages
     * with proper include paths for the shader preprocessor.
     *
     * @param graph Target render graph
     * @param hardwareRT Hardware RT nodes (shader library node)
     * @param raygenShader Ray generation shader filename (e.g., "VoxelRT.rgen")
     * @param missShader Miss shader filename (e.g., "VoxelRT.rmiss")
     * @param closestHitShader Closest hit shader filename (e.g., "VoxelRT.rchit")
     * @param intersectionShader Intersection shader filename (e.g., "VoxelRT.rint")
     */
    static void RegisterRTXShader(
        RG::RenderGraph* graph,
        const HardwareRTNodes& hardwareRT,
        const std::string& raygenShader,
        const std::string& missShader,
        const std::string& closestHitShader,
        const std::string& intersectionShader
    );

    /**
     * @brief Configure hardware RT pipeline node parameters
     *
     * Sets up parameters for AABB converter, acceleration structure, and trace rays nodes.
     *
     * @param graph Target render graph
     * @param nodes Hardware RT nodes
     * @param width Output width in pixels
     * @param height Output height in pixels
     */
    static void ConfigureHardwareRTParams(
        RG::RenderGraph* graph,
        const HardwareRTNodes& nodes,
        uint32_t width,
        uint32_t height
    );
};

} // namespace Vixen::Profiler
