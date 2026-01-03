---
title: Sprint 5 - CashSystem Robustness
aliases: [Sprint 5, CashSystem Robustness]
tags: [sprint, cashsystem, memory-safety, vulkan]
created: 2026-01-02
updated: 2026-01-02
status: in-progress
priority: P0
hacknplan-board: 651783
---

# Sprint 5: CashSystem Robustness

**Branch:** `production/sprint-5-cashsystem-robustness`
**Goal:** Memory safety, error handling, and code consolidation for CashSystem.
**Status:** 🟢 Phase 1 + Phase 2 + Phase 2.5 + Phase 3 COMPLETE (76h of 104h completed - 73%)

---

## Executive Summary

Sprint 5 focuses on hardening the CashSystem library:
- Fix memory safety issues (shared_ptr patterns)
- Add comprehensive Vulkan error checking
- Extract reusable buffer allocation
- Consolidate duplicate code patterns

**Total Effort:** 104 hours (16 tasks in HacknPlan)

---

## Phase 1: Critical Safety (P0) - 20h

### 1.1 VoxelAABBConverterNode Pointer Safety (4h) ✅ COMPLETE

**HacknPlan:** #185

**Solution:** Removed raw pointer dependency entirely. CachedAccelerationStructure now stores `sourceAABBCount` (metadata) instead of `aabbDataRef` (dangling pointer risk).

**Changes:**
- `AccelerationStructureCacher.h`: Replaced `const VoxelAABBData* aabbDataRef` with `uint32_t sourceAABBCount`
- `AccelerationStructureCacher.cpp`: Updated `Create()` and `Cleanup()` to use count instead of pointer
- `IsValid()` now checks `sourceAABBCount > 0` instead of dereferencing external pointer

**Files Changed:**
- `libraries/CashSystem/include/AccelerationStructureCacher.h:75-81`
- `libraries/CashSystem/src/AccelerationStructureCacher.cpp:126-130, 158-161`

### 1.2 VK_CHECK Macro Definition (4h) ✅ COMPLETE

**HacknPlan:** #214, #247

**Solution:** Added `VK_CHECK_LOG` and `VK_CHECK_RESULT` macros to `VulkanError.h`.

**Implementation:**
```cpp
#define VK_CHECK_LOG(expr, msg) \
    do { \
        VkResult _vk_result = (expr); \
        if (_vk_result != VK_SUCCESS) { \
            fprintf(stderr, "[VK_ERROR] %s: %s (VkResult: %d) at %s:%d\n", \
                    msg, VulkanError::resultToString(_vk_result).c_str(), \
                    static_cast<int>(_vk_result), __FILE__, __LINE__); \
            assert(false && "Vulkan call failed"); \
        } \
    } while(0)
```

**Files Changed:**
- `libraries/VulkanResources/include/error/VulkanError.h` (+50 lines)

### 1.3 Apply VK_CHECK to All Cachers (12h) ✅ COMPLETE

**HacknPlan:** #184

**Solution:** Applied VK_CHECK_LOG to 46 Vulkan calls across 3 files.

**Files Changed:**
- `AccelerationStructureCacher.cpp` - 24 calls wrapped (BLAS, TLAS, scratch, instance buffers)
- `VoxelAABBCacher.cpp` - 18 calls wrapped (AABB, material ID, brick mapping, staging)
- `VoxelSceneCacher.cpp` - 4 calls wrapped (staging buffer uploads)

**Note:** MeshCacher.cpp and CacherAllocationHelpers.cpp already have inline VkResult checks.

---

## Phase 2: Code Consolidation (P1) - 24h

### 2.1 Extract VulkanBufferAllocator Class (8h) ✅ COMPLETE

**HacknPlan:** #188, #251

**Note:** VulkanBufferAllocator already exists as `DirectAllocator` and `VMAAllocator` in `libraries/ResourceManagement/`. This task became adding device address support.

**Changes Made:**
- Added `VkDeviceAddress deviceAddress` field to `BufferAllocation` struct (`IMemoryAllocator.h:121`)
- `DirectAllocator`: Added `VkMemoryAllocateFlagsInfo` with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT` when buffer uses `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`
- `VMAAllocator`: Added `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` flag
- Both allocators now retrieve device address via `vkGetBufferDeviceAddress` after buffer creation
- Aliased buffer creation also supports device address retrieval

**API (existing, now enhanced):**
```cpp
// IMemoryAllocator interface
std::expected<BufferAllocation, AllocationError>
AllocateBuffer(const BufferAllocationRequest& request);

