#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include "Core/TypedConnection.h"  // Typed slot connection helpers
#include "VulkanShader.h"  // MVP: Direct shader loading
#include "wrapper.h"  // MVP: File reading utility
#include "CashSystem/MainCacher.h"  // Cache system initialization
#include "Core/LoopManager.h"  // Phase 0.4: Loop system

// Global VkInstance for nodes to access (temporary Phase 1 hack)
VkInstance g_VulkanInstance = VK_NULL_HANDLE;

// Include all node types
#include "Nodes/WindowNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/FrameSyncNode.h"  // Phase 0.2: Frame-in-flight synchronization
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/DepthBufferNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/VertexBufferNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/GeometryRenderNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/ConstantNode.h"  // MVP: Generic parameter node
#include "Nodes/ConstantNodeType.h"  // MVP: ConstantNode factory
#include "Data/Nodes/ConstantNodeConfig.h"  // MVP: ConstantNode configuration
#include "Nodes/LoopBridgeNode.h"  // Phase 0.4: Loop system bridge
#include "Nodes/BoolOpNode.h"  // Phase 0.4: Boolean logic for loops
#include "Nodes/ComputePipelineNode.h"  // Phase G: Compute pipeline
#include "Nodes/ComputeDispatchNode.h"  // Phase G: Compute dispatch
#include "Nodes/DescriptorResourceGathererNode.h"  // Phase H: Descriptor resource gatherer
#include "Nodes/CameraNode.h"  // Ray marching: Camera UBO
#include "Nodes/VoxelGridNode.h"  // Ray marching: 3D voxel texture
#include <ShaderManagement/ShaderBundleBuilder.h>  // Phase G: Shader builder API
#include "../generated/sdi/VoxelRayMarchNames.h"  // Generated shader binding constants

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

std::unique_ptr<VulkanGraphApplication> VulkanGraphApplication::instance;
std::once_flag VulkanGraphApplication::onlyOnce;

VulkanGraphApplication::VulkanGraphApplication()
    : VulkanApplicationBase(),
      currentFrame(0),
      graphCompiled(false),
      width(500),
      height(500) {

    // Enable main logger for application-level logging
    if (mainLogger) {
        mainLogger->SetEnabled(true);
        mainLogger->SetTerminalOutput(false);  // Set to true to see logs in real-time
        mainLogger->Info("VulkanGraphApplication (Graph-based) Starting");
    }
}

VulkanGraphApplication::~VulkanGraphApplication() {
    DeInitialize();
}

VulkanGraphApplication* VulkanGraphApplication::GetInstance() {
    std::call_once(onlyOnce, []() { instance.reset(new VulkanGraphApplication()); });
    return instance.get();
}

