---
tags: [feature, sprint-6-2, taskqueue, timeline, rendergraph]
created: 2026-01-06
status: planned
priority: high
complexity: medium
sprint: Sprint 6.2
design-element: 37
parent-work-item: 338
---

# Sprint 6.2: TaskQueue System

**Status:** Planned
**Sprint:** 6.2 - Phase 2 of Timeline Foundation
**Design Element:** [#37 TaskQueue System](hacknplan://design-element/37)
**Parent Work Item:** [#338 Sprint 6.2 TaskQueue System](hacknplan://work-item/338)

---

## Overview

Budget-aware task queue infrastructure for RenderGraph timeline execution with resource constraints. Extends MultiDispatchNode with per-task cost estimation and frame budget enforcement.

**Goal:** Enable dynamic task scheduling with GPU time/memory budgets to prevent frame budget overruns.

---

## Architecture

### Core Components

1. **TaskQueue<T> Template**
   - Generic queue with budget tracking
   - Priority-based task ordering
   - Frame budget enforcement (time + memory)

2. **TaskBudget Structure**
   - GPU time budget (nanoseconds)
   - GPU memory budget (bytes)
   - Strict vs lenient overflow modes

3. **MultiDispatchNode Integration**
   - Replaces existing `dispatchQueue_` with TaskQueue
   - Backward-compatible `QueueDispatch()` API
   - New `TryQueueDispatch()` with budget awareness

### Design Decisions

**Single-Threaded Execution:**
- RenderGraph execution model is single-threaded
- No mutex needed for queue operations
- Tasks execute synchronously within `ExecuteImpl()`

**Budget Types:**
- **Phase 1:** GPU time only (simpler)
- **Phase 2:** Add GPU memory (future work)

**Priority Range:** uint8_t (0-255) sufficient for scheduling

---

## Implementation Tasks

### Task 1: TaskQueue Template (16h)
**Work Item:** [#339](hacknplan://work-item/339)
**Files:**
- NEW: `libraries/RenderGraph/include/Core/TaskQueue.h`
- NEW: `libraries/RenderGraph/src/Core/TaskQueue.cpp`

**API:**
```cpp
template<typename TTaskData>
class TaskQueue {
    struct TaskSlot {
        TTaskData data;
        uint32_t priority = 0;
        uint64_t estimatedCostNs = 0;
        uint64_t estimatedMemoryBytes = 0;
    };

    bool TryEnqueue(TaskSlot&& slot, uint64_t frameBudgetNs);
    void Execute(std::function<void(const TTaskData&)> executor);
    void Clear();
    uint32_t GetQueuedCount() const;
    uint64_t GetTotalEstimatedCost() const;
};
```

### Task 2: Budget-Aware Dequeue (16h)
**Work Item:** [#342](hacknplan://work-item/342)
**Files:**
- MODIFY: TaskQueue.h/cpp
- NEW: `libraries/RenderGraph/include/Data/TaskBudget.h`

**Features:**
- Budget validation before enqueue
- Strict mode (reject) vs lenient mode (warn)
- Remaining budget queries

### Task 3: MultiDispatchNode Integration (16h)
**Work Item:** [#341](hacknplan://work-item/341)
**Files:**
- MODIFY: MultiDispatchNode.h/cpp
- MODIFY: MultiDispatchNodeConfig.h

**Backward Compatibility:**
- Existing `QueueDispatch()` unchanged (zero-cost tasks)
- New `TryQueueDispatch()` with budget parameters
- Sprint 6.1's 41 tests must pass

### Task 4: Stress Tests (16h)
**Work Item:** [#343](hacknplan://work-item/343)
**Files:**
- NEW: `libraries/RenderGraph/tests/test_task_queue.cpp` (15 tests)
- MODIFY: `test_group_dispatch.cpp` (+5 tests)

**Test Coverage:**
- Budget overflow scenarios
- Priority ordering
- Large task counts (1000 tasks)
- Backward compatibility verification

### Task 5: Documentation (8h)
**Work Item:** [#340](hacknplan://work-item/340)
**Files:**
- NEW: `Vixen-Docs/Libraries/RenderGraph/TaskQueue.md`
- MODIFY: `MultiDispatchNode.md`
- MODIFY: `01-Architecture/RenderGraph-System.md`

---

## Success Criteria

- ✅ TaskQueue template compiles and links
- ✅ Budget overflow detected correctly
- ✅ MultiDispatchNode backward compatible
- ✅ 20+ tests passing (15 new + 5 integration)
- ✅ Documentation complete
- ✅ Zero regressions in Sprint 6.1 tests (41 tests)

---

## Dependencies

**Prerequisite:**
- Sprint 6.1 Complete (MultiDispatchNode foundation)
- DeviceBudgetManager API (ResourceManagement library)

**Integration Points:**
- `MultiDispatchNode::dispatchQueue_` → replaced with TaskQueue
- `DeviceBudgetManager::GetStats()` → budget queries
- `GraphLifecycleHooks::PostExecute` → statistics reporting

---

## Related Documentation

- [[Sprint6.1-MultiDispatchNode]] - Foundation (group-based dispatch)
- [[../../01-Architecture/RenderGraph-System]] - RenderGraph architecture
- [[../../Libraries/RenderGraph/MultiDispatchNode]] - API reference
- [[timeline-execution-system]] - Timeline Foundation proposal

**HacknPlan:**
- Design Element: [#37 TaskQueue System](hacknplan://design-element/37)
- Parent: [#32 Timeline Execution System](hacknplan://design-element/32)

---

**Created:** 2026-01-06
**Sprint:** 6.2 - Phase 2 Timeline Foundation
**Estimated:** 72 hours
