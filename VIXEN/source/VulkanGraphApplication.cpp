#include "VulkanGraphApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"

// Include all node types
#include "RenderGraph/Nodes/TextureLoaderNode.h"
#include "RenderGraph/Nodes/DepthBufferNode.h"
#include "RenderGraph/Nodes/SwapChainNode.h"
#include "RenderGraph/Nodes/VertexBufferNode.h"
#include "RenderGraph/Nodes/RenderPassNode.h"
#include "RenderGraph/Nodes/FramebufferNode.h"
#include "RenderGraph/Nodes/ShaderNode.h"
#include "RenderGraph/Nodes/DescriptorSetNode.h"
#include "RenderGraph/Nodes/GraphicsPipelineNode.h"
#include "RenderGraph/Nodes/GeometryRenderNode.h"
#include "RenderGraph/Nodes/PresentNode.h"

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
    // Initialize base Vulkan core (instance, device)
    VulkanApplicationBase::Initialize();

    // Create renderer ONLY for window management (temporary workaround)
    // TODO: Extract window creation to standalone WindowManager
    rendererObj = std::make_unique<VulkanRenderer>(nullptr, deviceObj.get());
    rendererObj->CreatePresentationWindow(width, height);

    // Create swap chain wrapper
    swapChainObj = std::make_unique<VulkanSwapChain>(rendererObj.get());
    swapChainObj->Initialize();

    // Create node type registry
    nodeRegistry = std::make_unique<NodeTypeRegistry>();

    // Register all node types
    RegisterNodeTypes();

    // Create render graph
    if (deviceObj) {
        renderGraph = std::make_unique<RenderGraph>(deviceObj.get(), nodeRegistry.get());

        if (mainLogger) {
            mainLogger->Info("RenderGraph created successfully");
        }
    } else {
        if (mainLogger) {
            mainLogger->Error("Failed to create RenderGraph: Device not initialized");
        }
    }

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication initialized successfully");
    }
}

