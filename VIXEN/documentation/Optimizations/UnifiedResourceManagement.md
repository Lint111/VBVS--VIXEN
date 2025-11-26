# Unified Resource Management Architecture

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Design Proposal
**Priority**: Critical (Architecture Unification)

---

## Problem Statement

VIXEN currently has **four separate resource management systems** operating independently:

```
Current State (FRAGMENTED):

┌─────────────────────────┐     ┌─────────────────────────┐
│  ResourceManagement/    │     │  RenderGraph/           │
│  RM.h                   │     │  PerFrameResources      │
│                         │     │                         │
│  - State tracking       │     │  - Ring buffer pattern  │
│  - Metadata             │     │  - UBOs, descriptors    │
│  - Generation           │     │  - Command buffers      │
└─────────────────────────┘     └─────────────────────────┘

┌─────────────────────────┐     ┌─────────────────────────┐
│  RenderGraph/           │     │  ResourceManagement/    │
│  ResourceBudgetManager  │     │  StackAllocatedRM       │
│                         │     │                         │
│  - Host memory budget   │     │  - Stack allocation     │
│  - Device memory budget │     │  - Stack tracking       │
│  - Allocation tracking  │     │  - RM integration       │
└─────────────────────────┘     └─────────────────────────┘
```

**Issues**:
- ❌ Duplicated functionality (state tracking in multiple places)
- ❌ No unified budget tracking (stack, heap, device separate)
- ❌ Inconsistent APIs across systems
- ❌ Memory allocations not tracked holistically
- ❌ No single source of truth for resource lifecycle

---

## Proposed Solution: Unified Resource Management System (URMS)

```
Unified Architecture:

┌────────────────────────────────────────────────────────────────┐
│                   UNIFIED RESOURCE MANAGER                      │
├────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │              Core: UnifiedRM<T>                           │ │
│  │  (Replaces RM.h + StackAllocatedRM)                      │ │
│  │                                                            │ │
│  │  - State management (Ready/Outdated/Locked)              │ │
│  │  - Metadata tracking                                      │ │
│  │  - Generation counter                                     │ │
│  │  - Allocation strategy (stack/heap/device)               │ │
│  │  - Budget integration                                     │ │
│  └──────────────────────────────────────────────────────────┘ │
│                            │                                    │
│           ┌────────────────┼────────────────┐                  │
│           │                │                │                  │
│           ▼                ▼                ▼                  │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ StackPool   │  │  HeapPool    │  │ DevicePool   │         │
│  │             │  │              │  │              │         │
│  │ - Fixed     │  │ - Dynamic    │  │ - GPU VRAM   │         │
│  │ - Fast      │  │ - Flexible   │  │ - Buffers    │         │
│  │ - Tracked   │  │ - Tracked    │  │ - Images     │         │
│  └─────────────┘  └──────────────┘  └──────────────┘         │
│           │                │                │                  │
│           └────────────────┼────────────────┘                  │
│                            │                                    │
│                            ▼                                    │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │           UnifiedBudgetManager                            │ │
│  │  (Replaces ResourceBudgetManager)                        │ │
│  │                                                            │ │
│  │  - Unified tracking: stack + heap + device               │ │
│  │  - Per-type budgets                                       │ │
│  │  - Warning/critical thresholds                            │ │
│  │  - Automatic enforcement                                  │ │
│  └──────────────────────────────────────────────────────────┘ │
│                            │                                    │
│                            ▼                                    │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │           PerFrameResourceManager                         │ │
│  │  (Replaces PerFrameResources)                            │ │
│  │                                                            │ │
│  │  - Ring buffer pattern using UnifiedRM                    │ │
│  │  - Automatic budget tracking                              │ │
│  │  - Frame-in-flight synchronization                        │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                  │
└────────────────────────────────────────────────────────────────┘
```

---

## Component Design

### 1. UnifiedRM<T> - Core Resource Wrapper

**Purpose**: Single resource wrapper combining all functionality

**Features**:
- State management (from original RM.h)
- Metadata tracking (from original RM.h)
- Generation counter (from original RM.h)
- **NEW**: Allocation strategy selection (stack/heap/device)
- **NEW**: Automatic budget tracking
- **NEW**: Memory location tracking

