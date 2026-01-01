---
title: Sprint 4 - Resource Manager Integration
status: in_progress
sprint: 4
board_id: 651780
created: 2026-01-01
---

# Resource Manager Integration Plan

## Overview
Complete integration of resource tracking across entire application with split budget management architecture.

## Architecture
- **HostBudgetManager** (RenderGraph): CPU heap, staging buffers, SlotTask parallelism
- **DeviceBudgetManager** (VulkanDevice): GPU VRAM via IMemoryAllocator abstraction
- **IMemoryAllocator**: Interface supporting VMA (default) or custom allocators
- **Communication Bridge**: Staging quota, upload tracking between managers

## HostBudgetManager Stack Management

### Stack-First Allocation Strategy

The HostBudgetManager implements a stack-first allocation strategy for hot path optimization:

```
HostBudgetManager
├── StackBudget (Arena Allocator)
│   ├── Pre-allocated stack arena (configurable)
│   ├── Bump allocator for O(1) allocation
│   ├── Per-frame reset
│   └── RequestStackResource<T>() API
│
├── HeapBudget (Fallback)
│   ├── Graceful stack → heap fallback
│   ├── Tracks fallback frequency
│   └── Warning thresholds
│
└── IResourceConsumer Interface
    ├── RequestAllocation(size, alignment, scope)
    ├── Transparent stack/heap backing
    └── RAII cleanup for both
```

### Design Goals
- **Hot path**: 0 heap allocations in render loop
- **Stack utilization**: >90% before reset
- **Fallback rate**: <5% requests hit heap
- **Auto-sizing**: Profiling hints for arena sizing

## Phase Breakdown

### Phase A: Foundation (28h)
- A.1: ✅ Thread safety for ResourceBudgetManager (2h)
- A.2: ✅ Create IMemoryAllocator interface (2h)
- A.3: ✅ Implement VMAAllocator (2h)
- A.4: ✅ Split budget manager (Host vs Device) (2h)
- A.5: Communication bridge

### Phase B: Allocation Tracking (32h)
- B.1: TrackedAllocation<T> RAII wrapper
- B.2: Migrate BufferHelpers to VMA
- B.3: Migrate 15 node allocation sites
- B.4: Per-heap budget tracking

### Phase C: SlotTask Integration (16h)
- C.1: Mandatory budget manager in ExecuteParallel
- C.2: Budget enforcement in task execution
- C.3: Actual vs estimated tracking

### Phase D: Dashboard & Testing (24h)
- D.1: Resource usage dashboard
- D.2: Budget warning callbacks
- D.3: Thread-safe unit tests
- D.4: VMA integration tests

## Success Metrics
- [ ] All GPU allocations tracked by DeviceBudgetManager
- [ ] All host allocations tracked by HostBudgetManager
- [ ] Budget warnings triggered at thresholds
- [ ] Zero memory leaks detected
- [ ] Resource usage visible in benchmark output

## Related Links
- [[Production-Roadmap-2026]]
- HacknPlan Board: 651780
