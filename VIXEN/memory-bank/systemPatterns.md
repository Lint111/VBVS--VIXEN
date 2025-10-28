# System Patterns

## Architecture Overview

The project uses **two parallel architectures**:
1. **Legacy Monolithic** - VulkanApplication orchestrates direct Vulkan calls (original learning code)
2. **RenderGraph System** - Graph-based rendering with compile-time type safety (production architecture)

### RenderGraph Architecture (Primary)

```
┌─────────────────────────────────────────┐
│          RenderGraph                    │  ← Graph orchestrator
│  - Compilation phases                   │
│  - Resource ownership                   │
│  - Execution ordering                   │
└──────────────┬──────────────────────────┘
               │
    ┌──────────┼──────────┬────────────────────┐
    │          │          │                    │
    ▼          ▼          ▼                    ▼
┌────────────────┐  ┌──────────┐  ┌──────────────────┐  ┌──────────┐
│  NodeInstance  │  │ Resource │  │  EventBus        │  │ Topology │
│  (Base class)  │  │ (Variant)│  │  (Invalidation)  │  │  (DAG)   │
└───────┬────────┘  └──────────┘  └──────────────────┘  └──────────┘
        │
        ▼
┌───────────────────────┐
│ TypedNode<ConfigType> │  ← Template with compile-time type safety
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐
│  Concrete Nodes       │  ← SwapChainNode, FramebufferNode, etc.
│  (15+ implementations)│
└───────────────────────┘
```

### Legacy Architecture (Reference)

```
┌─────────────────────────────────────┐
│       VulkanApplication             │  ← Singleton orchestrator
│    (Application lifecycle mgmt)     │
└──────────────┬──────────────────────┘
               │
    ┌──────────┴──────────┬────────────────┬─────────────┐
    │                     │                │             │
    ▼                     ▼                ▼             ▼
┌──────────┐      ┌──────────────┐  ┌─────────────┐  ┌──────────────┐
│ Instance │      │    Device    │  │  Renderer   │  │ SwapChain    │
└──────────┘      └──────────────┘  └─────────────┘  └──────────────┘
```

## RenderGraph Design Patterns (October 27, 2025 Update)

### Core Architecture Principles
1. **Handle-Based Access** - O(1) node lookups via direct indexing
2. **Dependency-Ordered Cleanup** - Auto-detected via input connections
3. **Two-Semaphore Sync** - GPU-GPU synchronization per swapchain image
4. **Event-Driven Invalidation** - Cascade recompilation through dependency graph
5. **Graph-Owns-Resources** - Clear lifetime management, no circular dependencies

## RenderGraph Design Patterns

### 1. Resource Variant Pattern
**Classes**: `Resource`, `ResourceHandleVariant`, `ResourceDescriptorVariant`

**Implementation**:
```cpp
// Single macro registration
REGISTER_RESOURCE_TYPE(VkImage, ImageDescriptor)

// Auto-generates:
// - ResourceHandleVariant member
// - ResourceTypeTraits specialization
// - InitializeResourceFromType() switch case

// Compile-time type-safe access
template<typename T>
T Resource::GetHandle() { return std::get<T>(handleVariant); }
```

**Purpose**: Zero-overhead type safety, eliminates manual type-punning

**Location**: `RenderGraph/include/Core/ResourceVariant.h`

### 2. Typed Node Pattern
**Classes**: `NodeInstance`, `TypedNode<ConfigType>`, Concrete nodes

**Implementation**:
```cpp
// Config defines slots with compile-time types
struct MyNodeConfig {
    INPUT_SLOT(ALBEDO, VkImage, SlotMode::SINGLE);
    OUTPUT_SLOT(FRAMEBUFFER, VkFramebuffer, SlotMode::SINGLE);
};

// Node uses typed API
class MyNode : public TypedNode<MyNodeConfig> {
    void Compile() override {
        VkImage albedo = In(MyNodeConfig::ALBEDO);  // Compile-time type check
        Out(MyNodeConfig::FRAMEBUFFER, myFB);       // Compile-time type check
    }
};
```

**Purpose**: Compile-time slot validation, eliminates magic indices

**Location**: `RenderGraph/include/Core/TypedNodeInstance.h`

