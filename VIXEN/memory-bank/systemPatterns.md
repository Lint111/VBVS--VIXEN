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
