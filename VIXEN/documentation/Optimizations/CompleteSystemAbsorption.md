# Complete System Absorption: UnifiedRM Integration

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Comprehensive Design
**Related**: UnifiedResourceManagement.md, UnifiedRM-CapabilityGaps.md

---

## Executive Summary

The unified resource management system must absorb **7 fragmented systems** currently operating independently:

```
BEFORE (7 Separate Systems):

1. ResourceManagement/RM.h              - State/metadata tracking
2. RenderGraph/PerFrameResources        - Per-frame GPU resources
3. RenderGraph/ResourceBudgetManager    - Budget tracking
4. ResourceManagement/StackAllocatedRM  - Stack optimization
5. RenderGraph/ResourceDependencyTracker - Dependency tracking
6. PerFrameResources::FrameData         - Frame resource bundles
7. Resource state flags (scattered)      - Dirty/Stale/Locked flags

AFTER (1 Unified System):

UnifiedRM + UnifiedBudgetManager + PerFrameResourceManager
```

This document shows how **ALL** systems are absorbed into the unified architecture.

---

## System 5: ResourceDependencyTracker Absorption

### Current Implementation (ResourceDependencyTracker.h)

**Purpose**: Track which NodeInstance produces which Resource for automatic cleanup ordering.

```cpp
class ResourceDependencyTracker {
public:
    // Register producer
    void RegisterResourceProducer(
        Resource* resource,
        NodeInstance* producer,
        uint32_t outputSlotIndex
    );

    // Query producer
    NodeInstance* GetProducer(Resource* resource) const;

    // Get all dependencies for cleanup ordering
    std::vector<NodeInstance*> GetDependenciesForNode(NodeInstance* consumer) const;

    // Build cleanup dependency handles
    std::vector<NodeHandle> BuildCleanupDependencies(NodeInstance* consumer) const;

private:
    std::unordered_map<Resource*, NodeInstance*> resourceToProducer;
    std::unordered_map<NodeInstance*, std::vector<Resource*>> producerToResources;
};
```

**Use Cases**:
1. Find which node created a resource
2. Build cleanup dependency chains
3. Determine cleanup order for graph teardown

### Integration into UnifiedRM

**Automatic Tracking via Member Pointers**:

```cpp
template<typename Owner, typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    // Constructor already knows the owner!
    UnifiedRM(Owner* owner, MemberPtr memberPtr, AllocStrategy strategy)
        : identity_(owner, memberPtr)
        , owner_(owner)  // Track owner automatically!
    {
        // No need for separate dependency tracker!
    }

    // NEW: Get producing node automatically
    NodeInstance* GetProducerNode() const {
        // Owner is the producing node!
        return dynamic_cast<NodeInstance*>(owner_);
    }

    // NEW: Get slot information
    uint32_t GetOutputSlotIndex() const {
        // Can be inferred from member pointer offset
        return GetMetadataOr<uint32_t>("output_slot_index", 0);
    }

    // NEW: Track consumers automatically
    void RegisterConsumer(NodeInstance* consumer) {
        consumers_.push_back(consumer);
    }

    std::vector<NodeInstance*> GetConsumers() const {
        return consumers_;
    }

private:
    Owner* owner_;  // The producing node
    std::vector<NodeInstance*> consumers_;  // Consuming nodes
};
```

**Key Insight**: Member pointers already encode ownership! No separate tracker needed.

**Cleanup Dependency Resolution**:

```cpp
class UnifiedBudgetManager {
public:
    // NEW: Build cleanup dependencies automatically
    std::vector<NodeHandle> BuildCleanupDependencies(NodeInstance* consumer) const {
        std::vector<NodeHandle> dependencies;

        // Iterate all registered resources
        for (auto* resource : registeredResources_) {
            // Check if this resource is consumed by 'consumer'
            auto consumers = resource->GetConsumers();
            if (std::find(consumers.begin(), consumers.end(), consumer) != consumers.end()) {
                // Consumer depends on this resource's producer
                NodeInstance* producer = resource->GetProducerNode();
                if (producer) {
                    dependencies.push_back(producer->GetHandle());
                }
            }
        }

        return dependencies;
    }

    // NEW: Automatic dependency registration during wiring
    void WireConnection(
        NodeInstance* producer,
        uint32_t outputSlot,
        NodeInstance* consumer,
        uint32_t inputSlot,
        UnifiedRM_Base* resource
    ) {
        // Register consumer on the resource
        resource->RegisterConsumer(consumer);

        // Set output slot metadata
        resource->SetMetadata("output_slot_index", outputSlot);
        resource->SetMetadata("input_slot_index", inputSlot);

        // Dependency tracked automatically!
    }
};
```

