# Unified Resource Management: Capability Gaps Integration

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Design Enhancement
**Related**: UnifiedResourceManagement.md, ArchitecturalReview-CurrentState.md

---

## Executive Summary

The architectural review identified several capability gaps in VIXEN's resource management. This document integrates these missing features into the unified resource management system:

1. **Resource Aliasing** - Memory reuse for non-overlapping lifetimes
2. **Resource Lifetime Management** - Transient vs persistent resources
3. **Per-Element State Tracking** - Fine-grained dirty marking
4. **Budget Enforcement** - Memory limits and warnings
5. **Allocation Timing** - Deferred and lazy allocation strategies

---

## Capability Gap 1: Resource Aliasing 🔴

### Problem (From ArchitecturalReview-CurrentState.md:413)

**Current State**: "No memory aliasing: Not critical for research (Phase E deferred)"

**What is Resource Aliasing?**
Reusing the same memory allocation for multiple resources with non-overlapping lifetimes.

**Example**:
```
Frame timeline:
   0ms ----------- 16ms ----------- 33ms
   [Resource A used] [Resource B used]
         ^               ^
         |               |
   Same memory!    Same memory!
```

If Resource A is only used 0-10ms and Resource B is only used 15-25ms, they can share the same VkDeviceMemory allocation.

**Benefits**:
- **50-80% reduction** in VRAM usage for transient resources
- **Faster allocation** (reuse instead of VkAllocateMemory)
- **Less fragmentation** (fewer unique allocations)

### Solution: Integrated Aliasing in UnifiedRM

**New Features**:

```cpp
// Extended UnifiedRM with aliasing support
template<typename Owner, typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    // NEW: Aliasing configuration
    struct AliasingHint {
        bool canAlias = true;           // Allow aliasing
        uint32_t lifetimeStart = 0;     // Frame number or pass index
        uint32_t lifetimeEnd = UINT32_MAX; // When resource is no longer needed
        size_t minAlignment = 256;      // Minimum alignment requirement
        std::string aliasGroup = "";    // Group ID for aliasing pools
    };

    void SetAliasingHint(const AliasingHint& hint) {
        aliasingHint_ = hint;
        SetMetadata("aliasing_hint", hint);
    }

    const AliasingHint& GetAliasingHint() const {
        return aliasingHint_;
    }

    // NEW: Check if aliasing is possible with another resource
    bool CanAliasW(const UnifiedRM_Base* other) const {
        // Check non-overlapping lifetimes
        auto otherHint = dynamic_cast<const UnifiedRM*>(other)->GetAliasingHint();

        return (aliasingHint_.canAlias && otherHint.canAlias) &&
               (aliasingHint_.lifetimeEnd < otherHint.lifetimeStart ||
                otherHint.lifetimeEnd < aliasingHint_.lifetimeStart) &&
               (aliasingHint_.aliasGroup == otherHint.aliasGroup);
    }

private:
    AliasingHint aliasingHint_;
};
```

**Aliasing Manager Integration**:

```cpp
class UnifiedBudgetManager {
public:
    // NEW: Aliasing pools
    struct AliasingPool {
        std::string groupID;
        VkDeviceMemory memory;
        size_t totalSize;
        std::vector<UnifiedRM_Base*> aliasedResources;
        std::vector<std::pair<uint32_t, uint32_t>> lifetimes; // start, end

        bool CanFit(size_t size, uint32_t start, uint32_t end) const {
            // Check if new resource fits non-overlapping with existing
            for (size_t i = 0; i < lifetimes.size(); ++i) {
                auto [existingStart, existingEnd] = lifetimes[i];

                // Check overlap
                if (!(end < existingStart || start > existingEnd)) {
                    return false; // Overlaps - can't alias
                }
            }
            return totalSize >= size;
        }
    };

    // Aliasing API
    VkDeviceMemory RequestAliasingMemory(
        UnifiedRM_Base* resource,
        size_t size,
        const UnifiedRM_Base::AliasingHint& hint
    );

    void ReleaseAliasingMemory(UnifiedRM_Base* resource);

    // Report aliasing efficiency
    struct AliasingReport {
        size_t totalPools;
        size_t totalMemoryAllocated;
        size_t totalMemoryIfNoAliasing;
        float savingsPercentage;
    };

    AliasingReport GetAliasingReport() const;

private:
    std::unordered_map<std::string, AliasingPool> aliasingPools_;
};
```