### 3. Graph-Owns-Resources Pattern
**Class**: `RenderGraph`

**Implementation**:
```cpp
class RenderGraph {
    std::vector<std::unique_ptr<Resource>> resources;  // Graph owns lifetime
};

class NodeInstance {
    std::vector<std::vector<Resource*>> inputs;   // Raw pointers (logical access)
    std::vector<std::vector<Resource*>> outputs;
};
```

**Purpose**: Clear lifetime management, no circular dependencies

**Principle**: Graph owns resources (allocation/destruction), nodes access resources (logical containers)

### 4. EventBus Invalidation Pattern
**Classes**: `EventBus`, `IEventListener`, Concrete nodes

**Implementation**:
```cpp
// Node subscribes to events
void SwapChainNode::Setup() override {
    eventBus->Subscribe(EventType::WindowResize, this);
}

// Node handles event, marks dirty
void SwapChainNode::OnEvent(const Event& event) override {
    if (event.type == EventType::WindowResize) {
        SetDirty(true);
        eventBus->Emit(SwapChainInvalidatedEvent{});  // Cascade
    }
}

// Graph recompiles dirty nodes at safe points
graph.ProcessEvents();       // Drain event queue
graph.RecompileDirtyNodes(); // Recompile affected nodes
```

**Purpose**: Decoupled node communication, cascade invalidation

**Location**: `EventBus/include/EventBus.h`

### 5. Node Lifecycle Pattern
**Class**: `NodeInstance`

**Lifecycle Hooks**:
```cpp
void Setup()    // One-time initialization (subscribe to events)
void Compile()  // Create Vulkan resources (pipelines, descriptors)
void Execute()  // Record commands or no-op
void Cleanup()  // Destroy Vulkan resources
```

**State Machine**:
```
Created → Setup() → Ready
Ready → Compile() → Compiled
Compiled → Execute() → Complete
Complete → SetDirty() → Ready  // Invalidation
```

**Purpose**: Clear state transitions, supports lazy compilation

### 6. Protected API Enforcement Pattern
**Classes**: `NodeInstance`, `TypedNode`, `RenderGraph`

**Implementation**:
```cpp
class NodeInstance {
    friend class RenderGraph;  // Graph can wire nodes
protected:
    // Low-level accessors (internal use only)
    Resource* GetInput(uint32_t slot, uint32_t idx);
    void SetInput(uint32_t slot, uint32_t idx, Resource* res);
};

class TypedNode : public NodeInstance {
public:
    // High-level typed API (nodes use this)
    template<typename SlotType>
    typename SlotType::Type In(SlotType slot, size_t idx = 0);
};
```

**Purpose**: Single API pattern - nodes use `In()`/`Out()`, graph uses low-level wiring

**Note**: Friend access is broad (architectural review recommends `INodeWiring` interface)

### 7. Handle-Based Node Access Pattern (October 27, 2025)
**Classes**: `NodeHandle`, `RenderGraph`, `CleanupStack`

**Implementation**:
```cpp
// NodeHandle is a simple direct index
struct NodeHandle {
    size_t index;
    bool IsValid() const { return index != INVALID_INDEX; }
};

// CleanupStack uses handles for O(1) access
class CleanupStack {
    std::unordered_map<NodeHandle, std::unique_ptr<CleanupNode>> nodes;

    void RegisterNode(NodeHandle handle, std::function<void()> cleanup) {
        nodes[handle] = std::make_unique<CleanupNode>(cleanup);
    }
};

// GetAllDependents returns handles directly
std::unordered_set<NodeHandle> GetAllDependents(NodeHandle handle);
```

**Purpose**: Eliminates string-based lookups, reduces recompilation cascade from O(n²) to O(n)

**Key Insight**: Handles are stable during recompilation (no graph restructure), only invalidate when nodes added/removed

### 8. Cleanup Dependency Pattern (October 27, 2025)
**Classes**: `CleanupStack`, `NodeInstance`, `ResourceDependencyTracker`