void VulkanGraphApplication::Initialize() {
    std::cout << "[DEBUG] VulkanGraphApplication::Initialize() - START\n" << std::flush;
    mainLogger->Info("VulkanGraphApplication Initialize START");

    std::cout << "[DEBUG] About to call VulkanApplicationBase::Initialize()\n" << std::flush;
    // Initialize base Vulkan core (instance, device)
    VulkanApplicationBase::Initialize();
    std::cout << "[DEBUG] VulkanApplicationBase::Initialize() returned\n" << std::flush;

    mainLogger->Info("VulkanGraphApplication Base initialized");

    std::cout << "[DEBUG] About to export instance globally\n" << std::flush;
    // PHASE 1: Export instance globally for nodes to access
    g_VulkanInstance = instanceObj.instance;
    std::cout << "[DEBUG] Instance exported globally\n" << std::flush;

    mainLogger->Info("VulkanGraphApplication Instance exported globally");

    std::cout << "[DEBUG] Creating node type registry\n" << std::flush;
    // Create node type registry
    nodeRegistry = std::make_unique<NodeTypeRegistry>();
    std::cout << "[DEBUG] Node type registry created\n" << std::flush;

    std::cout << "[DEBUG] About to register node types\n" << std::flush;
    // Register all node types
    RegisterNodeTypes();
    std::cout << "[DEBUG] Node types registered\n" << std::flush;

    std::cout << "[DEBUG] Creating MessageBus\n" << std::flush;
    // Create render graph
    // Create a MessageBus for event-driven coordination and inject into RenderGraph
    messageBus = std::make_unique<Vixen::EventBus::MessageBus>();
    std::cout << "[DEBUG] MessageBus created\n" << std::flush;

    std::cout << "[DEBUG] Initializing MainCacher\n" << std::flush;
    // Initialize MainCacher and connect it to MessageBus for device invalidation events
    auto& mainCacher = CashSystem::MainCacher::Instance();
    std::cout << "[DEBUG] MainCacher instance obtained\n" << std::flush;
    mainCacher.Initialize(messageBus.get());
    std::cout << "[DEBUG] MainCacher initialized\n" << std::flush;
    mainLogger->Info("MainCacher initialized and subscribed to device invalidation events");

    // NOTE: Cache loading will happen automatically when devices request cached resources
    // This is because we need VulkanDevice to be created first before we can load caches

    std::cout << "[DEBUG] Creating RenderGraph\n" << std::flush;
    // Create RenderGraph with all dependencies (registry, messageBus, logger, mainCacher)
    renderGraph = std::make_unique<RenderGraph>(
        nodeRegistry.get(),
        messageBus.get(),
        mainLogger.get(),
        &mainCacher
    );
    std::cout << "[DEBUG] RenderGraph created\n" << std::flush;

    std::cout << "[DEBUG] Subscribing to WindowCloseEvent\n" << std::flush;
    // Subscribe to shutdown events
    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            HandleShutdownRequest();
            return true;
        }
    );
    std::cout << "[DEBUG] WindowCloseEvent subscription complete\n" << std::flush;

    std::cout << "[DEBUG] Subscribing to ShutdownAckEvent\n" << std::flush;
    messageBus->Subscribe(
        Vixen::EventBus::ShutdownAckEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            try {
                const auto* ackMsg = dynamic_cast<const Vixen::EventBus::ShutdownAckEvent*>(&msg);
                if (ackMsg && shutdownRequested) {
                    // Copy string immediately to avoid any lifetime issues
                    std::string systemName = ackMsg->systemName;
                    HandleShutdownAck(systemName);
                }
            } catch (...) {
                // Ignore any errors during shutdown handling
            }
            return true;
        }
    );
    std::cout << "[DEBUG] ShutdownAckEvent subscription complete\n" << std::flush;

    if (mainLogger) {
        mainLogger->Info("RenderGraph created successfully");
    }

    std::cout << "[DEBUG] Registering physics loop\n" << std::flush;
    // Phase 0.4: Register loops with the graph
    // Physics loop at 60Hz with multiple-step catchup
    physicsLoopID = renderGraph->RegisterLoop(LoopConfig{
        1.0 / 60.0,  // 60Hz timestep
        "PhysicsLoop",
        LoopCatchupMode::MultipleSteps,
        0.25  // Max 250ms catchup
    });
    std::cout << "[DEBUG] Physics loop registered with ID: " << physicsLoopID << "\n" << std::flush;
    mainLogger->Info("Registered PhysicsLoop (60Hz) with ID: " + std::to_string(physicsLoopID));

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication initialized successfully");
    }
    std::cout << "[DEBUG] VulkanGraphApplication::Initialize() - COMPLETE\n" << std::flush;
}

void VulkanGraphApplication::Prepare() {
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::Prepare] START");
    }
    isPrepared = false;

    try {
        // PHASE 1: Nodes manage their own resources
        // Build the render graph - nodes allocate their own resources
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] Calling BuildRenderGraph...");
        }
        BuildRenderGraph();
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] BuildRenderGraph complete");
        }

        // Get window handle for graceful shutdown
        auto* windowInst = renderGraph->GetInstanceByName("main_window");
        if (windowInst) {
            auto* windowNode = dynamic_cast<Vixen::RenderGraph::WindowNode*>(windowInst);
            if (windowNode) {
                windowHandle = windowNode->GetWindow();
            }
        }

        // Compile the render graph - nodes set up their pipelines
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] Calling CompileRenderGraph...");
        }
        CompileRenderGraph();
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] CompileRenderGraph complete");
        }

        isPrepared = true;

        if (mainLogger) {
            mainLogger->Info("VulkanGraphApplication prepared and ready to render");
        }
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::Prepare] SUCCESS - isPrepared = true");
        }
    }
    catch (const std::exception& e) {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error(std::string("[VulkanGraphApplication::Prepare] EXCEPTION: ") + e.what());
        }
        isPrepared = false;
        throw;  // Re-throw to let main() handle it
    }
}

