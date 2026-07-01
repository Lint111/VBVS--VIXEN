# Sprint 4: Resource Manager Integration - Phase A & B Technical Reference

## Overview

This document provides a detailed technical reference for the Resource Manager integration implemented in Sprint 4, covering Phase A (Foundation) and Phase B/B+ (Resource Lifetime & GPU Aliasing).

**Branch:** `production/sprint-4-resource-manager`
**Test Count:** 138 tests passing
**Key Commits:**
- `37d74ee` - Phase A: Memory management foundation
- `0c33531` - BudgetBridge for Host↔Device staging
- `2ce0880` - Phase B.1: Reference counting
- `ceceb94` - Phase B.2/B.3: Lifetime scopes + RenderGraph integration
- `63cd928` - Phase B+: GPU memory aliasing

---

## Phase A: Foundation

### A.1: IMemoryAllocator Interface

**File:** `include/Core/IMemoryAllocator.h`

**Design Goal:** Abstract GPU memory allocation to support multiple backends.

#### Key Design Choices

1. **Interface Abstraction over Concrete Types**
   ```cpp
   class IMemoryAllocator {
   public:
       virtual std::expected<BufferAllocation, AllocationError>
       AllocateBuffer(const BufferAllocationRequest& request) = 0;
       // ...
   };
   ```
   - Enables swapping VMA for direct Vulkan allocation (testing/fallback)
   - Enables mock allocator for unit tests without GPU