// BufferAllocation now includes:
struct BufferAllocation {
    VkBuffer buffer;
    VkDeviceSize size;
    void* mappedData;           // If host-visible
    VkDeviceAddress deviceAddress;  // If device-address enabled (for RT buffers)
    // ... other fields
};
```

**Files Changed:**
- `libraries/ResourceManagement/include/Memory/IMemoryAllocator.h` (+1 field)
- `libraries/ResourceManagement/src/Memory/DirectAllocator.cpp` (+20 lines)
- `libraries/ResourceManagement/src/Memory/VMAAllocator.cpp` (+15 lines)

### 2.2 Replace Duplicate Code with VulkanBufferAllocator (4h) ✅ COMPLETE

**HacknPlan:** #187

**Solution:** Consolidated duplicate `FindMemoryType` implementations into `CacherAllocationHelpers`.

**Changes Made:**
- Made `FindMemoryType` public in `CacherAllocationHelpers.h`
- Added overload taking `VkPhysicalDeviceMemoryProperties&` for cached lookups
- Removed duplicate implementations from:
  - `VoxelAABBCacher` (header + cpp)
  - `VoxelSceneCacher` (header + cpp)
  - `AccelerationStructureCacher` (header + cpp)
- Updated all call sites to use `CacherAllocationHelpers::FindMemoryType(m_device->gpuMemoryProperties, ...)`

**Files Changed:**
- `libraries/CashSystem/include/CacherAllocationHelpers.h` (+15 lines, new public API)
- `libraries/CashSystem/src/CacherAllocationHelpers.cpp` (+10 lines, new overload)
- `libraries/CashSystem/include/VoxelAABBCacher.h` (-4 lines)
- `libraries/CashSystem/src/VoxelAABBCacher.cpp` (-12 lines, +4 lines)
- `libraries/CashSystem/include/VoxelSceneCacher.h` (-4 lines)
- `libraries/CashSystem/src/VoxelSceneCacher.cpp` (-12 lines, +2 lines)
- `libraries/CashSystem/include/AccelerationStructureCacher.h` (-4 lines)
- `libraries/CashSystem/src/AccelerationStructureCacher.cpp` (-12 lines, +4 lines)

### 2.3 Migrate Cachers to AllocateBufferTracked (20h) ✅ COMPLETE

**HacknPlan:** Multiple tasks

**Goal:** Replace manual VkBuffer/VkDeviceMemory management with ResourceManagement infrastructure.

#### 2.3.1 AccelerationStructureCacher Migration (8h) ✅ COMPLETE

**Commit:** 855ea26 (2026-01-02)

**Changes:**
- Replace manual buffer/memory allocation with `AllocateBufferTracked` infrastructure
- Use `FreeBufferTracked` for cleanup in `Cleanup()` and exception paths
- Fix exception safety in `BuildTLAS`: free `instanceAllocation` on map/alloc failures
- Use `MapBufferTracked`/`UnmapBufferTracked` for instance buffer data upload
- Simplify device address queries using stored `allocation.deviceAddress` with fallback
- Remove duplicate cleanup methods (now handled by allocator infrastructure)

**Files Changed:**
- `libraries/CashSystem/include/AccelerationStructureCacher.h` (+28 changes)
- `libraries/CashSystem/src/AccelerationStructureCacher.cpp` (-104 lines)
- `libraries/RenderGraph/src/Nodes/AccelerationStructureNode.cpp` (+18 changes)

**Net Change:** -237 lines (consolidation)

#### 2.3.2 MeshCacher Migration (8h) ✅ COMPLETE

**Commit:** 24f18df (2026-01-02)

**Changes:**
- Replace manual VkBuffer/VkDeviceMemory with `BufferAllocation` structure
- Use `AllocateBufferTracked` for buffer creation with budget tracking
- Use `MapBufferTracked`/`UnmapBufferTracked` for data upload
- Use `FreeBufferTracked` for cleanup
- Add `GetVertexBuffer()`/`GetIndexBuffer()` accessors for compatibility
- Update `VertexBufferNode` to use new accessor methods
- Remove manual `CreateBuffer`/`UploadData` helper functions
- Add proper exception safety: cleanup on allocation failures

**Files Changed:**
- `libraries/CashSystem/include/MeshCacher.h` (+36 changes)
- `libraries/CashSystem/src/MeshCacher.cpp` (-91 lines)
- `libraries/RenderGraph/src/Nodes/VertexBufferNode.cpp` (+6 changes)

**Net Change:** -91 lines (consolidation)

#### 2.3.3 DeviceBudgetManager Wiring (4h) ✅ COMPLETE

**Commit:** 1a4fb91 (2026-01-02)

**Changes:**
- Create `DirectAllocator` and `DeviceBudgetManager` in `DeviceNode::CompileImpl`
- Configure budget based on actual device VRAM (90% budget, 80% warning)
- Wire budget manager to `DeviceRegistry` for per-device tracking
- Support multi-GPU scenarios with isolated budget managers per device

**Data Flow Complete:**
```
DeviceNode::CompileImpl()
  └─► DirectAllocator (per device)
       └─► DeviceBudgetManager (wraps allocator)
            └─► DeviceRegistry.SetBudgetManager()
                 └─► TypedCacher::m_budgetManager
                      └─► AllocateBufferTracked()
