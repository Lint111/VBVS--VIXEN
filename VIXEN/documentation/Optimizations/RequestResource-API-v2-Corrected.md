# RequestResource API: Slot-Agnostic Resource Creation

**Document Version**: 2.0 (CORRECTED)
**Created**: 2025-11-11
**Status**: Implementation Design
**Supersedes**: RequestResource-API-Design.md v1.0

---

## Core Principle

**Resources are graph-level entities, NOT node-owned or slot-bound.**

Slots are just **pointers** to resources. The URM manages allocation and tracking.

---

## Architecture

### Resource Ownership Hierarchy

```
RenderGraph
    └─ ResourceBudgetManager (URM)
           └─ Resource Pool (owns all Resources)
                  ├─ Resource #1: VkBuffer
                  ├─ Resource #2: VkImage
                  ├─ Resource #3: std::vector<VkImageView>
                  └─ ...

NodeInstance
    └─ bundles[arrayIndex]
           └─ outputs[slotIndex] = Resource*  (just a pointer!)
                                   ^^^^^^^^^^^
```

### Key Insight: Same Resource, Multiple Slots

```cpp
// Resource #42 referenced by multiple slots:
producerNode.bundles[0].outputs[0] → Resource #42  (creates)
consumerA.bundles[0].inputs[0]    → Resource #42  (reads)
consumerB.bundles[0].inputs[1]    → Resource #42  (reads)
```

**URM tracks Resource #42 by identity, NOT by which slots reference it!**

---

## API Design

### RequestResource<T>() - Slot-Agnostic

```cpp
/**
 * @brief Request URM-managed resource (NO slot information)
 *
 * Creates and tracks a resource through the unified resource manager.
 * The resource is independent of slots - wiring happens separately via ctx.Out().
 *
 * All non-trivial allocations MUST go through this API for:
 * - Budget tracking
 * - Lifetime analysis
 * - Memory aliasing
 * - Stack/heap optimization
 *
 * @param descriptor Resource descriptor (ImageDescriptor, BufferDescriptor, etc.)
 * @param strategy Allocation strategy (Stack/Heap/Device/Automatic)
 * @return Resource* ready to be populated and wired to slots
 *
 * Usage:
 * @code
 * // 1. REQUEST resource from URM
 * auto bufferDesc = BufferDescriptor{...};
 * Resource* buffer = ctx.RequestResource<VkBuffer>(bufferDesc);
 *
 * // 2. POPULATE the resource
 * VkBuffer vkBuffer;
 * vkCreateBuffer(device, &info, nullptr, &vkBuffer);
 * buffer->SetHandle(vkBuffer);
 *
 * // 3. WIRE to slot (separate concern!)
 * ctx.Out(OUTPUT_BUFFER_SLOT, buffer);
 * @endcode
 */
template<typename T>
Resource* RequestResource(
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
    ResourceManagement::AllocStrategy strategy = ResourceManagement::AllocStrategy::Automatic
);
```

### For Containers/Arrays

```cpp
/**
 * @brief Request container resource (vectors, arrays, etc.)
 *
 * For heap-allocated containers like std::vector<VkImageView>.
 * URM tracks the container allocation for budget enforcement.
 *
 * @param capacityHint Expected size for pre-allocation
 * @return Resource* containing empty container ready to populate
 */
template<typename ContainerT>
Resource* RequestContainer(
    size_t capacityHint = 0,
    ResourceManagement::AllocStrategy strategy = ResourceManagement::AllocStrategy::Heap
);
```

**Usage:**
```cpp
// Request tracked vector
Resource* views = ctx.RequestContainer<std::vector<VkImageView>>(4);

// Populate container
auto& viewsVec = views->GetHandle<std::vector<VkImageView>>();
viewsVec.push_back(imageView1);
viewsVec.push_back(imageView2);

// Wire to slot
ctx.Out(IMAGE_VIEWS_SLOT, views);
```

---

## URM Registry Design

### Resource Tracking by Identity

```cpp
class ResourceBudgetManager {
private:
    // Track by Resource* identity (NOT by slot!)
    struct ResourceMetadata {
        Resource* resource;                          // Back-pointer for validation
        ResourceManagement::AllocStrategy strategy;
        ResourceManagement::MemoryLocation location;
        size_t allocatedBytes;
        uint64_t allocationTimestamp;               // For debugging

        // Lifetime tracked separately by ResourceLifetimeAnalyzer via graph edges
        // (Producer/consumers derived from topology, not stored here)
    };

    std::unordered_map<Resource*, ResourceMetadata> resourceRegistry_;

    // Resource pool (owns all Resources)
    std::vector<std::unique_ptr<Resource>> resources_;

public:
    /**
     * @brief Create and track a new resource
     *
     * @param descriptor Resource descriptor
     * @param strategy Allocation strategy
     * @return Resource* tracked by URM
     */
    template<typename T>
    Resource* CreateResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceManagement::AllocStrategy strategy
    );

    /**
     * @brief Get metadata for a resource (for budgeting/reporting)
     */
    const ResourceMetadata* GetResourceMetadata(Resource* resource) const;

    /**
     * @brief Track resource allocation size update
     *
     * Called when resource handle is set (e.g., VkBuffer created)
     */
    void UpdateResourceSize(Resource* resource, size_t newSize);
};
```

---

## Implementation Flow

### 1. Node Requests Resource

```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Request URM-managed buffer (NO slot info!)
    BufferDescriptor desc{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    Resource* buffer = ctx.RequestResource<VkBuffer>(desc);
    // ↓
    // Delegates to: owningGraph->GetBudgetManager()->CreateResource<VkBuffer>(desc, strategy)
```

