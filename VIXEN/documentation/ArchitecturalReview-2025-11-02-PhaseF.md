# Architectural Review: Phase F - Slot Task System & Array Processing

**Date**: November 2, 2025
**Phase**: F (Array Processing)
**Status**: Planning Complete, Ready for Implementation
**Estimated Effort**: 16-21 hours

---

## Executive Summary

Phase F introduces **slot task system** for array processing with budget-based parallelism. This enables a single node to process multiple configuration variants (virtual nodes) with automatic parallel instance spawning based on resource budget and device capabilities.

### Key Innovation

**Slot Tasks = Virtual Node Specializations**

Instead of creating separate nodes for similar but parameterized workloads (e.g., AlbedoLoaderNode, NormalLoaderNode, RoughnessLoaderNode), a single node can have multiple **slot tasks**, each representing a configuration variant:

```cpp
// Single ImageLoaderNode with 3 slot tasks
Task 0: Load albedo maps  (sRGB, BC1 compression, gamma correction)
Task 1: Load normal maps  (Linear, BC5 compression, no gamma)
Task 2: Load roughness    (Linear, BC4, single channel)

// Each task can spawn parallel instances
Task 1: 4 normal maps → 4 parallel instances (multi-threaded loading)
```

---

## Core Concepts

### 1. Three-Level Granularity

```
Node (ImageLoaderNode)
├── Slot Task 0 (Albedo configuration)
│   ├── Instance 0 → Load brick_albedo.png
│   └── Instance 1 → Load wood_albedo.png
├── Slot Task 1 (Normal configuration)
│   ├── Instance 0 → Load brick_normal.png (parallel thread)
│   ├── Instance 1 → Load wood_normal.png (parallel thread)
│   ├── Instance 2 → Load metal_normal.png (parallel thread)
│   └── Instance 3 → Load stone_normal.png (parallel thread)
└── Slot Task 2 (Roughness configuration)
    └── Instance 0 → Load brick_rough.png
```

**Three Levels**:
1. **Node Level**: Shared resources (device, staging buffers, pools)
2. **Task Level**: Per-configuration resources (samplers, pipelines, descriptors)
3. **Instance Level**: Per-data-item work (individual image loading)

---

### 2. Slot Input Scope

```cpp
enum class SlotScope : uint8_t {
    NodeLevel,      // Shared across all tasks (VkDevice, command pool)
    TaskLevel,      // Per-task config (format, compression mode)
    InstanceLevel   // Parameterized input array[i] for instance i
};
```

**InstanceLevel slots** drive task count:
- Number of slot tasks = size of InstanceLevel input array
- Each element becomes configuration for one task

---

### 3. Slot Metadata Consolidation

All slot properties declared in config (not scattered across call sites):

```cpp
template<
    typename T,
    uint32_t Idx,
    SlotNullability Nullability = SlotNullability::Required,
    SlotRole Role = SlotRole::Dependency,
    SlotMutability Mutability = SlotMutability::ReadOnly,
    SlotScope Scope = SlotScope::NodeLevel
>
struct ResourceSlot { ... };
```

**Four New Enums**:
1. **SlotNullability**: Required vs Optional (replaces opaque `bool nullable`)
2. **SlotRole**: Dependency, ExecuteOnly, CleanupOnly (moved from call-site to config)
3. **SlotMutability**: ReadOnly, WriteOnly, ReadWrite (for parallel safety)
4. **SlotScope**: NodeLevel, TaskLevel, InstanceLevel (drives resource allocation)

---

### 4. Auto-Indexing Macros

Embedded counter bases in `ResourceConfigBase` enable automatic slot indexing:

```cpp
struct ImageLoaderConfig : public ResourceConfigBase<3, 1> {
    // AUTO_INPUT uses __COUNTER__ for automatic indexing
    AUTO_INPUT(DEVICE, VkDevice,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);  // Index 0

    AUTO_INPUT(IMAGE_PARAMS, std::vector<ImageLoadConfig>,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::InstanceLevel);  // Index 1 (PARAMETERIZED)

    AUTO_OUTPUT(IMAGE_VIEWS, VkImageView,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::WriteOnly);  // Index 0
};
```

**Benefits**:
- No manual index tracking
- No index collision bugs
- Per-config isolation (each config file resets to 0)

---

### 5. Budget-Based Parallelism

**Resource Budget Manager** tracks device capabilities and allocates resources safely:

```cpp
struct ResourceRequirement {
    size_t memoryPerInstance;           // Bytes per parallel instance
    uint32_t descriptorsPerInstance;    // Descriptor sets needed
    uint32_t commandBuffersPerInstance; // Command buffers needed
    bool supportsCPUParallelism;        // Can use multi-threading?
};

class ResourceBudgetManager {
public:
    // Static reservation (compile-time guarantee)
    AllocationHandle ReserveMinimum(const ResourceRequirement& req);

    // Dynamic query (per-frame opportunistic scaling)
    uint32_t GetAvailableParallelism(const ResourceRequirement& unitCost) const;

    void Release(AllocationHandle handle);
};
```

