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