**API**:
```cpp
template<typename T>
class UnifiedRM {
public:
    // Allocation strategies
    enum class AllocStrategy {
        Stack,      // Fixed-size stack allocation (StackAllocatedRM)
        Heap,       // Dynamic heap allocation (std::vector, std::unique_ptr)
        Device,     // GPU device memory (VkBuffer, VkImage)
        Automatic   // Let system decide based on size/lifetime
    };

    // Location tracking
    enum class MemoryLocation {
        HostStack,    // CPU stack
        HostHeap,     // CPU heap (malloc/new)
        DeviceLocal,  // GPU VRAM
        HostVisible   // GPU-visible host memory (mapped)
    };

    // Construction with strategy hint
    explicit UnifiedRM(
        AllocStrategy strategy = AllocStrategy::Automatic,
        std::string_view debugName = "unnamed"
    );

    // === From original RM.h ===
    bool Ready() const;
    T& Value();
    const T& Value() const;
    void Set(T value);
    void Reset();

    // State management
    bool Has(ResourceState checkState) const;
    ResourceState GetState() const;
    void SetState(ResourceState newState);
    void MarkOutdated();
    void MarkReady();
    void Lock();
    void Unlock();

    // Generation tracking
    uint64_t GetGeneration() const;

    // Metadata
    template<typename MetaType>
    void SetMetadata(const std::string& key, MetaType value);
    template<typename MetaType>
    MetaType GetMetadata(const std::string& key) const;

    // === NEW: Allocation tracking ===
    AllocStrategy GetAllocStrategy() const;
    MemoryLocation GetMemoryLocation() const;
    size_t GetAllocatedBytes() const;

    // Automatic budget registration
    void RegisterWithBudget(UnifiedBudgetManager* budgetMgr);

    // === NEW: Device memory specifics ===
    VkDeviceMemory GetDeviceMemory() const;  // If DeviceLocal
    void* GetMappedPointer() const;          // If HostVisible

private:
    std::optional<T> storage_;
    ResourceState state_;
    uint64_t generation_;
    std::unordered_map<std::string, std::any> metadata_;

    // NEW: Allocation tracking
    AllocStrategy allocStrategy_;
    MemoryLocation memoryLocation_;
    size_t allocatedBytes_;
    std::string_view debugName_;
    UnifiedBudgetManager* budgetManager_ = nullptr;
};
```

---

### 2. UnifiedBudgetManager - Holistic Budget Tracking

**Purpose**: Track ALL memory allocations (stack + heap + device)

**Improvements over ResourceBudgetManager**:
- ✅ Tracks stack allocations (from StackTracker)
- ✅ Tracks heap allocations (from ResourceBudgetManager)
- ✅ Tracks device allocations (from ResourceBudgetManager)
- ✅ Unified reporting and thresholds
- ✅ Automatic registration via UnifiedRM

**API**:
```cpp
class UnifiedBudgetManager {
public:
    // Budget types (extended from ResourceBudgetManager)
    enum class BudgetType {
        HostStack,        // NEW: Stack allocations
        HostHeap,         // System RAM (heap)
        DeviceLocal,      // GPU VRAM
        HostVisible,      // Mapped memory
        Descriptors,      // Descriptor sets
        CommandBuffers,   // Command buffers
        Custom            // User-defined
    };

    // Budget configuration (same as before)
    void SetBudget(BudgetType type, const ResourceBudget& budget);
    ResourceBudget GetBudget(BudgetType type) const;

    // Allocation tracking (enhanced)
    bool TryAllocate(BudgetType type, size_t bytes, std::string_view debugName);
    void RecordAllocation(BudgetType type, size_t bytes, std::string_view debugName);
    void RecordDeallocation(BudgetType type, size_t bytes);

    // Usage queries
    BudgetResourceUsage GetUsage(BudgetType type) const;
    BudgetResourceUsage GetTotalUsage() const;  // NEW: All types combined

    // Warnings and enforcement
    bool IsOverBudget(BudgetType type) const;
    bool IsNearWarningThreshold(BudgetType type) const;

    // NEW: Per-frame tracking (integrates StackTracker functionality)
    void BeginFrame();
    void EndFrame();
    BudgetResourceUsage GetFrameUsage() const;  // Stack allocations this frame

    // NEW: Unified reporting
    void PrintReport() const;  // Combines StackTracker + ResourceBudgetManager output

    // NEW: Resource registration (automatic via UnifiedRM)
    void RegisterResource(UnifiedRM_Base* resource);
    void UnregisterResource(UnifiedRM_Base* resource);

private:
    // Per-type budgets
    std::unordered_map<BudgetType, ResourceBudget> budgets_;
    std::unordered_map<BudgetType, BudgetResourceUsage> usage_;

    // NEW: Per-frame stack tracking
    BudgetResourceUsage frameStackUsage_;

    // NEW: Registered resources for automatic tracking
    std::unordered_set<UnifiedRM_Base*> registeredResources_;

    // NEW: Detailed allocation log (debug only)
    #ifndef NDEBUG
    struct AllocationRecord {
        BudgetType type;
        size_t bytes;
        std::string_view debugName;
        uint64_t timestamp;
    };
    std::vector<AllocationRecord> allocationLog_;
    #endif
};
```

