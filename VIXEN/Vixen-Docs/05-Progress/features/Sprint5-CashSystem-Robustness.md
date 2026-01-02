---
title: Sprint 5 - CashSystem Robustness
aliases: [Sprint 5, CashSystem Robustness]
tags: [sprint, cashsystem, memory-safety, vulkan]
created: 2026-01-02
status: planning
priority: P0
hacknplan-board: 651783
---

# Sprint 5: CashSystem Robustness

**Branch:** `production/sprint-5-cashsystem-robustness`
**Goal:** Memory safety, error handling, and code consolidation for CashSystem.

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

### 2.2 Replace Duplicate Code with VulkanBufferAllocator (4h)

**HacknPlan:** #187

**Files with buffer allocation to refactor:**
- `AccelerationStructureCacher.cpp` - BLAS, TLAS, scratch, instance buffers
- `VoxelAABBCacher.cpp` - AABB, material, brick mapping, staging buffers
- `MeshCacher.cpp` - vertex, index buffers
- `CacherAllocationHelpers.cpp` - already partially extracted

### 2.3 Implement Batched Buffer Uploads (8h)

**HacknPlan:** #183

**Current Pattern:**
- Each buffer creates its own staging buffer
- Each upload submits its own command buffer
- No batching of transfers

**Tasks:**
- [ ] Create `StagingBufferPool` for reusable staging memory
- [ ] Create `BatchedUploader` that queues multiple uploads
- [ ] Submit single command buffer for batch
- [ ] Use timeline semaphores for completion tracking

### 2.4 Fix Cache Key to Use Content Hash (4h)

**HacknPlan:** #213, #249

**Current State:**
- Cache keys may use object identity instead of content
- Risk of false cache misses or stale data

**Tasks:**
- [ ] Audit all `GetCacheKey()` implementations
- [ ] Replace identity-based keys with content hashes
- [ ] Use `XXH3` or `MurmurHash3` for fast hashing
- [ ] Add hash collision detection in debug builds

---

## Phase 3: TLAS Lifecycle (P2) - 8h

### 3.1 Consolidate AccelerationStructureData Types (4h)

**HacknPlan:** #215

**Current Types:**
- `AccelerationStructureData` (header:28-61) - BLAS + TLAS + scratch + instance
- `CachedAccelerationStructure` (header:73-85) - wrapper with aabbDataRef
- `VoxelAABBData` (VoxelAABBCacher.h:58-84) - AABB buffers

**Tasks:**
- [ ] Evaluate if types can be merged or simplified
- [ ] Consider: separate BLAS and TLAS data structs
- [ ] Document ownership model clearly

### 3.2 Fix Scratch Buffer TLAS Lifecycle (4h)

**HacknPlan:** #186

**Current State (from analysis):**
- Scratch buffer created during BLAS build (line 397)
- Reused for TLAS build (line 637)
- Properly cleaned up (lines 107-113)
- **Issue:** Size may be incorrect if TLAS needs larger scratch than BLAS

**Tasks:**
- [ ] Compare BLAS vs TLAS scratch size requirements
- [ ] Allocate max(BLAS, TLAS) scratch size
- [ ] Add validation in debug builds

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

- [ ] Zero shared_ptr aliasing with no-op deleters
- [ ] VK_CHECK on 100% of Vulkan calls in CashSystem
- [ ] VulkanBufferAllocator with 90%+ test coverage
- [ ] Batched uploads reduce command buffer submissions by 50%+
- [ ] Content hash eliminates false cache misses

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