### 2. URM Creates and Tracks Resource

```cpp
template<typename T>
Resource* ResourceBudgetManager::CreateResource(
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
    AllocStrategy strategy
) {
    // Create Resource object
    auto resource = Resource::Create<T>(descriptor);

    // Store in pool (URM owns it)
    Resource* resPtr = resource.get();
    resources_.push_back(std::move(resource));

    // Track metadata
    ResourceMetadata metadata{
        .resource = resPtr,
        .strategy = strategy,
        .location = DetermineMemoryLocation<T>(strategy),
        .allocatedBytes = 0,  // Updated when handle is set
        .allocationTimestamp = GetCurrentTimestamp()
    };
    resourceRegistry_[resPtr] = metadata;

    // Record budget allocation
    RecordAllocation(GetBudgetType<T>(), EstimateSize(descriptor));

    return resPtr;
}
```

### 3. Node Populates Resource

```cpp
    // Create Vulkan resource
    VkBuffer vkBuffer;
    vkCreateBuffer(device, &createInfo, nullptr, &vkBuffer);

    // Store in Resource
    buffer->SetHandle(vkBuffer);
    // ↓
    // Also updates URM metadata via callback:
    // budgetManager->UpdateResourceSize(buffer, actualSize);
```

### 4. Node Wires to Slot

```cpp
    // Wire Resource* to output slot
    ctx.Out(OUTPUT_BUFFER_SLOT, buffer);
    // ↓
    // bundles[taskIndex].outputs[OUTPUT_BUFFER_SLOT] = buffer;
    // (Just storing the pointer!)
}
```

---

## ResourceLifetimeAnalyzer Integration

### Lifetime Tracking via Graph Topology

```cpp
// ResourceLifetimeAnalyzer works at graph-level, not slot-level!

void ResourceLifetimeAnalyzer::ComputeTimelines(
    const std::vector<NodeInstance*>& executionOrder,
    const std::vector<GraphEdge>& edges
) {
    // For each edge: source → target
    for (const auto& edge : edges) {
        // Get Resource* from source's output slot
        Resource* resource = edge.source->GetOutput(
            edge.sourceOutputIndex,
            edge.sourceArrayIndex
        );

        if (!resource) continue;

        // Track timeline by Resource* identity (not by slot!)
        auto& timeline = timelines_[resource];
        timeline.resource = resource;
        timeline.producer = edge.source;
        timeline.consumers.push_back(edge.target);

        // Compute birth/death indices from execution order
        timeline.birthIndex = nodeToIndex[edge.source];
        timeline.deathIndex = std::max(timeline.deathIndex, nodeToIndex[edge.target]);
    }
}

// Query by Resource*:
const ResourceTimeline* GetTimeline(Resource* resource) const {
    return timelines_.find(resource);
}
```

---

## Benefits

✅ **Clean Separation** - URM manages allocation, nodes manage wiring
✅ **Resource Reuse** - Same Resource* can be referenced by multiple slots
✅ **True Tracking** - URM tracks actual resources, not slot associations
✅ **Budget Accuracy** - Each Resource tracked once, regardless of slot count
✅ **Aliasing Correctness** - Lifetime based on actual resource flow, not slots
✅ **Stack/Heap Tracking** - Vectors, arrays, all allocations go through URM

---

## Migration Examples

### Before (Untracked):
```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Untracked allocation - no budget enforcement!
    VkBuffer buffer;
    vkCreateBuffer(device, &info, nullptr, &buffer);

    ctx.Out(OUTPUT_SLOT, buffer);
}
```

### After (URM-Tracked):
```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Request tracked resource
    BufferDescriptor desc{.size = 1024, ...};
    Resource* bufferRes = ctx.RequestResource<VkBuffer>(desc);

    // Create Vulkan resource
    VkBuffer buffer;
    vkCreateBuffer(device, &info, nullptr, &buffer);
    bufferRes->SetHandle(buffer);

    // Wire to slot
    ctx.Out(OUTPUT_SLOT, bufferRes);
}
```

### Vector Example:
```cpp
// Before (untracked heap allocation):
std::vector<VkImageView> views;
views.push_back(view1);
ctx.Out(VIEWS_SLOT, views);  // Copies vector

// After (URM-tracked):
Resource* viewsRes = ctx.RequestContainer<std::vector<VkImageView>>(4);
auto& views = viewsRes->GetHandle<std::vector<VkImageView>>();
views.push_back(view1);
ctx.Out(VIEWS_SLOT, viewsRes);  // Passes Resource*
```

---

## Implementation Checklist

### Week 1: Core URM Resource Creation
- [ ] Add `CreateResource<T>()` to ResourceBudgetManager
- [ ] Add `ResourceMetadata` structure
- [ ] Implement resource pool (`std::vector<std::unique_ptr<Resource>>`)
- [ ] Add `UpdateResourceSize()` callback
- [ ] Add `RequestResource<T>()` to TypedCompileContext
- [ ] Unit tests for resource creation

### Week 2: Lifetime Integration
- [ ] Update ResourceLifetimeAnalyzer to track by Resource* (not slot)
- [ ] Fix GetOutputResource() to return Resource* from slot
- [ ] Update ComputeTimelines() to work with Resource* identity
- [ ] Integration tests

### Week 3: Container Support
- [ ] Add `RequestContainer<T>()` variant
- [ ] Support std::vector, std::array tracking
- [ ] Heap allocation budget enforcement
- [ ] Stack optimization hints

### Week 4: Migration & Testing
- [ ] Migrate SwapChainNode to use RequestResource
- [ ] Test multi-slot resource references
- [ ] Validate aliasing with real graphs
- [ ] Performance profiling

---

**End of Document**