---

### 3. PerFrameResourceManager - Unified Per-Frame Pattern

**Purpose**: Replace PerFrameResources with UnifiedRM-based implementation

**Improvements**:
- ✅ Uses UnifiedRM for state tracking
- ✅ Automatic budget integration
- ✅ Cleaner API (no separate Get/Set for each resource type)
- ✅ Generic resource storage

**API**:
```cpp
class PerFrameResourceManager {
public:
    /**
     * @brief Per-frame resource bundle using UnifiedRM
     */
    template<typename T>
    struct PerFrameResource {
        std::array<UnifiedRM<T>, MAX_FRAMES_IN_FLIGHT> frames;

        UnifiedRM<T>& Get(uint32_t frameIndex) {
            return frames[frameIndex];
        }

        const UnifiedRM<T>& Get(uint32_t frameIndex) const {
            return frames[frameIndex];
        }
    };

    // Construction
    explicit PerFrameResourceManager(
        VulkanDevice* device,
        UnifiedBudgetManager* budgetMgr,
        uint32_t frameCount = MAX_FRAMES_IN_FLIGHT
    );

    // Generic resource management
    template<typename T>
    PerFrameResource<T>* CreateResource(
        std::string_view resourceName,
        UnifiedRM<T>::AllocStrategy strategy = UnifiedRM<T>::AllocStrategy::Automatic
    );

    template<typename T>
    PerFrameResource<T>* GetResource(std::string_view resourceName);

    // Convenience methods for common resources
    PerFrameResource<VkBuffer>* CreateUniformBuffers(
        std::string_view name,
        VkDeviceSize bufferSize
    );

    PerFrameResource<VkDescriptorSet>* CreateDescriptorSets(
        std::string_view name
    );

    PerFrameResource<VkCommandBuffer>* CreateCommandBuffers(
        std::string_view name
    );

    // Frame synchronization
    uint32_t GetCurrentFrame() const { return currentFrame_; }
    void AdvanceFrame() { currentFrame_ = (currentFrame_ + 1) % frameCount_; }

    // Cleanup
    void Cleanup();

private:
    VulkanDevice* device_;
    UnifiedBudgetManager* budgetManager_;
    uint32_t frameCount_;
    uint32_t currentFrame_ = 0;

    // Generic resource storage
    std::unordered_map<std::string, void*> resources_;  // Type-erased storage

    // Helper for type-safe storage
    template<typename T>
    PerFrameResource<T>* GetResourceImpl(std::string_view name);
};
```

---

## Migration Strategy

### Phase 1: Create Unified Infrastructure (Week 1)

**Tasks**:
1. Create `UnifiedRM<T>` combining RM.h + StackAllocatedRM
2. Create `UnifiedBudgetManager` combining ResourceBudgetManager + StackTracker
3. Create `PerFrameResourceManager` wrapping UnifiedRM
4. Add comprehensive tests

**Files**:
- `VIXEN/ResourceManagement/include/UnifiedRM.h`
- `VIXEN/ResourceManagement/include/UnifiedBudgetManager.h`
- `VIXEN/ResourceManagement/include/PerFrameResourceManager.h`
- `VIXEN/ResourceManagement/src/UnifiedBudgetManager.cpp`
- `VIXEN/ResourceManagement/src/PerFrameResourceManager.cpp`

