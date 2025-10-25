#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"
#include "Core/TypedConnection.h"  // Typed slot connection helpers

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
    // NOTE: Device is passed to individual nodes, not to RenderGraph constructor
    // Pass nullptr for messageBus (event-driven cleanup optional)
    renderGraph = std::make_unique<RenderGraph>(nodeRegistry.get(), nullptr, mainLogger.get());

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
    // - Image acquisition (SwapChainNode)
    // - Command buffer allocation & recording (GeometryRenderNode)
    // - Queue submission with semaphores (nodes manage sync)
    // - Presentation (PresentNode)
    VkResult result = renderGraph->RenderFrame();

    // Handle swapchain recreation
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // TODO: Rebuild swapchain and recompile graph
        mainLogger->Info("Swapchain out of date - needs rebuild");
        return true;
    }

    if (result != VK_SUCCESS) {
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

    // Update time
    time.Update();
    float deltaTime = time.GetDeltaTime();

    // Update MVP matrix (rotate cube)
    static float rotation = 0.0f;
    rotation += glm::radians(45.0f) * deltaTime;

    glm::mat4 Projection = glm::perspective(
        glm::radians(45.0f),
        (float)width / (float)height,
        0.1f,
        256.0f
    );

    glm::mat4 View = glm::lookAt(
        glm::vec3(0, 0, 5),  // Camera position
        glm::vec3(0, 0, 0),  // Look at origin
        glm::vec3(0, 1, 0)   // Up vector
    );

    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::rotate(Model, rotation, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::rotate(Model, rotation, glm::vec3(1.0f, 1.0f, 1.0f));

    glm::mat4 MVP = Projection * View * Model;

    // TODO: Update MVP in descriptor node
    // For now, we'll add a SetGlobalParameter method to RenderGraph later
    // or update the descriptor node directly via GetInstanceByName()
}

void VulkanGraphApplication::DeInitialize() {
    // Wait for device to finish
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

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

    // Destroy render graph (nodes clean up their own resources)
    renderGraph.reset();

    // Destroy node registry
    nodeRegistry.reset();

    // Graph nodes handle their own cleanup (including window)

    // Call base class cleanup
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

    // Validate graph
    std::cout << "[CompileRenderGraph] Calling Validate..." << std::endl;
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        mainLogger->Error("Render graph validation failed: " + errorMessage);
        std::cerr << "[CompileRenderGraph] VALIDATION FAILED: " << errorMessage << std::endl;
        return;
    }
    std::cout << "[CompileRenderGraph] Validation passed" << std::endl;

    // Compile the graph
    // This calls Setup() and Compile() on all nodes
    // Each node allocates its Vulkan resources here
    std::cout << "[CompileRenderGraph] Calling graph.Compile()..." << std::endl;
    renderGraph->Compile();
    graphCompiled = true;
    std::cout << "[CompileRenderGraph] graphCompiled = true" << std::endl;

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

    mainLogger->Info("Successfully registered 14 node types");
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
    NodeHandle swapChainNode = renderGraph->AddNode("SwapChain", "main_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode("CommandPool", "main_cmd_pool");

    // --- Resource Nodes ---
    NodeHandle depthBufferNode = renderGraph->AddNode("DepthBuffer", "depth_buffer");
    NodeHandle vertexBufferNode = renderGraph->AddNode("VertexBuffer", "triangle_vb");

    // --- Rendering Configuration Nodes ---
    NodeHandle renderPassNode = renderGraph->AddNode("RenderPass", "main_pass");
    NodeHandle framebufferNode = renderGraph->AddNode("Framebuffer", "main_fb");
    NodeHandle shaderLibNode = renderGraph->AddNode("ShaderLibrary", "shader_lib");
    NodeHandle descriptorSetNode = renderGraph->AddNode("DescriptorSet", "main_descriptors");
    NodeHandle pipelineNode = renderGraph->AddNode("GraphicsPipeline", "triangle_pipeline");

    // --- Execution Nodes ---
    NodeHandle geometryRenderNode = renderGraph->AddNode("GeometryRender", "triangle_render");
    NodeHandle presentNode = renderGraph->AddNode("Present", "present");

    mainLogger->Info("Created 13 node instances");

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
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 3u);
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(float) * 6); // pos(3) + color(3)
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_USE_TEXTURE, false);
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0u); // No index buffer

    // Render pass parameters
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, static_cast<uint32_t>(VK_ATTACHMENT_LOAD_OP_CLEAR));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, static_cast<uint32_t>(VK_ATTACHMENT_STORE_OP_STORE));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_LOAD_OP, static_cast<uint32_t>(VK_ATTACHMENT_LOAD_OP_CLEAR));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_STORE_OP, static_cast<uint32_t>(VK_ATTACHMENT_STORE_OP_DONT_CARE));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, static_cast<uint32_t>(VK_IMAGE_LAYOUT_UNDEFINED));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, static_cast<uint32_t>(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
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
    pipeline->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::FRONT_FACE, std::string("CounterClockwise"));

    // Geometry render parameters
    auto* geometryRender = static_cast<GeometryRenderNode*>(renderGraph->GetInstance(geometryRenderNode));
    geometryRender->SetParameter(GeometryRenderNodeConfig::VERTEX_COUNT, 3u);
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

    // --- SwapChain → DepthBuffer connections ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::WIDTH_OUT,
                  depthBufferNode, DepthBufferNodeConfig::WIDTH)
         .Connect(swapChainNode, SwapChainNodeConfig::HEIGHT_OUT,
                  depthBufferNode, DepthBufferNodeConfig::HEIGHT);

    // --- Device → DepthBuffer device connection (for Vulkan operations) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  depthBufferNode, DepthBufferNodeConfig::VULKAN_DEVICE_IN);

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
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_IMAGES,
                  framebufferNode, FramebufferNodeConfig::COLOR_ATTACHMENTS)
         .Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_IMAGE_VIEW,
                  framebufferNode, FramebufferNodeConfig::DEPTH_ATTACHMENT)
         .Connect(swapChainNode, SwapChainNodeConfig::WIDTH_OUT,
                  framebufferNode, FramebufferNodeConfig::WIDTH)
         .Connect(swapChainNode, SwapChainNodeConfig::HEIGHT_OUT,
                  framebufferNode, FramebufferNodeConfig::HEIGHT);

    // --- Device → ShaderLibrary device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shaderLibNode, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → GraphicsPipeline device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pipelineNode, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

    // --- ShaderLibrary + RenderPass + DescriptorSet + SwapChain → Pipeline connections ---
    batch.Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_PROGRAMS,
                  pipelineNode, GraphicsPipelineNodeConfig::SHADER_PROGRAM)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  pipelineNode, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  descriptorSetNode, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  pipelineNode, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  pipelineNode, GraphicsPipelineNodeConfig::VIEWPORT)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  pipelineNode, GraphicsPipelineNodeConfig::SCISSOR);

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
                  geometryRenderNode, GeometryRenderNodeConfig::VIEWPORT)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  geometryRenderNode, GeometryRenderNodeConfig::SCISSOR)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_WIDTH)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_HEIGHT);

    // --- Device → Present device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  presentNode, PresentNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain + GeometryRender → Present connections ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(geometryRenderNode, GeometryRenderNodeConfig::COMMAND_BUFFERS,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");
    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}
