# Phase H: Unified ResourcePool Architecture

## Overview

This document describes the advanced Phase H features that unify all resource management systems into a single, coherent architecture with automatic memory aliasing, strict budget enforcement, and comprehensive profiling.

## Architecture Goals

1. **Unified Resource Identity**: All resources tracked by pointer identity (Resource*), not by slot/index
2. **Automatic Memory Aliasing**: 50-80% VRAM savings through lifetime-based aliasing
3. **Strict Budget Enforcement**: Configurable limits with automatic heap fallback
4. **Comprehensive Profiling**: Per-node, per-frame resource tracking
5. **Pool Control**: Explicit control over resource allocation strategies

## Core Components

### 1. ResourcePool - Central Resource Management

```cpp
class ResourcePool {
public:
    // Resource allocation with aliasing support
    template<typename T>
    Resource* AllocateResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceLifetime lifetime,
        AllocationStrategy strategy = AllocationStrategy::Automatic
    );

    // Release resource (enables aliasing)
    void ReleaseResource(Resource* resource);

    // Aliasing control
    void EnableAliasing(bool enable);
    void SetAliasingThreshold(size_t minBytes);

    // Budget control
    void SetBudget(BudgetResourceType type, size_t maxBytes, bool strict = false);
    BudgetStats GetBudgetStats(BudgetResourceType type) const;

    // Profiling
    ResourceAllocationStats GetAllocationStats(uint32_t nodeId) const;
    void BeginFrameProfiling(uint64_t frameNumber);
    void EndFrameProfiling();

private:
    // Aliasing engine
    std::unique_ptr<AliasingEngine> aliasingEngine_;

    // Budget manager (existing)
    std::unique_ptr<ResourceBudgetManager> budgetManager_;

    // Profiler
    std::unique_ptr<ResourceProfiler> profiler_;
};
```

### 2. AliasingEngine - Memory Reuse

```cpp
class AliasingEngine {
public:
    struct AliasCandidate {
        Resource* resource;
        size_t bytes;
        ResourceLifetime lifetime;
        VkMemoryRequirements requirements;
    };

    // Find alias candidate for new allocation
    Resource* FindAlias(
        const VkMemoryRequirements& requirements,
        ResourceLifetime lifetime
    );

    // Register resource for potential aliasing
    void RegisterForAliasing(
        Resource* resource,
        const VkMemoryRequirements& requirements,
        ResourceLifetime lifetime
    );

    // Mark resource as released (available for aliasing)
    void MarkReleased(Resource* resource, uint64_t frameNumber);

    // Statistics
    AliasingStats GetStats() const;

private:
    // Lifetime tracker integration
    ResourceLifetimeAnalyzer* lifetimeAnalyzer_;

    // Available resources for aliasing (sorted by size)
    std::multimap<size_t, AliasCandidate> availableResources_;

    // Active aliases
    std::unordered_map<Resource*, std::vector<Resource*>> aliasMap_;
};
```

### 3. ResourceProfiler - Per-Node Tracking

```cpp
class ResourceProfiler {
public:
    struct NodeResourceStats {
        uint32_t nodeId;
        std::string nodeName;

        // Per-frame stats
        size_t stackBytesUsed;
        size_t heapBytesUsed;
        size_t vramBytesUsed;
        uint32_t stackAllocations;
        uint32_t heapAllocations;
        uint32_t vramAllocations;

        // Aliasing stats
        uint32_t aliasedAllocations;
        size_t bytesSavedViaAliasing;

        // Performance
        double allocationTimeMs;
        double releaseTimeMs;
    };

    void RecordAllocation(
        uint32_t nodeId,
        ResourceLocation location,
        size_t bytes,
        bool wasAliased
    );

    void RecordRelease(
        uint32_t nodeId,
        Resource* resource
    );

    void BeginFrame(uint64_t frameNumber);
    void EndFrame();

    NodeResourceStats GetNodeStats(uint32_t nodeId, uint64_t frameNumber) const;
    std::vector<NodeResourceStats> GetAllNodeStats(uint64_t frameNumber) const;

private:
    // Per-frame, per-node statistics
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, NodeResourceStats>> frameStats_;

    // Rolling window (last N frames)
    static constexpr size_t MAX_FRAME_HISTORY = 120;
};
```

