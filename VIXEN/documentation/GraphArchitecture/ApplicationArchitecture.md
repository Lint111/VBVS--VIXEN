# VulkanGraphApplication Architecture
**Date:** October 18, 2025
**Status:** Clean Separation of Concerns

## Core Principle

**VulkanGraphApplication is a thin orchestrator** - it builds the graph, configures nodes, and tells the graph to execute. It does NOT manage Vulkan resources directly.

## Separation of Concerns

### ✅ Application Responsibilities
- **Window management** (temporary via VulkanRenderer - TODO: extract to WindowManager)
- **Build render graph** - Add nodes, configure parameters
- **Compile render graph** - Trigger node setup
- **Execute render graph** - Call `renderGraph->Execute()`
- **Update application state** - Time, MVP matrices, global parameters
- **Handle window events** - Process messages, detect quit

### ✅ Render Graph Responsibilities
- **Command buffer allocation** - Each node allocates what it needs
- **Semaphore management** - SwapChainNode/PresentNode handle synchronization
- **Memory allocation** - Nodes create their own buffers, images, etc.
- **Pipeline creation** - GraphicsPipelineNode builds pipeline
- **Descriptor management** - DescriptorSetNode creates layouts/pools/sets
- **Resource lifecycle** - Nodes clean up in Cleanup()

### ❌ Application Does NOT Do
- ❌ Create command pools or command buffers
- ❌ Create semaphores or fences
- ❌ Allocate vertex buffers or uniform buffers
- ❌ Create pipelines or descriptor sets
- ❌ Record commands or submit queues
- ❌ Present to swapchain

## Code Flow

### Initialization
```cpp
VulkanGraphApplication::Initialize() {
    // 1. Initialize Vulkan instance + device
    VulkanApplicationBase::Initialize();

    // 2. Create window (temporary)
    rendererObj->CreatePresentationWindow(width, height);

    // 3. Create swapchain wrapper
    swapChainObj->Initialize();

    // 4. Create node registry
    nodeRegistry = std::make_unique<NodeTypeRegistry>();

    // 5. Register all node types
    RegisterNodeTypes();

    // 6. Create empty render graph
    renderGraph = std::make_unique<RenderGraph>(deviceObj, nodeRegistry);
}
```

### Preparation
```cpp
VulkanGraphApplication::Prepare() {
    // 1. Build swapchain (nodes will query it)
    swapChainObj->CreateSwapChain(VK_NULL_HANDLE);

    // 2. Add nodes to graph
    BuildRenderGraph();

    // 3. Compile graph
    // This calls Setup() and Compile() on all nodes
    // Each node allocates its Vulkan resources here
    CompileRenderGraph();
}
```

### Render Loop
```cpp
while (running) {
    // 1. Update application state
    app->Update();  // Updates time, MVP, global params

    // 2. Execute graph
    // Graph internally:
    // - Acquires swapchain image (SwapChainNode)
    // - Records commands (GeometryRenderNode)
    // - Submits with semaphores (internally managed)
    // - Presents (PresentNode)
    app->Render();  // Just calls renderGraph->Execute()
}
```

### Cleanup
```cpp
VulkanGraphApplication::DeInitialize() {
    // Wait for GPU
    vkDeviceWaitIdle(deviceObj->device);

    // Destroy render graph
    // Each node cleans up its own resources in ~NodeInstance()
    renderGraph.reset();

    // Clean up window/swapchain
    swapChainObj->DestroySwapChain();
    rendererObj->DestroyPresentationWindow();

    // Base cleanup
    VulkanApplicationBase::DeInitialize();
}
```

## Node Resource Ownership

| **Resource** | **Owned By** | **Created In** | **Destroyed In** |
|-------------|-------------|---------------|-----------------|
| Command Pool | SwapChainNode/GeometryRenderNode | Setup() | Cleanup() |
| Command Buffers | GeometryRenderNode | Compile() | Cleanup() |
| Semaphores | SwapChainNode | Setup() | Cleanup() |
| Fences | SwapChainNode (future) | Setup() | Cleanup() |
| Swapchain | SwapChainNode | Compile() | Cleanup() |
| Depth Image | DepthBufferNode | Compile() | Cleanup() |
| Vertex Buffer | VertexBufferNode | Compile() | Cleanup() |
| Index Buffer | VertexBufferNode (optional) | Compile() | Cleanup() |
| Uniform Buffer | DescriptorSetNode | Compile() | Cleanup() |
| Texture Image | TextureLoaderNode | Compile() | Cleanup() |
| Texture Sampler | TextureLoaderNode | Compile() | Cleanup() |
| Descriptor Layout | DescriptorSetNode | Compile() | Cleanup() |
| Descriptor Pool | DescriptorSetNode | Compile() | Cleanup() |
| Descriptor Sets | DescriptorSetNode | Compile() | Cleanup() |
| Render Pass | RenderPassNode | Compile() | Cleanup() |
| Framebuffers | FramebufferNode | Compile() | Cleanup() |
| Shader Modules | ShaderNode | Compile() | Cleanup() |
| Pipeline Cache | GraphicsPipelineNode | Setup() | Cleanup() |
| Pipeline Layout | GraphicsPipelineNode | Compile() | Cleanup() |
| Pipeline | GraphicsPipelineNode | Compile() | Cleanup() |