void VulkanGraphApplication::Prepare() {
    isPrepared = false;

    // Build the swap chain (needed for graph nodes to query)
    swapChainObj->CreateSwapChain(VK_NULL_HANDLE);

    // Build the render graph - nodes allocate their own resources
    BuildRenderGraph();

    // Compile the render graph - nodes set up their pipelines
    CompileRenderGraph();

    isPrepared = true;

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication prepared and ready to render");
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

    // Destroy render graph (nodes clean up their own resources)
    renderGraph.reset();

    // Destroy node registry
    nodeRegistry.reset();

    // Destroy swap chain
    if (swapChainObj) {
        swapChainObj->DestroySwapChain();
        swapChainObj.reset();
    }

    // Destroy renderer (window)
    if (rendererObj) {
        rendererObj->DestroyPresentationWindow();
        rendererObj.reset();
    }

    // Call base class cleanup
    VulkanApplicationBase::DeInitialize();

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    mainLogger->Info("Building render graph for textured rotating cube");

    // ==== Add Nodes ====
    // Each node will allocate its own Vulkan resources during Setup/Compile

    // 1. Swap Chain Node (manages swapchain, image acquisition, semaphores)
    NodeHandle swapChainHandle = renderGraph->AddNode("SwapChain", "main_swapchain");
    auto* swapChainNode = static_cast<SwapChainNode*>(renderGraph->GetInstance(swapChainHandle));
    swapChainNode->SetParameter("width", width);
    swapChainNode->SetParameter("height", height);
    swapChainNode->SetSwapChainWrapper(swapChainObj.get());

    // 2. Depth Buffer Node (creates depth image, view, memory)
    NodeHandle depthHandle = renderGraph->AddNode("DepthBuffer", "main_depth");
    auto* depthNode = static_cast<DepthBufferNode*>(renderGraph->GetInstance(depthHandle));
    depthNode->SetParameter("width", width);
    depthNode->SetParameter("height", height);
    depthNode->SetParameter("format", std::string("D32"));

    // 3. Vertex Buffer Node (creates vertex buffer, uploads data)
    NodeHandle vertexHandle = renderGraph->AddNode("VertexBuffer", "cube_vertices");
    auto* vertexNode = static_cast<VertexBufferNode*>(renderGraph->GetInstance(vertexHandle));
    vertexNode->SetParameter("vertexData", reinterpret_cast<uint64_t>(geometryData));
    vertexNode->SetParameter("vertexDataSize", static_cast<uint32_t>(sizeof(geometryData)));
    vertexNode->SetParameter("vertexStride", static_cast<uint32_t>(sizeof(geometryData[0])));
    vertexNode->SetParameter("vertexCount", static_cast<uint32_t>(sizeof(geometryData) / sizeof(geometryData[0])));

    // 4. Texture Loader Node (loads texture, creates image/view/sampler)
    NodeHandle textureHandle = renderGraph->AddNode("TextureLoader", "earth_texture");
    auto* textureNode = static_cast<TextureLoaderNode*>(renderGraph->GetInstance(textureHandle));
    textureNode->SetParameter("filePath", std::string("C:\\Users\\liory\\Downloads\\earthmap.jpg"));
    textureNode->SetParameter("uploadMode", std::string("Optimal"));

    // 5. Descriptor Set Node (creates layout, pool, sets, uniform buffer)
    NodeHandle descriptorHandle = renderGraph->AddNode("DescriptorSet", "mvp_descriptor");
    auto* descriptorNode = static_cast<DescriptorSetNode*>(renderGraph->GetInstance(descriptorHandle));
    descriptorNode->SetParameter("uniformBufferSize", static_cast<uint32_t>(sizeof(glm::mat4)));
    descriptorNode->SetParameter("useTexture", true);

    // 6. Render Pass Node (creates render pass)
    NodeHandle renderPassHandle = renderGraph->AddNode("RenderPass", "main_renderpass");
    auto* renderPassNode = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassHandle));
    renderPassNode->SetParameter("includeDepth", true);
    renderPassNode->SetParameter("clear", true);
    renderPassNode->SetParameter("format", static_cast<uint32_t>(swapChainObj->scPublicVars.Format));
    renderPassNode->SetParameter("depthFormat", static_cast<uint32_t>(VK_FORMAT_D32_SFLOAT));

    // 7. Framebuffer Node (creates framebuffers per swapchain image)
    NodeHandle framebufferHandle = renderGraph->AddNode("Framebuffer", "main_framebuffers");
    auto* framebufferNode = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferHandle));
    framebufferNode->SetParameter("width", width);
    framebufferNode->SetParameter("height", height);
    framebufferNode->SetParameter("includeDepth", true);

    // 8. Shader Node (compiles shaders, creates modules)
    NodeHandle shaderHandle = renderGraph->AddNode("Shader", "main_shader");
    auto* shaderNode = static_cast<ShaderNode*>(renderGraph->GetInstance(shaderHandle));
    shaderNode->SetParameter("vertexShaderPath", std::string("../Shaders/Draw.vert"));
    shaderNode->SetParameter("fragmentShaderPath", std::string("../Shaders/Draw.frag"));
    shaderNode->SetParameter("compileGLSL", true);

    // 9. Graphics Pipeline Node (creates pipeline cache, layout, pipeline)
    NodeHandle pipelineHandle = renderGraph->AddNode("GraphicsPipeline", "main_pipeline");
    auto* pipelineNode = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(pipelineHandle));
    pipelineNode->SetParameter("enableDepthTest", true);
    pipelineNode->SetParameter("enableDepthWrite", true);
    pipelineNode->SetParameter("enableVertexInput", true);
    pipelineNode->SetParameter("width", width);
    pipelineNode->SetParameter("height", height);

    // 10. Geometry Render Node (records draw commands)
    NodeHandle geometryHandle = renderGraph->AddNode("GeometryRender", "cube_render");
    auto* geometryNode = static_cast<GeometryRenderNode*>(renderGraph->GetInstance(geometryHandle));
    geometryNode->SetParameter("vertexCount", static_cast<uint32_t>(sizeof(geometryData) / sizeof(geometryData[0])));
    geometryNode->SetParameter("instanceCount", static_cast<uint32_t>(1));
    geometryNode->SetParameter("clearColorR", 0.0f);
    geometryNode->SetParameter("clearColorG", 0.0f);
    geometryNode->SetParameter("clearColorB", 0.0f);
    geometryNode->SetParameter("clearColorA", 1.0f);

    // 11. Present Node (presents to swapchain)
    NodeHandle presentHandle = renderGraph->AddNode("Present", "present");
    auto* presentNode = static_cast<PresentNode*>(renderGraph->GetInstance(presentHandle));
    presentNode->SetParameter("waitForIdle", true);

    mainLogger->Info("Render graph built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}

void VulkanGraphApplication::CompileRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        return;
    }

    // Validate graph
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        mainLogger->Error("Render graph validation failed: " + errorMessage);
        return;
    }

    // Compile the graph
    // This calls Setup() and Compile() on all nodes
    // Each node allocates its Vulkan resources here
    renderGraph->Compile();
    graphCompiled = true;

    mainLogger->Info("Render graph compiled successfully");
    mainLogger->Info("Node count: " + std::to_string(renderGraph->GetNodeCount()));
}

void VulkanGraphApplication::RegisterNodeTypes() {
    if (!nodeRegistry) {
        mainLogger->Error("Cannot register node types: Registry not initialized");
        return;
    }

    mainLogger->Info("Registering all built-in node types");

    // Register all 11 node types
    nodeRegistry->RegisterNodeType(std::make_unique<TextureLoaderNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DepthBufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<SwapChainNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<VertexBufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<RenderPassNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<FramebufferNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<ShaderNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DescriptorSetNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<GraphicsPipelineNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<GeometryRenderNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<PresentNodeType>());

    mainLogger->Info("Successfully registered 11 node types");
}
