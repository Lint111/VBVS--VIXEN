# Central UnifiedRM: Transparent Integration Design

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Implementation Design
**Related**: AutomaticResourceAliasingFromTopology.md, UnifiedRM-CapabilityGaps.md

---

## Executive Summary

**Goal**: Integrate UnifiedRM as a **central system in RenderGraph** while keeping the existing `ctx.In()`/`ctx.Out()` API **completely unchanged** for node implementations.

**Key Principle**: Transparency - nodes continue to work as before, URM operates behind the scenes.

---

## Architecture Overview

```
RenderGraph (owns central URM)
    └─ UnifiedBudgetManager (Central Registry)
           ├─ Tracks all resources by (NodeInstance*, slotIndex)
           ├─ Automatic lifetime tracking via ResourceLifetimeAnalyzer
           ├─ Automatic aliasing pool management
           └─ Budget enforcement

NodeInstance
    ├─ CreateResource<T>() → delegates to graph.budgetManager_
    └─ GetOutputResource() → queries graph.budgetManager_

TypedNodeInstance
    └─ ctx.Out<T>() → internally uses URM (transparent!)

Node Implementation (NO CHANGES NEEDED)
    └─ ctx.Out<VkBuffer>(0) = myBuffer;  // Works as before!
```

---

## Central URM Registry Design

### Resource Identification

Resources are identified by `(NodeInstance*, slotIndex, arrayIndex)` triple:

```cpp
struct ResourceKey {
    NodeInstance* owner;
    uint32_t slotIndex;
    uint32_t arrayIndex;

    // Hash and equality for unordered_map
    size_t GetHash() const {
        return std::hash<void*>{}(owner) ^
               (std::hash<uint32_t>{}(slotIndex) << 1) ^
               (std::hash<uint32_t>{}(arrayIndex) << 2);
    }

    bool operator==(const ResourceKey& other) const {
        return owner == other.owner &&
               slotIndex == other.slotIndex &&
               arrayIndex == other.arrayIndex;
    }
};
```

### UnifiedBudgetManager API

```cpp
class UnifiedBudgetManager {
public:
    // Register a resource for tracking
    template<typename T>
    void RegisterResource(
        NodeInstance* owner,
        uint32_t slotIndex,
        uint32_t arrayIndex,
        T* resourcePtr,
        AllocStrategy strategy = AllocStrategy::Automatic
    );

    // Get tracked resource (for lifetime analysis)
    UnifiedRM_Base* GetResource(
        NodeInstance* owner,
        uint32_t slotIndex,
        uint32_t arrayIndex = 0
    ) const;

    // Create aliasing pools from topology analysis
    void UpdateAliasingPoolsFromTopology(
        const ResourceLifetimeAnalyzer& analyzer
    );

    // Budget queries
    size_t GetTotalAllocatedBytes() const;
    size_t GetTotalAllocatedBytes(MemoryLocation location) const;

    void PrintResourceReport() const;
    void PrintAliasingReport() const;

private:
    // Resource registry: (owner, slot, array) → UnifiedRM_Base*
    std::unordered_map<ResourceKey, std::unique_ptr<UnifiedRM_Base>> resources_;

    // Aliasing pools (computed from ResourceLifetimeAnalyzer)
    struct AliasingPool {
        std::string poolID;
        size_t totalSize;
        void* sharedMemory;
        std::vector<UnifiedRM_Base*> aliasedResources;
    };
    std::unordered_map<std::string, AliasingPool> aliasingPools_;
};
```

---

## Integration Points

### 1. NodeInstance::CreateResource() (Explicit API)

For nodes that need explicit resource management:

```cpp
// In NodeInstance.h
template<typename T>
class ResourceHandle {
public:
    T& Get() { return value_; }
    const T& Get() const { return value_; }

    void Set(T value) {
        value_ = std::move(value);
        if (rm_) rm_->Set(value_);
    }

private:
    friend class NodeInstance;
    ResourceHandle(T* storage, UnifiedRM_Base* rm)
        : value_(*storage), rm_(rm) {}

    T& value_;
    UnifiedRM_Base* rm_;
};

template<typename T>
ResourceHandle<T> CreateResource(
    uint32_t slotIndex,
    AllocStrategy strategy = AllocStrategy::Automatic
);
```

**Usage in node**:
```cpp
class MyNode : public TypedNode<MyConfig> {
    void CompileImpl(TypedCompileContext& ctx) override {
        // Explicit resource creation (registered with central URM)
        auto buffer = CreateResource<VkBuffer>(OUTPUT_SLOT, AllocStrategy::Device);

        // Create Vulkan buffer
        vkCreateBuffer(device, &createInfo, nullptr, &tmpBuffer);
        buffer.Set(tmpBuffer);  // Registers with URM

        // Output (works as before)
        ctx.Out<VkBuffer>(OUTPUT_SLOT) = buffer.Get();
    }
};
```

