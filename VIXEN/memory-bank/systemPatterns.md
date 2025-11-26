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
```

**Purpose**: Proper GPU-GPU synchronization without CPU stalls

**Location**: SwapChainNode, GeometryRenderNode, PresentNode

---

### 10. Compute Pipeline Pattern (November 5, 2025)
**Classes**: `ComputePipelineNode`, `ComputeDispatchNode`

**Implementation**:
```cpp
// ComputePipelineNode - Creates pipeline (data-driven)
class ComputePipelineNode {
    void CompileImpl(Context& ctx) override {
        auto device = ctx.In(VULKAN_DEVICE_IN);
        auto shaderBundle = ctx.In(SHADER_DATA_BUNDLE);

        // Auto-generate descriptor layout if not provided
        if (!ctx.In(DESCRIPTOR_SET_LAYOUT)) {
            descriptorSetLayout = DescriptorSetLayoutCacher::GetOrCreate(device, shaderBundle);
        }

        // Create pipeline via cacher
        VkPipeline pipeline = ComputePipelineCacher::GetOrCreate(device, params);

        ctx.Out(PIPELINE, pipeline);
        ctx.Out(PIPELINE_LAYOUT, pipelineLayout);
    }
};

// ComputeDispatchNode - Generic dispatcher (any shader)
class ComputeDispatchNode {
    void CompileImpl(Context& ctx) override {
        auto pipeline = ctx.In(COMPUTE_PIPELINE);
        auto descriptorSets = ctx.In(DESCRIPTOR_SETS);

        // Record dispatch command
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);
        vkCmdDispatch(cmd, dispatchX, dispatchY, dispatchZ);

        ctx.Out(COMMAND_BUFFER, cmd);
    }
};
```

**Purpose**: Separation of concerns - pipeline creation vs dispatch logic, enables generic compute shaders

**Key Insight**: Ray marching becomes application-level graph wiring, not node-level logic

**Location**: `RenderGraph/src/Nodes/ComputePipelineNode.cpp`, `RenderGraph/src/Nodes/ComputeDispatchNode.cpp`

---

### 11. Test Infrastructure Pattern (November 5, 2025)
**Classes**: GoogleTest suites, VS Code Test Explorer integration

**Implementation**:
```cpp
// Test fixture with RenderGraph setup
class ResourceManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        budgetManager = std::make_unique<ResourceBudgetManager>();
        destructionQueue = std::make_unique<DeferredDestruction>();
    }
};

// Comprehensive test coverage
TEST_F(ResourceBudgetManagerTest, SetBudget) {
    ResourceBudget budget;
    budget.totalBytes = 1024 * 1024 * 100;
    budgetManager->SetBudget(BudgetResourceType::Image, budget);
    // ... verification
}
```

**VS Code Integration**:
```json
{
  "testMate.cpp.test.executables": "{build}/**/*{test,Test,TEST}*",
  "cmake.testExplorer.enabled": true,
  "coverage-gutters.coverageBaseDir": "${workspaceFolder}/VIXEN/build"
}
```

**Purpose**: Hierarchical test organization, LCOV coverage visualization, one-click debugging

**Coverage Workflow**:
1. Build with ENABLE_COVERAGE=ON
2. Run tests (generates .profdata)
3. Generate LCOV (coverage target)
4. View in VS Code (green/orange/red gutters)

**Documentation**: `VIXEN/docs/VS_CODE_TESTING_SETUP.md` (~800 pages)

**Location**: `.vscode/settings.json`, `VIXEN/tests/RenderGraph/`

---

### 17. shared_ptr<T> Support Pattern (November 15, 2025 - Phase H)
**Purpose**: Enable shared ownership semantics for complex resources (ShaderDataBundle, reflection data)

**Implementation**:
- PassThroughStorage detects `shared_ptr<T>` at compile-time via template specialization detection
- Validates element type T via `SharedPtrElementType` helper
- Stores shared_ptr in Value mode (std::any for copy semantics)
- Type registration: `REGISTER_COMPILE_TIME_TYPE(std::shared_ptr<ShaderManagement::ShaderDataBundle>)`

**Migration from raw pointers**:
```cpp
// Before (Phase G - no-op deleter hack)
INPUT_SLOT(SHADER_DATA, ShaderDataBundle*, ...);
bundle = std::shared_ptr<ShaderDataBundle>(rawPtr, [](ShaderDataBundle*){});