2. **std::expected for Error Handling**
   ```cpp
   [[nodiscard]] virtual std::expected<BufferAllocation, AllocationError>
   AllocateBuffer(const BufferAllocationRequest& request) = 0;
   ```
   - C++23 feature for explicit error handling
   - No exceptions in hot paths
   - Caller must handle errors (can't ignore due to `[[nodiscard]]`)

3. **Opaque Allocation Handle**
   ```cpp
   using AllocationHandle = void*;
   ```
   - Implementation-defined pointer (VmaAllocation, custom record, etc.)
   - Avoids exposing VMA types in public headers
   - Allows different backends without API changes

4. **Request Structs with Designated Initializers**
   ```cpp
   BufferAllocationRequest request{
       .size = 1024,
       .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
       .location = MemoryLocation::DeviceLocal,
       .debugName = "VertexBuffer"
   };
   ```
   - Clear, self-documenting API
   - Optional fields have sensible defaults
   - Easy to extend without breaking existing code

---

### A.2: VMAAllocator Implementation

**Files:** `include/Core/VMAAllocator.h`, `src/Core/VMAAllocator.cpp`

**Design Goal:** Production-quality allocator using Vulkan Memory Allocator (VMA).

#### Key Techniques

1. **Forward Declaration to Hide VMA**
   ```cpp
   // In header - no vk_mem_alloc.h include
   struct VmaAllocator_T;
   typedef VmaAllocator_T* VmaAllocator;
   ```
   - Faster compilation (VMA header is massive)
   - Implementation detail hidden from consumers

2. **VMA_IMPLEMENTATION in Single TU**
   ```cpp
   // Only in VMAAllocator.cpp
   #define VMA_IMPLEMENTATION
   #include <vk_mem_alloc.h>
   ```
   - VMA is header-only library
   - Implementation compiled exactly once

3. **Thread-Safe Allocation Tracking**
   ```cpp
   mutable std::mutex mutex_;
   std::unordered_map<void*, AllocationRecord> allocationRecords_;
   ```
   - VMA itself is thread-safe
   - Additional tracking for budget integration

4. **Memory Location Mapping**
   ```cpp
   switch (request.location) {
       case MemoryLocation::DeviceLocal:
           allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
           break;
       case MemoryLocation::HostVisible:
           allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
           allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                            VMA_ALLOCATION_CREATE_MAPPED_BIT;
           break;
   }
   ```
   - Abstract memory location hints
   - VMA handles actual memory type selection

---

### A.3: DirectAllocator (Fallback)

**Files:** `include/Core/DirectAllocator.h`, `src/Core/DirectAllocator.cpp`

**Design Goal:** Simple allocator for testing without VMA dependency.

#### Key Differences from VMA

| Aspect | VMAAllocator | DirectAllocator |
|--------|--------------|-----------------|
| Suballocation | Yes | No (1 alloc per resource) |
| Memory overhead | Low | High for small resources |
| Defragmentation | Supported | Not supported |
| Null device support | No | Yes (for unit tests) |

#### Null Device Pattern
```cpp
DirectAllocator::DirectAllocator(VkPhysicalDevice physicalDevice, VkDevice device, ...)
    : physicalDevice_(physicalDevice), device_(device) {
    if (physicalDevice_ != VK_NULL_HANDLE) {
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties_);
    }
    // Works with null handles for testing
}
```

---

### A.4: ResourceBudgetManager

**File:** `include/Core/ResourceBudgetManager.h`

**Design Goal:** Track resource usage against configurable budgets.

#### Budget Types
```cpp
enum class BudgetResourceType : uint8_t {
    DeviceMemory,   // GPU VRAM
    HostMemory,     // CPU RAM
    StagingMemory,  // Upload buffers
    Descriptors,    // Descriptor pool
    UserDefined     // Custom budgets
};
```

#### Key Design: Soft vs Hard Limits
```cpp
struct ResourceBudget {
    uint64_t maxBytes;        // Hard limit
    uint64_t warningBytes;    // Soft warning threshold
    bool strictEnforcement;   // Fail allocations over budget?
};
```
- Warning threshold for proactive monitoring
- Strict enforcement optional (some systems need best-effort)

---

### A.5: DeviceBudgetManager

**File:** `include/Core/DeviceBudgetManager.h`

**Design Goal:** Facade combining allocator + budget tracking for GPU resources.

#### Auto-Detection Pattern
```cpp
if (deviceMemory == 0 && physicalDevice != VK_NULL_HANDLE) {
    deviceMemory = ResourceBudgetManager::DetectDeviceMemoryBytes(physicalDevice);
    deviceMemory = static_cast<uint64_t>(deviceMemory * 0.8);  // 80% headroom
}
```
- Automatic budget sizing
- Leaves 20% for OS/driver overhead

---

### A.6: BudgetBridge

**File:** `include/Core/BudgetBridge.h`

**Design Goal:** Coordinate Host↔Device staging with unified budget view.

#### Key Pattern: Cross-Manager Callback
```cpp
void RegisterTransferCallback(TransferCallback callback) {
    std::lock_guard lock(mutex_);
    transferCallback_ = std::move(callback);
}
```
- Decouples staging logic from budget tracking
- Application defines transfer policy

---

## Phase B: Resource Lifetime

### B.1: SharedResource & Reference Counting

**File:** `include/Core/SharedResource.h`

**Design Goal:** Intrusive reference counting with deferred destruction.

#### Intrusive vs Non-Intrusive Refcounting

| Approach | Memory | Cache | Flexibility |
|----------|--------|-------|-------------|
| `std::shared_ptr` | Extra control block | Poor (separate alloc) | High |
| Intrusive (`RefCountBase`) | In-object | Excellent | Lower |

We chose **intrusive** for GPU resources:
- Cache-friendly (count in same cacheline as resource)
- No separate allocation for control block
- Matches Vulkan's handle-based design

#### Key Technique: Deferred Destruction
```cpp
template<typename T>
void SharedResourcePtr<T>::Release() {
    if (resource_ && resource_->Release()) {
        if (destructionQueue_ && frameCounter_) {
            destructionQueue_->AddGeneric(
                [res = resource_]() { delete res; },
                *frameCounter_
            );
        } else {
            delete resource_;
        }
        resource_ = nullptr;
    }
}
```
- Resources queued for destruction, not immediately deleted
- Destruction happens N frames later (after GPU finishes using)
- Avoids vkDeviceWaitIdle() stalls

#### ResourceScope Enum
```cpp
enum class ResourceScope : uint8_t {
    Transient,   // Single frame, can be aliased
    Persistent,  // Survives frames, manual release
    Shared       // Ref-counted, destroyed on last ref
};
```

---

### B.2: LifetimeScope & LifetimeScopeManager

**File:** `include/Core/LifetimeScope.h`

**Design Goal:** Group resources for bulk lifecycle management.

#### Scope Hierarchy Pattern
```cpp
class LifetimeScopeManager {
    LifetimeScope frameScope_;                           // Frame-level
    std::stack<std::unique_ptr<LifetimeScope>> scopeStack_;  // Nested scopes
};
```
- Frame scope always exists
- Nested scopes pushed/popped for passes

#### RAII ScopeGuard
```cpp
class ScopeGuard {
public:
    explicit ScopeGuard(LifetimeScopeManager& manager, std::string_view name)
        : manager_(manager), scope_(manager.BeginScope(name)) {}

    ~ScopeGuard() { manager_.EndScope(); }
};

// Usage:
{
    ScopeGuard shadowPass(manager, "ShadowPass");
    auto buffer = shadowPass.GetScope().CreateBuffer(request);
    // ... use buffer ...
}  // Buffer released here
```

#### Thread Safety Documentation
```cpp
/**
 * Thread-safety: NOT thread-safe. Use one scope per thread or external sync.
 */
class LifetimeScope { ... };
```
- Explicit contract in comments
- Scopes typically single-threaded (per-frame)

---

### B.3: RenderGraph Integration

**File:** `include/Core/RenderGraph.h`, `src/Core/RenderGraph.cpp`

**Design Goal:** Wire lifetime management into frame loop.

#### Integration Points
```cpp
VkResult RenderGraph::RenderFrame() {
    // Process deferred destructions from previous frames
    deferredDestruction.ProcessFrame(globalFrameIndex);

    // Begin frame scope if configured
    if (scopeManager_) {
        scopeManager_->BeginFrame();
    }

    // ... execute nodes ...

    // End frame scope
    if (scopeManager_) {
        scopeManager_->EndFrame();
    }

    globalFrameIndex++;
    return VK_SUCCESS;
}
```

#### Optional Configuration Pattern
```cpp
void SetLifetimeScopeManager(LifetimeScopeManager* manager) {
    scopeManager_ = manager;
}
```
- Manager is externally owned
- Null = disabled (backward compatible)

---

## Phase B+: GPU Memory Aliasing

### B+.1: Aliasing API

**Design Goal:** Allow multiple non-overlapping resources to share memory.

#### When to Use Aliasing
- Resources with non-overlapping lifetimes (e.g., shadow map then GBuffer)
- Transient render targets
- Large temporary compute buffers

#### Aliasing Flag Pattern
```cpp
struct BufferAllocationRequest {
    // ... existing fields ...
    bool allowAliasing = false;  // Allow this allocation to be aliased
};

struct BufferAllocation {
    // ... existing fields ...
    bool canAlias = false;   // Created with allowAliasing=true
    bool isAliased = false;  // This is an aliased resource (shares memory)
};
```

#### VMA Integration
```cpp
if (request.allowAliasing) {
    allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
}
```

#### Aliased Resource Creation
```cpp
std::expected<BufferAllocation, AllocationError>
CreateAliasedBuffer(const AliasedBufferRequest& request) {
    // 1. Verify source supports aliasing
    // 2. Create buffer without new allocation
    // 3. Bind to source's memory at offset
    // 4. Mark as aliased (doesn't own memory)
}
```

---

### B+.2: Aliasing-Aware Budget Tracking

**Design Goal:** Aliased resources should NOT double-count memory.

```cpp
std::expected<BufferAllocation, AllocationError>
DeviceBudgetManager::CreateAliasedBuffer(const AliasedBufferRequest& request) {
    // Aliased allocations do NOT consume additional budget
    auto result = allocator_->CreateAliasedBuffer(request);

    if (result) {
        aliasedAllocationCount_.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}
```

#### Statistics Tracking
```cpp
std::atomic<uint32_t> aliasedAllocationCount_{0};

uint32_t GetAliasedAllocationCount() const {
    return aliasedAllocationCount_.load(std::memory_order_relaxed);
}
```

---

## Scope Mapping Utility

**Added to:** `include/Core/SharedResource.h`

**Design Goal:** Bridge existing `SlotScope` metadata to memory management.

```cpp
inline ResourceScope SlotScopeToResourceScope(SlotScope slotScope) {
    switch (static_cast<uint8_t>(slotScope)) {
        case 2:  return ResourceScope::Transient;   // InstanceLevel
        case 1:  return ResourceScope::Transient;   // TaskLevel
        case 0:  return ResourceScope::Persistent;  // NodeLevel
        default: return ResourceScope::Transient;
    }
}
```

**Usage in SlotTaskContext:**
```cpp
ResourceScope GetResourceScope() const {
    return SlotScopeToResourceScope(resourceScope);
}
```

This avoids duplicate scope logic - the graph already knows resource scope from node configs.

---

## Testing Strategy

### Test Categories

| Category | Count | Purpose |
|----------|-------|---------|
| Unit: Allocator | ~20 | IMemoryAllocator contract |
| Unit: Budget | ~30 | ResourceBudgetManager logic |
| Unit: SharedResource | ~25 | Reference counting correctness |
| Unit: LifetimeScope | ~24 | Scope hierarchy |
| Unit: Aliasing | ~16 | Memory aliasing API |
| Integration | ~20 | Cross-component behavior |
| Stress | 1 | 32000 ops concurrent |

### Null Device Testing Pattern
```cpp
allocator_ = MemoryAllocatorFactory::CreateDirectAllocator(
    VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
```
- Tests run without GPU
- Validates logic independent of Vulkan
- Fast CI execution

---

## Key C++ Techniques Used

1. **std::expected (C++23)** - Error handling without exceptions
2. **Designated initializers** - Clear struct initialization
3. **[[nodiscard]]** - Enforce return value checking
4. **Intrusive refcounting** - Cache-efficient lifetime management
5. **RAII guards** - Automatic scope cleanup
6. **Atomic operations** - Lock-free counters
7. **Forward declarations** - Minimize header dependencies
8. **Factory pattern** - Allocator creation abstraction
9. **Facade pattern** - DeviceBudgetManager unifies allocator + budget

---

## Files Summary

| Phase | File | Purpose |
|-------|------|---------|
| A.1 | `IMemoryAllocator.h` | Allocator interface |
| A.2 | `VMAAllocator.h/cpp` | VMA implementation |
| A.3 | `DirectAllocator.h/cpp` | Fallback implementation |
| A.4 | `ResourceBudgetManager.h` | Budget tracking |
| A.5 | `DeviceBudgetManager.h/cpp` | GPU budget facade |
| A.6 | `BudgetBridge.h/cpp` | Host↔Device coordination |
| B.1 | `SharedResource.h` | Reference counting |
| B.2 | `LifetimeScope.h` | Scope management |
| B.3 | `RenderGraph.h/cpp` | Frame integration |
| B+.1-3 | Various | GPU aliasing |

---

## Next: Phase C (SlotTask Integration)

Phase C will wire budget awareness into task execution:
- C.1: Budget manager parameter in ExecuteParallel
- C.2: Budget enforcement during task execution
- C.3: Actual vs estimated resource tracking

The `SlotScopeToResourceScope()` mapping enables automatic scope selection based on existing graph metadata.