**Hybrid Strategy**:
1. **Compile time**: Reserve minimum budget (guaranteed baseline)
2. **Execute time**: Query available budget (opportunistic scaling)

Example:
- Task requires 4MB per instance
- Reserve minimum for 2 instances (8MB guaranteed)
- At runtime: If 128MB available → scale to 32 instances
- At runtime: If only 12MB available → throttle to 3 instances

---

### 6. Three-Tier Lifecycle

```cpp
class NodeInstance {
protected:
    // Node-level: Shared resources (called once)
    virtual void SetupNode() {}
    virtual void CleanupNode() {}

    // Task-level: Per-configuration resources (called per task)
    virtual void CompileTask(size_t taskIdx) {}
    virtual void CleanupTask(size_t taskIdx) {}

    // Instance-level: Per-data-item work (called per instance)
    virtual void ExecuteInstance(size_t taskIdx, size_t instanceIdx) {}

public:
    // FINAL lifecycle methods (loop over tasks)
    void Compile() final;
    void Execute() final;
    void Cleanup() final;
};
```

**Benefits**:
- Clear separation of concerns
- Node authors override only what they need
- Graph orchestrates lifecycle automatically

---

## Design Decisions

### Decision 1: Declarative Task Definition

**Chosen**: Auto-detect task count from InstanceLevel input array size

**Rationale**: Simplest for 90% of use cases. Complex cases can use struct-of-arrays pattern.

**Example**:
```cpp
// Input: vector of 100 ImageLoadConfig structs
// → Automatically creates 100 slot tasks
auto params = In(IMAGE_PARAMS);  // size() == 100
// Graph calls CompileTask(0..99) automatically
```

---

### Decision 2: Task-Local Output Indexing

**Chosen**: Each task's instances write to indices 0..N within their task

**Rationale**: Simpler for node authors. Graph handles global mapping automatically.

**Implementation**:
```cpp
// Node code uses task-local indices
void ExecuteInstance(size_t taskIdx, size_t instanceIdx) {
    VkImageView view = LoadImage(...);
    OutLocal(IMAGE_VIEWS, taskIdx, instanceIdx) = view;  // Local index
    // Graph maps to global output array internally
}
```

---

### Decision 3: Node-Decided Parallelism

**Chosen**: Per-task flag for CPU threads vs Vulkan parallelism

**Rationale**: Workload-dependent. File I/O benefits from threads, GPU work doesn't.

**Implementation**:
```cpp
struct ImageLoaderConfig {
    static constexpr bool SUPPORTS_CPU_PARALLELISM = true;  // File I/O
};

struct GeometryRenderConfig {
    static constexpr bool SUPPORTS_CPU_PARALLELISM = false;  // GPU-bound
};
```

---

### Decision 4: Hybrid Budget Allocation

**Chosen**: Reserve minimum at compile, scale dynamically at runtime

**Rationale**: Balances predictability with resource efficiency.

**Flow**:
```cpp
// Compile time
void CompileTask(size_t taskIdx) {
    ResourceRequirement req = GetTaskRequirement();
    budgetHandle = budgetManager.ReserveMinimum(req);  // Guaranteed 2 instances
}

// Execute time
void ExecuteInstance(size_t taskIdx, size_t instanceIdx) {
    uint32_t available = budgetManager.GetAvailableParallelism(req);
    if (instanceIdx >= available) {
        DeferToNextFrame(taskIdx, instanceIdx);  // Budget exhausted
        return;
    }
    // Proceed with work
}
```

---

## Implementation Phases

### F.0: Slot Metadata Consolidation (2-3h)

**Deliverables**:
1. `SlotScope`, `SlotNullability`, `SlotMutability` enums
2. Updated `ResourceSlot` template with all metadata
3. Embedded counter bases in `ResourceConfigBase`
4. `AUTO_INPUT`/`AUTO_OUTPUT` macros
5. Updated `In()`/`Out()` to auto-apply metadata

**Files**:
- `RenderGraph/include/Core/ResourceConfig.h`
- `RenderGraph/include/Core/TypedNodeInstance.h`

---

### F.1: Resource Budget Manager (3-4h)

**Deliverables**:
1. `ResourceRequirement` struct
2. `ResourceBudgetManager` class
3. Device capability tracking (`VkPhysicalDeviceLimits`)
4. Static reservation + dynamic query API
5. Integration into RenderGraph