// After (Phase H - proper shared ownership)
INPUT_SLOT(SHADER_DATA, std::shared_ptr<ShaderDataBundle>, ...);
bundle = std::make_shared<ShaderDataBundle>(...);
```

**Benefits**:
- Eliminates no-op deleter hacks
- Clear ownership semantics
- Automatic lifetime management
- Type-safe shared access across nodes

**Location**: `RenderGraph/include/Data/Core/CompileTimeResourceSystem.h` (lines 214-294)

---

### 18. ResourceVariant Elimination Pattern (November 15, 2025 - Phase H)
**Purpose**: Eliminate redundant type-erasure wrapper, use PassThroughStorage directly

**Migration**:
```cpp
// Before (Phase G)
struct ResourceVariant {
    PassThroughStorage storage;
    // Wrapper with no additional value
};

// After (Phase H)
// ResourceVariant deleted - use PassThroughStorage directly
using Resource = PassThroughStorage;
```

**Affected code**:
- Deleted: `RenderGraph/include/Data/Core/ResourceVariant.h`
- Updated: `ResourceGathererNode.h` to use PassThroughStorage
- Updated: All test files to use new API

**Rationale**: ResourceVariant added no functionality beyond PassThroughStorage. Eliminating wrapper reduces indirection and clarifies architecture.

**Location**: Phase H refactoring (November 2025)

---

### 19. Slot Type Compatibility Pattern (November 16, 2025 - Phase H)
**Purpose**: Validate type compatibility before connecting slots, prevent runtime type errors

**Implementation**:
```cpp
// TypedConnection.h - Compile-time and runtime type compatibility checks
template<typename SourceSlotType, typename DestSlotType>
bool AreTypesCompatible() {
    using SourceType = typename SourceSlotType::Type;
    using DestType = typename DestSlotType::Type;

    // Direct type match
    if constexpr (std::is_same_v<SourceType, DestType>) {
        return true;
    }

    // Reference unwrapping: const T& matches T
    if constexpr (IsConstReference<SourceType> && !IsConstReference<DestType>) {
        using UnwrappedSource = std::remove_cvref_t<SourceType>;
        return std::is_same_v<UnwrappedSource, DestType>;
    }

    // Vector element compatibility: vector<T> matches vector<const T&>
    if constexpr (IsVector<SourceType> && IsVector<DestType>) {
        using SourceElement = typename SourceType::value_type;
        using DestElement = typename DestType::value_type;
        return std::is_same_v<std::remove_cvref_t<SourceElement>,
                               std::remove_cvref_t<DestElement>>;
    }

    return false;
}
```

**Benefits**:
- Catches type mismatches at connection time (not Execute time)
- Supports const-correctness (const T& → T automatic unwrapping)
- Handles container types (vector<T> compatibility checks)
- Clear error messages for incompatible connections

**Location**: `RenderGraph/include/Core/TypedConnection.h` (lines 40-80, November 2025)

---

### 20. Perfect Forwarding Pattern (November 16, 2025 - Phase H)
**Purpose**: Zero-overhead resource output, avoid unnecessary copies

**Implementation**:
```cpp
// TypedNodeInstance.h - Perfect forwarding for Out() and SetResource()
template<typename SlotType, typename T>
void Out(SlotType slot, T&& value) {
    static_assert(SlotType::IS_OUTPUT, "Out() requires output slot");
    SetResource(SlotType::INDEX, std::forward<T>(value));
}

template<typename T>
void SetResource(size_t slotIndex, T&& value) {
    PassThroughStorage storage;

    // Use perfect forwarding to avoid copies
    if constexpr (std::is_lvalue_reference_v<T>) {
        storage = PassThroughStorage::CreateReference(value);  // Reference mode
    } else {
        storage = PassThroughStorage::CreateValue(std::forward<T>(value));  // Move semantics
    }

    outputs[slotIndex][0] = std::move(storage);
}
```

**Benefits**:
- Zero-overhead: lvalues → reference mode, rvalues → move semantics
- Automatic reference vs value distinction based on value category
- Type-safe: PassThroughStorage handles lifetime correctly
- Compiler-optimized: std::forward enables copy elision

**Migration**:
```cpp
// Before (Phase G): Always copied
Out(OUTPUT_SLOT, myResource);