**Usage Example**:

```cpp
class VoxelRayMarchNode : public TypedNode<VoxelRayMarchConfig> {
    // Transient depth buffer - only used during this pass
    UnifiedRM<VoxelRayMarchNode, VkImage> tempDepthBuffer_{
        this, &VoxelRayMarchNode::tempDepthBuffer_
    };

    // Another transient used later in different pass
    UnifiedRM<VoxelRayMarchNode, VkImage> tempColorTarget_{
        this, &VoxelRayMarchNode::tempColorTarget_
    };

    void SetupImpl(TypedSetupContext& ctx) override {
        // Configure aliasing for temp depth
        UnifiedRM::AliasingHint depthHint;
        depthHint.canAlias = true;
        depthHint.lifetimeStart = 0;  // Pass 0
        depthHint.lifetimeEnd = 1;    // Done after pass 1
        depthHint.aliasGroup = "transient_images";
        tempDepthBuffer_.SetAliasingHint(depthHint);

        // Configure aliasing for temp color
        UnifiedRM::AliasingHint colorHint;
        colorHint.canAlias = true;
        colorHint.lifetimeStart = 3;  // Pass 3
        colorHint.lifetimeEnd = 4;    // Done after pass 4
        colorHint.aliasGroup = "transient_images";
        tempColorTarget_.SetAliasingHint(colorHint);

        // Register with budget manager
        tempDepthBuffer_.RegisterWithBudget(ctx.budgetManager);
        tempColorTarget_.RegisterWithBudget(ctx.budgetManager);

        // Budget manager automatically aliases them!
        // They share the same VkDeviceMemory allocation
    }
};
```

**Aliasing Report**:
```
=== Resource Aliasing Report ===
Aliasing Pools: 3
  Pool "transient_images":
    Total Memory:     512 MB
    Resources:        12 (aliased)
    If No Aliasing:   2.4 GB
    Savings:          78.7% (1.9 GB saved)

  Pool "per_frame_buffers":
    Total Memory:     128 MB
    Resources:        6 (aliased)
    If No Aliasing:   384 MB
    Savings:          66.7% (256 MB saved)

Total Savings: 2.2 GB (72.5%)
================================
```

---

## Capability Gap 2: Resource Lifetime Management 🔴

### Problem (From ResourceStateManagement.md)

**Current State**: Resources don't distinguish between transient (temporary) and persistent (long-lived) allocations.

**What is Missing?**
- No automatic cleanup of transient resources
- No optimization for short-lived allocations
- No ring buffer for per-frame resources

**Example Use Cases**:
1. **Transient**: Temporary depth buffer for single pass → deallocate immediately
2. **Persistent**: Pipeline objects → keep until app shutdown
3. **PerFrame**: UBOs that rotate per frame-in-flight → ring buffer

### Solution: Lifetime Policies in UnifiedRM

**Lifetime Enum** (Already in ResourceState.h):
```cpp
enum class ResourceState : uint32_t {
    Clean         = 0,
    Dirty         = 1 << 0,
    Deleted       = 1 << 1,
    Stale         = 1 << 2,
    Locked        = 1 << 3,
    Transient     = 1 << 4,   // ← Already exists!
    Persistent    = 1 << 5,   // ← Already exists!
};
```

**Enhanced Lifetime Support**:

```cpp
template<typename Owner, typename T>
class UnifiedRM : public UnifiedRM_Base {
public:
    enum class LifetimePolicy {
        Persistent,      // Explicit cleanup only
        Transient,       // Auto-cleanup after use
        PerFrame,        // Ring buffer (multiple copies)
        Pooled,          // Return to pool for reuse
        Lazy             // Allocate on first access, never free
    };

    void SetLifetimePolicy(LifetimePolicy policy) {
        lifetimePolicy_ = policy;

        // Update resource state flags
        if (policy == LifetimePolicy::Transient) {
            AddState(ResourceState::Transient);
        } else if (policy == LifetimePolicy::Persistent) {
            AddState(ResourceState::Persistent);
        }
    }

    LifetimePolicy GetLifetimePolicy() const {
        return lifetimePolicy_;
    }

    // For transient resources: mark for auto-cleanup
    void MarkUsed() {
        if (lifetimePolicy_ == LifetimePolicy::Transient) {
            usageCount_++;
            SetMetadata("last_used_frame", currentFrame_);
        }
    }

    // Check if transient resource can be freed
    bool CanCleanupTransient(uint32_t currentFrame) const {
        if (lifetimePolicy_ != LifetimePolicy::Transient) {
            return false;
        }

        uint32_t lastUsed = GetMetadataOr<uint32_t>("last_used_frame", 0);
        return (currentFrame - lastUsed) > 2; // Unused for 2+ frames
    }

private:
    LifetimePolicy lifetimePolicy_ = LifetimePolicy::Persistent;
    uint32_t usageCount_ = 0;
    uint32_t currentFrame_ = 0;
};
```

**Per-Frame Resource Support**:

```cpp
// Helper for ring-buffer resources
template<typename Owner, typename T>
class PerFrameRM {
public:
    PerFrameRM(
        Owner* owner,
        typename UnifiedRM<Owner, T>::MemberPtr memberPtr,
        uint32_t frameCount = MAX_FRAMES_IN_FLIGHT
    ) : frameCount_(frameCount) {
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Create UnifiedRM for each frame
            frames_[i] = std::make_unique<UnifiedRM<Owner, T>>(
                owner, memberPtr
            );
            frames_[i]->SetLifetimePolicy(UnifiedRM::LifetimePolicy::PerFrame);
            frames_[i]->SetMetadata("frame_index", i);
        }
    }

    UnifiedRM<Owner, T>& GetFrame(uint32_t frameIndex) {
        return *frames_[frameIndex % frameCount_];
    }

    uint32_t GetFrameCount() const { return frameCount_; }

private:
    std::array<std::unique_ptr<UnifiedRM<Owner, T>>, MAX_FRAMES_IN_FLIGHT> frames_;
    uint32_t frameCount_;
};
```

**Usage Example**:

```cpp
class DescriptorSetNode {
    // Transient temp buffer for descriptor writes
    UnifiedRM<DescriptorSetNode, BoundedArray<VkWriteDescriptorSet, 32>> tempWrites_{
        this, &DescriptorSetNode::tempWrites_,
        AllocStrategy::Stack
    };

    // Per-frame UBO
    PerFrameRM<DescriptorSetNode, VkBuffer> perFrameUBO_{
        this, &DescriptorSetNode::perFrameUBO_,
        MAX_FRAMES_IN_FLIGHT
    };

    // Persistent pipeline
    UnifiedRM<DescriptorSetNode, VkPipeline> pipeline_{
        this, &DescriptorSetNode::pipeline_
    };

    void SetupImpl(TypedSetupContext& ctx) override {
        // Configure lifetimes
        tempWrites_.SetLifetimePolicy(UnifiedRM::LifetimePolicy::Transient);
        pipeline_.SetLifetimePolicy(UnifiedRM::LifetimePolicy::Persistent);
        // perFrameUBO_ automatically PerFrame from PerFrameRM
    }

    void ExecuteImpl(TypedExecuteContext& ctx) override {
        // Use transient
        tempWrites_.Value().Clear();
        tempWrites_.Value().Add(...);
        tempWrites_.MarkUsed();  // Track usage for auto-cleanup

        // Use per-frame UBO
        auto& ubo = perFrameUBO_.GetFrame(ctx.currentFrame);
        UpdateUBO(ubo.Value());

        // Use persistent pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.Value());
    }
};
```

