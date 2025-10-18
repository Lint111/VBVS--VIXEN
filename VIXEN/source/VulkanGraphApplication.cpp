#include "VulkanGraphApplication.h"

extern std::vector<const char*> instanceExtensionNames;
extern std::vector<const char*> layerNames;
extern std::vector<const char*> deviceExtensionNames;

std::unique_ptr<VulkanGraphApplication> VulkanGraphApplication::instance;
std::once_flag VulkanGraphApplication::onlyOnce;

VulkanGraphApplication::VulkanGraphApplication() 
    : VulkanApplicationBase(), 
      commandPool(VK_NULL_HANDLE),
      graphCompiled(false) {
    
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

    // Create command pool
    CreateCommandPool();

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication initialized successfully");
    }
}

void VulkanGraphApplication::Prepare() {
    isPrepared = false;

    // Build the render graph
    BuildRenderGraph();

    // Compile the render graph
    CompileRenderGraph();

    // Create command buffers
    CreateCommandBuffers();

    isPrepared = true;

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication prepared and ready to render");
    }
}

bool VulkanGraphApplication::Render() {
    if (!isPrepared || !graphCompiled || !renderGraph) {
        return false;
    }

    // TODO: Implement actual rendering with swap chain
    // For now, just execute the graph on the first command buffer
    if (!commandBuffers.empty()) {
        VkCommandBuffer cmdBuffer = commandBuffers[0];
        
        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(cmdBuffer, &beginInfo) != VK_SUCCESS) {
            if (mainLogger) {
                mainLogger->Error("Failed to begin command buffer");
            }
            return false;
        }

        // Execute render graph
        renderGraph->Execute(cmdBuffer);

        // End command buffer
        if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
            if (mainLogger) {
                mainLogger->Error("Failed to end command buffer");
            }
            return false;
        }

        // Submit to queue (simplified - in real implementation, use swap chain)
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        if (deviceObj && deviceObj->queue != VK_NULL_HANDLE) {
            vkQueueSubmit(deviceObj->queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(deviceObj->queue);
        }
    }

    return true;
}

void VulkanGraphApplication::Update() {
    if (!isPrepared) {
        return;
    }

    // Update application logic here
    // Graph nodes can have their own update logic
}

void VulkanGraphApplication::DeInitialize() {
    // Wait for device to finish
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceObj->device);
    }

    // Destroy command buffers
    DestroyCommandBuffers();

    // Destroy command pool
    DestroyCommandPool();

    // Destroy render graph
    renderGraph.reset();

    // Destroy node registry
    nodeRegistry.reset();

    // Call base class cleanup
    VulkanApplicationBase::DeInitialize();

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        if (mainLogger) {
            mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        }
        return;
    }

    // Default implementation - override in derived classes
    // Example: Add a simple pass-through node
    if (mainLogger) {
        mainLogger->Info("Building default render graph (override BuildRenderGraph() for custom graph)");
    }

    // Derived classes should override this to add their specific nodes
}

void VulkanGraphApplication::CompileRenderGraph() {
    if (!renderGraph) {
        if (mainLogger) {
            mainLogger->Error("Cannot compile render graph: RenderGraph not initialized");
        }
        return;
    }

    // Validate graph
    std::string errorMessage;
    if (!renderGraph->Validate(errorMessage)) {
        if (mainLogger) {
            mainLogger->Error("Render graph validation failed: " + errorMessage);
        }
        return;
    }

    // Compile the graph
    renderGraph->Compile();
    graphCompiled = true;

    if (mainLogger) {
        mainLogger->Info("Render graph compiled successfully");
        mainLogger->Info("Node count: " + std::to_string(renderGraph->GetNodeCount()));
    }
}

void VulkanGraphApplication::RegisterNodeTypes() {
    if (!nodeRegistry) {
        if (mainLogger) {
            mainLogger->Error("Cannot register node types: Registry not initialized");
        }
        return;
    }

    // Default implementation - override in derived classes to register custom nodes
    if (mainLogger) {
        mainLogger->Info("Registering default node types (override RegisterNodeTypes() for custom nodes)");
    }

    // TODO: Register built-in node types here
    // Example:
    // nodeRegistry->RegisterNodeType<RenderPassNode>("RenderPass");
    // nodeRegistry->RegisterNodeType<ComputeNode>("Compute");
}

void VulkanGraphApplication::CreateCommandPool() {
    if (!deviceObj) {
        if (mainLogger) {
            mainLogger->Error("Cannot create command pool: Device not initialized");
        }
        return;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = deviceObj->graphicsQueueIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(deviceObj->device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        if (mainLogger) {
            mainLogger->Error("Failed to create command pool");
        }
    } else {
        if (mainLogger) {
            mainLogger->Info("Command pool created successfully");
        }
    }
}

void VulkanGraphApplication::CreateCommandBuffers() {
    if (!deviceObj || commandPool == VK_NULL_HANDLE) {
        if (mainLogger) {
            mainLogger->Error("Cannot create command buffers: Device or command pool not initialized");
        }
        return;
    }

    // Create one command buffer for now (in real app, create per swap chain image)
    commandBuffers.resize(1);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(deviceObj->device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        if (mainLogger) {
            mainLogger->Error("Failed to allocate command buffers");
        }
    } else {
        if (mainLogger) {
            mainLogger->Info("Command buffers created successfully");
        }
    }
}

void VulkanGraphApplication::DestroyCommandPool() {
    if (deviceObj && deviceObj->device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(deviceObj->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
        
        if (mainLogger) {
            mainLogger->Info("Command pool destroyed");
        }
    }
}

void VulkanGraphApplication::DestroyCommandBuffers() {
    // Command buffers are automatically freed when command pool is destroyed
    commandBuffers.clear();
}

void VulkanGraphApplication::RecordCommandBuffer(uint32_t imageIndex) {
    if (!renderGraph || imageIndex >= commandBuffers.size()) {
        return;
    }

    VkCommandBuffer cmdBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmdBuffer, &beginInfo) != VK_SUCCESS) {
        if (mainLogger) {
            mainLogger->Error("Failed to begin recording command buffer");
        }
        return;
    }

    // Execute the render graph
    renderGraph->Execute(cmdBuffer);

    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
        if (mainLogger) {
            mainLogger->Error("Failed to end recording command buffer");
        }
    }
}
