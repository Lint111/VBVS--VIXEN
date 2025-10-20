#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "MeshData.h"
#include "Logger.h"

// Global VkInstance for nodes to access (temporary Phase 1 hack)
VkInstance g_VulkanInstance = VK_NULL_HANDLE;

// Include all node types
#include "RenderGraph/Nodes/WindowNode.h"
#include "RenderGraph/Nodes/DeviceNode.h"
#include "RenderGraph/Nodes/TextureLoaderNode.h"
#include "RenderGraph/Nodes/DepthBufferNode.h"
#include "RenderGraph/Nodes/SwapChainNode.h"
#include "RenderGraph/Nodes/VertexBufferNode.h"
#include "RenderGraph/Nodes/RenderPassNode.h"
#include "RenderGraph/Nodes/FramebufferNode.h"
#include "RenderGraph/Nodes/ShaderLibraryNode.h"
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
    if (deviceObj) {
        renderGraph = std::make_unique<RenderGraph>(deviceObj.get(), nodeRegistry.get(), mainLogger.get());

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

    // PHASE 1: Nodes manage their own resources
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
    if (!renderGraph) {
        mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        return;
    }

    // PHASE 1: Minimal wiring for Window + Present only
    auto* presentNode = static_cast<PresentNode*>(renderGraph->GetInstanceByName("present"));
    if (presentNode) {
        presentNode->SetQueue(deviceObj->queue);
        // PresentNode will get fpQueuePresentKHR from device extension
        mainLogger->Info("Wired PresentNode with queue");
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

    // Register all 13 node types
    nodeRegistry->RegisterNodeType(std::make_unique<WindowNodeType>());
    nodeRegistry->RegisterNodeType(std::make_unique<DeviceNodeType>());
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

    mainLogger->Info("Successfully registered 13 node types");
}

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    mainLogger->Info("Building Phase 1 MVP render graph (Window only)");

    // ==== Phase 1: Bare Minimum ====
    // Just window creation for now

    // 1. Window Node (creates window + surface)
    NodeHandle windowHandle = renderGraph->AddNode("Window", "main_window");
    auto* windowNode = static_cast<WindowNode*>(renderGraph->GetInstance(windowHandle));
    // Use typed parameter names from config (compile-time validation)
    windowNode->SetParameter(WindowNodeConfig::PARAM_WIDTH, width);
    windowNode->SetParameter(WindowNodeConfig::PARAM_HEIGHT, height);

    mainLogger->Info("Phase 1 MVP render graph built with " + std::to_string(renderGraph->GetNodeCount()) + " node(s)");
}