**Automatic Cleanup**:

```cpp
void UnifiedBudgetManager::EndFrame() {
    currentFrame_++;

    // Auto-cleanup transient resources
    for (auto* resource : registeredResources_) {
        if (resource->CanCleanupTransient(currentFrame_)) {
            LOG_DEBUG("Auto-cleanup transient: " + resource->GetDebugName());
            // Reset the resource (triggers deallocation)
            // resource->Reset(); // Would need type-erased reset
        }
    }
}
```

---

## Capability Gap 3: Per-Element State Tracking 🟡

### Problem (From ResourceStateManagement.md:1-20)

**Current State**: When working with resource arrays (swapchain images), the entire array is recompiled when any single element changes.

**Example**:
```cpp
// Window resize → SwapChain recreates ALL 3 images
// → Framebuffer recreates ALL 3 framebuffers
// Even if only image #2 changed!
```

**Performance Impact**:
- Unnecessary Vulkan object recreation
- O(N) recompilation instead of O(1)
- Full dependency chain invalidation

### Solution: Fine-Grained Dirty Tracking

**Already Supported in ResourceState.h**:
```cpp
enum class ResourceState : uint32_t {
    Clean         = 0,
    Dirty         = 1 << 0,   // ✅ Already exists!
    Deleted       = 1 << 1,   // ✅ Already exists!
    Stale         = 1 << 2,   // ✅ Already exists!
    Locked        = 1 << 3,   // ✅ Already exists!
    // ...
};
```

**UnifiedRM Integration**:

```cpp
template<typename Owner, typename T>
class UnifiedRM {
public:
    // State management (already implemented)
    bool IsDirty() const { return Has(ResourceState::Dirty); }
    void MarkDirty() { AddState(ResourceState::Dirty); }
    void MarkClean() { RemoveState(ResourceState::Dirty); }

    // NEW: Generation-based staleness detection
    bool IsStale(uint64_t expectedGeneration) const {
        return generation_ != expectedGeneration;
    }

    void MarkStale() {
        AddState(ResourceState::Stale);
    }

    // NEW: Compare with another resource for selective update
    bool HasChanged(const T& newValue) const {
        if (!storage_.has_value()) return true;
        return !(*storage_ == newValue); // Requires operator==
    }
};
```

**Selective Recompilation Example**:

```cpp
class FramebufferNode {
    std::array<UnifiedRM<FramebufferNode, VkFramebuffer>, MAX_SWAPCHAIN_IMAGES> framebuffers_;

    void CompileImpl(TypedCompileContext& ctx) override {
        size_t imageCount = GetInputCount(COLOR_ATTACHMENTS);

        for (size_t i = 0; i < imageCount; ++i) {
            auto& inputRes = GetInputResource(COLOR_ATTACHMENTS, i);

            // Check if THIS specific input changed
            if (inputRes->IsDirty() || !framebuffers_[i].Ready()) {
                LOG_DEBUG("Rebuilding framebuffer " + std::to_string(i));

                // Destroy old framebuffer (if exists)
                if (framebuffers_[i].Ready()) {
                    vkDestroyFramebuffer(device, framebuffers_[i].Value(), nullptr);
                }

                // Recreate ONLY this framebuffer
                VkImageView colorView = inputRes->GetValue<VkImageView>();
                VkFramebuffer newFB = CreateFramebuffer(colorView);
                framebuffers_[i].Set(newFB);

                // Mark output as dirty for downstream
                framebuffers_[i].MarkDirty();
            } else {
                // No change - skip this one
                LOG_DEBUG("Framebuffer " + std::to_string(i) + " unchanged");
                framebuffers_[i].MarkClean();
            }
        }
    }
};
```