**Files**:
- `RenderGraph/include/Core/ResourceBudget.h` (NEW)
- `RenderGraph/src/Core/ResourceBudget.cpp` (NEW)
- `RenderGraph/include/Core/RenderGraph.h`

---

### F.2: Slot Task Infrastructure (5-6h)

**Deliverables**:
1. `SlotTask` struct
2. `AutoGenerateSlotTasks()` method
3. Three-tier lifecycle hooks
4. `OutLocal()` helper for task-local indexing
5. Final lifecycle orchestration in base class

**Files**:
- `RenderGraph/include/Core/NodeInstance.h`
- `RenderGraph/include/Core/TypedNodeInstance.h`
- `RenderGraph/src/Core/RenderGraph.cpp`

---

### F.3: Budget-Based Parallelism (4-5h)

**Deliverables**:
1. `GetTaskRequirement()` virtual method
2. Hybrid budget allocation implementation
3. Thread pool for CPU-parallel tasks
4. Vulkan batch submission for GPU tasks
5. `GetAvailableParallelism()` helper

**Files**:
- `RenderGraph/include/Core/NodeInstance.h`
- `RenderGraph/src/Core/NodeInstance.cpp`
- `RenderGraph/src/Core/RenderGraph.cpp`

---

### F.4: InstanceGroup Migration (2-3h)

**Deliverables**:
1. Deprecate `InstanceGroup` class
2. Migration guide documentation
3. Update existing multi-instance nodes
4. Optional compatibility shim

**Files**:
- `RenderGraph/include/Core/InstanceGroup.h`
- `documentation/SlotTaskMigrationGuide.md` (NEW)

---

## Example Usage

### Before (Manual Node Creation)

```cpp
// Need 3 separate nodes for 3 texture types
auto albedoNode = graph.AddNode("TextureLoader", "AlbedoLoader");
auto normalNode = graph.AddNode("TextureLoader", "NormalLoader");
auto roughNode = graph.AddNode("TextureLoader", "RoughnessLoader");

// Wire each separately
graph.ConnectNodes(device, 0, albedoNode, 0);
graph.ConnectNodes(device, 0, normalNode, 0);
graph.ConnectNodes(device, 0, roughNode, 0);
```

### After (Slot Tasks)

```cpp
// Single node with 3 slot tasks
auto texLoader = graph.AddNode("TextureLoader", "AllTextures");

// Input parameterized array
std::vector<ImageLoadConfig> configs = {
    {"brick_albedo.png", VK_FORMAT_R8G8B8A8_SRGB, true},
    {"brick_normal.png", VK_FORMAT_R8G8B8A8_UNORM, false},
    {"brick_rough.png", VK_FORMAT_R8_UNORM, false}
};
graph.SetNodeInput(texLoader, IMAGE_PARAMS, configs);

// Automatically creates 3 slot tasks with parallel instances
```

---

## Benefits

### For Node Authors
1. **Less boilerplate**: Single node handles multiple variants
2. **Auto-parallelism**: Budget system handles scaling
3. **Type safety**: Compile-time slot validation
4. **Clear lifecycle**: Three-tier structure is explicit

### For Graph Users
1. **Fewer nodes**: 10 texture loaders → 1 node with 10 tasks
2. **Better performance**: Automatic parallel scaling
3. **Resource safety**: Budget prevents exhaustion
4. **Cleaner graphs**: Less visual clutter

### For Engine
1. **Better scheduling**: Task-level dependencies enable finer DAG
2. **Resource pooling**: Shared node-level resources
3. **Future-proof**: Enables Phase D wave-based execution
4. **Migration path**: InstanceGroup → Slot Tasks

---

## Risks & Mitigations

### Risk 1: Complexity Increase

**Mitigation**: Backwards compatible. Existing single-task nodes work unchanged.

### Risk 2: Budget Allocation Tuning

**Mitigation**: Conservative defaults. Nodes can override `GetTaskRequirement()` for custom budgets.

### Risk 3: Threading Bugs

**Mitigation**: SlotMutability flags catch read-write conflicts at compile time.

---

## Success Criteria

1. ✅ Single ImageLoaderNode handles 3+ texture types with different configs
2. ✅ Parallel instance spawning verified (4+ threads loading in parallel)
3. ✅ Budget manager prevents resource exhaustion (tested with 1000+ tasks)
4. ✅ Zero regressions in existing single-task nodes
5. ✅ Migration guide enables InstanceGroup → Slot Task conversion

---

## Next Steps

1. Implement F.0 (Slot Metadata Consolidation)
2. Implement F.1 (Resource Budget Manager)
3. Implement F.2 (Slot Task Infrastructure)
4. Implement F.3 (Budget-Based Parallelism)
5. Implement F.4 (InstanceGroup Migration)
6. Update Memory Bank with Phase F completion