**Implementation**:
```cpp
// Nodes register cleanup during compilation
void NodeInstance::RegisterCleanup() {
    if (!cleanupCallback) {
        cleanupCallback = [this]() { this->Cleanup(); };
    }

    if (auto graph = GetRenderGraph()) {
        // Auto-detect dependencies from input connections
        auto dependencies = ResourceDependencyTracker::BuildCleanupDependencies(this);
        graph->GetCleanupStack().RegisterNode(handle, cleanupCallback, dependencies);
    }
}

// CleanupStack executes in dependency order
void CleanupStack::ExecuteAll() {
    std::unordered_set<NodeHandle> visited;
    for (auto& [handle, node] : nodes) {
        ExecuteCleanup(handle, &visited);  // Recursive, visits children first
    }
}

void CleanupStack::ExecuteCleanup(NodeHandle handle, std::unordered_set<NodeHandle>* visited) {
    if (visited->count(handle)) return;  // Prevent duplicate cleanup
    visited->insert(handle);

    // Clean dependents first (children before parents)
    for (auto& dep : nodes[handle]->dependents) {
        ExecuteCleanup(dep, visited);
    }

    // Clean this node
    nodes[handle]->cleanupCallback();
}
```

**Purpose**: Ensures Vulkan resources destroyed in correct order (children before parents), prevents validation errors

**Recompilation Flow**:
```cpp
// RenderGraph.cpp lines 1060-1075
node->Cleanup();              // Destroy old resources
node->Setup();                // Re-subscribe to events
node->Compile();              // Create new resources
node->RegisterCleanup();      // Register new cleanup callback
node->ResetCleanupFlag();     // Allow cleanup to run again
cleanupStack.ResetExecuted(handle);  // Allow CleanupStack to execute again
```

**Key Insight**: Nodes don't call `Cleanup()` in `Compile()` - RenderGraph handles lifecycle

### 9. Two-Semaphore Synchronization Pattern (October 27, 2025)
**Classes**: `SwapChainNode`, `GeometryRenderNode`, `PresentNode`

**Implementation**:
```cpp
// SwapChainNode creates image available semaphores
void SwapChainNode::Compile() {
    imageAvailableSemaphores.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
    }
}

// GeometryRenderNode creates render complete semaphores
void GeometryRenderNode::Compile() {
    renderCompleteSemaphores.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]);
    }
}

// GeometryRenderNode waits on imageAvailable, signals renderComplete
void GeometryRenderNode::Execute() {
    VkSemaphore imageAvailableSem = In(IMAGE_AVAILABLE_SEMAPHORE);
    VkSemaphore renderCompleteSem = renderCompleteSemaphores[imageIndex];

    VkSubmitInfo submitInfo = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSem,           // Wait for image
        .pWaitDstStageMask = &waitStage,                 // COLOR_ATTACHMENT_OUTPUT_BIT
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderCompleteSem          // Signal render done
    };
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);

    Out(RENDER_COMPLETE_SEMAPHORE, renderCompleteSem);
}

// PresentNode waits on renderComplete
void PresentNode::Present() {
    VkSemaphore renderCompleteSem = In(RENDER_COMPLETE_SEMAPHORE);
    VkPresentInfoKHR presentInfo = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSem            // Wait for render
    };
    vkQueuePresentKHR(queue, &presentInfo);
}
```

**Purpose**: GPU-GPU synchronization without CPU stalls, enables frame pipelining

**Synchronization Timeline**:
```
vkAcquireNextImageKHR → signals imageAvailableSemaphores[i]
    ↓ (GPU waits)
vkQueueSubmit → waits imageAvailable, signals renderCompleteSemaphores[i]
    ↓ (GPU waits)
vkQueuePresentKHR → waits renderComplete
```

**Key Insight**: Distributed ownership - each node owns semaphores it creates, passes via slots

## Legacy Design Patterns (Reference)

### 1. Singleton Pattern
**Class**: `VulkanApplication`

**Implementation**:
```cpp
static std::unique_ptr<VulkanApplication> instance;
static VulkanApplication* GetInstance();
```

**Purpose**: Single application instance

### 2. Layered Initialization
**Sequence**:
```
main()
  → VulkanApplication::Initialize()
      → VulkanInstance::CreateInstance()
      → EnumeratePhysicalDevices()
      → HandShakeWithDevice()
  → VulkanApplication::Prepare()
  → VulkanApplication::render() (loop)
  → VulkanApplication::DeInitialize()
```