**Benefit**: If only swapchain image #2 changes, only framebuffer #2 is rebuilt. Images #0 and #1 are untouched.

---

## Capability Gap 4: Budget Enforcement 🟡

### Problem (From ArchitecturalReview-CurrentState.md:418)

**Current State**: "No memory budget tracking: Not critical for research"

**What's Missing**:
- No VRAM budget limits
- No automatic cleanup when approaching limits
- No priority-based eviction

### Solution: Already Designed in UnifiedBudgetManager!

**Features** (from UnifiedResourceManagement.md):

```cpp
class UnifiedBudgetManager {
public:
    enum class BudgetType {
        HostStack,        // Stack allocations
        HostHeap,         // Heap allocations
        DeviceLocal,      // GPU VRAM
        HostVisible,      // Mapped memory
    };

    struct ResourceBudget {
        uint64_t maxBytes = 0;        // Hard limit
        uint64_t warningThreshold = 0; // Soft limit
        bool strict = false;           // Fail allocation if over?
    };

    // Set budgets
    void SetBudget(BudgetType type, const ResourceBudget& budget);

    // Try allocation (respects budget)
    bool TryAllocate(BudgetType type, size_t bytes, std::string_view debugName);

    // Query state
    bool IsOverBudget(BudgetType type) const;
    bool IsNearWarningThreshold(BudgetType type) const;

    // NEW: Priority-based eviction
    void EvictLowestPriority(BudgetType type, size_t bytesNeeded);
};
```

**Priority-Based Eviction**:

```cpp
template<typename Owner, typename T>
class UnifiedRM {
public:
    enum class Priority : uint8_t {
        Critical = 0,     // Never evict (pipelines, shaders)
        High = 1,         // Evict only if necessary (active scene data)
        Medium = 2,       // Evict when approaching limit (cached textures)
        Low = 3,          // Evict first (temp buffers, debug resources)
    };

    void SetPriority(Priority p) {
        priority_ = p;
        SetMetadata("priority", static_cast<uint8_t>(p));
    }

    Priority GetPriority() const { return priority_; }

    // For eviction scoring
    uint64_t GetEvictionScore() const {
        // Higher score = evict first
        uint64_t score = static_cast<uint64_t>(priority_) << 48;  // Priority dominant
        score |= (UINT64_MAX - lastAccessFrame_) << 32;            // Least recently used
        score |= (UINT64_MAX - usageCount_) << 16;                 // Least frequently used
        return score;
    }

private:
    Priority priority_ = Priority::Medium;
    uint64_t lastAccessFrame_ = 0;
    uint64_t usageCount_ = 0;
};
```

**Automatic Eviction**:

```cpp
void UnifiedBudgetManager::TryAllocate(BudgetType type, size_t bytes) {
    auto usage = GetUsage(type);
    auto budget = GetBudget(type);

    if (usage.currentBytes + bytes > budget.maxBytes) {
        // Over budget - try eviction
        LOG_WARN("Budget exceeded, attempting eviction...");

        size_t bytesNeeded = (usage.currentBytes + bytes) - budget.maxBytes;
        EvictLowestPriority(type, bytesNeeded);

        // Retry
        usage = GetUsage(type);
        if (usage.currentBytes + bytes > budget.maxBytes) {
            if (budget.strict) {
                throw std::runtime_error("Budget exceeded even after eviction");
            } else {
                LOG_ERROR("Soft budget exceeded");
            }
        }
    }

    // Allocate...
}

void UnifiedBudgetManager::EvictLowestPriority(BudgetType type, size_t bytesNeeded) {
    // Sort resources by eviction score
    std::vector<UnifiedRM_Base*> candidates;
    for (auto* res : registeredResources_) {
        if (res->GetMemoryLocation() == type) {
            candidates.push_back(res);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const auto* a, const auto* b) {
            return a->GetEvictionScore() > b->GetEvictionScore();
        });

    // Evict until we have enough space
    size_t freedBytes = 0;
    for (auto* res : candidates) {
        if (freedBytes >= bytesNeeded) break;

        LOG_INFO("Evicting: " + res->GetDebugName() +
                 " (" + FormatBytes(res->GetAllocatedBytes()) + ")");

        freedBytes += res->GetAllocatedBytes();
        // res->Reset(); // Would need type-erased reset API
    }
}
```