// After (Phase H): Perfect forwarding
Out(OUTPUT_SLOT, myResource);              // Reference mode (lvalue)
Out(OUTPUT_SLOT, std::move(myResource));    // Move semantics (rvalue)
Out(OUTPUT_SLOT, CreateResource());         // Move semantics (temporary)
```

**Location**: `RenderGraph/include/Core/TypedNodeInstance.h` (lines 200-230, November 2025)

---

### 21. EntityBrickView Zero-Storage Pattern (November 2025 - Phase H.2)
**Classes**: `EntityBrickView`, `GaiaVoxelWorld`, `LaineKarrasOctree`

**Implementation**:
```cpp
// EntityBrickView stores only reference data (16 bytes)
class EntityBrickView {
    GaiaVoxelWorld* world;      // 8 bytes - reference to entity storage
    uint64_t baseMortonKey;     // 8 bytes - brick origin in Morton space
    // No voxel data storage - queries ECS on-demand

public:
    // Query voxel by local brick position (0-7 per axis)
    bool getVoxelAt(int x, int y, int z) const {
        uint64_t key = baseMortonKey + encodeMorton(x, y, z);
        return world->hasEntity(key);
    }

    // Get component values for specific voxel
    template<typename T>
    std::optional<T> getComponent(int x, int y, int z) const {
        uint64_t key = baseMortonKey + encodeMorton(x, y, z);
        return world->getComponentValue<T>(key);
    }
};
```

**Memory Comparison**:
```
Legacy BrickStorage: 8x8x8 voxels × 8-byte attributes = 4,096 bytes/brick
+ Occupancy mask: 512 bits = 64 bytes
+ Metadata: ~4,000 bytes
Total: ~70 KB per brick

EntityBrickView: 16 bytes per brick (world ptr + morton key)
Reduction: 94% memory savings
```

**Purpose**: Eliminate brick data duplication by viewing entities through Morton-indexed queries

**Key Insight**: Bricks don't need to store voxel data - they're spatial indices into the ECS world. Ray traversal queries components on-demand.

**Location**: `libraries/SVO/include/EntityBrickView.h`, `libraries/SVO/src/EntityBrickView.cpp`

---

### 22. Modern rebuild() Workflow Pattern (November 2025 - Phase H.2)
**Classes**: `LaineKarrasOctree`, `GaiaVoxelWorld`

**Implementation**:
```cpp
// Modern workflow replaces legacy VoxelInjector::inject()
// Step 1: Populate entity world
GaiaVoxelWorld world;
world.createVoxel(VoxelCreationRequest{
    position,
    {Density{1.0f}, Color{red}}  // Component pack
});

// Step 2: Build octree from entities (single call)
LaineKarrasOctree octree(world, maxLevels, brickDepth);
octree.rebuild(world, worldMin, worldMax);

// Step 3: Ray cast using entity-based SVO
RayHit hit = octree.castRay(origin, direction);
if (hit.hit) {
    auto color = world.getComponentValue<Color>(hit.entity);
}
```

**rebuild() Implementation**:
```cpp
void LaineKarrasOctree::rebuild(GaiaVoxelWorld& world,
                                 const glm::vec3& worldMin,
                                 const glm::vec3& worldMax) {
    // 1. Clear existing hierarchy
    blocks.clear();

    // 2. Calculate brick grid dimensions
    bricksPerAxis = calculateBricksPerAxis(worldMin, worldMax, brickDepth);

    // 3. Iterate Morton-ordered bricks
    for (uint64_t mortonKey = 0; mortonKey < totalBricks; ++mortonKey) {
        EntityBrickView brick(world, mortonKey, brickDepth);

        // 4. Skip empty bricks
        if (brick.isEmpty()) continue;

        // 5. Register occupied octants in parent descriptor
        registerBrickInHierarchy(mortonKey, brick);
    }

    // 6. Build parent descriptors bottom-up
    buildParentDescriptors();
}
```

**Why Legacy API Failed**:
- VoxelInjector::inject() + BrickStorage created malformed octree hierarchy
- Root cause: Legacy API populated bricks without proper ChildDescriptor linking
- Symptom: Infinite loop in castRay() (ADVANCE phase never found valid child)

**Purpose**: Clean separation - GaiaVoxelWorld owns data, LaineKarrasOctree owns spatial index

**Location**: `libraries/SVO/src/LaineKarrasOctree.cpp`

---

### 23. Component Macro System Pattern (November 2025 - Phase H.2)
**Classes**: `VoxelComponents.h`, component types (Density, Color, Normal, etc.)

**Implementation**:
```cpp
// Single source of truth for all voxel components
#define FOR_EACH_COMPONENT(MACRO) \
    MACRO(Density)                \
    MACRO(Color)                  \
    MACRO(Normal)                 \
    MACRO(Roughness)              \
    MACRO(Metallic)               \
    MACRO(Emissive)

// Auto-generate ComponentVariant
#define VARIANT_MEMBER(T) T,
using ComponentVariant = std::variant<
    FOR_EACH_COMPONENT(VARIANT_MEMBER)
    std::monostate
