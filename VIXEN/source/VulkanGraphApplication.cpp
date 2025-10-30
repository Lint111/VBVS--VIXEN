#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include "Core/TypedConnection.h"  // Typed slot connection helpers
#include "VulkanShader.h"  // MVP: Direct shader loading
#include "wrapper.h"  // MVP: File reading utility
#include "CashSystem/MainCacher.h"  // Cache system initialization

// Global VkInstance for nodes to access (temporary Phase 1 hack)
VkInstance g_VulkanInstance = VK_NULL_HANDLE;

// Include all node types
#include "Nodes/WindowNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/CommandPoolNode.h"
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
#include "Nodes/ConstantNodeConfig.h"  // MVP: ConstantNode configuration

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

    if (mainLogger) {
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
    mainLogger->Info("VulkanGraphApplication Initialize START");

    // Initialize base Vulkan core (instance, device)
    VulkanApplicationBase::Initialize();

    mainLogger->Info("VulkanGraphApplication Base initialized");

    // PHASE 1: Export instance globally for nodes to access
    g_VulkanInstance = instanceObj.instance;

    mainLogger->Info("VulkanGraphApplication Instance exported globally");

    // Create node type registry
    nodeRegistry = std::make_unique<NodeTypeRegistry>();

    // Register all node types
    RegisterNodeTypes();

    // Create render graph
    // Create a MessageBus for event-driven coordination and inject into RenderGraph
    messageBus = std::make_unique<Vixen::EventBus::MessageBus>();

    // Initialize MainCacher and connect it to MessageBus for device invalidation events
    auto& mainCacher = CashSystem::MainCacher::Instance();
    mainCacher.Initialize(messageBus.get());
    mainLogger->Info("MainCacher initialized and subscribed to device invalidation events");

    // Create RenderGraph with all dependencies (registry, messageBus, logger, mainCacher)
    renderGraph = std::make_unique<RenderGraph>(
        nodeRegistry.get(),
        messageBus.get(),
        mainLogger.get(),
        &mainCacher
    );

    // Subscribe to shutdown events
    messageBus->Subscribe(
        Vixen::EventBus::WindowCloseEvent::TYPE,
        [this](const Vixen::EventBus::BaseEventMessage& msg) -> bool {
            HandleShutdownRequest();
            return true;
        }
    );

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

    if (mainLogger) {
        mainLogger->Info("RenderGraph created successfully");
    }

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication initialized successfully");
    }
}

