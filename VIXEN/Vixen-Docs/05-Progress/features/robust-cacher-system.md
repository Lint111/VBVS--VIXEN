---
tags: [feature, planned, cashsystem, vulkan, architecture]
created: 2025-12-09
status: planning
priority: high
---

# Feature: Robust CashSystem Resource Management

## Overview

**Objective:** Refactor CashSystem cachers to fix memory safety issues, eliminate code duplication, improve performance, and add proper error handling.

**Requester:** Architecture review identified critical issues

**Branch:** TBD (create from `claude/phase-k-hardware-rt`)

---

## Discovery Findings

### Related Code

| File | Relevance | Notes |
|------|-----------|-------|
| `libraries/CashSystem/src/VoxelAABBCacher.cpp:120-123` | Critical | Dangling shared_ptr aliasing |
| `libraries/CashSystem/src/AccelerationStructureCacher.cpp:285-296` | Major | Duplicated FindMemoryType |
| `libraries/CashSystem/src/VoxelAABBCacher.cpp:328-339` | Major | Duplicated FindMemoryType |
| `libraries/CashSystem/src/VoxelAABBCacher.cpp:415-416` | Major | Blocking vkQueueWaitIdle per buffer |
| `libraries/CashSystem/include/AccelerationStructureCacher.h:172-178` | Major | Pointer-based cache key |
| `libraries/RenderGraph/src/Nodes/VoxelAABBConverterNode.cpp:120-123` | Critical | No-op deleter creates aliased ptr |
| `libraries/RenderGraph/src/Nodes/AccelerationStructureNode.cpp:188-222` | Major | reinterpret_cast + manual field copy |

### Related Documentation

- [[RenderGraph-System]] - Node architecture
- [[Libraries/CashSystem]] - Cacher patterns

### Affected Subsystems

- [x] CashSystem
- [x] RenderGraph
- [ ] SVO
- [ ] VulkanResources
- [ ] Profiler

### Complexity Assessment

- **Estimated effort:** Large
- **Risk level:** Medium (breaking changes in internal APIs only)
- **Breaking changes:** Internal API only (CashSystem ↔ RenderGraph)

---

## Design Decisions

### Decision 1: Shared_ptr Aliasing Fix Strategy

**Context:** VoxelAABBConverterNode creates shared_ptr with no-op deleter to externally-owned data, risking use-after-free.

**Options Considered:**
1. **Option A:** Raw pointer + deep copy during Create()
   - Pros: Simple, no ownership ambiguity
   - Cons: Memory overhead for copy, stale data possible
2. **Option B:** VoxelSceneCacher owns data, VoxelAABBCacher uses weak_ref to cache key
   - Pros: Single ownership, automatic invalidation
   - Cons: More complex, requires VoxelSceneCacher changes
3. **Option C:** Require explicit lifetime token (like Unreal RDG)
   - Pros: Industry-proven pattern
   - Cons: Significant refactor

**Chosen:** Option B - Weak reference to cacher-owned data

**Rationale:** Centralizes ownership in VoxelSceneCacher (already manages the data), enables automatic cache invalidation when source changes.

**User Approved:** Pending

---

### Decision 2: Buffer Allocation Strategy

**Context:** Duplicated Vulkan memory allocation code in multiple cachers.

**Options Considered:**
1. **Option A:** Extract to VulkanBufferAllocator utility class
   - Pros: Immediate deduplication, testable
   - Cons: Still manual memory management
2. **Option B:** Integrate VMA (Vulkan Memory Allocator)
   - Pros: Industry standard, handles fragmentation
   - Cons: New dependency, learning curve

**Chosen:** Option A for now, with VMA as future extension

**Rationale:** VMA integration is larger scope. VulkanBufferAllocator provides immediate benefits and can wrap VMA later.

**User Approved:** Pending

---

### Decision 3: Error Handling Approach

**Context:** Vulkan calls ignore VkResult.

**Options Considered:**
1. **Option A:** VK_CHECK macro that throws
   - Pros: Simple, immediate failure detection
   - Cons: Hard to test without mock
