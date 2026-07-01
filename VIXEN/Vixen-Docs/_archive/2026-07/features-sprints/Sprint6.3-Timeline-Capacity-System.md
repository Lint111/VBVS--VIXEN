# Sprint 6.3: Timeline Capacity System

**Status**: ✅ COMPLETE (January 2026)
**Board**: 651785
**Design Element**: #38 Timeline Capacity Tracker

---

## Overview

Sprint 6.3 builds the adaptive scheduling infrastructure that bridges budget planning (estimates) with runtime execution (measurements). The system learns from actual performance to improve future estimates and dynamically adjusts workload based on available capacity.

**Update (2026-01-08)**: Sprint expanded to include event-driven architecture refactoring and system decoupling analysis.

---

## Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        TIMELINE CAPACITY SYSTEM                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────┐    ┌─────────────────────┐    ┌─────────────────┐  │
│  │ GPUQueryManager     │    │ TimelineCapacity    │    │ TaskProfile     │  │
│  │ (Timestamp queries) │───▶│ Tracker (Budget/    │───▶│ Registry        │  │
│  └─────────────────────┘    │ Measurement/Stats)  │    │ (Per-task cal.) │  │
│            │                └─────────────────────┘    └─────────────────┘  │
│            │                         │                          │           │
│            ▼                         ▼                          ▼           │
│  ┌─────────────────────┐    ┌─────────────────────┐    ┌─────────────────┐  │
│  │ MultiDispatchNode   │    │ PredictionError     │    │ FrameManager    │  │
│  │ (Timing integration)│    │ Tracker (Learning)  │    │ (Event source)  │  │
│  └─────────────────────┘    └─────────────────────┘    └─────────────────┘  │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                     Event-Driven Architecture                         │   │
│  │  FrameStartEvent → TimelineCapacityTracker → BudgetOverrunEvent      │   │
│  │  FrameEndEvent   → TimelineCapacityTracker → BudgetAvailableEvent    │   │
│  │                                            → TaskProfileRegistry      │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                     CalibrationStore (JSON Persistence)               │   │
│  │                     - Cross-session learning                          │   │
│  │                     - Hardware fingerprint detection                  │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Event-Driven Data Flow (Phase 4)

```
Frame N:
  1. FrameManager.BeginFrame() → Publish FrameStartEvent
  2. TimelineCapacityTracker receives event → BeginFrame()
  3. Tasks execute with GPU timestamps
  4. Measure actual costs
  5. FrameManager.EndFrame() → Publish FrameEndEvent
  6. TimelineCapacityTracker receives event → EndFrame()
     → Publishes BudgetOverrunEvent OR BudgetAvailableEvent
  7. TaskProfileRegistry receives budget event → Auto-adjusts pressure

Decoupled: RenderGraph no longer orchestrates subsystem lifecycles
```

---

## Phase Completion Status

### Phase 0: GPUQueryManager Infrastructure ✅ COMPLETE
- Timestamp query pool management
- Per-frame query allocation
- Elapsed time calculation from GPU timestamps

### Phase 1: TimelineCapacityTracker Foundation ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 1.1 | ✅ | Core structure (DeviceTimeline, SystemTimeline) |
| 1.2 | ✅ | Measurement recording (RecordGPUTime, RecordCPUTime) |
| 1.3 | ✅ | History & statistics tracking (rolling window) |
| 1.4 | ✅ | Damped hysteresis system (±10% max change, 5% deadband) |

**Tests**: 47 tests passing

### Phase 2: TaskQueue Integration ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 2.1 | ✅ | TaskQueue capacity extensions (SetCapacityTracker, RecordActualCost) |
| 2.2 | ✅ | MultiDispatchNode timing integration (GPU timestamps) |

**Tests**: 22 integration tests passing

### Phase 3: Prediction & Calibration ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 3.1 | ✅ | PredictionErrorTracker (error ratio, bias, correction factors) |
| 3.2a | ✅ | TaskProfile + TaskProfileRegistry (core structure) |
| 3.2b | ✅ | Pressure valve logic (Increase/DecreaseLowestPriority) |
| 3.2c | ⏳ | Persistence layer (JSON save/load) - deferred to Phase 6 |

**Tests**: 27 PredictionErrorTracker + 12 TaskProfile tests passing

