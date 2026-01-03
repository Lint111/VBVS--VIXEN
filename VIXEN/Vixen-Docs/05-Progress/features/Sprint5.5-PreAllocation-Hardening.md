---
title: Sprint 5.5 - Pre-Allocation Hardening
aliases: [Sprint 5.5, Pre-Allocation Hardening]
tags: [sprint, pre-allocation, memory, infrastructure]
created: 2026-01-03
updated: 2026-01-03
status: planned
priority: P1
hacknplan-board: 651953
source: ARCHITECTURE_CRITIQUE_2026-01-03.md
---

# Sprint 5.5: Pre-Allocation Hardening

**Board:** 651953
**Goal:** Quick-win pre-allocation from architecture critique to harden foundation before Timeline system.
**Status:** 🆕 PLANNED

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

The following remained unimplemented:
- ❌ EventBus queue capacity pre-allocation
- ❌ Command buffer pool sizing
- ❌ Deferred destruction pool pre-sizing
- ❌ Full allocation tracker instrumentation

---

## Tasks

### #302: EventBus Queue Pre-Allocation (4h) - HIGH

**Problem:** EventBus event queue can reallocate during frame execution.

**Solution:**
```cpp
class EventBus {
    std::vector<Event> eventQueue;
    static constexpr size_t INITIAL_CAPACITY = 1024;

    EventBus(size_t expectedEventRate = INITIAL_CAPACITY) {
        eventQueue.reserve(expectedEventRate);
    }
};

// In RenderGraph::Setup()
size_t expectedEvents = nodes.size() * 3;  // Heuristic
eventBus->PreAllocate(expectedEvents);
```

**Files to Change:**
- `libraries/EventBus/include/MessageBus.h`
- `libraries/RenderGraph/src/Core/RenderGraph.cpp`

---

### #301: Command Buffer Pool Sizing (4h) - MEDIUM

**Problem:** Command buffers allocated per-frame without pre-sizing.

**Solution:**
- Add `GetCommandBufferEstimate()` to nodes
- Pre-size command buffer pool in CommandPoolNode based on node count

**Files to Change:**
- `libraries/RenderGraph/include/Core/NodeInstance.h`
- `libraries/RenderGraph/src/Nodes/CommandPoolNode.cpp`

---

### #300: Deferred Destruction Pool Pre-Sizing (4h) - MEDIUM

**Problem:** DeferredDestruction queue grows dynamically during cleanup.

**Solution:**
```cpp
class DeferredDestruction {
    std::vector<std::unique_ptr<Resource>> slots;

    void PreReserve(size_t maxResourcesPerFrame) {
        slots.reserve(maxResourcesPerFrame);
    }
};

// In RenderGraph::Setup()
size_t maxResources = nodes.size() * 5;  // Heuristic
deferredDestruction.PreReserve(maxResources);
```

**Files to Change:**
- `libraries/ResourceManagement/include/Lifetime/DeferredDestruction.h`
- `libraries/RenderGraph/src/Core/RenderGraph.cpp`

---

### #299: Allocation Tracker Full Instrumentation (4h) - MEDIUM

**Problem:** Allocation tracking design doc exists (Phase 4.3) but not fully implemented.

**Solution:**
- Add `AllocationSnapshot` struct to `IMemoryAllocator`
- Log deltas between Setup and Execute phases
- Warn if runtime allocations exceed threshold

**Files to Change:**
- `libraries/ResourceManagement/include/Memory/IMemoryAllocator.h`
- `libraries/ResourceManagement/src/Memory/DeviceBudgetManager.cpp`

---

## Success Metrics

- [ ] EventBus handles 1K+ events without reallocation
- [ ] Command buffer pool pre-sized to node count
- [ ] Deferred destruction queue bounded
- [ ] Allocation delta logged between Setup and Execute

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