void VulkanGraphApplication::Prepare() {
    std::cout << "[VulkanGraphApplication::Prepare] START" << std::endl;
    isPrepared = false;

    try {
        // PHASE 1: Nodes manage their own resources
        // Build the render graph - nodes allocate their own resources
        std::cout << "[VulkanGraphApplication::Prepare] Calling BuildRenderGraph..." << std::endl;
        BuildRenderGraph();
        std::cout << "[VulkanGraphApplication::Prepare] BuildRenderGraph complete" << std::endl;

        // Cache window handle for graceful shutdown
        if (windowNodeHandle.IsValid()) {
            auto* windowInst = renderGraph->GetInstance(windowNodeHandle);
            if (windowInst) {
                auto* windowNode = dynamic_cast<Vixen::RenderGraph::WindowNode*>(windowInst);
                if (windowNode) {
                    windowHandle = windowNode->GetWindow();
                }
            }
        }

        // Compile the render graph - nodes set up their pipelines
        std::cout << "[VulkanGraphApplication::Prepare] Calling CompileRenderGraph..." << std::endl;
        CompileRenderGraph();
        std::cout << "[VulkanGraphApplication::Prepare] CompileRenderGraph complete" << std::endl;

        isPrepared = true;

        if (mainLogger) {
            mainLogger->Info("VulkanGraphApplication prepared and ready to render");
        }
        std::cout << "[VulkanGraphApplication::Prepare] SUCCESS - isPrepared = true" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[VulkanGraphApplication::Prepare] EXCEPTION: " << e.what() << std::endl;
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

    // Before destroying the render graph, extract logs from the main logger.
    // Node instances register child loggers with the main logger; if we
    // destroy the graph first those child loggers are destroyed and their
    // entries won't appear in the aggregated log output.
    if (mainLogger) {
        try {
            std::string logs = mainLogger->ExtractLogs();
            // Write logs into the binaries folder so logs are colocated with the build artifacts.
            std::ofstream logFile("binaries\\vulkan_app_log.txt");
            if (logFile.is_open()) {
                logFile << logs;
                logFile.close();
                std::cout << "Logs written to binaries\\vulkan_app_log.txt" << std::endl;
            }
        } catch (...) {
            // Best-effort: don't throw during cleanup
        }
        
        // Clear all child logger pointers before destroying nodes
        // This prevents dangling pointer access during final cleanup
        mainLogger->ClearChildren();
    }

    // CRITICAL: Destroy render graph (triggers CleanupStack execution) BEFORE base class destroys device
    // This ensures all node-owned Vulkan resources (buffers, images, views, shaders) are destroyed
    // while the VkDevice is still valid.
    // Note: ConstantNode's cleanup callback will destroy the shader via registered callback
    std::cout << "[DeInitialize] Destroying render graph..." << std::endl;
    renderGraph.reset();
    std::cout << "[DeInitialize] Render graph destroyed" << std::endl;

    // Destroy node registry
    nodeRegistry.reset();

    // Graph nodes handle their own cleanup (including window)

    // Call base class cleanup (destroys device and instance)
    VulkanApplicationBase::DeInitialize();

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}

void VulkanGraphApplication::CompileRenderGraph() {
    std::cout << "[CompileRenderGraph] START" << std::endl;
    if (!renderGraph) {
        mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        std::cerr << "[CompileRenderGraph] ERROR: renderGraph is null" << std::endl;
        return;
    }

    // NOTE: Legacy Phase 1 wiring removed - nodes now use typed slots.
    // All connections should be established via Connect() API during BuildRenderGraph().
    // Example:
    // Connect(deviceNode, DeviceNodeConfig::QUEUE, presentNode, PresentNodeConfig::QUEUE);
    // Connect(deviceNode, DeviceNodeConfig::PRESENT_FUNCTION, presentNode, PresentNodeConfig::PRESENT_FUNCTION);


    // MVP: Load shaders BEFORE compilation
    // We need to inject the shader into ConstantNode before Compile() is called
    // Strategy: Compile device first, load shaders, inject into ConstantNode, then compile rest
    
    std::cout << "[CompileRenderGraph] Pre-compiling device node..." << std::endl;
    auto* deviceNode = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNodeHandle));
    deviceNode->Setup();
    deviceNode->Compile();
    deviceNode->SetState(NodeState::Compiled);  // CRITICAL: Mark as compiled to prevent re-compilation
    std::cout << "[CompileRenderGraph] Device node compiled and marked as Compiled" << std::endl;
    
    // Now device is ready, load shaders
    mainLogger->Info("Loading SPIR-V shaders...");
    auto* vulkanDevice = deviceNode->GetVulkanDevice();
    if (!vulkanDevice) {
        mainLogger->Error("Cannot load shaders: VulkanDevice not available");
        throw std::runtime_error("VulkanDevice required for shader loading");
    }
    
    // Load SPIR-V bytecode from files (paths relative to binaries/ execution directory)
    size_t vertSize = 0, fragSize = 0;
    uint32_t* vertSpv = static_cast<uint32_t*>(::ReadFile("../builtAssets/CompiledShaders/Draw.vert.spv", &vertSize));
    uint32_t* fragSpv = static_cast<uint32_t*>(::ReadFile("../builtAssets/CompiledShaders/Draw.frag.spv", &fragSize));
    
    if (!vertSpv || !fragSpv) {
        mainLogger->Error("Failed to load shader files");
        throw std::runtime_error("Shader files not found");
    }
    
    // Create VulkanShader and build shader modules
    triangleShader = new VulkanShader();
    triangleShader->BuildShaderModuleWithSPV(vertSpv, vertSize, fragSpv, fragSize, vulkanDevice);
    
    // Clean up file buffers
    delete[] vertSpv;
    delete[] fragSpv;
    mainLogger->Info("Shaders loaded successfully");
    
    // Inject shader into ConstantNode BEFORE graph compilation
    mainLogger->Info("Injecting shader into ConstantNode...");
    std::cout << "[CompileRenderGraph] Getting shader_constant node instance..." << std::endl;
    auto* shaderConstNode = static_cast<ConstantNode*>(renderGraph->GetInstance(shaderConstantNodeHandle));
    if (!shaderConstNode) {
        std::cout << "[CompileRenderGraph] ERROR: shader_constant node is NULL!" << std::endl;
        throw std::runtime_error("shader_constant node not found");
    }
    std::cout << "[CompileRenderGraph] Calling SetValue() on shader_constant..." << std::endl;
    shaderConstNode->SetValue(triangleShader);
    std::cout << "[CompileRenderGraph] SetValue() complete" << std::endl;
    
    // CRITICAL: Register cleanup callback for shader destruction
    // Capture the shader pointer and device to ensure proper cleanup during graph destruction
    // IMPORTANT: Shader cleanup must happen BEFORE device cleanup (dependency ordering)
    mainLogger->Info("Registering shader cleanup callback...");
    std::cout << "[CompileRenderGraph] Calling SetCleanupCallback() on shader_constant..." << std::endl;
    shaderConstNode->SetCleanupCallback(
        [this]() {
            std::cout << "[ShaderCleanupCallback] Destroying shader..." << std::endl;
            if (triangleShader) {
                triangleShader->DestroyShader(nullptr);  // Uses creationDevice stored in VulkanShader
                delete triangleShader;
                triangleShader = nullptr;
                std::cout << "[ShaderCleanupCallback] Shader destroyed successfully" << std::endl;
            }
        },
        {deviceNodeHandle}  // Dependency: shader must be destroyed BEFORE device node
    );
    std::cout << "[CompileRenderGraph] SetCleanupCallback() complete" << std::endl;
    
    mainLogger->Info("Shader value injected and cleanup registered - ready for compilation");

    // Now compile the full graph (all nodes including pipeline with shaders ready)
    std::cout << "[CompileRenderGraph] Calling graph.Compile()..." << std::endl;
    renderGraph->Compile();
    graphCompiled = true;
    std::cout << "[CompileRenderGraph] Graph compilation complete" << std::endl;

    // Validate final graph (should pass with shaders connected)
    std::cout << "[CompileRenderGraph] Validating graph..." << std::endl;
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        mainLogger->Error("Render graph validation failed: " + errorMessage);
        std::cerr << "[CompileRenderGraph] VALIDATION FAILED: " << errorMessage << std::endl;
        return;
    }
    std::cout << "[CompileRenderGraph] Validation passed" << std::endl;

    mainLogger->Info("Render graph compiled successfully");
    mainLogger->Info("Node count: " + std::to_string(renderGraph->GetNodeCount()));
    std::cout << "[CompileRenderGraph] SUCCESS" << std::endl;
}