**Application owns:** NOTHING (except window, swapchain wrapper - temporary)

## VulkanGraphApplication API

### Minimal Interface
```cpp
class VulkanGraphApplication : public VulkanApplicationBase {
public:
    // Lifecycle (inherited from base)
    void Initialize() override;      // Setup graph infrastructure
    void Prepare() override;         // Build + compile graph
    void Update() override;          // Update application state
    bool Render() override;          // Execute graph
    void DeInitialize() override;    // Cleanup

    // Graph access (read-only)
    RenderGraph* GetRenderGraph() const;
    NodeTypeRegistry* GetNodeTypeRegistry() const;

protected:
    // Customization points
    virtual void BuildRenderGraph();     // Override to build custom graphs
    virtual void RegisterNodeTypes();    // Override to register custom nodes
    void CompileRenderGraph();           // Validate + compile

private:
    // Application owns ONLY:
    std::unique_ptr<NodeTypeRegistry> nodeRegistry;
    std::unique_ptr<RenderGraph> renderGraph;
    std::unique_ptr<VulkanRenderer> rendererObj;  // TODO: extract
    std::unique_ptr<VulkanSwapChain> swapChainObj;

    // State
    uint32_t currentFrame;
    EngineTime time;
    bool graphCompiled;
    int width, height;
};
```

## Benefits of This Architecture

### 1. **Decoupling**
- Application doesn't know about command buffers, semaphores, pipelines
- Nodes are self-contained, reusable units
- Easy to swap implementations (e.g., different pipeline configs)

### 2. **Encapsulation**
- Each node manages its own lifecycle
- No shared mutable state between application and nodes
- Clear ownership boundaries

### 3. **Testability**
- Can test nodes in isolation
- Can test graph compilation without rendering
- Can mock nodes for unit tests

### 4. **Composability**
- Add/remove nodes without changing application code
- Reorder graph execution by changing dependencies
- Create multiple graphs for different rendering modes

### 5. **Scalability**
- Easy to add new node types (compute, ray tracing, etc.)
- Graph optimization happens inside RenderGraph, not application
- Multi-threading can be added to graph execution

## Example: Adding a New Effect

**Old Way (VulkanRenderer):**
```cpp
// Modify VulkanRenderer
class VulkanRenderer {
    VulkanStatus CreateBloomPass();
    VulkanStatus CreateBloomPipeline();
    VulkanStatus CreateBloomFramebuffer();
    void RenderBloom();
    // ... 10+ new methods
};
```

**New Way (RenderGraph):**
```cpp
// Create BloomNode (self-contained)
class BloomNode : public NodeInstance {
    void Setup() override { /* allocate resources */ }
    void Compile() override { /* create pipeline */ }
    void Execute(VkCommandBuffer cmd) override { /* render */ }
    void Cleanup() override { /* destroy resources */ }
};

// Register once
nodeRegistry->RegisterNodeType(std::make_unique<BloomNodeType>());

// Use anywhere
auto bloomHandle = renderGraph->AddNode("Bloom", "bloom_effect");
auto* bloomNode = static_cast<BloomNode*>(renderGraph->GetInstance(bloomHandle));
bloomNode->SetParameter("intensity", 0.8f);
```

## Comparison: Old vs New

| **Aspect** | **VulkanRenderer** | **VulkanGraphApplication** |
|------------|-------------------|---------------------------|
| Command buffers | Application allocates | GeometryRenderNode allocates |
| Semaphores | Application creates | SwapChainNode creates |
| Pipeline | Renderer::CreatePipeline() | GraphicsPipelineNode::Compile() |
| Descriptors | Drawable::CreateDescriptor() | DescriptorSetNode::Compile() |
| Rendering | Drawable::Render() | renderGraph->Execute() |
| Cleanup | Manual destructor order | Automatic (node destruction) |
| Adding effects | Modify renderer class | Add node to graph |
| Resource ownership | Scattered | Centralized in nodes |

## Future Improvements

### 1. **Extract Window Management**
```cpp
class WindowManager {
    HWND CreateWindow(int width, int height);
    void ProcessEvents();
    void DestroyWindow();
};

// VulkanGraphApplication becomes even simpler
VulkanGraphApplication::Initialize() {
    VulkanApplicationBase::Initialize();
    windowManager->CreateWindow(width, height);
    // No VulkanRenderer dependency!
}
```

### 2. **Add Resource Graph**
- Track dependencies between nodes
- Automatic barrier insertion
- Resource aliasing for transient resources

### 3. **Multi-threaded Compilation**
- Compile independent nodes in parallel
- Record command buffers on multiple threads

### 4. **Graph Optimization**
- Merge compatible render passes
- Batch descriptor updates
- Reorder for better cache locality

### 5. **Hot Reload**
- Rebuild graph without restarting application
- Swap node implementations at runtime
- Live shader editing

## Conclusion

**VulkanGraphApplication is a coordinator, not a manager.**

It knows **what** to render (the graph structure), but not **how** to render (Vulkan API calls). This separation makes the codebase:
- Easier to understand (clear responsibilities)
- Easier to extend (add nodes, not modify app)
- Easier to test (nodes are isolated)
- Easier to optimize (graph-level transformations)

The render graph is now the single source of truth for rendering behavior, while the application focuses on high-level orchestration and user interaction.