### 3. Manager-Owns-Resources Pattern
**Legacy classes own their Vulkan resources**:
- `VulkanInstance` owns `VkInstance`
- `VulkanDevice` owns logical device
- `VulkanRenderer` owns rendering resources

## Key Component Responsibilities

### VulkanApplication (Orchestrator)
- **Single Responsibility**: Application lifecycle management
- **Key Methods**:
  - `Initialize()`: Sets up Vulkan instance and device
  - `Prepare()`: Prepares rendering resources
  - `Update()`: Updates scene/state
  - `render()`: Executes render loop
  - `DeInitialize()`: Cleans up resources

### VulkanInstance (Instance Manager)
- **Single Responsibility**: Vulkan instance lifecycle
- **Key Methods**:
  - `CreateInstance()`: Creates VkInstance with layers/extensions
  - Destruction handles VkInstance cleanup

### VulkanDevice (Device Manager)
- **Single Responsibility**: Physical and logical device management
- **Key Methods**:
  - Physical device selection
  - Logical device creation with queue families
  - Device properties and capabilities query

### VulkanRenderer (Rendering Manager)
- **Single Responsibility**: Manages rendering pipeline and execution
- **Coordinates**:
  - Command buffers
  - Synchronization primitives
  - Drawing operations

### VulkanSwapChain (Presentation Manager)
- **Single Responsibility**: Manages presentation swapchain
- **Handles**:
  - Swapchain creation and configuration
  - Image views
  - Surface capabilities

## Data Flow Patterns

### Initialization Flow
```
Extensions/Layers (main.cpp)
    ↓
VulkanApplication::CreateVulkanInstance()
    ↓
VulkanInstance::CreateInstance()
    ↓
VulkanApplication::EnumeratePhysicalDevices()
    ↓
VulkanApplication::HandShakeWithDevice()
    ↓
VulkanDevice creation
```

### Render Loop Flow
```
main() while loop
    ↓
VulkanApplication::render()
    ↓
VulkanRenderer::render()
    ↓
Command buffer execution
    ↓
Swapchain present
```

## Error Handling Strategy

### Pattern: VkResult Propagation
- Methods return `VkResult` for Vulkan operations
- Caller checks result and handles accordingly
- Exceptions used for catastrophic failures (in main.cpp)

### Validation Layers
- Enabled via `VK_LAYER_KHRONOS_validation`
- Debug callback for runtime validation
- Helps catch API misuse during development

## Memory Management Patterns

### Smart Pointers
- `std::unique_ptr` for singleton instance
- Raw pointers for Vulkan object handles (managed by owner classes)

### RAII Principle
- Constructors acquire resources
- Destructors release resources
- Prevents leaks through automatic cleanup

### Member Variables
- `gpuList` stored as member to prevent premature destruction
- Ownership clearly defined per class

## Configuration Pattern

### Compile-Time Configuration
- Platform defines: `VK_USE_PLATFORM_WIN32_KHR`
- Language standard: C++23

### Runtime Configuration
- Extensions and layers defined in `main.cpp`
- Passed to application during initialization
- Allows flexible configuration without recompilation

## Critical Implementation Paths

### Path 1: Instance Creation
`main.cpp` → `VulkanApplication::Initialize()` → `VulkanInstance::CreateInstance()`

### Path 2: Device Selection
`VulkanApplication::EnumeratePhysicalDevices()` → `VulkanApplication::HandShakeWithDevice()`

### Path 3: Rendering Setup
`VulkanApplication::Prepare()` → Renderer/SwapChain/Pipeline initialization

### Path 4: Frame Rendering
`main.cpp` loop → `VulkanApplication::render()` → `VulkanRenderer::render()`

### Path 5: Cleanup
`VulkanApplication::DeInitialize()` → Component destructors

## Future Extensibility

The architecture supports extension through:
1. Adding new component classes (e.g., `VulkanTexture`, `VulkanBuffer`)
2. Extending renderer capabilities
3. Adding new pipeline stages
4. Implementing additional rendering passes

Each addition follows the established pattern:
- Create dedicated class for subsystem
- Manage resource lifecycle via RAII
- Integrate with VulkanApplication orchestration