### Phase 4: Event-Driven Architecture ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 4.1 | ✅ | FrameManager class (single source of frame lifecycle) |
| 4.2 | ✅ | BudgetOverrunEvent / BudgetAvailableEvent message types |
| 4.3 | ✅ | TimelineCapacityTracker event subscriptions |
| 4.4 | ✅ | TaskProfileRegistry budget event subscriptions |
| 4.5 | ✅ | ScopedSubscriptions RAII helper class |
| 4.6 | ✅ | RenderGraph simplified (removed direct calls) |
| 4.7 | ✅ | Codebase-wide EventBus API refactoring |

**Key Files Modified**:
- `FrameManager.h` (NEW) - Single frame lifecycle source
- `Message.h` - Added BudgetOverrun/Available events
- `MessageBus.h` - Added ScopedSubscriptions class
- `TimelineCapacityTracker.h/cpp` - Event subscriptions
- `TaskProfileRegistry.h` - Budget event subscriptions
- `RenderGraph.h/cpp` - Simplified, uses ScopedSubscriptions
- `NodeInstance.h/cpp` - Uses ScopedSubscriptions
- `DeviceBudgetManager.h` - Uses ScopedSubscriptions
- `MainCacher.h` - Uses ScopedSubscriptions

**Build Status**: 108 tests passing, all libraries compile

### Phase 5: System Decoupling Analysis ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 5.1 | ✅ | RenderGraph core system mapping |
| 5.2 | ✅ | VulkanDevice/GPU layer analysis |
| 5.3 | ✅ | Host layer (Window/Input) analysis |
| 5.4 | ✅ | Decoupling recommendations document |

**Output**: `System-Decoupling-Analysis-Phase2.md`

### Phase 6: Frame Lifecycle Decoupling ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 6.1 | ✅ | LifetimeScopeManager → FrameStart/End events |
| 6.2 | ✅ | DeferredDestructionQueue → FrameStart event |
| 6.3 | ⏭️ | GPUQueryManager - SKIPPED (requires command buffer context) |
| 6.4 | ✅ | RenderGraph updated - direct calls skipped when event-driven |

**Impact**: Systems self-manage via event subscriptions when `SetAutoPressureAdjustment(true)`

**Key Files Modified**:
- `LifetimeScope.h` - Added `SubscribeToFrameEvents()`, ScopedSubscriptions member
- `DeferredDestruction.h` - Added `SubscribeToFrameEvents()`, ScopedSubscriptions member
- `RenderGraph.cpp` - `InitializeEventDrivenSystems()` subscribes new systems
- `RenderGraph.cpp` - `RenderFrame()` skips direct calls when event-driven

### Phase 7: Persistence & Polish ✅ COMPLETE

| Sub-phase | Status | Description |
|-----------|--------|-------------|
| 7.1 | ✅ | CalibrationStore (JSON save/load for TaskProfiles) |
| 7.2 | ✅ | Hardware fingerprint detection |
| 7.3 | ✅ | Documentation and examples |

**Phase 7.1 Implementation (2026-01-08)**:
- Added `ApplicationInitializedEvent` and `ApplicationShuttingDownEvent` to Message.h
- Refactored `CalibrationStore` to be fully autonomous (event-driven)
  - Subscribes to `DeviceMetadataEvent` → configures GPU + loads calibration
  - Subscribes to `ApplicationShuttingDownEvent` → saves calibration
- Integrated into `VulkanGraphApplication` with zero orchestration needed
- Added 6 new event-driven tests (13 total CalibrationStore tests)
- All 56 TaskProfile/CalibrationStore tests passing

**Phase 7.2 Implementation (2026-01-08)**:
- Added `driverVersion` to `DeviceInfo` struct in Message.h
- Extended `GPUIdentifier` with driver version tracking
- CalibrationStore now detects driver version changes and warns during Load()
- `HasDriverVersionMismatch()` API for consumers to check recalibration need

**Phase 7.3 Documentation (2026-01-08)**:
- API documentation in CalibrationStore.h header
- Usage examples inline in header
- Sprint documentation updated

---

## Files Created/Modified

### Phase 1-2 Files