**Migration**:

```cpp
// BEFORE: Manual dependency tracking
ResourceDependencyTracker tracker;
tracker.RegisterResourceProducer(resource, producerNode, outputSlot);
auto dependencies = tracker.GetDependenciesForNode(consumerNode);

// AFTER: Automatic via UnifiedRM
// No manual registration needed!
// Dependencies tracked automatically via member pointer ownership
auto dependencies = budgetMgr.BuildCleanupDependencies(consumerNode);
```

**Benefits**:
- ✅ No manual registration needed
- ✅ Member pointer ownership is automatic
- ✅ Impossible to forget to register (compile-time guarantee)
- ✅ Bi-directional lookup (producer→consumers, consumer→producers)

---

## System 6: PerFrameResources::FrameData Absorption

### Current Implementation (PerFrameResources.h:44-60)

**Purpose**: Bundle per-frame GPU resources (UBOs, descriptor sets, command buffers).

```cpp
struct FrameData {
    // Uniform buffer resources
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    void* uniformMappedData = nullptr;
    VkDeviceSize uniformBufferSize = 0;

    // Descriptor set (if using per-frame descriptors)
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // Command buffer (if node records per-frame commands)
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // Frame synchronization (optional)
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
};

class PerFrameResources {
    std::vector<FrameData> frames;  // One per frame-in-flight
};
```

**Use Cases**:
1. Per-frame UBOs that rotate
2. Per-frame descriptor sets
3. Per-frame command buffers
4. Per-frame synchronization primitives

### Integration into UnifiedRM

**Structured Per-Frame Resources**:

```cpp
// NEW: Generic per-frame resource bundle
template<typename Owner>
class PerFrameResourceBundle {
public:
    // Structured resource access
    UnifiedRM<Owner, VkBuffer>* GetUniformBuffer(uint32_t frame);
    UnifiedRM<Owner, VkDescriptorSet>* GetDescriptorSet(uint32_t frame);
    UnifiedRM<Owner, VkCommandBuffer>* GetCommandBuffer(uint32_t frame);
    UnifiedRM<Owner, VkFence>* GetFence(uint32_t frame);
    UnifiedRM<Owner, VkSemaphore>* GetSemaphore(uint32_t frame);

    // Typed access
    template<typename T>
    UnifiedRM<Owner, T>* Get(uint32_t frame, const char* name);

private:
    std::unordered_map<std::string, std::vector<std::unique_ptr<UnifiedRM_Base>>> resources_;
    uint32_t frameCount_;
};
```

**Replacement for PerFrameResources**:

```cpp
class PerFrameResourceManager {
public:
    // NEW: Create structured bundle for a node
    template<typename Owner>
    PerFrameResourceBundle<Owner>* CreateBundle(
        Owner* owner,
        uint32_t frameCount = MAX_FRAMES_IN_FLIGHT
    ) {
        auto bundle = std::make_unique<PerFrameResourceBundle<Owner>>();
        bundle->frameCount_ = frameCount;

        bundles_[owner] = std::move(bundle);
        return static_cast<PerFrameResourceBundle<Owner>*>(bundles_[owner].get());
    }

    // Helper: Create uniform buffer per-frame
    template<typename Owner>
    void CreateUniformBuffers(
        Owner* owner,
        typename UnifiedRM<Owner, VkBuffer>::MemberPtr memberPtr,
        VkDeviceSize bufferSize
    ) {
        auto* bundle = GetOrCreateBundle(owner);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            auto ubo = std::make_unique<UnifiedRM<Owner, VkBuffer>>(
                owner, memberPtr, AllocStrategy::Device
            );

            ubo->SetLifetimePolicy(UnifiedRM::LifetimePolicy::PerFrame);
            ubo->SetMetadata("frame_index", i);
            ubo->SetMetadata("buffer_size", bufferSize);

            // Create actual Vulkan buffer
            VkBuffer buffer = CreateVulkanBuffer(bufferSize);
            ubo->Set(buffer);

            bundle->AddResource("uniformBuffer", i, std::move(ubo));
        }
    }

private:
    std::unordered_map<void*, std::unique_ptr<PerFrameResourceBundle_Base>> bundles_;
};
```