>;

// Auto-generate AllComponents tuple for compile-time iteration
#define TUPLE_MEMBER(T) T,
using AllComponents = std::tuple<FOR_EACH_COMPONENT(TUPLE_MEMBER) void*>;

// Auto-generate ComponentTraits
#define TRAITS_CASE(T) \
    template<> struct ComponentTraits<T> { \
        static constexpr const char* name = #T; \
        static constexpr size_t index = __COUNTER__ - ComponentBaseCounter; \
    };
FOR_EACH_COMPONENT(TRAITS_CASE)
```

**Adding New Component**:
```cpp
// Before: 3+ edits (variant, tuple, traits, each handler)
// After: 1 edit
#define FOR_EACH_COMPONENT(MACRO) \
    MACRO(Density)                \
    MACRO(Color)                  \
    MACRO(NewComponent)  // <-- Add here, everything else auto-generated
```

**Benefits**:
- Single edit point (eliminates 3+ location updates)
- Compile-time safety (impossible to have mismatched types)
- Zero duplication across variant/tuple/traits
- Automatic visitor pattern support

**Purpose**: Maintain type safety across voxel component system with minimal boilerplate

**Location**: `libraries/VoxelComponents/include/VoxelComponents.h`

---

### 24. Const-Correctness Pattern (November 16, 2025 - Phase H)
**Purpose**: Enforce const-correctness across node config slots, enable compiler optimizations

**Implementation**:
```cpp
// Node config slots use const references where appropriate
struct GraphicsPipelineNodeConfig {
    // Before: INPUT_SLOT(SHADER_DATA, ShaderDataBundle*, ...)
    // After: INPUT_SLOT(SHADER_DATA, const std::shared_ptr<ShaderDataBundle>&, ...)

    AUTO_INPUT(SHADER_DATA_BUNDLE,
               const std::shared_ptr<ShaderManagement::ShaderDataBundle>&,
               SlotNullability::Required,
               SlotRole::Dependency,
               SlotMutability::ReadOnly);