### 2. ctx.Out() Transparent Integration (Behind-the-Scenes)

**No changes to node implementations!** Behind the scenes, ctx.Out() registers resources:

```cpp
// In TypedNodeInstance - updated implementation
template<typename T>
T& TypedCompileContext::Out(uint32_t slotIndex) {
    // Get output storage (existing mechanism)
    T* storage = GetOutputStorage<T>(slotIndex);

    // Register with central URM (NEW - transparent!)
    if (node_->GetOwningGraph()) {
        node_->GetOwningGraph()->GetBudgetManager()->RegisterResource(
            node_, slotIndex, taskIndex_, storage, AllocStrategy::Automatic
        );
    }

    // Return reference (node writes to it as before)
    return *storage;
}
```

**From node's perspective**: No change!
```cpp
// Nodes continue working as before:
ctx.Out<VkBuffer>(0) = myBuffer;  // Transparent URM tracking!
```

### 3. GetOutputResource() Implementation

Bridge for ResourceLifetimeAnalyzer:

```cpp
// In NodeInstance.cpp
ResourceManagement::UnifiedRM_Base* NodeInstance::GetOutputResource(uint32_t slotIndex) const {
    if (!owningGraph) return nullptr;

    // Query central URM
    return owningGraph->GetBudgetManager()->GetResource(this, slotIndex, 0);
}
```

---

## Migration Strategy

### Phase 1: Passive Tracking (Current)
- Central URM tracks resources transparently
- Existing nodes work without changes
- No aliasing yet (just tracking)

### Phase 2: Automatic Aliasing
- UnifiedBudgetManager creates aliasing pools
- ResourceLifetimeAnalyzer feeds data
- Automatic 50-80% VRAM savings

### Phase 3: Budget Enforcement
- URM can reject allocations exceeding budget
- Priority-based eviction
- Real-time memory pressure handling

### Phase 4: Explicit CreateResource() (Optional)
- Nodes that want fine-grained control use CreateResource()
- Most nodes continue using ctx.Out() unchanged

---

## Implementation Checklist

### Week 1: Central URM Foundation
- [ ] Implement UnifiedBudgetManager class
- [ ] Resource registry with ResourceKey
- [ ] RegisterResource() template method
- [ ] GetResource() query method
- [ ] Unit tests

### Week 2: NodeInstance Integration
- [ ] Add RenderGraph::budgetManager_ member
- [ ] Implement NodeInstance::CreateResource()
- [ ] Implement GetOutputResource() delegation
- [ ] Update ctx.Out() for transparent registration
- [ ] Integration tests

### Week 3: Automatic Aliasing
- [ ] Implement UpdateAliasingPoolsFromTopology()
- [ ] Aliasing pool creation
- [ ] Memory binding for aliased resources
- [ ] Call from RenderGraph::Compile()
- [ ] Validate savings

### Week 4: Reporting & Optimization
- [ ] PrintResourceReport() implementation
- [ ] PrintAliasingReport() implementation
- [ ] Performance profiling
- [ ] Documentation updates

---

## Backward Compatibility

**100% backward compatible!**

Existing nodes:
```cpp
// OLD CODE (still works!)
class OldNode : public TypedNode<OldConfig> {
    void CompileImpl(TypedCompileContext& ctx) override {
        VkBuffer buffer = CreateVulkanBuffer();
        ctx.Out<VkBuffer>(0) = buffer;  // ✅ Tracked by URM transparently!
    }
};
```

New nodes (optional explicit management):
```cpp
// NEW CODE (optional)
class NewNode : public TypedNode<NewConfig> {
    void CompileImpl(TypedCompileContext& ctx) override {
        auto buffer = CreateResource<VkBuffer>(0, AllocStrategy::Device);
        buffer.Set(CreateVulkanBuffer());  // Explicit URM interaction
        ctx.Out<VkBuffer>(0) = buffer.Get();
    }
};
```

---

## Benefits

✅ **Zero Breaking Changes** - All existing nodes work
✅ **Transparent Tracking** - Automatic resource registration
✅ **Automatic Aliasing** - 50-80% VRAM savings with no config
✅ **Optional Explicit API** - CreateResource() for fine-grained control
✅ **Central Management** - Single source of truth
✅ **Type-Safe** - Compile-time validation

---

## Next Steps

1. Implement UnifiedBudgetManager core (resource registry)
2. Add budgetManager_ to RenderGraph
3. Update NodeInstance::GetOutputResource() to query central URM
4. Add transparent registration in ctx.Out()
5. Test with existing render graphs (should work unchanged)
6. Implement automatic aliasing integration

---

**End of Document**