| File | Lines | Purpose |
|------|-------|---------|
| `Core/GPUQueryManager.h` | ~150 | Timestamp query management |
| `Core/GPUQueryManager.cpp` | ~200 | Implementation |
| `Core/TimelineCapacityTracker.h` | ~700 | Budget/measurement tracking |
| `Core/TimelineCapacityTracker.cpp` | ~300 | Implementation + event subscriptions |
| `Core/TaskQueue.h` | ~450 | Capacity tracker integration |
| `Core/TaskQueue.cpp` | ~80 | RecordActualCost impl |
| `Nodes/MultiDispatchNode.h` | +60 | GPU timing members |
| `Nodes/MultiDispatchNode.cpp` | +100 | Timing measurement logic |

### Phase 3 Files

| File | Lines | Purpose |
|------|-------|---------|
| `Core/PredictionErrorTracker.h` | ~380 | Error tracking & correction |
| `Core/TaskProfile.h` | ~200 | Work unit calibration |
| `Core/TaskProfileRegistry.h` | ~350 | Central profile management + event subscriptions |

### Phase 4 Files

| File | Lines | Purpose |
|------|-------|---------|
| `Core/FrameManager.h` | ~60 | Single frame lifecycle source |
| `EventBus/Message.h` | +80 | Budget event types |
| `EventBus/MessageBus.h` | +100 | ScopedSubscriptions class |

### Phase 7 Files

| File | Lines | Purpose |
|------|-------|---------|
| `Core/CalibrationStore.h` | ~460 | Event-driven JSON persistence |
| `EventBus/Message.h` | +60 | ApplicationInitialized/ShuttingDown events |

### Test Files

| File | Tests | Purpose |
|------|-------|---------|
| `test_gpu_query_manager.cpp` | 15 | GPUQueryManager unit tests |
| `test_timeline_capacity_tracker.cpp` | 47 | TimelineCapacityTracker tests |
| `test_task_queue.cpp` | 35 | TaskQueue tests (incl. Phase 2.1) |
| `test_multidispatch_integration.cpp` | 22 | Phase 2.2 integration tests |
| `test_prediction_error_tracker.cpp` | 27 | Phase 3.1 error tracking tests |
| `test_task_profile.cpp` | 56 | Phase 3.2 TaskProfile + Phase 7.1 CalibrationStore tests |

---

## Test Summary

| Component | Tests | Status |
|-----------|-------|--------|
| GPUQueryManager | 35 | ✅ PASS |
| TimelineCapacityTracker | 47 | ✅ PASS |
| TaskQueue | 35 | ✅ PASS |
| MultiDispatchIntegration | 22 | ✅ PASS |
| PredictionErrorTracker | 27 | ✅ PASS |
| TaskProfile + CalibrationStore | 56 | ✅ PASS |
| **Total** | **222** | ✅ ALL PASS |

---

## ScopedSubscriptions API (Phase 4.5)

New RAII helper for clean EventBus subscriptions:

```cpp
class ScopedSubscriptions {
public:
    ScopedSubscriptions() = default;
    explicit ScopedSubscriptions(MessageBus* bus);
    ~ScopedSubscriptions(); // Auto-unsubscribe all

    void SetBus(MessageBus* bus);

    // Type-safe subscription
    template<typename EventType>
    void Subscribe(std::function<void(const EventType&)> handler);

    // With return value support
    template<typename EventType>
    void SubscribeWithResult(std::function<bool(const EventType&)> handler);

    void UnsubscribeAll();
    bool HasSubscriptions() const;
    MessageBus* GetBus() const;
};
```

**Usage Example:**
```cpp
class MySystem {
    ScopedSubscriptions subscriptions_;

    void Initialize(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(
            [this](const FrameStartEvent& e) { OnFrameStart(e); }
        );
    }
    // Destructor auto-unsubscribes - no manual cleanup needed
};
```

---

## Phase 6 Implementation Plan

### 6.1 LifetimeScopeManager Event Integration

```cpp
// Current (in RenderGraph::RenderFrame):
if (scopeManager_) scopeManager_->BeginFrame();
// ...
if (scopeManager_) scopeManager_->EndFrame();

// Proposed:
class LifetimeScopeManager {
    ScopedSubscriptions subscriptions_;

    void SubscribeToFrameEvents(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(
            [this](const auto& e) { BeginFrame(); }
        );
        subscriptions_.Subscribe<FrameEndEvent>(
            [this](const auto& e) { EndFrame(); }
        );
    }
};
```

### 6.2 DeferredDestructionQueue Event Integration