    // Vectors of primitives use const references
    AUTO_INPUT(IMAGE_AVAILABLE_SEMAPHORES,
               const std::vector<VkSemaphore>&,
               SlotNullability::Required,
               SlotRole::Execute,
               SlotMutability::ReadOnly);
}
```

**Enforcement Pattern**:
```cpp
// ResourceConfig.h - Slot type assertions
#define AUTO_INPUT(name, type, ...) \
    static_assert(!std::is_pointer_v<type> || std::is_const_v<std::remove_pointer_t<type>>, \
                  #name " pointer must be const"); \
    static_assert(!IsVector<type> || IsConstReference<type>, \
                  #name " vector must be const reference");
```

**Benefits**:
- Compiler optimizations (const enables caching, reordering)
- Intent clarity (ReadOnly slots are const, WriteOnly slots are mutable)
- Type safety (prevents accidental mutation of read-only resources)
- ABI compatibility (const references are ABI-stable)

**Migration Impact**:
- All node config headers updated (ComputePipelineNodeConfig, DescriptorSetNodeConfig, etc.)
- TypedConnection handles const reference unwrapping automatically
- Zero runtime overhead (const is compile-time only)

**Location**: All `RenderGraph/include/Data/Nodes/*Config.h` files (November 2025)

---

## Additional Pattern Updates (November 5, 2025)

### Generic Dispatcher Pattern
**Principle**: Separate pipeline creation from dispatch logic

**Example**:
- `GraphicsPipelineNode` creates → `GeometryRenderNode` dispatches
- `ComputePipelineNode` creates → `ComputeDispatchNode` dispatches

**Benefits**:
- Reusable dispatchers (one node handles all compute shaders)
- Data-driven shader selection (via graph wiring)
- Easy algorithm swapping (Phase L research comparisons)

---

### 12. Two-Semaphore Synchronization Details (Continued)

**Implementation (continued)**:
```cpp
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

### 10. Split SDI Architecture Pattern (October 31, 2025)
**Classes**: `SpirvInterfaceGenerator`, `ShaderBundleBuilder`, `ShaderDataBundle`

**Implementation**:
```cpp
// Generic interface file: {content-hash}-SDI.h
namespace ShaderInterface {
namespace 2071dff093caf4b3 {  // UUID from content hash

    // UBO struct definition from SPIRV reflection
    struct bufferVals {
        mat4 mvp;  // Offset: 0, extracted via ExtractBlockMembers()
    };

    // Descriptor binding metadata
    namespace Set0 {
        struct myBufferVals {
            static constexpr uint32_t SET = 0;
            static constexpr uint32_t BINDING = 0;
            static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_VERTEX_BIT;
            using DataType = bufferVals;  // Linked via structDefIndex
        };
    };
}
}

// Shader-specific names file: Draw_ShaderNames.h
namespace Draw_Shader {
    namespace SDI = ShaderInterface::2071dff093caf4b3;

    // Convenience constants
    using myBufferVals_t = SDI::Set0::myBufferVals;
    constexpr uint32_t myBufferVals_SET = myBufferVals_t::SET;
    constexpr uint32_t myBufferVals_BINDING = myBufferVals_t::BINDING;
}
```

**Recursive UBO Struct Extraction**:
```cpp
// ShaderManagement/src/SpirvReflector.cpp:177-220
void ExtractBlockMembers(
    const ::SpvReflectBlockVariable* blockVar,
    std::vector<SpirvStructMember>& outMembers
) {
    for (uint32_t i = 0; i < blockVar->member_count; ++i) {
        const auto& member = blockVar->members[i];
        SpirvStructMember spirvMember;

        // Matrix detection via stride checking
        if (member.numeric.matrix.stride > 0 && member.numeric.matrix.column_count > 0) {
            spirvMember.type.baseType = SpirvTypeInfo::BaseType::Matrix;
            spirvMember.type.columns = member.numeric.matrix.column_count;
            spirvMember.type.rows = member.numeric.matrix.row_count;
        }
        // Nested struct handling (recursive)
        else if (member.member_count > 0) {
            spirvMember.type.baseType = SpirvTypeInfo::BaseType::Struct;
            spirvMember.type.structName = member.type_description->type_name;
        }

        outMembers.push_back(spirvMember);
    }
}
```

**Index-Based Struct Linking**:
```cpp
// SpirvReflectionData.h - prevents dangling pointers
struct SpirvDescriptorBinding {
    int structDefIndex = -1;  // Index into SpirvReflectionData::structDefinitions
    // NOT: SpirvStructDefinition* structDef (invalidated on vector reallocation)
};

// SpirvInterfaceGenerator.cpp - safe access
if (binding.structDefIndex >= 0 &&
    binding.structDefIndex < static_cast<int>(data.structDefinitions.size())) {
    const auto& structDef = data.structDefinitions[binding.structDefIndex];
    code << "using DataType = " << structDef.name << ";\n";
}
```

**Content-Hash UUID System**:
```cpp
// ShaderBundleBuilder.cpp - deterministic UUID generation
std::string uuid = GenerateContentBasedUuid(preprocessedSource);
// Same source → same UUID → shared generic .si.h file
// Example: "2071dff093caf4b3" for specific interface layout
```

**Purpose**:
- Generic interface sharing across shaders with same layout
- Type-safe UBO updates via generated struct definitions
- Shader-specific convenience while maintaining interface reuse
- Deterministic UUID enables interface caching and sharing

**Key Insight**: Split architecture enables interface sharing (generic `.si.h`) while maintaining shader-specific convenience (`Names.h`). Content-hash UUID ensures determinism. Index-based linking prevents dangling pointer bugs during vector reallocation. Matrix detection requires checking stride in block variable, not type description.

**Directory Separation**:
- Build-time shaders: `generated/sdi/` (project level, version controlled)
- Runtime shaders: `binaries/generated/sdi/` (application-specific)

###11. Slot Task Pattern (November 2, 2025 - Phase F)
**Classes**: `NodeInstance`, `TypedNode<ConfigType>`, `SlotTask`, `ResourceBudgetManager`

**Implementation**:
```cpp
// Three-level granularity
struct SlotTask {
    size_t taskIndex;              // Task ID within node
    size_t parameterIndex;         // Index into InstanceLevel input array
    std::vector<Resource*> inputs; // Task-specific input resources
    std::vector<Resource*> outputs;// Task-specific output resources
    AllocationHandle budgetHandle; // Budget reservation for this task
};

// Three-tier lifecycle
class NodeInstance {
protected:
    std::vector<SlotTask> slotTasks_;

    // Node-level (once)
    virtual void SetupNode() {}
    virtual void CleanupNode() {}

    // Task-level (per configuration variant)
    virtual void CompileTask(size_t taskIdx) {}
    virtual void CleanupTask(size_t taskIdx) {}

    // Instance-level (per data item)
    virtual void ExecuteInstance(size_t taskIdx, size_t instanceIdx) {}

public:
    // FINAL lifecycle (orchestration)
    void Compile() final {
        for (auto& task : slotTasks_) {
            CompileTask(task.taskIndex);
        }
    }
};

// Declarative task generation
enum class SlotScope : uint8_t {
    NodeLevel,      // Shared across all tasks (VkDevice)
    TaskLevel,      // Per-task config (format, compression)
    InstanceLevel   // Parameterized array - drives task count
};

// Config with auto-indexing
struct ImageLoaderConfig : public ResourceConfigBase<2, 1> {
    AUTO_INPUT(DEVICE, VkDevice,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    AUTO_INPUT(IMAGE_PARAMS, std::vector<ImageLoadConfig>,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::InstanceLevel);  // Task count = array size

    AUTO_OUTPUT(IMAGE_VIEWS, VkImageView,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::WriteOnly);
};
```

**Purpose**: Enable single node to process multiple configuration variants with budget-based parallelism

**Key Features**:
1. **Virtual Node Specializations**: Slot tasks = independent config variants (albedo vs normal vs roughness)
2. **Three-Tier Lifecycle**: Node (shared) → Task (per-config) → Instance (per-data)
3. **Declarative Task Definition**: Task count = InstanceLevel input array size
4. **Task-Local Indexing**: Each task's instances write to 0..N, graph maps to global output
5. **Budget-Based Parallelism**: Hybrid allocation (reserve minimum at compile, scale dynamically at runtime)
6. **Auto-Indexing**: `__COUNTER__` embedded in `ResourceConfigBase` for automatic slot numbering

**Benefits**:
- Reduces graph complexity (10 texture loaders → 1 node with 10 tasks)
- Automatic parallel scaling based on device capabilities
- Type-safe slot metadata (SlotScope, SlotNullability, SlotMutability)
- Backwards compatible (single-task nodes work unchanged)

**Location**: `RenderGraph/include/Core/NodeInstance.h`, `RenderGraph/include/Core/ResourceConfig.h`

---

### 13. SlotRole Bitwise Flags Pattern (November 8, 2025 - Phase G)
**Classes**: `SlotRole` enum, descriptor binding infrastructure

**Implementation**:
```cpp
// Bitwise combinable flags for slot access semantics
enum class SlotRole : uint8_t {
    None         = 0u,
    Dependency   = 1u << 0,  // Accessed during Compile (creates dependency)
    Execute      = 1u << 1,  // Accessed during Execute (can combine with Dependency)
    CleanupOnly  = 1u << 2,  // Only accessed during Cleanup
    Output       = 1u << 3   // Output slot (role only applies to inputs)
};

// Helper functions for clean role checks
inline bool HasDependency(SlotRole role) {
    return (static_cast<uint8_t>(role) & static_cast<uint8_t>(SlotRole::Dependency)) != 0;
}

inline bool HasExecute(SlotRole role) {
    return (static_cast<uint8_t>(role) & static_cast<uint8_t>(SlotRole::Execute)) != 0;
}

inline bool IsDependencyOnly(SlotRole role) {
    return role == SlotRole::Dependency;
}

inline bool IsExecuteOnly(SlotRole role) {
    return role == SlotRole::Execute;
}

// Combined role example: descriptor used in both Compile and Execute
SlotRole descriptorRole = SlotRole::Dependency | SlotRole::Execute;
if (HasDependency(descriptorRole)) {
    // Bind descriptor in Compile phase
}
if (HasExecute(descriptorRole)) {
    // Re-bind descriptor in Execute phase (transient resources)
}
```

**Purpose**: Flexible descriptor binding semantics - enables slots used in multiple lifecycle phases

**Key Insight**: Bitwise flags enable combined roles (Dependency | Execute) for descriptors that need compile-time dependency tracking AND runtime rebinding (e.g., per-frame resources).

**Location**: `RenderGraph/include/Data/Core/ResourceConfig.h`

---

### 14. Deferred Descriptor Binding Pattern (November 8, 2025 - Phase G)
**Classes**: `DescriptorSetNode`, `TypedConnection` (PostCompile hooks)

**Implementation**:
```cpp
// DescriptorSetNode splits descriptor binding across lifecycle phases
class DescriptorSetNode {
    void CompileImpl(Context& ctx) override {
        // 1. Create descriptor resources
        CreateDescriptorSetLayout();
        CreateDescriptorPool();
        AllocateDescriptorSets();

        // 2. Bind Dependency-role descriptors (static resources)
        PopulateDependencyDescriptors();

        // 3. Mark node for initial Execute binding
        SetFlag(NodeFlags::NeedsInitialBind);
    }

    void ExecuteImpl(Context& ctx) override {
        // 4. Bind Execute-role descriptors (transient/per-frame resources)
        if (HasFlag(NodeFlags::NeedsInitialBind)) {
            PopulateExecuteDescriptors();
            ClearFlag(NodeFlags::NeedsInitialBind);
        }
    }
};

// PostCompile hooks enable resource population before binding
// TypedConnection.h invokes PostCompile after Compile completes
void TypedConnection::PostCompileImpl() {
    // Node populates output resources here
    // DescriptorSetNode can now safely bind descriptors to these resources
}
```

**Purpose**: Clean separation - Compile creates resources, Execute binds them. Enables PostCompile hooks to populate resources before descriptor binding.

**Key Features**:
1. **Dependency descriptors**: Bound once in Compile (static resources like textures, samplers)
2. **Execute descriptors**: Bound every Execute (transient resources like per-frame UBOs)
3. **PostCompile hooks**: Populate outputs after Compile, before Execute
4. **Per-frame descriptor sets**: Allocated once, bound every frame

**Benefits**:
- Zero validation errors (resources exist before binding)
- Generalized architecture (no hardcoded UBO/MVP logic)
- Supports dynamic resource updates (swapchain recreation)

**Location**: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`, `RenderGraph/include/Core/TypedConnection.h`

---

### 15. NodeFlags Enum Pattern (November 8, 2025 - Phase G)
**Classes**: `DescriptorSetNode` (example implementation)

**Implementation**:
```cpp
// Node state flags (bitwise combinable)
enum class NodeFlags : uint8_t {
    None                = 0,
    Paused              = 1 << 0,  // Rendering paused (swapchain recreation)
    NeedsInitialBind    = 1 << 1,  // Needs to bind Dependency descriptors on first Execute
    // Add more flags as needed
};

// State helper functions (inline for zero overhead)
inline bool HasFlag(NodeFlags flag) const {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

inline void SetFlag(NodeFlags flag) {
    flags = static_cast<NodeFlags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(flag));
}

inline void ClearFlag(NodeFlags flag) {
    flags = static_cast<NodeFlags>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(flag));
}

// Semantic accessors built on flags
inline bool IsPaused() const { return HasFlag(NodeFlags::Paused); }
inline bool IsRendering() const { return !IsPaused(); }

// Single consolidated member variable
NodeFlags flags = NodeFlags::None;
```

**Purpose**: Consolidated state management - replaces scattered bool member variables with single bitwise flag field

**Benefits**:
- Memory efficient (1 byte vs N bools)
- Cache-friendly (single memory location)
- Easy to extend (add new flags without member variables)
- Clear semantics (flag names are self-documenting)

**Example Usage**:
```cpp
// Before: bool isPaused; bool needsInitialBind; bool isDirty; (3+ bytes)
// After: NodeFlags flags; (1 byte for 8 flags)

SetFlag(NodeFlags::Paused);  // Pause rendering
if (IsRendering()) {
    // Only execute when not paused
}
```

**Location**: `RenderGraph/include/Nodes/DescriptorSetNode.h` (lines 108-126)

---

### 16. Modularized CompileImpl Pattern (November 8, 2025 - Phase G)
**Classes**: `DescriptorSetNode` (refactoring example)

**Implementation**:
```cpp
// Before: Monolithic CompileImpl (~230 lines)
void CompileImpl(Context& ctx) override {
    // 200+ lines of descriptor layout creation, pool creation,
    // descriptor set allocation, binding, UBO logic, etc.
}

// After: Focused helper methods (~80 lines in CompileImpl)
void CompileImpl(Context& ctx) override {
    CreateDescriptorSetLayout();       // ~20 lines
    CreateDescriptorPool();            // ~15 lines
    AllocateDescriptorSets();          // ~10 lines
    PopulateDependencyDescriptors();   // ~20 lines
    SetFlag(NodeFlags::NeedsInitialBind);
}

// Private helper methods (single responsibility)
void CreateDescriptorSetLayout() {
    // Only handles VkDescriptorSetLayout creation
}

void CreateDescriptorPool() {
    // Only handles VkDescriptorPool creation with size calculation
}

void AllocateDescriptorSets() {
    // Only handles descriptor set allocation from pool
}

void PopulateDependencyDescriptors() {
    // Only handles binding Dependency-role descriptors
}

void PopulateExecuteDescriptors() {
    // Only handles binding Execute-role descriptors (called from Execute)
}
```

**Purpose**: Decompose monolithic lifecycle methods into focused, single-responsibility helper functions

**Benefits**:
- Improved readability (method names document intent)
- Easier testing (test individual helpers)
- Reduced cognitive load (each helper <20 lines)
- Better error isolation (stack trace shows which helper failed)
- Adheres to cpp-programming-guidelines.md (<20 instructions per function)

**Key Insight**: Extract each conceptual step into a private helper method. CompileImpl becomes high-level orchestration, helpers contain implementation details.

**Location**: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`

---

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

---

## Voxel Infrastructure Patterns (November 26, 2025 - Phase H.2)

### 19. Entity-Based Voxel Storage Pattern
**Classes**: `GaiaVoxelWorld`, `VoxelComponents`, `MortonKey`

**Implementation**:
```cpp
// Create voxel world and populate
GaiaVoxelWorld world;
ComponentQueryRequest components[] = {
    Density{1.0f},
    Color{glm::vec3(1, 0, 0)}
};
world.createVoxel(VoxelCreationRequest{glm::vec3(2, 2, 2), components});

// Morton key encodes 3D position in single uint64
MortonKey key = MortonKey::fromPosition(glm::ivec3(x, y, z));
```

**Purpose**: Sparse voxel storage with O(1) position lookup via Morton codes

**Location**: `libraries/GaiaVoxelWorld/`, `libraries/VoxelComponents/`

---

### 20. EntityBrickView Zero-Storage Pattern
**Classes**: `EntityBrickView`, `LaineKarrasOctree`

**Implementation**:
```cpp
// Zero-storage brick view - queries entities on demand
EntityBrickView brickView(world, localGridOrigin, depth, worldMin, EntityBrickView::LocalSpace);

// View stores only: world ref + grid origin + depth = 16 bytes
// vs legacy: 512 voxels × 140 bytes = 70 KB per brick (94% reduction)

// Query voxel data on-demand
if (brickView.hasVoxel(x, y, z)) {
    auto entity = brickView.getVoxel(x, y, z);
    auto color = world.getComponentValue<Color>(entity);
}
```

**Purpose**: Eliminate data duplication - SVO stores spatial index, ECS owns data

**Location**: `libraries/GaiaVoxelWorld/include/EntityBrickView.h`

---

### 21. ESVO Ray Casting Pattern
**Classes**: `LaineKarrasOctree`, `ESVOTraversalState`

**Implementation**:
```cpp
// Build octree from entities
LaineKarrasOctree octree(world, nullptr, maxLevels, brickDepth);
octree.rebuild(world, worldMin, worldMax);

// Cast ray
auto hit = octree.castRay(origin, direction, tMin, tMax);
if (hit.hit) {
    glm::vec3 hitPoint = hit.hitPoint;
    auto entity = hit.entity;
}
```

**Key Algorithm Components**:
- **Mirrored Space**: `octant_mask` XOR convention for ray direction handling
- **Parametric Plane Traversal**: tx_coef, ty_coef, tz_coef for efficient octant selection
- **Brick DDA**: 3D DDA within leaf bricks (Amanatides & Woo 1987)

**Coordinate Conversion**:
```cpp
// Mirrored ↔ Local conversion
int localOctant = mirroredIdx ^ ((~octant_mask) & 7);
```

**Location**: `libraries/SVO/src/LaineKarrasOctree.cpp`

---

### 22. Partial Block Update Pattern
**Classes**: `LaineKarrasOctree`

**Implementation**:
```cpp
// Thread-safe partial updates
octree.updateBlock(blockWorldMin, depth);  // Add or update brick
octree.removeBlock(blockWorldMin, depth);  // Remove brick

// Concurrency control for rendering
octree.lockForRendering();
// ... ray casting (GPU or CPU) ...
octree.unlockAfterRendering();
```

**Internal Details**:
- Uses `std::shared_mutex` for reader-writer locking
- Brick lookup via hash map: `brickGridToBrickView[gridKey]`
- Placement new for brick view updates (no default constructor)

**Location**: `libraries/SVO/src/LaineKarrasOctree.cpp:2443-2552`

---

### 23. GLSL Shader Sync Pattern
**Files**: `VoxelRayMarch.comp`, `OctreeTraversal-ESVO.glsl`

**Key Functions (mirrored C++ ↔ GLSL)**:
```glsl
// Coordinate space conversion
int mirrorMask(int mask, int octant_mask);
int mirroredToLocalOctant(int mirroredIdx, int octant_mask);

// Child descriptor navigation
uint countChildrenBefore(uint childMask, int childIndex);

// FP precision fix for rays starting inside voxels
float t = max(max(brickT.x, startT), 0.0);
```

**Purpose**: Keep CPU (C++) and GPU (GLSL) implementations in sync

**Location**: `shaders/VoxelRayMarch.comp`, `shaders/OctreeTraversal-ESVO.glsl`