### 4. Unified RM<T> Template

```cpp
// Unified resource management template
template<typename T>
class RM {
public:
    // Construction from ResourcePool
    static RM<T> Request(
        ResourcePool& pool,
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceLifetime lifetime = ResourceLifetime::FrameLocal
    ) {
        auto* resource = pool.AllocateResource<T>(descriptor, lifetime);
        return RM<T>(resource, &pool);
    }

    // RAII semantics
    ~RM() {
        if (resource_ && pool_) {
            pool_->ReleaseResource(resource_);
        }
    }

    // Move-only
    RM(RM&& other) noexcept;
    RM& operator=(RM&& other) noexcept;
    RM(const RM&) = delete;
    RM& operator=(const RM&) = delete;

    // Access
    T* Get() const { return resource_ ? resource_->As<T>() : nullptr; }
    T* operator->() const { return Get(); }
    T& operator*() const { return *Get(); }

    // Status queries
    bool IsAliased() const;
    ResourceLocation GetLocation() const;
    size_t GetBytes() const;

private:
    RM(Resource* resource, ResourcePool* pool)
        : resource_(resource), pool_(pool) {}

    Resource* resource_ = nullptr;
    ResourcePool* pool_ = nullptr;
};
```

## Integration with Existing Systems

### ResourceBudgetManager Integration

```cpp
// ResourceBudgetManager now owned by ResourcePool
class ResourceBudgetManager {
public:
    // Existing budget tracking
    void SetBudget(BudgetResourceType type, const ResourceBudget& budget);
    BudgetUsage GetUsage(BudgetResourceType type) const;

    // NEW: Aliasing integration
    void RecordAllocation(
        BudgetResourceType type,
        size_t bytes,
        bool wasAliased = false  // NEW
    );

    void RecordRelease(
        BudgetResourceType type,
        size_t bytes,
        bool wasAliased = false  // NEW
    );

    // NEW: Profiling integration
    void SetProfiler(ResourceProfiler* profiler);
};
```

### NodeInstance API Updates

```cpp
class NodeInstance {
public:
    // Existing URM APIs
    template<typename T>
    Resource* RequestResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceManagement::AllocStrategy strategy = ResourceManagement::AllocStrategy::Automatic
    );

    template<typename T, size_t Capacity>
    StackResourceResult<T, Capacity> RequestStackResource(std::string_view name);

    // NEW: Unified RM<T> API
    template<typename T>
    RM<T> RequestManagedResource(
        const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
        ResourceLifetime lifetime = ResourceLifetime::FrameLocal
    ) {
        auto* pool = GetResourcePool();
        return RM<T>::Request(*pool, descriptor, lifetime);
    }

    // NEW: Pool access
    ResourcePool* GetResourcePool() const {
        return GetOwningGraph()->GetResourcePool();
    }
};
```

## Aliasing Strategy

### Lifetime-Based Aliasing

Resources with non-overlapping lifetimes can share the same memory:

```
Frame Timeline:
├─ Compile Phase
│  ├─ Resource A (lifetime: CompilePhase)
│  └─ Resource B (lifetime: CompilePhase)
├─ Execute Phase
│  ├─ Resource C (lifetime: FrameLocal) ← can alias with A or B
│  └─ Resource D (lifetime: FrameLocal) ← can alias with C (if sequential)
└─ Present Phase
   └─ Resource E (lifetime: FrameLocal) ← can alias with C or D
```

### Aliasing Rules

1. **Same Memory Type**: Only alias resources with identical memory requirements
2. **Non-Overlapping Lifetimes**: Guaranteed by ResourceLifetimeAnalyzer
3. **Size Compatibility**: New resource ≤ available resource size
4. **Alignment**: Respect VkMemoryRequirements alignment

### Aliasing Algorithm

```cpp
Resource* AliasingEngine::FindAlias(
    const VkMemoryRequirements& requirements,
    ResourceLifetime lifetime
) {
    // 1. Query lifetime analyzer for non-overlapping resources
    auto candidates = lifetimeAnalyzer_->GetNonOverlappingResources(lifetime);

    // 2. Filter by memory requirements
    auto compatible = FilterByMemoryRequirements(candidates, requirements);

    // 3. Find smallest compatible resource (best-fit)
    auto best = FindBestFit(compatible, requirements.size);

    if (best) {
        // Record aliasing relationship
        aliasMap_[best].push_back(/* new resource */);
        return best;
    }

    return nullptr; // No alias found, allocate new memory
}
```