---

### Phase 2: Migrate Existing Code (Week 2-3)

**Priority Order**:

#### 2.1: Migrate RM.h usages → UnifiedRM
```cpp
// Before
RM<VkPipeline> pipeline;
pipeline.Set(vkPipeline);
if (pipeline.Ready()) {
    UsePipeline(pipeline.Value());
}

// After
UnifiedRM<VkPipeline> pipeline(
    UnifiedRM<VkPipeline>::AllocStrategy::Heap,
    "MyNode:pipeline"
);
pipeline.Set(vkPipeline);
if (pipeline.Ready()) {
    UsePipeline(pipeline.Value());
}
// Automatic budget tracking!
```

#### 2.2: Migrate PerFrameResources → PerFrameResourceManager
```cpp
// Before
PerFrameResources perFrame;
perFrame.Initialize(device, imageCount);
for (uint32_t i = 0; i < imageCount; i++) {
    perFrame.CreateUniformBuffer(i, sizeof(MyUBO));
}
void* mapped = perFrame.GetUniformBufferMapped(currentFrame);

// After
PerFrameResourceManager perFrame(device, budgetMgr, imageCount);
auto* ubos = perFrame.CreateUniformBuffers("MyNode:ubos", sizeof(MyUBO));
void* mapped = ubos->Get(currentFrame).GetMappedPointer();
// Automatic budget tracking + state management!
```

#### 2.3: Migrate ResourceBudgetManager → UnifiedBudgetManager
```cpp
// Before
ResourceBudgetManager budgetMgr;
budgetMgr.SetBudget(BudgetResourceType::DeviceMemory,
    ResourceBudget(2ULL * 1024 * 1024 * 1024));  // 2 GB
budgetMgr.RecordAllocation(BudgetResourceType::DeviceMemory, bufferSize);

// After
UnifiedBudgetManager budgetMgr;
budgetMgr.SetBudget(UnifiedBudgetManager::BudgetType::DeviceLocal,
    ResourceBudget(2ULL * 1024 * 1024 * 1024));  // 2 GB

// Allocations recorded automatically via UnifiedRM!
// Stack allocations also tracked!
```

#### 2.4: Migrate StackAllocatedRM → UnifiedRM with stack strategy
```cpp
// Before
StackAllocatedRM<VkImageView, MAX_SWAPCHAIN_IMAGES> views("views");
views.Add(view1);
views.Add(view2);

// After (Option 1: Explicit stack)
UnifiedRM<std::array<VkImageView, MAX_SWAPCHAIN_IMAGES>> views(
    UnifiedRM::AllocStrategy::Stack,
    "views"
);
views.Value()[0] = view1;
views.Value()[1] = view2;

// After (Option 2: Helper type alias)
using StackImageViewArray = UnifiedRM<
    BoundedArray<VkImageView, MAX_SWAPCHAIN_IMAGES>
>;
StackImageViewArray views(UnifiedRM::AllocStrategy::Stack, "views");
views.Value().Add(view1);
views.Value().Add(view2);
```

---

### Phase 3: Deprecate Old Systems (Week 4)

1. Mark as deprecated:
   - `RM.h` → `[[deprecated("Use UnifiedRM")]]`
   - `PerFrameResources` → `[[deprecated("Use PerFrameResourceManager")]]`
   - `ResourceBudgetManager` → `[[deprecated("Use UnifiedBudgetManager")]]`
   - `StackAllocatedRM` → `[[deprecated("Use UnifiedRM with Stack strategy")]]`

2. Update documentation to point to unified system

3. Add migration guide in documentation

---

### Phase 4: Remove Old Systems (Week 5)

1. Delete deprecated files:
   - `ResourceManagement/RM.h` (move to `RM_Legacy.h` for reference)
   - `RenderGraph/PerFrameResources.{h,cpp}`
   - `RenderGraph/ResourceBudgetManager.{h,cpp}`
   - `ResourceManagement/StackAllocatedRM.h`

2. Update CMakeLists.txt

3. Verify all tests pass

---

## Benefits of Unified System

### ✅ Consistency
- Single API for all resource types
- Consistent state management everywhere
- Unified error handling

### ✅ Visibility
- **One place** to see all memory usage
- Holistic budget tracking
- Unified reporting