**Usage Example**:

```cpp
class MyNode : public TypedNode<MyConfig> {
    // Member pointer for UBO (single declaration)
    UnifiedRM<MyNode, VkBuffer> uniformBuffer_{this, &MyNode::uniformBuffer_};

    void SetupImpl(TypedSetupContext& ctx) override {
        // Create per-frame uniform buffers using member pointer
        ctx.perFrameManager->CreateUniformBuffers(
            this,
            &MyNode::uniformBuffer_,
            sizeof(MyUBO)
        );

        // Also create per-frame descriptor sets
        ctx.perFrameManager->CreateDescriptorSets(
            this,
            &MyNode::descriptorSet_
        );
    }

    void ExecuteImpl(TypedExecuteContext& ctx) override {
        // Get current frame's resources
        auto* bundle = ctx.perFrameManager->GetBundle(this);

        auto* ubo = bundle->GetUniformBuffer(ctx.currentFrame);
        UpdateUBO(ubo->Value());

        auto* descriptorSet = bundle->GetDescriptorSet(ctx.currentFrame);
        vkCmdBindDescriptorSets(cmd, ..., descriptorSet->Value(), ...);
    }

private:
    UnifiedRM<MyNode, VkDescriptorSet> descriptorSet_{this, &MyNode::descriptorSet_};
};
```

**Benefits**:
- ✅ Type-safe access via member pointers
- ✅ Structured bundles (not loose FrameData struct)
- ✅ Each resource individually trackable
- ✅ Automatic budget integration
- ✅ Clear ownership model

**Migration**:

```cpp
// BEFORE: PerFrameResources with FrameData struct
PerFrameResources perFrame;
perFrame.Initialize(device, imageCount);
perFrame.CreateUniformBuffer(0, sizeof(UBO));
void* mapped = perFrame.GetUniformBufferMapped(0);

// AFTER: PerFrameResourceManager with UnifiedRM
auto* bundle = perFrameMgr.CreateBundle(this);
perFrameMgr.CreateUniformBuffers(this, &MyNode::ubo_, sizeof(UBO));
auto* ubo = bundle->GetUniformBuffer(0);
void* mapped = ubo->GetMappedPointer();  // Via UnifiedRM API
```

---

## System 7: Scattered Resource State Flags Absorption

### Current State (Scattered Across Codebase)

**Problem**: Resource state flags are partially implemented in multiple places:

1. **ResourceState.h** (ResourceManagement) - State enum exists
2. **Resource class** (RenderGraph) - Some state tracking
3. **NodeInstance** - Node-level dirty flags
4. **PerFrameResources** - No state tracking

**Missing**:
- No unified state management API
- Inconsistent dirty marking
- No generation tracking for cache invalidation

### Integration into UnifiedRM

**Unified State API** (Already Designed):

```cpp
template<typename Owner, typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    // State management (full ResourceState enum support)
    bool Has(ResourceState checkState) const;
    ResourceState GetState() const;
    void SetState(ResourceState newState);
    void AddState(ResourceState flags);
    void RemoveState(ResourceState flags);

    // Convenience methods
    bool IsDirty() const { return Has(ResourceState::Dirty); }
    bool IsStale() const { return Has(ResourceState::Stale); }
    bool IsLocked() const { return Has(ResourceState::Locked); }
    bool IsTransient() const { return Has(ResourceState::Transient); }

    void MarkDirty() { AddState(ResourceState::Dirty); }
    void MarkStale() { AddState(ResourceState::Stale); }
    void MarkClean() { RemoveState(ResourceState::Dirty | ResourceState::Stale); }

    // Generation tracking (cache invalidation)
    uint64_t GetGeneration() const { return generation_; }
    void IncrementGeneration() { generation_++; }

    // Check staleness against expected generation
    bool IsStaleGeneration(uint64_t expectedGen) const {
        return generation_ != expectedGen;
    }

private:
    ResourceState state_ = ResourceState::Clean;
    uint64_t generation_ = 0;
};
```