```

**Files Changed:**
- `libraries/RenderGraph/include/Nodes/DeviceNode.h` (+2 members)
- `libraries/RenderGraph/src/Nodes/DeviceNode.cpp` (+73 lines)
- `libraries/ResourceManagement/tests/test_budget_manager_integration.cpp` (+531 lines, 12 new tests)

**Integration Tests Added:**
- Multi-device budget isolation
- Stats tracking through allocation chain
- Staging quota management
- Budget limit detection
- Thread safety with concurrent allocations

**Net Change:** +620 lines (infrastructure + tests)

### 2.4 Fix Cache Key to Use Content Hash (4h) ✅ COMPLETE

**HacknPlan:** #213, #249

**Status:** Content hash cache keys already implemented in prior work.

---

## Phase 2.5: Upload Infrastructure - 16h ✅ COMPLETE

### 2.5.1 Create StagingBufferPool (8h) ✅ COMPLETE

**HacknPlan:** #183 (part 1)

**Implementation:**
- Created `StagingBufferPool` class in ResourceManagement
- Pool with fixed chunk size (default 16MB)
- Acquire/release API with automatic recycling
- Integration with `DeviceBudgetManager` for tracked allocations

**API:**
```cpp
class StagingBufferPool {
public:
    StagingBufferPool(IMemoryAllocator* allocator, VkDeviceSize chunkSize = 16 * 1024 * 1024);

    std::expected<StagingChunk, AllocationError> Acquire(VkDeviceSize size);
    void Release(StagingChunk chunk);
    void Cleanup();
};
```

**Files Created:**
- `libraries/ResourceManagement/include/Memory/StagingBufferPool.h`
- `libraries/ResourceManagement/src/Memory/StagingBufferPool.cpp`

**Changes:**
- `libraries/ResourceManagement/CMakeLists.txt` - Added new source files to build

### 2.5.2 Create BatchedUploader (8h) ✅ COMPLETE

**HacknPlan:** #183 (part 2)

**Implementation:**
- Created `BatchedUploader` class combining `StagingBufferPool` + timeline semaphores
- Centralized uploader pattern in `TypedCacher` base class
- Migrated `VoxelSceneCacher` and `VoxelAABBCacher` to use `BatchedUploader`
- Removed blocking `vkQueueWaitIdle` patterns from cachers

**Architecture:**
```cpp
class BatchedUploader {
    StagingBufferPool m_pool;
    VkQueue m_transferQueue;
    VkSemaphore m_timelineSemaphore;
    uint64_t m_currentValue;

public:
    std::expected<UploadTicket, AllocationError>
    ScheduleUpload(VkBuffer dst, const void* data, VkDeviceSize size);