bool VulkanGraphApplication::Render() {
    if (!isPrepared || !graphCompiled || !renderGraph) {
        return false;
    }

    // Process window messages
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Render a complete frame via the graph
    // The graph internally handles:
    // - Event processing and deferred recompilation
    // - Image acquisition (SwapChainNode)
    // - Command buffer allocation & recording (GeometryRenderNode)
    // - Queue submission with semaphores (nodes manage sync)
    // - Presentation (PresentNode)
    VkResult result = renderGraph->RenderFrame();

    // Event-driven swapchain recreation is now handled internally by RenderGraph
    // VK_ERROR_OUT_OF_DATE_KHR will trigger events that mark nodes for recompilation
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        mainLogger->Error("Frame rendering failed with result: " + std::to_string(result));
        return false;
    }

    currentFrame++;
    return true;
}

void VulkanGraphApplication::Update() {
    if (!isPrepared) {
        return;
    }

    // Update time for both application and graph
    // Application time (legacy - kept for compatibility)
    time.Update();

    // Graph time (used by nodes for frame-rate independent animations)
    if (renderGraph) {
        renderGraph->UpdateTime();
    }

    // Process events and handle deferred recompilation
    // This must happen in update phase, not render phase, to allow:
    // - Updating without rendering (minimized windows)
    // - Different update/render frame rates
    // - Proper event-driven invalidation handling
    if (renderGraph) {
        renderGraph->ProcessEvents();
        renderGraph->RecompileDirtyNodes();
    }

    // Note: MVP matrix updates now handled by DescriptorSetNode during Execute()
    // using graph's centralized time system for frame-rate independent rotation
}

void VulkanGraphApplication::DeInitialize() {
    // Prevent double cleanup (called from both main and destructor)
    if (deinitialized) {
        return;
    }
    deinitialized = true;

    // Before destroying the render graph, extract logs from the main logger.
    // Node instances register child loggers with the main logger; if we
    // destroy the graph first those child loggers are destroyed and their
    // entries won't appear in the aggregated log output.
    if (mainLogger && mainLogger->IsEnabled()) {
        try {
            std::string logs = mainLogger->ExtractLogs();
            // Write logs into the binaries folder so logs are colocated with the build artifacts.
            std::ofstream logFile("binaries\\vulkan_app_log.txt");
            if (logFile.is_open()) {
                logFile << logs;
                logFile.close();
                mainLogger->Info("Logs written to binaries\\vulkan_app_log.txt");
            }
        } catch (...) {
            // Best-effort: don't throw during cleanup
        }

        // Clear all child logger pointers before destroying nodes
        // This prevents dangling pointer access during final cleanup
        mainLogger->ClearChildren();
    }

    // CRITICAL: Destroy render graph (triggers CleanupStack execution) BEFORE base class destroys device
    // NOTE: RenderGraph destructor handles cache saving automatically
    // This ensures all node-owned Vulkan resources (buffers, images, views, shaders) are destroyed
    // while the VkDevice is still valid.
    // Note: ConstantNode's cleanup callback will destroy the shader via registered callback
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] Destroying render graph...");
    }
    renderGraph.reset();
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] Render graph destroyed");
    }

    // Destroy node registry
    nodeRegistry.reset();

    // Graph nodes handle their own cleanup (including window)

    // Call base class cleanup (destroys device and instance)
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Calling base class DeInitialize...");
    }
    VulkanApplicationBase::DeInitialize();
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Base class DeInitialize complete");
    }

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}

void VulkanGraphApplication::CompileRenderGraph() {
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] START");
    }
    if (!renderGraph) {
        mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("[CompileRenderGraph] ERROR: renderGraph is null");
        }
        return;
    }

    // Field extraction now integrated into RenderGraph::Compile() via post-node-compile callbacks
    // Callbacks are registered during RegisterAll() and executed automatically as nodes compile
    renderGraph->Compile();
    graphCompiled = true;

    // Validate final graph
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] Validating graph...");
    }
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        mainLogger->Error("Render graph validation failed: " + errorMessage);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("[CompileRenderGraph] VALIDATION FAILED: " + errorMessage);
        }
        return;
    }
    mainLogger->Info("Render graph compiled and validated successfully");
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[CompileRenderGraph] Complete - " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
    }
}

