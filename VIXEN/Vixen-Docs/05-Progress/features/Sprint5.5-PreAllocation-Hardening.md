---
title: Sprint 5.5 - Pre-Allocation Hardening
aliases: [Sprint 5.5, Pre-Allocation Hardening]
tags: [sprint, pre-allocation, memory, infrastructure]
created: 2026-01-03
updated: 2026-01-04
status: complete
priority: P1
hacknplan-board: 651953
source: ARCHITECTURE_CRITIQUE_2026-01-03.md
---

# Sprint 5.5: Pre-Allocation Hardening

**Board:** 651953
**Goal:** Quick-win pre-allocation from architecture critique to harden foundation before Timeline system.
**Status:** ✅ COMPLETE (16h/16h - 100%)

---

## Executive Summary

This mini-sprint addresses non-implemented pre-allocation tasks identified in the architecture critique (ARCHITECTURE_CRITIQUE_2026-01-03.md). These are quick wins that prevent allocation fragmentation before the complex Timeline system is built in Sprint 6.

**Total Effort:** 16 hours (4 tasks)

---

## Source: Architecture Critique Analysis

The critique identified that while Sprint 5 completed:
- ✅ StagingBufferPool pre-warming (Phase 4.1)
- ✅ EventBus statistics logging (Phase 4.2)
- ✅ Frame boundary hooks (Phase 4.4)

The following were implemented in Sprint 5.5:
- ✅ EventBus queue capacity pre-allocation (#302)
- ✅ Command buffer pool sizing (#301)
- ✅ Deferred destruction pool pre-sizing (#300)
- ✅ Full allocation tracker instrumentation (#299)

---

## Tasks

### #302: EventBus Queue Pre-Allocation (4h) ✅ COMPLETE

**Problem:** EventBus event queue can reallocate during frame execution.

**Solution Implemented:**
- Created `PreAllocatedQueue<T>` ring buffer template (`PreAllocatedQueue.h`)
- Replaced `std::queue` with pre-allocatable queue in MessageBus
- Auto-reserves based on node count heuristic (nodes × 3)
- Growth fallback with stats tracking for capacity tuning

**Files Changed:**
- `libraries/EventBus/include/PreAllocatedQueue.h` (NEW)
- `libraries/EventBus/include/MessageBus.h`
- `libraries/EventBus/src/MessageBus.cpp`
- `libraries/RenderGraph/src/Core/RenderGraph.cpp`

---

### #301: Command Buffer Pool Sizing (4h) ✅ COMPLETE

**Problem:** Command buffers allocated per-frame without pre-sizing.

**Solution Implemented:**
- Added `PreAllocationRequirements` struct to `NodeInstance`
- Added `PreAllocateCommandBuffers()` to `CommandPoolNode`
- Added `AcquireCommandBuffer()` / `ReleaseAllCommandBuffers()` pool API
- Growth fallback with warning logging for capacity tuning

**Files Changed:**
- `libraries/RenderGraph/include/Core/NodeInstance.h`
- `libraries/RenderGraph/include/Nodes/CommandPoolNode.h`
- `libraries/RenderGraph/src/Nodes/CommandPoolNode.cpp`
- `libraries/RenderGraph/src/Core/RenderGraph.cpp`

---

### #300: Deferred Destruction Pool Pre-Sizing (4h) ✅ COMPLETE

**Problem:** DeferredDestruction queue grows dynamically during cleanup.

**Solution Implemented:**
- Converted `std::queue<PendingDestruction>` to pre-allocatable ring buffer
- Added `PreReserve(capacity)` for setup-time allocation
- Fixed unsigned underflow bug in `ProcessFrame()`
- Added `PreAllocationStats` for capacity tuning (capacity, growthCount, maxSizeReached)
- Heuristic: nodeCount × 5 resources × 3 frames in flight

**Files Changed:**
- `libraries/ResourceManagement/include/Lifetime/DeferredDestruction.h`
- `libraries/RenderGraph/src/Core/RenderGraph.cpp`

**Tests Added:** 12 new tests for pre-allocation functionality

---

### #299: Allocation Tracker Full Instrumentation (4h) ✅ COMPLETE

**Problem:** Allocation tracking design doc exists (Phase 4.3) but not fully implemented.

**Solution Implemented:**
- Frame tracking already existed (`OnFrameStart`/`OnFrameEnd`, `GetLastFrameDelta`)
- Added `exceededThreshold` flag to `FrameAllocationDelta`
- Added `warningCallback` to `DeviceBudgetManager::Config`
- Replaced `std::cerr` with callback-based warning mechanism (logging rule fix)

**Files Changed:**
- `libraries/ResourceManagement/include/Memory/DeviceBudgetManager.h`
- `libraries/ResourceManagement/src/Memory/DeviceBudgetManager.cpp`

**Tests Added:** 7 new tests for frame allocation tracking

---

## Success Metrics

- [x] EventBus handles 1K+ events without reallocation (PreAllocatedQueue)
- [x] Command buffer pool pre-sized to node count (PreAllocateCommandBuffers)
- [x] Deferred destruction queue bounded (PreReserve ring buffer)
- [x] Allocation delta logged between Setup and Execute (warningCallback)

---

## Dependencies

- **Predecessor:** Sprint 5 (CashSystem Robustness) ✅ COMPLETE
- **Successor:** Sprint 6 (Timeline Foundation)

---

## Related Documentation

- [[Sprint5-CashSystem-Robustness]] - Predecessor sprint
- [[Production-Roadmap-2026]] - Master roadmap
- `ARCHITECTURE_CRITIQUE_2026-01-03.md` - Source analysis
- [[01-Architecture/AllocationTracking]] - Design document

---

## Change Log

| Date | Change |
|------|--------|
| 2026-01-03 | Sprint created from architecture critique analysis |
| 2026-01-03 | 4 tasks created in HacknPlan (Board 651953) |
| 2026-01-04 | Tasks #302, #301, #300 complete - pre-allocation infrastructure |
| 2026-01-04 | Task #299 complete - allocation tracker with callback warnings |
| 2026-01-04 | Sprint 5.5 COMPLETE: 16h/16h (100%), 19 new tests |

## Commits

| Hash | Description |
|------|-------------|
| `3fdb9a7` | feat(Sprint5.5): Pre-allocation hardening - Tasks #300, #301, #302 |
| `e01d8a2` | feat(Sprint5.5): Allocation tracker full instrumentation - Task #299 |
