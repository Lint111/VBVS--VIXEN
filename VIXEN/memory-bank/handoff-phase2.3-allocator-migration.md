# Handoff: Phase 2.3 - Allocator Migration + Batched Uploads

**Date:** 2026-01-02
**Branch:** `production/sprint-5-cashsystem-robustness`
**Last Commit:** `a917523` - refactor(CashSystem): Migrate VoxelSceneCacher staging buffer to AllocateBufferTracked

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

### Phase 2.3.0: VoxelSceneCacher Staging Buffer ✅

**File Modified:** `CashSystem/src/VoxelSceneCacher.cpp`

**Changes:**
- `UploadBufferData()` now uses `AllocateBufferTracked()` for staging buffer
- Uses `MapBufferTracked()`/`UnmapBufferTracked()` for memory operations
- `FreeBufferTracked()` for cleanup

**Note:** Main scene buffers still use shared memory pattern (7 buffers bound to single VkDeviceMemory at offsets). This is intentional optimization - not migrated.

---

## Remaining Tasks

### Phase 2.3.0: Migrate Remaining Cachers

#### 1. AccelerationStructureCacher (4 direct allocations) - REQUIRES HEADER CHANGES
- **File:** `CashSystem/src/AccelerationStructureCacher.cpp`
- **Header:** `CashSystem/include/AccelerationStructureCacher.h`
- **Buffers to migrate in `AccelerationStructureData`:**
  - blasBuffer + blasMemory → BufferAllocation
  - tlasBuffer + tlasMemory → BufferAllocation
  - scratchBuffer + scratchMemory → BufferAllocation (temp, freed after build)
  - instanceBuffer + instanceMemory → BufferAllocation (host-visible)
- **Note:** All need device address support (already handled by allocator when `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` in usage)
- **Complexity:** Medium - need to update struct, BuildBLAS(), BuildTLAS(), and Cleanup()

#### 2. MeshCacher (2 direct allocations) - REQUIRES HEADER CHANGES
- **File:** `CashSystem/src/MeshCacher.cpp`
- **Buffers to migrate:**
  - vertexBuffer + vertexMemory
  - indexBuffer + indexMemory
- **Note:** Uses host-visible memory, has `CreateBuffer()` helper that can be refactored
- **Header change:** `MeshWrapper` struct needs to use `BufferAllocation` instead of raw VkBuffer/VkDeviceMemory

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