**Usage Example**:

```cpp
class FramebufferNode {
    UnifiedRM<FramebufferNode, VkFramebuffer> framebuffer_{this, &FramebufferNode::framebuffer_};

    void OnSwapChainRecreate() override {
        // Mark dirty when input changes
        framebuffer_.MarkDirty();
    }

    void CompileImpl(TypedCompileContext& ctx) override {
        if (framebuffer_.IsDirty()) {
            // Rebuild only if dirty
            RebuildFramebuffer();
            framebuffer_.MarkClean();
            framebuffer_.IncrementGeneration();  // Notify downstream
        }
    }
};

class RenderPassNode {
    uint64_t cachedFramebufferGen_ = 0;

    void CompileImpl(TypedCompileContext& ctx) override {
        auto* fbRes = GetInput(FRAMEBUFFER);

        // Check if input changed via generation
        if (fbRes->IsStaleGeneration(cachedFramebufferGen_)) {
            LOG_DEBUG("Framebuffer changed, recompiling render pass");
            RebuildRenderPass();
            cachedFramebufferGen_ = fbRes->GetGeneration();
        }
    }
};
```

---

## Complete System Absorption Summary

### Systems Absorbed

| System | Current Lines | Status | Replacement |
|--------|--------------|--------|-------------|
| **RM.h** | ~300 | ✅ Absorbed | UnifiedRM core |
| **PerFrameResources** | ~200 | ✅ Absorbed | PerFrameResourceManager |
| **ResourceBudgetManager** | ~400 | ✅ Absorbed | UnifiedBudgetManager |
| **StackAllocatedRM** | ~600 | ✅ Absorbed | UnifiedRM + AllocStrategy |
| **ResourceDependencyTracker** | ~80 | ✅ Absorbed | UnifiedRM auto-tracking |
| **FrameData struct** | ~20 | ✅ Absorbed | PerFrameResourceBundle |
| **Resource state flags** | ~scattered | ✅ Absorbed | UnifiedRM state API |

**Total Reduction**: ~1,600 lines of fragmented code → **1 unified system**

---

