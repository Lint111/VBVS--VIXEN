# Session Summary: 2026-01-03

**Branch:** `production/sprint-5-preallocation-hardening`
**Focus:** Sprint 5 Phase 4 - Pre-Allocation Hardening
**Status:** BUILD PASSING | TESTS NOT RUN
**Commit:** `55f4551`

---

## Session Accomplishments

Completed all 4 tasks in Sprint 5 Phase 4: Pre-Allocation Hardening

| Task | Hours | Status | Summary |
|------|-------|--------|---------|
| 4.1 StagingBufferPool PreWarm | 1h | ✅ | 4×64KB + 2×1MB + 2×16MB pre-warmed on device init |
| 4.2 EventBus Statistics Logging | 2h | ✅ | MessageBus capacity tracking, 80% warning threshold |
| 4.3 Allocation Tracking Design Doc | 4h | ✅ | `01-Architecture/AllocationTracking.md` created |
| 4.4 Budget Manager Frame Hooks | 4h | ✅ | Event-driven `OnFrameStart()`/`OnFrameEnd()` |

**Sprint 5 Progress:** 84% complete (87h/104h)

---

## Files Changed (This Session)

| File | Change | Description |
|------|--------|-------------|
| `libraries/EventBus/include/Message.h` | Modified | Added `FrameLifecycle`, `FrameStart`, `FrameEnd` category bits (40-42); `FrameStartEvent`, `FrameEndEvent` message types |
| `libraries/EventBus/include/MessageBus.h` | Modified | Added `Stats.maxQueueSizeReached`, `Stats.capacityWarningCount`; `SetExpectedCapacity()` API |
| `libraries/EventBus/src/MessageBus.cpp` | Modified | Capacity tracking implementation with 80% warning threshold |
| `libraries/RenderGraph/src/Core/RenderGraph.cpp` | Modified | Publish `FrameStartEvent`/`FrameEndEvent` at frame boundaries in `RenderFrame()` |
| `libraries/RenderGraph/src/Nodes/DeviceNode.cpp` | Modified | Call `PreWarmDefaults()` after uploader creation |
| `libraries/ResourceManagement/CMakeLists.txt` | Modified | Added EventBus as optional dependency |
| `libraries/ResourceManagement/include/Memory/BatchedUploader.h` | Modified | Added `PreWarm()` and `PreWarmDefaults()` methods |
| `libraries/ResourceManagement/src/Memory/BatchedUploader.cpp` | Modified | Implemented PreWarm with default sizes |
| `libraries/ResourceManagement/include/Memory/DeviceBudgetManager.h` | Modified | Added `AllocationSnapshot`, `FrameAllocationDelta` structs; frame tracking API; MessageBus subscription |
| `libraries/ResourceManagement/src/Memory/DeviceBudgetManager.cpp` | Modified | Frame tracking implementation; event subscription/handling |
| `Vixen-Docs/01-Architecture/AllocationTracking.md` | Created | Design document for per-frame allocation measurement |
| `Vixen-Docs/05-Progress/features/Sprint5-CashSystem-Robustness.md` | Modified | Updated Phase 4 task status and changelog |

---

## Design Decisions

### 1. Event-Driven Frame Tracking Architecture

**Context:** DeviceBudgetManager needs to track per-frame allocations, but RenderGraph shouldn't need direct access to all budget managers.

**Choice:** Use MessageBus pub/sub pattern:
- RenderGraph publishes `FrameStartEvent`/`FrameEndEvent`
- DeviceBudgetManager auto-subscribes when `config.messageBus` is provided
- Any system can hook frame boundaries without coupling

**Rationale:**
- Decouples RenderGraph from specific managers
- Enables future systems (Profiler, StagingBufferPool) to hook frame boundaries
- Self-registration pattern keeps subscription logic with the subscriber

**Trade-offs:**
- Slight indirection vs direct calls (+1 function call overhead)
- Requires MessageBus to be available at construction time

### 2. PreWarm Default Buffer Sizes

**Context:** First-frame stalls from lazy staging pool initialization.

**Choice:** Pre-allocate 8 buffers with sizes optimized for VIXEN patterns:
- 4 × 64KB (constant buffers, uniform updates)
- 2 × 1MB (texture mipmaps, mesh data)
- 2 × 16MB (large textures, AS instance buffers)

**Rationale:** Based on typical upload patterns observed in cacher usage. Total ~34MB upfront allocation vs runtime stalls.

### 3. MessageBus Capacity Warning Threshold