---

## Capability Gap 5: Allocation Timing ⚪

### Problem

**What's Missing**: No support for deferred/lazy allocation strategies.

**Use Cases**:
1. **Lazy Allocation**: Don't allocate until first access
2. **Deferred Allocation**: Allocate in batch during compile phase
3. **Pre-Allocation**: Reserve memory upfront for predictable workloads

### Solution: Allocation Timing Policies

```cpp
template<typename Owner, typename T>
class UnifiedRM {
public:
    enum class AllocationTiming {
        Immediate,    // Allocate on Set()
        Lazy,         // Allocate on first Value() access
        Deferred,     // Allocate during CompilePhase
        Pooled        // Request from pre-allocated pool
    };

    void SetAllocationTiming(AllocationTiming timing) {
        allocationTiming_ = timing;
    }

    T& Value() override {
        // Lazy allocation
        if (allocationTiming_ == AllocationTiming::Lazy && !allocated_) {
            AllocateNow();
            allocated_ = true;
        }

        if (!Ready()) {
            throw std::runtime_error("UnifiedRM::Value() on unready resource");
        }
        return *storage_;
    }

private:
    AllocationTiming allocationTiming_ = AllocationTiming::Immediate;
    bool allocated_ = false;

    void AllocateNow() {
        // Perform actual allocation
        // (Device memory, buffer creation, etc.)
    }
};
```

---

## Summary: UnifiedRM Feature Matrix

| Feature | Status | Priority | Benefits |
|---------|--------|----------|----------|
| **Resource Aliasing** | 🟢 Designed | 🔴 High | 50-80% VRAM savings |
| **Lifetime Management** | 🟢 Designed | 🔴 High | Auto-cleanup transients |
| **Per-Element Dirty** | 🟢 Ready | 🟡 Medium | Selective recompilation |
| **Budget Enforcement** | 🟢 Designed | 🟡 Medium | Prevent OOM crashes |
| **Allocation Timing** | 🟢 Designed | ⚪ Low | Batch allocation optimization |
| **Type-Safe Identity** | ✅ Implemented | 🔴 High | Compile-time safety |
| **State Management** | ✅ Implemented | 🔴 High | RM state tracking |
| **Stack Tracking** | ✅ Implemented | 🔴 High | Stack overflow prevention |

---

## Implementation Roadmap

### Phase 1: Core Unification (Week 1-2)
- [ ] Implement UnifiedRM base class
- [ ] Implement UnifiedBudgetManager
- [ ] Integrate PerFrameResourceManager
- [ ] Basic tests

### Phase 2: Aliasing Support (Week 3)
- [ ] AliasingHint structure
- [ ] AliasingPool in budget manager
- [ ] Lifetime-based aliasing logic
- [ ] Aliasing report generation

### Phase 3: Lifetime Policies (Week 4)
- [ ] LifetimePolicy enum
- [ ] PerFrameRM helper class
- [ ] Transient auto-cleanup
- [ ] Pooled resource support

### Phase 4: Advanced Features (Week 5)
- [ ] Priority-based eviction
- [ ] Allocation timing policies
- [ ] Fine-tuned per-element dirty tracking
- [ ] Complete migration guide

---

**End of Document**
