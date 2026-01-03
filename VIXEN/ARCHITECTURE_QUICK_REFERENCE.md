# VIXEN Architecture Quick Reference
**Last Updated:** January 3, 2026

---

## 30-Second Overview

**VIXEN** is a production Vulkan render graph engine with:
- ✅ Strong resource ownership (graph owns, nodes access)
- ✅ Event-driven invalidation cascade
- ✅ Compile-time type safety
- ⚠️ **Risk:** Runtime allocations not pre-planned for 11-sprint roadmap
- 🎯 **Solution:** 5 pre-allocation phases, 52-100 hours total

---

## Architecture Layers

```
┌─────────────────────────────────────────┐
│  Application (graph compilation)         │ Setup → Compile → Execute → Cleanup
├─────────────────────────────────────────┤
│  RenderGraph (orchestrator)              │ Owns resources, manages lifecycle
├────────┬────────────┬────────────────────┤
│ Nodes  │ EventBus   │ ResourceManagement │ Node types, invalidation, budget
├────────┼────────────┼────────────────────┤
│ VulkanDevice | CashSystem | Profiler     │ Low-level Vulkan, caching, metrics
└────────┴────────────┴────────────────────┘
```

---

## Pre-Allocation Checklist

| Phase | Component | Status | Impact | Timeline |
|-------|-----------|--------|--------|----------|
| 2.1 | EventBus Queue | Ready | ✅ Zero frame allocations | Sprint 5.3 |
| 2.2 | DescriptorSetCache | Design | 🎯 Eliminates descriptor stalls | Sprint 5.3 |
| 2.3 | Timeline FrameHistory | Design | 🎯 TAA/Motion blur efficient | Sprint 6 |
| 2.4 | Multi-GPU Transfer Queue | Design | 🎯 Prevents deadlock | Sprint 10 |
| 2.5 | StagingBufferPool | Ready | ✅ No upload stalls | Sprint 5.3 |
| 3 | Container Capacity | Design | ✅ Maps/vectors pre-sized | Sprint 5.3-5.4 |
| 4 | Validation Pass | Design | ✅ Early error detection | Sprint 5.4 |

---

## Risk Matrix

| Risk | Level | Mitigation | Timeline |
|------|-------|-----------|----------|
| EventBus queue overflow | 🟡 Medium | Pre-allocate Phase 2.1 | Sprint 5.3 |
| Descriptor pool exhaustion | 🔴 High | Pre-calculate Phase 2.2 | Sprint 5.3 |
| Timeline allocation fragmentation | 🔴 High | Design RFC Sprint 5.4 | Sprint 6 |
| Multi-GPU transfer deadlock | 🔴 High | Size queue Phase 2.4 | Sprint 10 |
| Physics frame stalls | 🟡 Medium | Profile early, use LOD | Sprint 7 |
| VR 90 FPS unstable | 🔴 High | Budget tracking, foveation | Sprint 14 |

---

## Code Patterns

### ✅ GOOD: Pre-allocated
```cpp
class EventBus {
    std::vector<Event> eventQueue;
    void PreAllocate(size_t capacity) { 
        eventQueue.reserve(capacity); 
    }
};
```

### ❌ BAD: Dynamic allocation
```cpp
class EventBus {
    std::vector<Event> eventQueue;
    void Emit(const Event& evt) {
        eventQueue.push_back(evt);  // Reallocates!
    }
};
```

### ✅ GOOD: Declare requirements
```cpp
class MyNode : public TypedNode {
    std::vector<DescriptorSetRequirement> 
    GetDescriptorRequirements() const override {
        return { {...}, {...} };
    }
};
```

### ❌ BAD: Allocate on-demand
```cpp
class MyNode : public TypedNode {
    void Execute() {
        auto set = AllocateDescriptorSet(...);  // Late!
    }
};
```

---

## Phases at a Glance

### Setup Phase (Graph initialization)
- Resources pre-allocated
- EventBus queue reserved
- Descriptor pools sized
- Timeline frame history pre-allocated
- Multi-GPU transfer queue sized
- Staging buffers pre-warmed