    void WaitForUpload(const UploadTicket& ticket);
};
```

**Centralized Pattern:**
```cpp
// TypedCacher base class
class TypedCacher {
protected:
    BatchedUploader* GetUploader(); // Single uploader per cacher
};
```

**Migration:**
- `VoxelSceneCacher`: Uses `GetUploader()` for AABB/material uploads
- `VoxelAABBCacher`: Uses `GetUploader()` for AABB/brick uploads
- `AccelerationStructureCacher`: Can use same pattern for instance buffer uploads (future)
- `MeshCacher`: Can use same pattern for vertex/index uploads (future)
- `TextureCacher`: Will use same pattern when implemented (future)

**Files Created:**
- `libraries/ResourceManagement/include/Memory/BatchedUploader.h`
- `libraries/ResourceManagement/src/Memory/BatchedUploader.cpp`

**Changes:**
- `libraries/ResourceManagement/CMakeLists.txt` - Added BatchedUploader sources

**Benefits:**
- Eliminated blocking synchronization (`vkQueueWaitIdle`) from upload paths
- Timeline semaphores provide fine-grained completion tracking
- Reusable staging buffers reduce allocation overhead
- Centralized pattern in base class simplifies future cachers

---

## Phase 3: TLAS Lifecycle (P2) - 16h ✅ COMPLETE

**Completion Date:** 2026-01-03
**Commit:** da95c62

### 3.1 TLAS Instance Lifecycle Management (8h) ✅ COMPLETE

**HacknPlan:** #215, #186

**Implementation:**

**Core Infrastructure:**
- **TLASInstanceManager**: Manages TLAS instance lifecycle with add/remove/rebuild operations
- **TLASInstanceBuffer**: GPU buffer wrapper for `VkAccelerationStructureInstanceKHR` array
- **DynamicTLAS Support**: Rebuild triggers when instances added/removed, budget-aware reconstruction
- **ASBuildMode Enum**: Static vs Dynamic TLAS rebuild modes

**AccelerationStructureNode Refactor:**
- **5 Input Slots**:
  - `DEVICE` (VulkanDevice*)
  - `AABB_DATA` (VoxelAABBData)
  - `BUILD_MODE` (ASBuildMode enum)
  - `INSTANCE_TRANSFORMS` (std::vector<VkTransformMatrixKHR>*)
  - `SCRATCH_BUFFER_SIZE` (uint64_t for size validation)
- **2 Output Slots**:
  - `ACCELERATION_STRUCTURE` (CachedAccelerationStructure)
  - `DEVICE_ADDRESS` (VkDeviceAddress for shader access)

**Architecture:**
```cpp
// CompileTimeResourceSystem integration
class AccelerationStructureNodeConfig {
    SLOT(0, DEVICE, VulkanDevice*)
    SLOT(1, AABB_DATA, VoxelAABBData)
    SLOT(2, BUILD_MODE, ASBuildMode)
    SLOT(3, INSTANCE_TRANSFORMS, std::vector<VkTransformMatrixKHR>*)
    SLOT(4, SCRATCH_BUFFER_SIZE, uint64_t)
    OUTPUT_SLOT(0, ACCELERATION_STRUCTURE, CachedAccelerationStructure)
    OUTPUT_SLOT(1, DEVICE_ADDRESS, VkDeviceAddress)
};
```

**Key Features:**
- Proper lifecycle management for TLAS instances
- Budget-aware buffer allocation via DeviceBudgetManager
- Scratch buffer size validation (max(BLAS, TLAS) requirements)
- Static and dynamic rebuild support
- Device address output for RT shader access

**Files Changed:**
- `libraries/RenderGraph/include/Data/Core/CompileTimeResourceSystem.h` (+slot validation)
- `libraries/RenderGraph/include/Data/Nodes/AccelerationStructureNodeConfig.h` (5 inputs, 2 outputs)
- `libraries/RenderGraph/include/Nodes/AccelerationStructureNode.h` (TLASInstanceManager, ASBuildMode)
- `libraries/RenderGraph/src/Nodes/AccelerationStructureNode.cpp` (lifecycle implementation)
- `libraries/RenderGraph/tests/test_critical_nodes.cmake` (new test file)

### 3.2 Comprehensive Test Coverage (8h) ✅ COMPLETE

**Test Suites Added:**

**AccelerationStructureNode Tests (22 tests):**
- Slot configuration validation (5 inputs, 2 outputs)
- ASBuildMode enum coverage (Static, Dynamic)
- TLAS instance lifecycle (add/remove/rebuild)
- Scratch buffer size validation
- Device address output verification
- Budget manager integration
- Error handling for invalid inputs

**Critical Nodes Integration Tests (18 tests):**
- DeviceNode → AccelerationStructureNode wiring
- VoxelGridNode → AccelerationStructureNode data flow
- Multi-node graph compilation
- Resource cleanup ordering
- Cross-node slot validation
- End-to-end pipeline verification

**Test Results:**
- **40 new tests passing** (22 + 18)
- Zero validation errors
- Comprehensive coverage of slot configuration and lifecycle

**Files Created:**
- `libraries/RenderGraph/tests/test_acceleration_structure_node.cpp` (22 tests)
- `libraries/RenderGraph/tests/test_critical_nodes_integration.cpp` (18 tests)
- `libraries/RenderGraph/tests/test_critical_nodes.cmake` (test registration)

---

## Phase 4: Testing (P0) - 28h

### 4.1 Lifetime/Safety Tests for shared_ptr Fix (4h)

**HacknPlan:** #190

**Tests:**
- [ ] Test: AccelerationStructure outlives VoxelAABBData
- [ ] Test: VoxelAABBData cleanup doesn't crash with live AS
- [ ] Test: Weak pointer correctly invalidates

### 4.2 Unit Tests for VulkanBufferAllocator (8h)

**HacknPlan:** #192

**Tests:**
- [ ] Allocate/Free DeviceLocal buffer
- [ ] Allocate/Free HostVisible buffer
- [ ] Device address retrieval
- [ ] Error handling for OOM
- [ ] Integration with DeviceBudgetManager

### 4.3 Integration Tests for Cacher Chain (8h)

**HacknPlan:** #191

**Tests:**
- [ ] Full pipeline: VoxelAABB → AccelerationStructure → Render
- [ ] Hot-reload: shader change doesn't corrupt AS
- [ ] Resource cleanup on scene change

### 4.4 General Unit Tests (8h)

**HacknPlan:** #244

**Tests:**
- [ ] VK_CHECK macro behavior
- [ ] Content hash cache keys
- [ ] Batched upload correctness

---

## Implementation Order

```mermaid
graph TD
    P1_1[1.1 Pointer Safety] --> P4_1[4.1 Lifetime Tests]
    P1_2[1.2 VK_CHECK Macro] --> P1_3[1.3 Apply VK_CHECK]
    P1_3 --> P2_1[2.1 VulkanBufferAllocator]
    P2_1 --> P2_2[2.2 Replace Duplicate Code]
    P2_1 --> P4_2[4.2 Allocator Tests]
    P2_2 --> P2_3[2.3 Batched Uploads]
    P2_2 --> P2_4[2.4 Content Hash]
    P3_1[3.1 Consolidate Types] --> P3_2[3.2 TLAS Scratch]
    P3_2 --> P4_3[4.3 Integration Tests]
    P4_1 --> P4_4[4.4 Unit Tests]
    P4_2 --> P4_4
    P4_3 --> P4_4