### ✅ Maintainability
- One system to maintain instead of four
- Clearer ownership and lifecycle
- Easier to extend

### ✅ Performance
- Automatic allocation strategy selection
- Budget-aware allocations
- Better tracking overhead (unified)

### ✅ Safety
- Compile-time + runtime checks unified
- Automatic budget enforcement
- Comprehensive warning system

---

## Example: Unified Usage

```cpp
class MyNode : public TypedNode<MyConfig> {
public:
    void SetupImpl(TypedSetupContext& ctx) override {
        // Get unified budget manager from graph
        auto* budgetMgr = ctx.graph->GetBudgetManager();

        // Create per-frame UBOs (automatic budget tracking)
        perFrameUBOs_ = std::make_unique<PerFrameResourceManager::PerFrameResource<VkBuffer>>(
            budgetMgr, "MyNode:ubos"
        );

        // Create pipeline (heap allocation, automatic budget tracking)
        pipeline_ = UnifiedRM<VkPipeline>(
            UnifiedRM<VkPipeline>::AllocStrategy::Heap,
            "MyNode:pipeline"
        );
        pipeline_.RegisterWithBudget(budgetMgr);

        // Create temporary stack array (automatic budget tracking)
        tempViews_ = UnifiedRM<BoundedArray<VkImageView, 4>>(
            UnifiedRM::AllocStrategy::Stack,
            "MyNode:tempViews"
        );
        tempViews_.RegisterWithBudget(budgetMgr);
    }

    void ExecuteImpl(TypedExecuteContext& ctx) override {
        // Use resources transparently
        auto& ubo = perFrameUBOs_->Get(ctx.currentFrame);

        if (pipeline_.Ready()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_.Value());
        }

        // Stack array used transparently
        tempViews_.Value().Clear();
        tempViews_.Value().Add(view1);
        tempViews_.Value().Add(view2);
    }

private:
    std::unique_ptr<PerFrameResourceManager::PerFrameResource<VkBuffer>> perFrameUBOs_;
    UnifiedRM<VkPipeline> pipeline_;
    UnifiedRM<BoundedArray<VkImageView, 4>> tempViews_;
};
```

**Budget manager automatically sees**:
- Device memory: UBO allocations (per-frame)
- Heap memory: VkPipeline handle storage
- Stack memory: tempViews_ array (per-frame tracked)

**One unified report**:
```
=== Unified Budget Report ===
Host Stack:
  Current:  12.5 KB
  Peak:     18.2 KB
  Budget:   512.0 KB (3.6% used)

Host Heap:
  Current:  2.4 MB
  Peak:     3.1 MB
  Budget:   Unlimited

Device Local:
  Current:  256.0 MB
  Peak:     384.0 MB
  Budget:   2.0 GB (12.5% used)

Total Allocations: 247
Frame: 1,234
================================
```

---

## Implementation Checklist

### Week 1: Infrastructure
- [ ] Create UnifiedRM.h
- [ ] Create UnifiedBudgetManager.h/.cpp
- [ ] Create PerFrameResourceManager.h/.cpp
- [ ] Add BoundedArray helper type
- [ ] Write unit tests

### Week 2-3: Migration
- [ ] Migrate all RM.h usages
- [ ] Migrate all PerFrameResources usages
- [ ] Migrate all ResourceBudgetManager usages
- [ ] Migrate all StackAllocatedRM usages
- [ ] Update RenderGraph to use UnifiedBudgetManager
- [ ] Integration tests

### Week 4: Deprecation
- [ ] Mark old systems deprecated
- [ ] Update all documentation
- [ ] Write migration guide
- [ ] Add compiler warnings

### Week 5: Cleanup
- [ ] Remove old files
- [ ] Update CMake
- [ ] Final testing
- [ ] Performance profiling

---

## Open Questions

1. **Type-erased storage in PerFrameResourceManager** - How to handle without std::any overhead?
2. **AllocStrategy::Automatic** - What heuristics determine stack vs heap?
3. **Cross-platform stack sizes** - Different limits on Windows/Linux?
4. **VulkanDevice integration** - Should UnifiedRM have direct VkDevice access?
5. **Thread safety** - Do we need thread-safe budget tracking for multi-threaded recording?

---

**End of Document**