```cpp
// Current (in RenderGraph::RenderFrame):
deferredDestruction.ProcessFrame(globalFrameIndex);

// Proposed:
class DeferredDestructionQueue {
    ScopedSubscriptions subscriptions_;

    void SubscribeToFrameEvents(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(
            [this](const auto& e) { ProcessFrame(e.frameNumber); }
        );
    }
};
```

### 6.3 GPUQueryManager Event Integration

```cpp
// Current (scattered in nodes):
queryManager_->BeginFrame(); // MultiDispatchNode
queryManager_->BeginFrame(); // GeometryRenderNode
queryManager_->BeginFrame(); // TraceRaysNode

// Proposed (single initialization):
class GPUQueryManager {
    ScopedSubscriptions subscriptions_;

    void SubscribeToFrameEvents(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(
            [this](const auto& e) { BeginFrame(); }
        );
    }
};
```

---

## Next Steps

### Sprint 6.3 Completion Summary

All phases complete:
- [x] Phase 0: GPUQueryManager Infrastructure
- [x] Phase 1: TimelineCapacityTracker Foundation
- [x] Phase 2: TaskQueue Integration
- [x] Phase 3: Prediction & Calibration
- [x] Phase 4: Event-Driven Architecture
- [x] Phase 5: System Decoupling Analysis
- [x] Phase 6: Frame Lifecycle Decoupling
- [x] Phase 7: Persistence & Polish

### Additional Fixes (2026-01-08)

- Fixed VulkanGraphApplication.cpp syntax errors (extra parentheses in SlotRoleModifier calls)
- Fixed ExtractField API to return modifier directly (not unique_ptr) for variadic template compatibility
- Added 2-argument ExtractField overload for combined field extraction + slot role override

### Additional Fixes (2026-01-09)

**Accumulation + Field Extraction Pipeline Fix:**
- Fixed SEH exception (access violation) in accumulation + field extraction tests
- Root cause: `AccumulationConnectionRule::Resolve` was calling `AddDependency` on mock node pointers
- Added `skipDependencyRegistration` flag to `ConnectionContext` for test control
- Reordered `Resolve` to add accumulation entry FIRST (before dependency registration)

**Accumulation Slot Semantics Enforcement:**
- Removed `Role` parameter from `ACCUMULATION_INPUT_SLOT` and `ACCUMULATION_INPUT_SLOT_V2` macros
- Accumulation slots now ALWAYS use `SlotRole::Execute` (hardcoded, not configurable)
- Rationale:
  - Accumulated vector is rebuilt each frame (reset semantics)
  - No dependency propagation needed - consumer processes fresh data each cycle
  - Source changes don't need to trigger target rebuild
- Result lifetime is always **Transient** (ephemeral, don't cache across frames)
- Source lifetime can still be Persistent (enables field extraction) or Transient

**Files Modified:**
- `ConnectionTypes.h` - Added `skipDependencyRegistration` flag
- `AccumulationConnectionRule.cpp` - Reordered operations in Resolve
- `ResourceConfig.h` - Updated macros, added documentation
- `MultiDispatchNodeConfig.h`, `BoolOpNodeConfig.h` - Removed Role arguments
- `test_connection_rule.cpp`, `test_connection_concepts.cpp` - Updated test configs

**Test Results:** 109 tests pass (including new `AccumulationSlotForcesExecuteRole` test)

### Potential Next Sprints

1. **Sprint 7: MultiDispatchNode GPU Timing Integration** - Wire actual GPU timestamps into TaskProfiles
2. **Sprint 8: Adaptive LOD System** - Use TaskProfile pressure valves for LOD decisions
3. **Sprint 9: Full Event-Driven Decoupling** - Convert remaining direct calls to event subscriptions

---

## References

- [System-Decoupling-Analysis-Phase2.md](System-Decoupling-Analysis-Phase2.md) - Full decoupling analysis
- [RenderGraph-System-Architecture-Analysis.md](RenderGraph-System-Architecture-Analysis.md) - Original architecture analysis
- [TaskQueue.md](../../Libraries/RenderGraph/TaskQueue.md) - Sprint 6.2 TaskQueue API
- [Design Element #38](https://app.hacknplan.com/p/230809/kanban?elementId=38) - HacknPlan design element
- [Sprint 6.2 Summary](Sprint6.2-TaskQueue-System.md) - Previous sprint completion