2. **Option B:** IVulkanErrorHandler interface injection
   - Pros: Testable, flexible error handling
   - Cons: More complex, runtime overhead

**Chosen:** Option A with testable helper function

**Rationale:** 
```cpp
void ThrowOnVkError(VkResult result, const char* call, const char* file, int line);
#define VK_CHECK(call) ThrowOnVkError(call, #call, __FILE__, __LINE__)
```
Helper function is testable without Vulkan; macro provides ergonomics.

**User Approved:** Pending

---

## Implementation Plan

### Phase 1: Critical Fixes (P0 - Blockers)

- [ ] **Task 1.1:** Fix dangling shared_ptr in VoxelAABBConverterNode
  - Files: `VoxelAABBConverterNode.cpp`, `VoxelAABBCacher.h/cpp`
  - Agent: coding-partner
  - Dependencies: None
  - Tests: Lifetime test, use-after-cleanup test

- [ ] **Task 1.2:** Add VK_CHECK macro and ThrowOnVkError helper
  - Files: New `libraries/CashSystem/include/VulkanErrorHandling.h`
  - Agent: coding-partner
  - Dependencies: None
  - Tests: Unit tests for each VkResult error code

- [ ] **Task 1.3:** Apply VK_CHECK to all Vulkan calls in cachers
  - Files: `VoxelAABBCacher.cpp`, `AccelerationStructureCacher.cpp`
  - Agent: intern-army-refactor
  - Dependencies: Task 1.2

### Phase 2: Code Consolidation (P1 - Should Fix)

- [ ] **Task 2.1:** Extract VulkanBufferAllocator class
  - Files: New `libraries/CashSystem/include/VulkanBufferAllocator.h/cpp`
  - Agent: coding-partner
  - Dependencies: Phase 1 complete
  - Contents: FindMemoryType, CreateBuffer, CreateStagingBuffer, UploadBufferData

- [ ] **Task 2.2:** Replace duplicated code with VulkanBufferAllocator calls
  - Files: `VoxelAABBCacher.cpp`, `AccelerationStructureCacher.cpp`
  - Agent: intern-army-refactor
  - Dependencies: Task 2.1

- [ ] **Task 2.3:** Implement batched buffer uploads
  - Files: `VulkanBufferAllocator.cpp` (add BatchedUploader)
  - Agent: coding-partner
  - Dependencies: Task 2.1
  - Note: Single command buffer + fence instead of per-buffer vkQueueWaitIdle

- [ ] **Task 2.4:** Fix cache key to use content hash
  - Files: `AccelerationStructureCacher.h`, `VoxelAABBCacher.h`
  - Agent: coding-partner
  - Dependencies: None
  - Hash components: sceneType, resolution, voxelSize, buildFlags

### Phase 3: Cleanup & Optimization (P2 - Nice to Have)

- [ ] **Task 3.1:** Consolidate AccelerationStructureData types
  - Files: `AccelerationStructureCacher.h`, `AccelerationStructureNodeConfig.h`
  - Agent: intern-army-refactor
  - Dependencies: Phase 2 complete
  - Approach: Type alias in RenderGraph namespace

- [ ] **Task 3.2:** Fix scratch buffer lifecycle (REVISED per QA)
  - Files: `AccelerationStructureCacher.cpp`
  - Agent: coding-partner
  - Dependencies: None
  - **IMPORTANT:** Keep scratch for TLAS if TLAS scratch > BLAS scratch, or allocate separate TLAS scratch. Release only after both builds complete.

- [ ] **Task 3.3:** Build inverse lookup map for ExtractAABBsFromSceneData
  - Files: `VoxelAABBCacher.cpp`
  - Agent: coding-partner
  - Dependencies: None
  - Fix: O(n²) → O(n) with pre-built inverse map

- [ ] **Task 3.4:** Extract magic numbers to shared constants
  - Files: New `libraries/SVO/include/SVOConstants.h` or existing header
  - Agent: intern-army-refactor
  - Dependencies: None

