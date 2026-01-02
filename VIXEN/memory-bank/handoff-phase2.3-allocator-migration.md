# Handoff: Phase 2.3 - Allocator Migration + Batched Uploads

**Date:** 2026-01-02
**Branch:** `production/sprint-5-cashsystem-robustness`
**Last Commit:** `338a4e4` - refactor(CashSystem): Update AABB data handling to use BufferAllocation

---

## Completed Work

### Phase 2.3.0: VoxelAABBCacher Migration ✅

**Files Modified:**
| File | Changes |
|------|---------|
| `CashSystem/include/VoxelAABBCacher.h` | Replaced `VkBuffer`/`VkDeviceMemory` pairs with `BufferAllocation` + accessor methods |
| `CashSystem/src/VoxelAABBCacher.cpp` | Rewrote `UploadToGPU()` and `UploadBufferData()` to use `AllocateBufferTracked()` |
| `CashSystem/src/AccelerationStructureCacher.cpp` | Updated to use `GetAABBBuffer()`/`GetAABBDeviceAddress()` accessors |
| `CashSystem/include/AccelerationStructureCacher.h` | Updated `ComputeHash()` to use accessor |
| `CashSystem/src/CacherAllocationHelpers.cpp` | Added device address support (`VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`) |
| `RenderGraph/src/Nodes/VoxelAABBConverterNode.cpp` | Updated to use accessor methods |

**Key Patterns Established:**
```cpp
// Allocation with device address support (automatic based on usage flags)
auto alloc = AllocateBufferTracked(
    size,
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | ...,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    "debug_name"
);

// Exception-safe multi-buffer allocation pattern
auto alloc1 = AllocateBufferTracked(...);
if (!alloc1) throw ...;

auto alloc2 = AllocateBufferTracked(...);
if (!alloc2) {
    FreeBufferTracked(*alloc1);  // Cleanup on failure
    throw ...;
}

// Only assign after all succeed
data.allocation1 = *alloc1;
data.allocation2 = *alloc2;

// Cleanup via FreeBufferTracked
FreeBufferTracked(data.allocation1);
```

---

## Remaining Tasks

### Phase 2.3.0: Migrate Remaining Cachers

#### 1. VoxelSceneCacher (2 direct allocations)
- **File:** `CashSystem/src/VoxelSceneCacher.cpp`
- **Pattern:** Same as VoxelAABBCacher - replace raw Vulkan allocs with `AllocateBufferTracked()`
- **Note:** May have simpler buffer requirements (no device address)

#### 2. AccelerationStructureCacher (4 direct allocations)
- **File:** `CashSystem/src/AccelerationStructureCacher.cpp`
- **Buffers to migrate:**
  - BLAS buffer
  - TLAS buffer
  - Scratch buffer
  - Instance buffer
- **Note:** These require device address support (already in allocator)

#### 3. MeshCacher (1 direct allocation)
- **File:** `CashSystem/src/MeshCacher.cpp`
- **Pattern:** Straightforward migration

### Phase 2.3.1: Pass Allocator from Nodes to Cachers

**Problem:** Currently cachers have `m_budgetManager = nullptr` so they fall back to direct allocation. Nodes that create cachers need to pass the allocator.

**Required Changes:**
1. Nodes access allocator via RenderGraph's resource management
2. Pass allocator to cacher during registration/initialization
3. Update `TypedCacher::Initialize()` or add `SetAllocator()` method

**Example flow:**
```
RenderGraph
  └─> Node (has access to allocator)
        └─> RegisterCacher(cacher, allocator)
              └─> cacher->SetBudgetManager(allocator)
```

### Phase 2.3.2: Create StagingBufferPool

**Purpose:** Reuse staging buffers instead of allocating/freeing per upload

**Design:**
- Pool of host-visible buffers
- Size tiers (e.g., 64KB, 256KB, 1MB, 4MB)
- Thread-safe checkout/return
- Auto-resize based on demand

### Phase 2.3.3: Create BatchedUploader

**Purpose:** Batch multiple buffer uploads into single command buffer submission

**Design:**
- Queue uploads during frame
- Single vkQueueSubmit with all copies
- Fence-based completion tracking
- Staging buffer reuse from pool

### Phase 2.3.4: TypedCacher Integration

**Purpose:** Make allocator access seamless in TypedCacher base class

**Changes:**
- Add allocator getter in TypedCacher
- Ensure all derived cachers use it
- Remove fallback paths after migration complete

---

## Architecture Reference

```
IMemoryAllocator (interface)
├── DirectAllocator (implemented in Phase 2.1)
└── VMAAllocator (future)
        │
        ▼
DeviceBudgetManager (wraps allocator + budget tracking)
        │
        ▼
CacherAllocationHelpers (bridge for cachers)
        │
        ▼
TypedCacher::AllocateBufferTracked()
        │
        ▼
VoxelAABBCacher, VoxelSceneCacher, AccelerationStructureCacher, MeshCacher
```

---

## Build & Test

```bash
# Build
cmake --build build --config Debug --parallel 16

# Run (visual test - no unit tests for cacher yet)
./binaries/vixen_benchmark.exe
```

---

## Notes

- Device address flags handled automatically when `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` is in usage
- Exception safety pattern established - use for all multi-buffer allocations
- Staging buffer also uses allocator now (not pooled yet)