```

**Recommended Execution:**
1. Phase 1.2 → 1.3 (VK_CHECK first - catches errors early)
2. Phase 1.1 (Pointer safety)
3. Phase 2.1 → 2.2 (VulkanBufferAllocator extraction)
4. Phase 3.1 → 3.2 (TLAS lifecycle in parallel)
5. Phase 2.3, 2.4 (Batching and hashing)
6. Phase 4 (Testing throughout and at end)

---

## Success Metrics

- [x] Zero shared_ptr aliasing with no-op deleters
- [x] VK_CHECK on 100% of Vulkan calls in CashSystem (46 call sites)
- [x] StagingBufferPool and BatchedUploader implemented
- [x] Centralized uploader pattern in TypedCacher base class
- [x] Removed blocking vkQueueWaitIdle from VoxelSceneCacher and VoxelAABBCacher
- [ ] VulkanBufferAllocator with 90%+ test coverage
- [ ] Batched uploads reduce command buffer submissions by 50%+
- [ ] Content hash eliminates false cache misses (already verified)

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| VK_CHECK changes break silent failures | Medium | Medium | Add logging before throwing |
| VulkanBufferAllocator doesn't cover all cases | Low | High | Audit all allocation sites first |
| Batched uploads increase latency | Low | Medium | Keep batch size bounded |

---

## Related Documentation

- [[ResourceManagement]] - DeviceBudgetManager integration
- [[Sprint4-ResourceManager-Integration]] - Budget tracking APIs
- [[Production-Roadmap-2026]] - Sprint overview

---

## Change Log

| Date | Change |
|------|--------|
| 2026-01-02 | Initial plan created from roadmap + codebase analysis |
| 2026-01-02 | Phase 2.3 COMPLETE: Migrated AccelerationStructureCacher, MeshCacher to AllocateBufferTracked (-328 lines) |
| 2026-01-02 | Phase 2.3.3 COMPLETE: Wired DeviceBudgetManager to cacher infrastructure (+620 lines w/ 12 integration tests) |
| 2026-01-02 | Phase 2.4 COMPLETE: Content hash cache keys verified (already implemented) |
| 2026-01-03 | Phase 2.5.1 COMPLETE: Created StagingBufferPool class |
| 2026-01-03 | Phase 2.5.2 COMPLETE: Created BatchedUploader class with timeline semaphores |
| 2026-01-03 | Phase 2.5.3 COMPLETE: Migrated VoxelSceneCacher to BatchedUploader via TypedCacher base |
| 2026-01-03 | Phase 2.5.4 COMPLETE: Migrated VoxelAABBCacher to BatchedUploader via TypedCacher base |
| 2026-01-03 | Established centralized uploader pattern - ready for future cachers (TextureCacher, etc.) |
| 2026-01-03 | Phase 3 COMPLETE: TLAS Lifecycle management with 40 new tests (commit da95c62) |