void VulkanGraphApplication::RegisterNodeTypes() {
    if (!nodeRegistry) {
        mainLogger->Error("Cannot register node types: Registry not initialized");
        return;
    }

    mainLogger->Info("Registering all built-in node types");

    // Register all 14 node types
    nodeRegistry->RegisterNodeType(std::make_unique<WindowNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DeviceNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<CommandPoolNodeType>());
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

    mainLogger->Info("Successfully registered 16 node types");
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
    windowNodeHandle = windowNode; // Cache for shutdown handling
    NodeHandle deviceNode = renderGraph->AddNode("Device", "main_device");
    deviceNodeHandle = deviceNode; // MVP: Cache for post-compile shader loading
    NodeHandle swapChainNode = renderGraph->AddNode("SwapChain", "main_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode("CommandPool", "main_cmd_pool");

    // --- Resource Nodes ---
    NodeHandle depthBufferNode = renderGraph->AddNode("DepthBuffer", "depth_buffer");
    NodeHandle vertexBufferNode = renderGraph->AddNode("VertexBuffer", "triangle_vb");
    NodeHandle textureNode = renderGraph->AddNode("TextureLoader", "main_texture");

    // --- Rendering Configuration Nodes ---
    NodeHandle renderPassNode = renderGraph->AddNode("RenderPass", "main_pass");
    NodeHandle framebufferNode = renderGraph->AddNode("Framebuffer", "main_fb");
    NodeHandle shaderLibNode = renderGraph->AddNode("ShaderLibrary", "shader_lib");
    NodeHandle descriptorSetNode = renderGraph->AddNode("DescriptorSet", "main_descriptors");
    NodeHandle pipelineNode = renderGraph->AddNode("GraphicsPipeline", "triangle_pipeline");
    pipelineNodeHandle = pipelineNode; // MVP: Cache for post-compile shader connection
    
    // MVP: Shader constant node (value set during compilation after device creation)
    std::cout << "[BuildRenderGraph] Creating shader_constant node..." << std::endl;
    NodeHandle shaderConstantNode;
    try {
        shaderConstantNode = renderGraph->AddNode("ShaderConstant", "shader_constant");
        std::cout << "[BuildRenderGraph] shader_constant node created successfully, handle=" 
                  << shaderConstantNode.index << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[BuildRenderGraph] ERROR creating shader_constant: " << e.what() << std::endl;
        throw;
    }
    shaderConstantNodeHandle = shaderConstantNode; // Cache for post-compile value injection

    // --- Execution Nodes ---
    NodeHandle geometryRenderNode = renderGraph->AddNode("GeometryRender", "triangle_render");
    NodeHandle presentNode = renderGraph->AddNode("Present", "present");

    mainLogger->Info("Created 14 node instances");

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

    // Present parameters
    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    mainLogger->Info("Configured all node parameters");

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

    // --- Device → CommandPool connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

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
    // MVP: Shader connection deferred - shaders loaded after compilation
    // batch.Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_PROGRAMS,
    //               pipelineNode, GraphicsPipelineNodeConfig::SHADER_STAGES)
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  pipelineNode, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  descriptorSetNode, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  pipelineNode, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  pipelineNode, GraphicsPipelineNodeConfig::SWAPCHAIN_INFO);
    
    // MVP: Connect shader constant node to pipeline (value injected during compilation)
    batch.Connect(shaderConstantNode, ConstantNodeConfig::OUTPUT,
                  pipelineNode, GraphicsPipelineNodeConfig::SHADER_STAGES);

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
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORE,
                  geometryRenderNode, GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORE);

    // --- Device → Present device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  presentNode, PresentNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain + GeometryRender → Present connections ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(geometryRenderNode, GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);

    // MVP: Shader connection happens in CompileRenderGraph (after device creation)

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();
    
    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");
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