### Phase 4: Testing

- [ ] **Task 4.1:** Create unit tests for VulkanBufferAllocator
  - Files: New `libraries/CashSystem/tests/test_vulkan_buffer_allocator.cpp`
  - Agent: test-framework-qa
  - Target coverage: 90%+

- [ ] **Task 4.2:** Create lifetime/safety tests for shared_ptr fix
  - Files: `libraries/RenderGraph/tests/test_voxel_aabb_converter_node.cpp`
  - Agent: test-framework-qa

- [ ] **Task 4.3:** Create integration tests for full cacher chain
  - Files: New integration test
  - Agent: test-framework-qa
  - Test: VoxelSceneCacher → VoxelAABBCacher → AccelerationStructureCacher

- [ ] **Task 4.4:** Run full test suite and fix regressions
  - Agent: test-framework-qa

---

## Test Requirements

### Unit Tests

| Component | Test File | Coverage Target |
|-----------|-----------|-----------------|
| VulkanBufferAllocator | `test_vulkan_buffer_allocator.cpp` | 90% |
| VK_CHECK/ThrowOnVkError | `test_vulkan_error_handling.cpp` | 95% |
| Cache key hashing | `test_cacher_hash.cpp` | 85% |
| Inverse lookup map | Existing VoxelAABBCacher tests | Update |

### Integration Tests

| Flow | Test Description |
|------|------------------|
| Full cacher chain | VoxelScene → AABB → AccelStruct produces valid BLAS |
| Cache invalidation | Scene regeneration invalidates dependent caches |
| Multi-device | Per-device cache isolation |

### Edge Cases (from QA review)

- [ ] Empty scene (0 bricks)
- [ ] Maximum brick count (memory limits)
- [ ] Size overflow (size > UINT32_MAX)
- [ ] Device memory exhaustion
- [ ] RT buffer alignment (256-byte)
- [ ] NaN/Inf in voxelSize
- [ ] TLAS scratch reuse after BLAS scratch release

---

## Acceptance Criteria

- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] No new warnings introduced
- [ ] VK_CHECK on all Vulkan calls
- [ ] No shared_ptr aliasing with no-op deleters
- [ ] Duplicated code eliminated (single VulkanBufferAllocator)
- [ ] Cache keys use content hash, not pointer address
- [ ] Documentation updated

---

## Progress Log

### 2025-12-09 - Discovery & Planning

- Architecture-critic reviewed current implementation
- Identified 2 critical, 5 major, 4 minor issues
- QA review added edge cases and test requirements
- **Critical QA finding:** Scratch buffer TLAS dependency - revised Task 3.2
- Feature plan created and awaiting user approval

---

## Files Changed

| File | Change Type | Lines | Description |
|------|-------------|-------|-------------|
| (TBD during implementation) | | | |

---

## Deferred Work

<!-- TODO:DEFERRED - searchable marker for future work queue -->

- [ ] TODO:DEFERRED VMA integration (Option B for buffer allocation) - Defer until after VulkanBufferAllocator proves insufficient
- [ ] TODO:DEFERRED IBufferAllocator interface for full mocking - Defer unless test coverage insufficient with current approach

---

## Future Extensions

<!-- TODO:EXTENSION - ideas considered but not needed now -->

- [ ] TODO:EXTENSION Async transfer queue for uploads - Consider if batched uploads insufficient
- [ ] TODO:EXTENSION Handle-based resource system (like Frostbite) - Consider if adding hot-reload/editor
- [ ] TODO:EXTENSION Transient resource pool (RDG-style) - Consider if moving to frame-graph execution

---

## Completion Checklist

- [ ] All tasks complete
- [ ] All tests pass
- [ ] Code reviewed by architecture-critic
- [ ] Code reviewed by coding-partner
- [ ] QA sign-off
- [ ] Documentation updated
- [ ] activeContext.md updated
- [ ] Branch ready for merge

**Completed:** TBD