## Final Unified Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                  UNIFIED RESOURCE MANAGEMENT                    │
├────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │         UnifiedRM<Owner, T> (Core Resource Wrapper)      │ │
│  │                                                            │ │
│  │  From RM.h:                                               │ │
│  │    ✓ State management (Ready/Dirty/Stale/Locked)         │ │
│  │    ✓ Metadata tracking                                    │ │
│  │    ✓ Generation counter                                   │ │
│  │                                                            │ │
│  │  From StackAllocatedRM:                                   │ │
│  │    ✓ AllocStrategy (Stack/Heap/Device)                    │ │
│  │    ✓ Stack tracking integration                           │ │
│  │                                                            │ │
│  │  From ResourceDependencyTracker:                          │ │
│  │    ✓ Producer tracking (owner pointer)                    │ │
│  │    ✓ Consumer tracking (registered)                       │ │
│  │    ✓ Auto dependency resolution                           │ │
│  │                                                            │ │
│  │  From Capability Gaps:                                    │ │
│  │    ✓ Resource aliasing (AliasingHint)                     │ │
│  │    ✓ Lifetime management (LifetimePolicy)                 │ │
│  │    ✓ Priority eviction (Priority enum)                    │ │
│  │    ✓ Allocation timing (AllocationTiming)                 │ │
│  └──────────────────────────────────────────────────────────┘ │
│                             │                                   │
│                             ▼                                   │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │      UnifiedBudgetManager (Holistic Budget Tracking)     │ │
│  │                                                            │ │
│  │  From ResourceBudgetManager:                              │ │
│  │    ✓ Host heap budget tracking                            │ │
│  │    ✓ Device memory budget tracking                        │ │
│  │    ✓ Budget enforcement                                   │ │
│  │                                                            │ │
│  │  From StackTracker:                                       │ │
│  │    ✓ Stack allocation tracking                            │ │
│  │    ✓ Per-frame stack usage                                │ │
│  │                                                            │ │
│  │  From ResourceDependencyTracker:                          │ │
│  │    ✓ BuildCleanupDependencies()                           │ │
│  │    ✓ WireConnection() auto-tracking                       │ │
│  │                                                            │ │
│  │  New Capabilities:                                        │ │
│  │    ✓ Aliasing pools (50-80% VRAM savings)                 │ │
│  │    ✓ Priority-based eviction                              │ │
│  │    ✓ Transient auto-cleanup                               │ │
│  └──────────────────────────────────────────────────────────┘ │
│                             │                                   │
│                             ▼                                   │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │     PerFrameResourceManager (Structured Frame Resources) │ │
│  │                                                            │ │
│  │  From PerFrameResources:                                  │ │
│  │    ✓ Per-frame UBO creation                               │ │
│  │    ✓ Per-frame descriptor sets                            │ │
│  │    ✓ Per-frame command buffers                            │ │
│  │    ✓ Frame synchronization primitives                     │ │
│  │                                                            │ │
│  │  From FrameData struct:                                   │ │
│  │    ✓ Structured bundle access                             │ │
│  │    ✓ Uniform buffer + memory + mapped pointer             │ │
│  │    ✓ Descriptor sets                                      │ │
│  │    ✓ Command buffers                                      │ │
│  │    ✓ Fences + Semaphores                                  │ │
│  │                                                            │ │
│  │  New Capabilities:                                        │ │
│  │    ✓ Type-safe via UnifiedRM member pointers              │ │
│  │    ✓ Individual resource tracking                         │ │
│  │    ✓ Automatic budget integration                         │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                  │
└────────────────────────────────────────────────────────────────┘
```

---

## Benefits of Complete Absorption

### 1. Single Source of Truth
- All resource management in one system
- No confusion about which API to use
- Consistent behavior across all resource types

### 2. Automatic Tracking
- Dependency tracking via member pointers (no manual registration)
- Budget tracking via UnifiedRM registration (automatic)
- State tracking unified (ResourceState flags)

### 3. Zero Duplication
- 1,600+ lines of fragmented code eliminated
- Single API surface
- One place to fix bugs

### 4. Type Safety
- Member pointer identification (compile-time)
- No string-based lookups
- No manual tracking

### 5. Performance
- Automatic aliasing (50-80% VRAM savings)
- Priority eviction (prevent OOM)
- Transient auto-cleanup (no leaks)
- Selective recompilation (O(1) vs O(N))

---

## Implementation Checklist

### Week 1: Core Unification
- [ ] Implement UnifiedRM<Owner, T> base class
- [ ] Integrate ResourceState flags
- [ ] Implement member pointer identification
- [ ] Add producer/consumer tracking
- [ ] Basic tests

### Week 2: Budget Manager
- [ ] Implement UnifiedBudgetManager
- [ ] Absorb ResourceBudgetManager functionality
- [ ] Absorb StackTracker functionality
- [ ] Add BuildCleanupDependencies()
- [ ] Integration tests

### Week 3: Per-Frame Resources
- [ ] Implement PerFrameResourceManager
- [ ] Implement PerFrameResourceBundle
- [ ] Absorb PerFrameResources functionality
- [ ] Absorb FrameData struct
- [ ] Migration guide

### Week 4: Advanced Features
- [ ] Resource aliasing (AliasingPool)
- [ ] Lifetime policies
- [ ] Priority eviction
- [ ] Allocation timing

### Week 5: Migration
- [ ] Migrate all RM.h usages
- [ ] Migrate all PerFrameResources usages
- [ ] Migrate all ResourceDependencyTracker usages
- [ ] Remove deprecated systems
- [ ] Final validation

---

**Total Lines Unified**: ~1,600 lines across 7 systems → 1 system
**Estimated Implementation**: 5 weeks
**Impact**: Complete resource management unification

---

**End of Document**