void VulkanGraphApplication::RegisterNodeTypes() {
    if (!nodeRegistry) {
        mainLogger->Error("Cannot register node types: Registry not initialized");
        return;
    }

    mainLogger->Info("Registering all built-in node types");

    // Register all node types
    nodeRegistry->RegisterNodeType(std::make_unique<WindowNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DeviceNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<CommandPoolNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<FrameSyncNodeType>());  // Phase 0.2
    nodeRegistry->RegisterNodeType(std::make_unique<TextureLoaderNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DepthBufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<SwapChainNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<VertexBufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<RenderPassNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<FramebufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<ShaderLibraryNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DescriptorSetNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<GraphicsPipelineNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<GeometryRenderNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<PresentNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<ShaderConstantNodeType>());  // MVP: VulkanShader* injection
    nodeRegistry->RegisterNodeType(std::make_unique<ConstantNodeType>());  // Generic parameter injection
    nodeRegistry->RegisterNodeType(std::make_unique<LoopBridgeNodeType>());  // Phase 0.4: Loop system
    nodeRegistry->RegisterNodeType(std::make_unique<BoolOpNodeType>());  // Phase 0.4: Boolean logic
    nodeRegistry->RegisterNodeType(std::make_unique<ComputePipelineNodeType>());  // Phase G: Compute pipeline
    nodeRegistry->RegisterNodeType(std::make_unique<ComputeDispatchNodeType>());  // Phase G: Compute dispatch
    nodeRegistry->RegisterNodeType(std::make_unique<DescriptorResourceGathererNodeType>());  // Phase H: Descriptor resource gatherer
    nodeRegistry->RegisterNodeType(std::make_unique<CameraNodeType>());  // Ray marching: Camera UBO
    nodeRegistry->RegisterNodeType(std::make_unique<VoxelGridNodeType>());  // Ray marching: Voxel grid

    mainLogger->Info("Successfully registered 24 node types");
}

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    mainLogger->Info("Building complete render pipeline with typed connections");

    // ===================================================================
    // PHASE 1: Create all nodes
    // ===================================================================

    // --- Infrastructure Nodes ---
    NodeHandle windowNode = renderGraph->AddNode("Window", "main_window");
    NodeHandle deviceNode = renderGraph->AddNode("Device", "main_device");
    NodeHandle frameSyncNode = renderGraph->AddNode("FrameSync", "frame_sync");
    NodeHandle swapChainNode = renderGraph->AddNode("SwapChain", "main_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode("CommandPool", "main_cmd_pool");

    // --- Resource Nodes ---
    // DISABLED FOR COMPUTE TEST: Graphics pipeline nodes
    /*
    NodeHandle depthBufferNode = renderGraph->AddNode("DepthBuffer", "depth_buffer");
    NodeHandle vertexBufferNode = renderGraph->AddNode("VertexBuffer", "triangle_vb");
    NodeHandle textureNode = renderGraph->AddNode("TextureLoader", "main_texture");

    // --- Rendering Configuration Nodes ---
    NodeHandle renderPassNode = renderGraph->AddNode("RenderPass", "main_pass");
    NodeHandle framebufferNode = renderGraph->AddNode("Framebuffer", "main_fb");
    NodeHandle shaderLibNode = renderGraph->AddNode("ShaderLibrary", "shader_lib");
    NodeHandle descriptorSetNode = renderGraph->AddNode("DescriptorSet", "main_descriptors");
    NodeHandle pipelineNode = renderGraph->AddNode("GraphicsPipeline", "triangle_pipeline");

    // Phase 1: ShaderLibraryNode replaces manual shader loading
    // Removed ConstantNode - ShaderLibraryNode outputs VulkanShader directly

    // --- Execution Nodes ---
    NodeHandle geometryRenderNode = renderGraph->AddNode("GeometryRender", "triangle_render");
    */
    NodeHandle presentNode = renderGraph->AddNode("Present", "present");

    // --- Phase G: Compute Pipeline Nodes ---
    NodeHandle computeShaderLib = renderGraph->AddNode("ShaderLibrary", "compute_shader_lib");
    NodeHandle descriptorGatherer = renderGraph->AddNode("DescriptorResourceGatherer", "compute_desc_gatherer");  // Phase H
    NodeHandle computeDescriptorSet = renderGraph->AddNode("DescriptorSet", "compute_descriptors");
    NodeHandle computePipeline = renderGraph->AddNode("ComputePipeline", "test_compute_pipeline");
    NodeHandle computeDispatch = renderGraph->AddNode("ComputeDispatch", "test_dispatch");

    // --- Ray Marching Nodes ---
    NodeHandle cameraNode = renderGraph->AddNode("Camera", "raymarch_camera");
    NodeHandle voxelGridNode = renderGraph->AddNode("VoxelGrid", "voxel_grid");

    // --- Phase 0.4: Loop System Nodes ---
    NodeHandle physicsLoopBridge = renderGraph->AddNode("LoopBridge", "physics_loop");
    NodeHandle physicsLoopIDConstant = renderGraph->AddNode("ConstantNode", "physics_loop_id");

    mainLogger->Info("Created 22 node instances (including camera and voxel grid)");

    // ===================================================================
    // PHASE 2: Configure node parameters
    // ===================================================================

    // Window parameters
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, width);
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, height);

    // Device parameters (default GPU = 0)
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // DISABLED FOR COMPUTE TEST: Graphics pipeline parameters
    /*
    // Vertex buffer parameters (simple triangle)
    auto* vertexBuffer = static_cast<VertexBufferNode*>(renderGraph->GetInstance(vertexBufferNode));
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 36u);
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(VertexWithUV)); // pos(vec4) + UV(vec2)
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_USE_TEXTURE, true); // Shader uses vec2 UV at location 1
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0u); // No index buffer

    // Texture loader parameters
    auto* textureLoader = static_cast<TextureLoaderNode*>(renderGraph->GetInstance(textureNode));
    textureLoader->SetParameter(TextureLoaderNodeConfig::FILE_PATH, std::string("C:\\Users\\liory\\Downloads\\earthmap.jpg"));
    textureLoader->SetParameter(TextureLoaderNodeConfig::SAMPLER_FILTER, std::string("Linear"));
    textureLoader->SetParameter(TextureLoaderNodeConfig::SAMPLER_ADDRESS_MODE, std::string("Repeat"));

    // Render pass parameters
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_STORE_OP, AttachmentStoreOp::DontCare);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    // Framebuffer parameters
    auto* framebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferNode));
    framebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    // Depth buffer parameters
    auto* depthBuffer = static_cast<DepthBufferNode*>(renderGraph->GetInstance(depthBufferNode));
    depthBuffer->SetParameter(DepthBufferNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(VK_FORMAT_D32_SFLOAT));

    // Graphics pipeline parameters
    auto* pipeline = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(pipelineNode));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::CULL_MODE, std::string("Back"));

    // MVP: Shader loading deferred to CompileRenderGraph (after device is created)
    mainLogger->Info("Shader loading will occur during compilation phase");
    
    pipeline->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::FRONT_FACE, std::string("CounterClockwise"));

    // Geometry render parameters
    auto* geometryRender = static_cast<GeometryRenderNode*>(renderGraph->GetInstance(geometryRenderNode));
    geometryRender->SetParameter(GeometryRenderNodeConfig::VERTEX_COUNT, 36u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::INSTANCE_COUNT, 1u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_VERTEX, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_INSTANCE, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::USE_INDEX_BUFFER, false);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_R, 0.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_G, 0.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_B, 0.2f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_A, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_DEPTH, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_STENCIL, 0u);

    // Phase G: Configure shader libraries with builder functions

    // Graphics shader library (Draw.vert + Draw.frag)
    auto* graphicsShaderLib = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(shaderLibNode));
    graphicsShaderLib->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Find shader paths
        std::vector<std::filesystem::path> possiblePaths = {
            "Draw.vert", "Shaders/Draw.vert", "../Shaders/Draw.vert", "binaries/Draw.vert"
        };
        std::filesystem::path vertPath, fragPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                vertPath = path;
                fragPath = path.parent_path() / "Draw.frag";
                break;
            }
        }

        // Configure SDI generation
        ShaderManagement::SdiGeneratorConfig sdiConfig;
        sdiConfig.outputDirectory = std::filesystem::current_path() / "generated" / "sdi";
        sdiConfig.namespacePrefix = "ShaderInterface";
        sdiConfig.generateComments = true;

        builder.SetProgramName("Draw_Shader")
               .SetSdiConfig(sdiConfig)
               .EnableSdiGeneration(true)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddStageFromFile(ShaderManagement::ShaderStage::Vertex, vertPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, fragPath, "main");

        return builder;
    });
    */

    // Present parameters (needed for both graphics and compute)
    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Phase 0.4: Loop ID constant (connects to LoopBridgeNode) - needed for both graphics and compute
    auto* loopIDConst = static_cast<ConstantNode*>(renderGraph->GetInstance(physicsLoopIDConstant));
    loopIDConst->SetValue<uint32_t>(physicsLoopID);
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Loop ID set, moving to shader library...");
    }

    // Voxel ray marching compute shader (VoxelRayMarch.comp)
    auto* computeShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(computeShaderLib));
    computeShaderLibNode->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Find voxel ray march shader path
        std::vector<std::filesystem::path> possiblePaths = {
            "VoxelRayMarch.comp", "Shaders/VoxelRayMarch.comp", "../Shaders/VoxelRayMarch.comp", "binaries/VoxelRayMarch.comp"
        };
        std::filesystem::path compPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                compPath = path;
                break;
            }
        }

        builder.SetProgramName("VoxelRayMarch")
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, compPath, "main");

        return builder;
    });

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Configured voxel ray marching compute shader");
    }

    // Phase G: Compute dispatch parameters
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    uint32_t dispatchX = width / 8;
    uint32_t dispatchY = height / 8;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Setting dispatch dims: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x1 (from window " + std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_X, dispatchX);  // Workgroup size 8x8
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Y, dispatchY);
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Z, 1u);

    // Ray marching: Camera parameters
    auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode));
    camera->SetParameter(CameraNodeConfig::PARAM_FOV, 45.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    camera->SetParameter(CameraNodeConfig::PARAM_FAR_PLANE, 500.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 0.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, 80.0f);  // Closer to see sphere (radius ~38 units)
    camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);

    // Ray marching: Voxel grid parameters
    auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_SCENE_TYPE, std::string("test"));

    mainLogger->Info("Configured all node parameters (including camera and voxel grid)");

    // ===================================================================
    // PHASE 3: Wire connections using TypedConnection API
    // ===================================================================

    using namespace Vixen::RenderGraph;

    mainLogger->Info("Wiring node connections using TypedConnection API");

    // Use ConnectionBatch for atomic registration
    ConnectionBatch batch(renderGraph.get());

    // --- Window → SwapChain connections ---
    batch.Connect(windowNode, WindowNodeConfig::HWND_OUT,
                  swapChainNode, SwapChainNodeConfig::HWND)
         .Connect(windowNode, WindowNodeConfig::HINSTANCE_OUT,
                  swapChainNode, SwapChainNodeConfig::HINSTANCE)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                  swapChainNode, SwapChainNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                  swapChainNode, SwapChainNodeConfig::HEIGHT);

    // --- Device → SwapChain connections ---
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE,
                  swapChainNode, SwapChainNodeConfig::INSTANCE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  swapChainNode, SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → FrameSync connection (Phase 0.2) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);

    // --- FrameSync → SwapChain connections (Phase 0.4) ---
    // Phase 0.4: Per-flight semaphores and current frame index
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    // Phase 0.7: Present fences array (VK_EXT_swapchain_maintenance1)
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY);

    // --- Device → CommandPool connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // DISABLED FOR COMPUTE TEST: Graphics pipeline connections
    /*
    // --- Device → DepthBuffer device connection (for Vulkan operations) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  depthBufferNode, DepthBufferNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → DepthBuffer connection (for dimensions) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  depthBufferNode, DepthBufferNodeConfig::SWAPCHAIN_PUBLIC_VARS);

    // --- CommandPool → DepthBuffer connection ---
    batch.Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  depthBufferNode, DepthBufferNodeConfig::COMMAND_POOL);

    // --- Device → RenderPass device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  renderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → RenderPass connection (swapchain info bundle) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  renderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    // --- DepthBuffer → RenderPass connection (depth format) ---
    batch.Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_FORMAT,
                  renderPassNode, RenderPassNodeConfig::DEPTH_FORMAT);

    // --- Device → Framebuffer device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN);

    // --- RenderPass + SwapChain + DepthBuffer → Framebuffer connections ---
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
        framebufferNode, FramebufferNodeConfig::RENDER_PASS)
        .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
            framebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO)
        .Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_IMAGE_VIEW,
            framebufferNode, FramebufferNodeConfig::DEPTH_ATTACHMENT);


    // --- Device → ShaderLibrary device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shaderLibNode, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → GraphicsPipeline device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pipelineNode, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

    // --- RenderPass + DescriptorSet + SwapChain → Pipeline connections ---
    // Phase 2: Connect ShaderDataBundle to both DescriptorSetNode and GraphicsPipelineNode
    batch.Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorSetNode, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pipelineNode, GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  pipelineNode, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  descriptorSetNode, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  descriptorSetNode, DescriptorSetNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  descriptorSetNode, DescriptorSetNodeConfig::IMAGE_INDEX)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  pipelineNode, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  pipelineNode, GraphicsPipelineNodeConfig::SWAPCHAIN_INFO);

    // --- Device → TextureLoader device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  textureNode, TextureLoaderNodeConfig::VULKAN_DEVICE_IN);

    // --- TextureLoader → DescriptorSet texture connections ---
    batch.Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_IMAGE,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_IMAGE)
         .Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_VIEW)
         .Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_SAMPLER,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_SAMPLER);

    // --- Device → VertexBuffer device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  vertexBufferNode, VertexBufferNodeConfig::VULKAN_DEVICE_IN);

    // --- All resources → GeometryRender connections ---
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_PASS)
         .Connect(framebufferNode, FramebufferNodeConfig::FRAMEBUFFERS,
                  geometryRenderNode, GeometryRenderNodeConfig::FRAMEBUFFERS)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE,
                  geometryRenderNode, GeometryRenderNodeConfig::PIPELINE)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE_LAYOUT,
                  geometryRenderNode, GeometryRenderNodeConfig::PIPELINE_LAYOUT)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  geometryRenderNode, GeometryRenderNodeConfig::DESCRIPTOR_SETS)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::VERTEX_BUFFER,
                  geometryRenderNode, GeometryRenderNodeConfig::VERTEX_BUFFER)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::INDEX_BUFFER,
                  geometryRenderNode, GeometryRenderNodeConfig::INDEX_BUFFER)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  geometryRenderNode, GeometryRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  geometryRenderNode, GeometryRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  geometryRenderNode, GeometryRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  geometryRenderNode, GeometryRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  geometryRenderNode, GeometryRenderNodeConfig::CURRENT_FRAME_INDEX)  // Phase 0.5: Frame-in-flight index for semaphore indexing
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  geometryRenderNode, GeometryRenderNodeConfig::IN_FLIGHT_FENCE)  // Phase 0.5: Per-flight fence (CPU-GPU sync)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  geometryRenderNode, GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)  // Phase 0.5: Array of per-flight semaphores (indexed by frameIndex)
         .Connect(frameSyncNode, FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);  // Phase 0.5: Array of per-image semaphores (indexed by imageIndex)
    */

    // --- Device → Present device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  presentNode, PresentNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → Present connections (for compute-only rendering) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);  // Wait on ComputeDispatch's output

    // --- FrameSync → Present connections (Phase 0.7) ---
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::PRESENT_FENCES_ARRAY,
                  presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // MVP: Shader connection happens in CompileRenderGraph (after device creation)

    // --- Phase 0.4: Loop System Connections ---
    batch.Connect(physicsLoopIDConstant, ConstantNodeConfig::OUTPUT,
                  physicsLoopBridge, LoopBridgeNodeConfig::LOOP_ID);

    // --- Phase G: Compute Pipeline Connections ---
    // Pipeline setup
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         // Phase H: Shader bundle → Gatherer for descriptor discovery
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         // Phase H: Gatherer → DescriptorSet (data-driven resources + slot roles)
         .Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_SLOT_ROLES,
                  computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SLOT_ROLES)
         // Pass shader bundle directly to descriptor set and pipeline (needed during Compile)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDispatch, ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  computePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDispatch, ComputeDispatchNodeConfig::VULKAN_DEVICE_IN)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE,
                  computeDispatch, ComputeDispatchNodeConfig::COMPUTE_PIPELINE)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  computeDispatch, ComputeDispatchNodeConfig::PIPELINE_LAYOUT)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  computeDispatch, ComputeDispatchNodeConfig::DESCRIPTOR_SETS)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  computeDispatch, ComputeDispatchNodeConfig::COMMAND_POOL);

    // --- Ray Marching Resource Connections ---
    // Camera node connections
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  cameraNode, CameraNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  cameraNode, CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  cameraNode, CameraNodeConfig::IMAGE_INDEX);

    // Voxel grid node connections
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  voxelGridNode, VoxelGridNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  voxelGridNode, VoxelGridNodeConfig::COMMAND_POOL);

    // Connect ray marching resources to descriptor gatherer using VoxelRayMarchNames.h bindings
    // Binding 0: outputImage (swapchain image view)
    batch.ConnectVariadic(swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW,
                          descriptorGatherer, VoxelRayMarch::outputImage);

    // Binding 1: camera (uniform buffer)
    batch.ConnectVariadic(cameraNode, CameraNodeConfig::CAMERA_BUFFER,
                          descriptorGatherer, VoxelRayMarch::camera);

    // Binding 2: voxelGrid (combined image sampler)
    batch.ConnectVariadic(voxelGridNode, VoxelGridNodeConfig::VOXEL_COMBINED_SAMPLER,
                          descriptorGatherer, VoxelRayMarch::voxelGrid);

    // Swapchain connections to descriptor set and dispatch
    // Extract imageCount metadata using field extraction, DESCRIPTOR_RESOURCES provides actual bindings
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT,
                  &SwapChainPublicVariables::swapChainImageCount)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         .Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDispatch, ComputeDispatchNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Sync connections
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  computeDispatch, ComputeDispatchNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(frameSyncNode, FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // Connect compute output to Present
    batch.Connect(computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");

    // --- Phase 0.4: Loop Propagation Connections ---
    // TODO: Re-enable loop propagation connections after implementing proper API
    // Note: AUTO_LOOP slots exist on all nodes, but direct Connect() is not exposed on RenderGraph
    // batch.Connect(
    //     physicsLoopBridge, NodeInstance::AUTO_LOOP_OUT_SLOT,
    //     geometryRenderNode, NodeInstance::AUTO_LOOP_IN_SLOT
    // );
    // mainLogger->Info("Connected physics loop propagation to GeometryRenderNode");

    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}

void VulkanGraphApplication::HandleShutdownRequest() {
    if (shutdownRequested) {
        return;  // Already shutting down
    }

    mainLogger->Info("Shutdown requested - initiating graceful shutdown sequence");
    shutdownRequested = true;

    // Register systems that need to acknowledge shutdown
    // In this case, we want the RenderGraph to cleanup first
    shutdownAcksPending.insert("RenderGraph");

    // Window handle should already be cached from WindowNode during graph build
    // RenderGraph will cleanup via WindowCloseEvent subscription, then publish ShutdownAckEvent
    mainLogger->Info("Waiting for RenderGraph cleanup acknowledgment...");
}

void VulkanGraphApplication::HandleShutdownAck(const std::string& systemName) {
    if (mainLogger) {
        mainLogger->Info("Received shutdown acknowledgment from: " + systemName);
    }

    auto it = shutdownAcksPending.find(systemName);
    if (it != shutdownAcksPending.end()) {
        shutdownAcksPending.erase(it);
    }

    // Check if all systems have acknowledged
    if (shutdownAcksPending.empty()) {
        if (mainLogger) {
            mainLogger->Info("All systems acknowledged shutdown - destroying window");
        }
        CompleteShutdown();
    } else {
        if (mainLogger) {
            mainLogger->Info("Still waiting for " + std::to_string(shutdownAcksPending.size()) + " system(s) to acknowledge");
        }
    }
}

void VulkanGraphApplication::CompleteShutdown() {
    // All systems have cleaned up - now destroy the window
    if (windowHandle) {
        if (mainLogger) {
            mainLogger->Info("Destroying window to complete shutdown");
        }
        DestroyWindow(windowHandle);
        windowHandle = nullptr;
    }
}

void VulkanGraphApplication::EnableNodeLogger(NodeHandle handle, bool enableTerminal) {
    // Handle-based API not yet implemented - use string-based version
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Warning("EnableNodeLogger(NodeHandle) not yet implemented - use string version");
    }
}

void VulkanGraphApplication::EnableNodeLogger(const std::string& nodeName, bool enableTerminal) {
    if (!renderGraph) {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("EnableNodeLogger: RenderGraph not initialized");
        }
        return;
    }

    NodeInstance* node = renderGraph->GetNodeByName(nodeName);
    if (!node) {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Error("EnableNodeLogger: Node '" + nodeName + "' not found");
        }
        return;
    }

    Logger* logger = node->GetLogger();
    if (logger) {
        logger->SetEnabled(true);
        logger->SetTerminalOutput(enableTerminal);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("Enabled logger for node '" + nodeName + "' (terminal=" + std::to_string(enableTerminal) + ")");
        }
    } else {
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Warning("Node '" + nodeName + "' has no logger");
        }
    }
}