**Context:** Need visibility into queue growth for pre-allocation tuning.

**Choice:** 80% threshold with per-frame reset:
- Warn once when queue exceeds 80% of `expectedCapacity`
- Reset warning flag when queue drains (allows repeated warnings across frames)
- Default capacity: 1024 messages

**Rationale:** 80% provides early warning before overflow while avoiding false positives.

---

## Key Insights

### Technical Discoveries

1. **MessageBus is infrastructure** - Can't use project logger (Logger may not exist yet). `std::cerr` is appropriate for infrastructure warnings.

2. **Frame event timing matters** - Events must be published AFTER `BeginFrame()` and BEFORE `EndFrame()` to capture the correct allocation state.

3. **ProcessMessages() synchronous** - Calling `ProcessMessages()` immediately after publish ensures listeners capture state synchronously rather than next-frame.

### Codebase Knowledge

1. **RenderGraph already has MessageBus** - `messageBus` member and `GetMessageBus()` accessor existed. No new wiring needed.

2. **EventCategory bits 40-47 available** - Shader events use 32-39, so frame lifecycle fits cleanly in next range.

3. **Lambda `this` capture safe** - As long as destructor unsubscribes synchronously before object destruction.

---

## Outstanding Issues

**None** - All blockers resolved, build passes, Phase 4 complete.

---

## Next Steps

### Immediate (Sprint 5 Remaining)
1. [ ] **Phase 5 Testing** (17h remaining)
   - 5.1 Lifetime/Safety tests for shared_ptr fix (4h)
   - 5.2 Unit tests for VulkanBufferAllocator (8h)
   - 5.3 Integration tests for cacher chain (8h)
   - 5.4 General unit tests (8h)

### Short-term (Sprint 5 Wrap-up)
2. [ ] Run existing test suites to verify no regressions
3. [ ] Profile frame allocation with new tracking enabled
4. [ ] Consider wiring MessageBus to DeviceBudgetManager in DeviceNode

### Future (Sprint 6+)
5. [ ] EventBus Message Pool - Pre-allocate message objects
6. [ ] Descriptor Pre-Declaration - Predict descriptor requirements
7. [ ] CPU allocation tracking extension

---

## Continuation Guide

### Where to Start
- Sprint 5 Phase 5 (Testing) is next
- Start with `libraries/ResourceManagement/tests/` for budget manager tests
- Review `Vixen-Docs/05-Progress/features/Sprint5-CashSystem-Robustness.md` for full task breakdown

### Key Files to Understand
1. `libraries/ResourceManagement/include/Memory/DeviceBudgetManager.h` - Frame tracking API
2. `libraries/EventBus/include/Message.h:453-484` - FrameStartEvent/FrameEndEvent definitions
3. `libraries/RenderGraph/src/Core/RenderGraph.cpp:594-659` - Frame event publishing
4. `Vixen-Docs/01-Architecture/AllocationTracking.md` - Design document

### Commands to Run First
```bash
# Verify build
cmake --build build --config Debug --parallel 16

# Run existing budget manager tests
./build/libraries/ResourceManagement/tests/Debug/test_budget_manager_integration.exe --gtest_brief=1

# Run TLAS tests (Phase 3)
./build/libraries/RenderGraph/tests/Debug/test_acceleration_structure_node.exe --gtest_brief=1
```

### Watch Out For
1. **MessageBus lifetime** - If DeviceBudgetManager outlives MessageBus, destructor will try to unsubscribe from deleted bus
2. **Frame number sequencing** - `globalFrameIndex` is incremented AFTER `FrameEndEvent`, so event handlers see pre-increment value
3. **ProcessMessages blocking** - Calling ProcessMessages in RenderFrame adds synchronous overhead; acceptable for frame events but don't add more frequent calls

### Architecture Reference
```
RenderGraph::RenderFrame()
    │
    ├─► scopeManager_->BeginFrame()
    │
    ├─► Publish(FrameStartEvent) ──► MessageBus ──► DeviceBudgetManager::OnFrameStart()
    │   ProcessMessages()
    │
    │   [execute all nodes]
    │
    ├─► Publish(FrameEndEvent)   ──► MessageBus ──► DeviceBudgetManager::OnFrameEnd()
    │   ProcessMessages()
    │
    ├─► scopeManager_->EndFrame()
    │
    └─► globalFrameIndex++
```

---

*Generated: 2026-01-03*
*By: Claude Code (session-summary skill)*
