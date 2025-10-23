# RenderGraph Architecture Overview

**Last Updated**: October 23, 2025  
**Status**: Production-ready for single-threaded rendering

This is the **master architecture reference** for VIXEN's RenderGraph system. All future development should reference this document.

---

## Table of Contents

1. [Core Philosophy](#core-philosophy)
2. [System Components](#system-components)
3. [Variant Resource System](#variant-resource-system)
4. [Typed Node API](#typed-node-API)
5. [EventBus Integration](#eventbus-integration)
6. [Compilation & Execution](#compilation--execution)
7. [Resource Ownership Model](#resource-ownership-model)
8. [Adding New Nodes](#adding-new-nodes)
9. [Best Practices](#best-practices)

---

## Core Philosophy

**Three Pillars**:
1. **Compile-Time Type Safety** - Eliminate runtime type errors via templates and variants
2. **Clear Resource Ownership** - Graph owns resources, nodes access them
3. **Event-Driven Invalidation** - Decouple nodes via EventBus

**Design Goal**: Production-quality architecture matching industry leaders (Unity HDRP, Unreal RDG, Frostbite FrameGraph) with superior type safety.

---

## System Components

### 1. RenderGraph (Orchestrator)
**File**: `RenderGraph/src/Core/RenderGraph.cpp`

**Responsibilities**:
- Graph lifecycle (AddNode, ConnectNodes, Compile, Execute)
- Resource lifetime management (owns `std::vector<std::unique_ptr<Resource>>`)
- Topology analysis (dependency ordering via topological sort)
- Compilation phases (Validate → Analyze → Allocate → Generate → Build)

**Key Methods**:
```cpp
NodeHandle AddNode(NodeTypeId typeId, const std::string& name);
void ConnectNodes(NodeHandle from, uint32_t outSlot, NodeHandle to, uint32_t inSlot);
void Compile();  // 5-phase compilation
void Execute(VkCommandBuffer cmd);  // Executes in topological order
```

### 2. NodeInstance (Base Class)
**File**: `RenderGraph/include/Core/NodeInstance.h`

**Responsibilities**:
- Lifecycle hooks (Setup, Compile, Execute, Cleanup)
- Low-level resource accessors (GetInput/GetOutput - protected)
- State management (dirty flags, device affinity)

**Lifecycle**:
```
Created → Setup() → Ready → Compile() → Compiled → Execute() → Complete
                                                        ↑
                                                 SetDirty() (event-driven)
```

### 3. TypedNode<ConfigType> (Template Base)
**File**: `RenderGraph/include/Core/TypedNodeInstance.h`

**Responsibilities**:
- Compile-time type safety via slot definitions
- High-level typed API (`In()`, `Out()`, `SetOutput()`)
- Macro-generated storage

**Example**:
```cpp
struct MyNodeConfig {
    INPUT_SLOT(ALBEDO, VkImage, SlotMode::SINGLE);
    OUTPUT_SLOT(COLOR, VkImage, SlotMode::SINGLE);
};

class MyNode : public TypedNode<MyNodeConfig> {
    void Compile() override {
        VkImage albedo = In(MyNodeConfig::ALBEDO);  // Compile-time type check
        // ... process ...
        Out(MyNodeConfig::COLOR, resultImage);      // Compile-time type check
    }
};
```

### 4. Resource (Variant Container)
**File**: `RenderGraph/include/Core/ResourceVariant.h`

**Responsibilities**:
- Type-safe handle storage via `ResourceHandleVariant`
- Descriptor storage via `ResourceDescriptorVariant`
- Compile-time type validation

**Key Methods**:
```cpp
template<typename T>
void SetHandle(T handle) { handleVariant = handle; }  // Compile-time type check

template<typename T>
T GetHandle() const { return std::get<T>(handleVariant); }  // Throws if wrong type
```

### 5. EventBus (Invalidation System)
**File**: `EventBus/include/EventBus.h`

**Responsibilities**:
- Event subscription (nodes register for event types)
- Event queuing (processes at safe points, not during Compile/Execute)
- Cascade invalidation (WindowResize → SwapChainInvalidated → ...)

**Usage**:
```cpp
// Node subscribes in Setup()
void MyNode::Setup() override {
    eventBus->Subscribe(EventType::WindowResize, this);
}

// Node handles event, cascades
void MyNode::OnEvent(const Event& event) override {
    if (event.type == EventType::WindowResize) {
        SetDirty(true);
        eventBus->Emit(MyResourceInvalidatedEvent{});  // Cascade
    }
}
```

---

## Variant Resource System

### Single-Source Type Registry

**Macro**: `RESOURCE_TYPE_REGISTRY`  
**Location**: `RenderGraph/include/Core/ResourceVariant.h`

**Example**:
```cpp
#define RESOURCE_TYPE_REGISTRY(X) \
    X(VkImage, ImageDescriptor) \
    X(VkBuffer, BufferDescriptor) \
    X(VkImageView, HandleDescriptor) \
    X(VkFramebuffer, HandleDescriptor) \
    // ... 25+ types ...
```

**Auto-Generates**:
1. `ResourceHandleVariant` - `std::variant<std::monostate, VkImage, VkBuffer, ...>`
2. `ResourceTypeTraits<T>` - Compile-time type → descriptor mapping
3. `InitializeResourceFromType()` - Runtime descriptor initialization

**Benefits**:
- ✅ Zero duplication (single source of truth)
- ✅ Zero overhead (std::variant optimized away)
- ✅ Compile-time type safety
- ✅ Easy extension (add one line, everything updates)

---

## Typed Node API

### Config-Driven Slot Definitions

**Pattern**: Define slots in config struct, inherit from `TypedNode<Config>`

**Example**:
```cpp
struct FramebufferNodeConfig {
    // Input slots with types
    INPUT_SLOT(COLOR_ATTACHMENTS, VkImageView, SlotMode::ARRAY);
    INPUT_SLOT(DEPTH_ATTACHMENT, VkImageView, SlotMode::SINGLE);
    INPUT_SLOT(RENDER_PASS, VkRenderPass, SlotMode::SINGLE);
    
    // Output slot
    OUTPUT_SLOT(FRAMEBUFFER, VkFramebuffer, SlotMode::SINGLE);
    
    // Compile-time counts
    static constexpr uint32_t INPUT_COUNT = 3;
    static constexpr uint32_t OUTPUT_COUNT = 1;
};
```

### Typed Access Methods

**`In(SlotType, arrayIndex = 0)`** - Read input slot
```cpp
VkImageView colorView = In(FramebufferNodeConfig::COLOR_ATTACHMENTS, 0);
VkImageView depthView = In(FramebufferNodeConfig::DEPTH_ATTACHMENT);
```

**`Out(SlotType, value, arrayIndex = 0)`** - Write output slot
```cpp
Out(FramebufferNodeConfig::FRAMEBUFFER, myFramebuffer);
```

**`SetOutput(SlotType, arrayIndex, value)`** - Set output at index
```cpp
SetOutput(TextureNodeConfig::TEXTURES, 0, texture0);
SetOutput(TextureNodeConfig::TEXTURES, 1, texture1);
```

**Type Safety**:
- Compile-time: Slot definition enforces type
- Runtime (debug): Validates variant type matches expected

---

## EventBus Integration

### Event Categories

| Category | Range | Examples | Purpose |
|----------|-------|----------|---------|
| Resource Invalidation | 0-99 | WindowResize, SwapChainInvalidated, ShaderReloaded | Trigger recompilation |
| Application State | 100-199 | SceneChanged, CameraUpdated | Update scene state |
| Execution | 200-299 | FrameComplete | Rarely used (prefer SynchronizationManager) |

### Cascade Invalidation Pattern

**Flow**:
```
User resizes window
    ↓
WindowNode emits WindowResizeEvent
    ↓
EventBus queues event
    ↓
RenderGraph::ProcessEvents() drains queue
    ↓
SwapChainNode receives event, marks dirty, emits SwapChainInvalidatedEvent
    ↓
FramebufferNode receives event, marks dirty
    ↓
RenderGraph::RecompileDirtyNodes() recompiles SwapChainNode → FramebufferNode
```

**Code Example**:
```cpp
// SwapChainNode.cpp
void SwapChainNode::OnEvent(const Event& event) {
    if (event.type == EventType::WindowResize) {
        const auto& resizeEvent = static_cast<const WindowResizeEvent&>(event);
        
        // Mark dirty (will recompile next frame)
        SetDirty(true);
        
        // Cascade: downstream nodes need to rebuild
        eventBus->Emit(std::make_unique<SwapChainInvalidatedEvent>(
            GetInstanceId(),
            newWidth,
            newHeight
        ));
    }
}
```

---

## Compilation & Execution

### Compilation Phases

**Phase 1: Validate**
- Check all inputs connected
- Verify no cycles (DAG property)

**Phase 2: AnalyzeDependencies**
- Build directed graph from connections
- Topological sort for execution order

**Phase 3: AllocateResources**
- Analyze resource lifetimes
- Allocate physical Vulkan resources (future: alias transients)

**Phase 4: GeneratePipelines**
- Group instances by type
- Create shared pipelines (pipeline sharing across instances)
- Check pipeline cache

**Phase 5: BuildExecutionOrder**
- Finalize execution order list
- (Future: Generate execution waves for parallelism)

### Execution Flow

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (NodeInstance* node : executionOrder) {
        if (node->GetState() == NodeState::Ready || 
            node->GetState() == NodeState::Compiled) {
            
            node->SetState(NodeState::Executing);
            node->Execute(cmd);  // Virtual call (acceptable overhead <200 nodes)
            node->SetState(NodeState::Complete);
        }
    }
}
```

**Note**: Virtual dispatch adds ~2-5ns per call. Acceptable for <200 nodes. For 500+ nodes, consider compiled execution (devirtualized function pointers).

---

## Resource Ownership Model

### Golden Rule

**Graph owns lifetime, nodes access logically**

```cpp
class RenderGraph {
    std::vector<std::unique_ptr<Resource>> resources;  // OWNS lifetime
};

class NodeInstance {
    std::vector<std::vector<Resource*>> inputs;   // Raw pointers (logical access)
    std::vector<std::vector<Resource*>> outputs;  // Raw pointers (logical access)
};
```

### Why This Works

1. **No circular dependencies** - Nodes don't own resources, can't have cycles
2. **Clear destruction order** - Graph destructs → resources destruct → no dangling pointers
3. **RAII compliance** - Graph constructor allocates, destructor frees
4. **Simple lifetime reasoning** - "Does this resource outlive the graph?" → Always yes

### Anti-Pattern (Don't Do This)

```cpp
// ❌ BAD: Node owns resources
class BadNode : public NodeInstance {
    std::unique_ptr<Resource> ownedResource;  // DON'T DO THIS
};
```

**Why bad**: Creates lifetime ambiguity. Who owns resource if node is removed from graph?

---

## Adding New Nodes

### Step-by-Step Guide

**1. Create Node Config** (`include/RenderGraph/Nodes/MyNodeConfig.h`)
```cpp
struct MyNodeConfig {
    INPUT_SLOT(INPUT_IMAGE, VkImage, SlotMode::SINGLE);
    OUTPUT_SLOT(OUTPUT_IMAGE, VkImage, SlotMode::SINGLE);
    
    static constexpr uint32_t INPUT_COUNT = 1;
    static constexpr uint32_t OUTPUT_COUNT = 1;
};
```

**2. Create Node Implementation** (`src/RenderGraph/Nodes/MyNode.cpp`)
```cpp
#include "RenderGraph/Nodes/MyNodeConfig.h"

class MyNode : public TypedNode<MyNodeConfig> {
public:
    void Setup() override {
        // Subscribe to events if needed
        if (eventBus) {
            eventBus->Subscribe(EventType::SomeEvent, this);
        }
    }
    
    void Compile() override {
        // Get inputs
        VkImage inputImg = In(MyNodeConfig::INPUT_IMAGE);
        
        // Create Vulkan resources (pipeline, descriptors, etc.)
        // ... create resources ...
        
        // Set outputs
        Out(MyNodeConfig::OUTPUT_IMAGE, resultImage);
    }
    
    void Execute(VkCommandBuffer cmd) override {
        // Record commands or no-op (many nodes are setup-only)
        if (cmd != VK_NULL_HANDLE) {
            // vkCmdDraw, vkCmdDispatch, etc.
        }
    }
    
    void Cleanup() override {
        // Destroy Vulkan resources (reverse of Compile)
    }
    
    void OnEvent(const Event& event) override {
        // Handle invalidation events
        if (event.type == EventType::SomeEvent) {
            SetDirty(true);
        }
    }
};
```

**3. Register in NodeTypeRegistry**
```cpp
registry.RegisterNodeType(std::make_unique<MyNodeType>());
```

**4. Add to CMakeLists.txt** (if not using Unity Build)
```cmake
# RenderGraph/CMakeLists.txt (Nodes library)
# No need if using Unity Build (auto-includes all .cpp in Nodes/)
```

---

## Best Practices

### DO ✅

1. **Use In()/Out() exclusively in nodes** - Don't call GetInput/GetOutput directly
2. **Subscribe to events in Setup()** - Unsubscribe in Cleanup()
3. **Mark dirty on invalidation** - SetDirty(true) when resources need rebuild
4. **Create resources in Compile()** - Destroy in Cleanup()
5. **Keep Execute() lightweight** - Record commands only, no heavy computation
6. **Use const-correctness** - Mark accessors const if they don't modify state
7. **Document node purpose** - Header comment explaining what node does

### DON'T ❌

1. **Don't own resources in nodes** - Graph owns resources, nodes access
2. **Don't call ProcessEvents() during Compile/Execute** - Events processed at safe points
3. **Don't use magic numbers** - Use slot definitions (In(Config::SLOT) not In(0))
4. **Don't assume execution order** - Use dependencies, not hardcoded order
5. **Don't create Vulkan resources in Setup()** - Setup is for event subscription only
6. **Don't forget to handle events** - Override OnEvent() if subscribed
7. **Don't make Execute() stateful** - Must be repeatable (future multithreading)

### Common Patterns

**Pattern 1: Resource Creation Node**
```cpp
void Compile() override {
    // Get dimensions from inputs
    uint32_t width = In(Config::WIDTH);
    
    // Create Vulkan resource
    VkImageCreateInfo imageInfo{};
    // ... fill info ...
    vkCreateImage(device, &imageInfo, nullptr, &image);
    
    // Output resource
    Out(Config::OUTPUT_IMAGE, image);
}
```

**Pattern 2: Pass-Through Node**
```cpp
void Compile() override {
    // Just forward input to output
    VkImage img = In(Config::INPUT_IMAGE);
    Out(Config::OUTPUT_IMAGE, img);
}
```

**Pattern 3: Multi-Output Node**
```cpp
void Compile() override {
    for (size_t i = 0; i < textureCount; ++i) {
        VkImage tex = LoadTexture(i);
        SetOutput(Config::TEXTURES, i, tex);
    }
}
```

---

## Next Steps

**For Contributors**:
1. Read this document (you're here!)
2. Review `documentation/ArchitecturalReview-2025-10.md` (industry comparison, recommendations)
3. Study existing nodes in `RenderGraph/src/Nodes/` (FramebufferNode is good example)
4. Read `documentation/EventBusArchitecture.md` (event-driven patterns)

**For Architecture Evolution**:
1. Implement HIGH priority recommendations (resource validation, replace friend access)
2. Add execution wave metadata (parallelization)
3. Add memory budget tracking (console/mobile support)
4. Implement transient resource aliasing (memory optimization)

---

## Related Documentation

- **EventBusArchitecture.md** - Event system details
- **ArchitecturalReview-2025-10.md** - October 2025 critique with industry comparison
- **GraphArchitecture/** - 20+ detailed docs on specific topics
- **render-graph-quick-reference.md** - Quick API reference
- **memory-bank/systemPatterns.md** - Design patterns overview

---

**Questions?** See existing nodes in `RenderGraph/src/Nodes/` for real-world examples.