### Compile Phase (Node preparation)
- Pipelines created
- Descriptors validated
- Command buffers recorded
- **No allocations after pre-reservation**

### Execute Phase (Frame execution)
- **Zero allocations (by design)**
- Reuse pre-allocated resources
- Update descriptor sets (no allocation)
- Record commands (using reserved buffers)

### Cleanup Phase (Resource teardown)
- Deferred destruction (pre-queued)
- Reverse dependency order
- **No allocations**

---

## Document Map

| Document | Purpose | Read Time |
|----------|---------|-----------|
| ARCHITECTURE_CRITIQUE_2026-01-03.md | Full analysis, strengths/weaknesses, risk matrix | 30 min |
| PRE_ALLOCATION_IMPLEMENTATION_GUIDE.md | Code samples, testing strategy, checklist | 45 min |
| ARCHITECTURE_REVIEW_SUMMARY_2026-01-03.md | Executive summary, next steps, validation gates | 15 min |
| **[This file]** | Quick reference for developers | 5 min |

---

## Key Questions Answered

**Q: Is the current architecture production-ready?**  
A: ✅ Yes, for rendering. Needs pre-allocation planning for next 11 sprints.

**Q: What's the biggest risk?**  
A: 🔴 Runtime allocations during Timeline/Multi-GPU/Physics without pre-planning.

**Q: How much work to fix?**  
A: 52-100 hours, fits within Sprints 5-10 budgets.

**Q: Can we achieve zero-allocation runtime?**  
A: 🎯 Yes, with 5-phase pre-allocation strategy outlined.

**Q: When should we start?**  
A: ⏳ Sprint 5.3 (now), complete by Sprint 5.4.

**Q: What if pre-allocation isn't enough?**  
A: Fallback modes (Fallback allocation with warning, Discard oldest, Strict rejection).

---

## Quick Start for Developers

### Implementing a New Node
1. ✅ Declare descriptor requirements: `GetDescriptorRequirements()`
2. ✅ Declare resource estimates: `GetResourceEstimate()`
3. ✅ Allocate during Compile, not Execute
4. ✅ Use only pre-allocated resources in Execute

### Adding a Feature
1. ✅ Calculate allocation needs (audit phase)
2. ✅ Pre-allocate in Setup
3. ✅ Add tests to NoRuntimeAllocationTest suite
4. ✅ Get AllocationTracker sign-off in code review

### Debugging Allocation Issues
1. Run with `ENABLE_ALLOCATION_TRACKING=ON` build flag
2. Check `AllocationTracker::GetStats()` post-Execute
3. Look for largest allocation: `stats.largestAllocation`
4. Consult PRE_ALLOCATION_IMPLEMENTATION_GUIDE.md for solutions

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| **Compile phase** | Pre-allocates all Vulkan resources | 🔨 In Progress (Sprint 5.3) |
| **Execute phase** | Zero allocations | 🟡 Partial (needs Phases 2-4) |
| **EventBus** | Pre-sized queue | 🔨 Sprint 5.3 |
| **Descriptors** | Pre-calculated pools | 🔨 Sprint 5.3 |
| **Timeline** | Pre-reserved frame history | 🎯 Sprint 6 (design first) |
| **Multi-GPU** | Pre-sized transfer queue | 🎯 Sprint 10 (design first) |
| **Frame time** | Stable (no allocation stalls) | ⏳ Validation in Sprint 5.4 |
| **Code review** | Zero-allocation audit | ✅ Checklist ready |

---

## Contact Points

**Architecture Questions:** See ARCHITECTURE_CRITIQUE_2026-01-03.md  
**Implementation Questions:** See PRE_ALLOCATION_IMPLEMENTATION_GUIDE.md  
**Sprint Planning:** See ARCHITECTURE_REVIEW_SUMMARY_2026-01-03.md  
**Code Review:** Refer to Validation Checklist (phase 4)

---

*Generated January 3, 2026 as part of comprehensive architecture review and pre-allocation planning.*