## Budget Enforcement Modes

### Soft Mode (Default)
- Budget tracked but not enforced
- Warnings when approaching limits
- Automatic heap fallback for stack allocations

### Strict Mode
- Budget strictly enforced
- Allocation fails if budget exceeded
- Returns AllocationError::BudgetExceeded
- No automatic fallback

### Configuration

```cpp
// Per-budget-type configuration
struct ResourceBudget {
    size_t maxBytes;
    bool strict;              // NEW: Enable strict mode
    float warningThreshold;   // 0.0-1.0 (e.g., 0.8 = 80%)
    float criticalThreshold;  // 0.0-1.0 (e.g., 0.95 = 95%)
};

// Example usage
ResourcePool pool;
pool.SetBudget(BudgetResourceType::HostMemory, {
    .maxBytes = 512 * 1024 * 1024,  // 512 MB
    .strict = true,                  // Enforce limit
    .warningThreshold = 0.8f,
    .criticalThreshold = 0.95f
});
```

## Profiling Output Format

### Per-Frame Report

```
Frame #1234 Resource Report
════════════════════════════════════════════════════════════════

Node: DescriptorSetNode (ID: 42)
  Stack:  256 bytes (2 allocations)
  Heap:   0 bytes (0 allocations)
  VRAM:   4 MB (1 allocation, 0 aliased)
  Time:   0.05 ms

Node: TextureLoaderNode (ID: 67)
  Stack:  0 bytes (0 allocations)
  Heap:   16 KB (1 allocation)
  VRAM:   128 MB (4 allocations, 3 aliased ← 75% savings!)
  Time:   2.3 ms

Total Frame Stats:
  Stack:  2 KB / 64 KB (3% usage)
  Heap:   45 MB / 512 MB (8% usage)
  VRAM:   1.2 GB / 4 GB (30% usage, 450 MB saved via aliasing)

Aliasing Efficiency: 27% VRAM saved
```

### JSON Export

```json
{
  "frameNumber": 1234,
  "nodes": [
    {
      "nodeId": 42,
      "nodeName": "DescriptorSetNode",
      "stackBytes": 256,
      "heapBytes": 0,
      "vramBytes": 4194304,
      "stackAllocations": 2,
      "heapAllocations": 0,
      "vramAllocations": 1,
      "aliasedAllocations": 0,
      "bytesSavedViaAliasing": 0,
      "allocationTimeMs": 0.05
    }
  ],
  "totals": {
    "stackBytes": 2048,
    "heapBytes": 46080,
    "vramBytes": 1288490188,
    "bytesSavedViaAliasing": 471859200,
    "aliasEfficiencyPercent": 27.0
  }
}
```

## Migration Path

### Phase 1: Add ResourcePool Infrastructure
1. Implement ResourcePool class
2. Integrate with ResourceBudgetManager
3. Add to RenderGraph

### Phase 2: Implement AliasingEngine
1. Implement AliasingEngine class
2. Integrate with ResourceLifetimeAnalyzer
3. Add aliasing tests

### Phase 3: Add ResourceProfiler
1. Implement ResourceProfiler class
2. Integrate with ResourcePool
3. Add profiling output

### Phase 4: Unify RM<T> Template
1. Create unified RM<T> template
2. Update NodeInstance APIs
3. Migrate existing code

### Phase 5: Enable Budget Enforcement
1. Add strict mode support
2. Update error handling
3. Add budget tests

## Performance Targets

- **Memory Savings**: 50-80% VRAM reduction via aliasing
- **Allocation Overhead**: <1% additional CPU time
- **Profiling Overhead**: <0.5% in debug, 0% in release (compiled out)
- **Stack Usage**: <1 MB per frame total
- **Heap Usage**: Configurable limits with strict enforcement

## References

- **ResourceLifetimeAnalyzer.h** - Lifetime tracking for aliasing
- **ResourceBudgetManager.h** - Budget tracking and enforcement
- **StackResourceTracker.h** - Stack allocation tracking
- **Resource.h** - Base resource types
