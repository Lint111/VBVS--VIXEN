# Sprint 6.5 Consolidation Audit

**Date**: 2026-01-09
**Sprint**: 6.5 (Task-Level Parallelism Integration)
**Status**: All Phases Complete ✅

---

## Executive Summary

This audit examines Sprint 5-6 features for overlapping logic, implementation gaps, integration issues, API clunkiness, and over-robustness. The core finding is **fragmentation rather than redundancy** - excellent components exist but lack last-mile integration.

| Area | Status | Critical Issues | Action Required |
|------|--------|-----------------|-----------------|
| Cost Estimation | 🟢 Fixed | ~~4 overlapping mechanisms~~ Now uses profiles | ✅ Consolidated around ITaskProfile |
| Measurement Systems | 🟢 Fixed | ~~GPUPerfLogger disconnected~~ Now connected | ✅ Connected in 3 GPU nodes |
| Event Architecture | 🟢 Good | WorkUnitChangeCallback kept for adaptive workload | Kept for future integration |
| Node Profiles | 🟢 Complete | 8 pipeline nodes + 3 GPU nodes profiled | ✅ 11 nodes have profiles |
| API Ergonomics | 🟢 Improved | CreateParallelTasks() helper added | ✅ Reduced boilerplate |
| Calibration Persistence | 🟢 Fixed | ~~Factories not registered~~ Init() added | ✅ Load/Save works end-to-end |
| Frame Processing | 🟢 Fixed | ~~ProcessSamples not called~~ Now at frame end | ✅ Timely statistics |

---

## Implementation Commits (2026-01-09)

| Commit | Description |
|--------|-------------|
| `5a4e59a` | Phase 1: Unified cost estimation via profiles |
| `03c32ef` | Phase 2: 8 pipeline nodes with compile-time profiling |
| `e35471d` | Phase 3: TimelineCapacityTracker Config API simplified |
| `c2c6681` | Integration: Profile factory init + shutdown event |
| `6c01346` | Integration: ProcessAllSamples at frame end |

---

## Integration Gaps Addressed

| Gap | Before | After |
|-----|--------|-------|
| Profile factories | Never registered | `TaskProfileRegistry::Init()` registers built-ins |
| Shutdown event | Never published | `RenderGraph::~RenderGraph()` triggers auto-save |
| Profile registration | Manual | `GetOrCreateProfile()` auto-registers |
| Sample processing | Batched indefinitely | `ProcessAllSamples()` at frame end |
| Config API | 10 params, no helpers | `Config::ForTargetFPS()` factory method |

---

## Remaining Technical Debt

| Item | Priority | Status |
|------|----------|--------|
| PredictionErrorTracker connection | MEDIUM | Future - requires estimate capture before execution |
| 83 TODO comments | LOW | Triage into backlog or remove |
| GPU query integration tests | LOW | Disabled tests in test_gpu_query_manager_integration.cpp |
| MVP stubs (DescriptorSet, ShaderLibrary) | LOW | Document as Phase 2 features |

## Phase 1 Implementation Status (2026-01-09)

| Task | Status | Notes |
|------|--------|-------|
| Remove `VirtualTask.estimatedCostNs` | ✅ Done | Cost now via `GetEstimatedCostFromProfiles()` |
| Bridge `EstimateTaskCost()` to profiles | ✅ Done | Queries Execute phase profiles |
| Connect GPUPerfLogger → TaskProfile | ✅ Done | 3 nodes: Compute, Geometry, TraceRays |
| Add `Sampler::Finalize()` API | ✅ Done | Unified API for external timing sources |
| Add `CreateParallelTasks()` helper | ✅ Done | TypedNode/VariadicNode refactored |
| Remove `WorkUnitChangeCallback` | ❌ Kept | Needed for adaptive workload feature |

---

## 1. Cost Estimation Fragmentation

### 1.1 Problem Statement

Four separate estimation mechanisms exist that don't integrate:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    COST ESTIMATION FRAGMENTATION                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  VirtualTask.estimatedCostNs ──────┐                                   │
│        (VirtualTask.h:185)         │                                   │
│                                    │    NO SYNCHRONIZATION             │
│  ITaskProfile::GetEstimatedCostNs()│◄─── BETWEEN THESE ───►           │
│        (ITaskProfile.h:379)        │                                   │
│                                    │                                   │
│  NodeInstance::EstimateTaskCost()  │                                   │
│        (NodeInstance.h:507)        │                                   │
│                                    │                                   │
│  TaskQueue.estimatedCostNs ────────┘                                   │
│        (TaskQueue.h:86)                                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Mechanism Details

| Mechanism | File:Line | Type | Purpose | Issue |
|-----------|-----------|------|---------|-------|
| `VirtualTask.estimatedCostNs` | VirtualTask.h:185 | Field | Per-task cost | Duplicates profiles |
| `ITaskProfile::GetEstimatedCostNs()` | ITaskProfile.h:379 | Virtual | Polymorphic cost models | Not integrated with NodeInstance |
| `NodeInstance::EstimateTaskCost()` | NodeInstance.h:507 | Virtual | Per-node estimation | Returns 0 by default |
| `TaskQueue::TaskSlot.estimatedCostNs` | TaskQueue.h:86 | Field | Budget checking | 4th copy, manual sync |

### 1.3 Code Evidence

**VirtualTask has both raw field AND profile vector:**
```cpp
// VirtualTask.h:185
uint64_t estimatedCostNs = 0;  // Raw field

// VirtualTask.h:197
std::vector<ITaskProfile*> profiles;  // Also has profiles!

// VirtualTask.h:316 - Tries to reconcile
uint64_t GetEstimatedCostFromProfiles() const {
    if (profiles.empty()) return estimatedCostNs;  // Fallback to raw field
    // ... otherwise compute from profiles
}
```

**NodeInstance::EstimateTaskCost() returns 0:**
```cpp
// NodeInstance.h:507
virtual uint64_t EstimateTaskCost(uint32_t taskIndex) const { return 0; }
// Most nodes don't override - no profile integration!
```

### 1.4 Recommended Fix

**Consolidate around ITaskProfile as single source of truth:**

```cpp
// NodeInstance.h - Bridge to profiles
virtual uint64_t EstimateTaskCost(uint32_t taskIndex) const {
    // Query profile registry instead of returning 0
    if (!owningGraph) return 0;

    auto& registry = owningGraph->GetTaskProfileRegistry();
    std::string profileId = instanceName + "_task_" + std::to_string(taskIndex);

    if (auto* profile = registry.GetProfile(profileId)) {
        return profile->GetEstimatedCostNs();
    }
    return 0;
}
```

**Remove VirtualTask.estimatedCostNs field:**
```cpp
// VirtualTask.h - REMOVE this field
// uint64_t estimatedCostNs = 0;  // DELETE

// Always use GetEstimatedCostFromProfiles()
```

---

## 2. Measurement Pipeline Gaps

### 2.1 Problem Statement

Three measurement systems work in isolation without data sharing:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    DISCONNECTED MEASUREMENT PIPELINES                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  GPUPerformanceLogger          TimelineCapacityTracker    ITaskProfile  │
│         │                              │                       │        │
│         ▼                              ▼                       ▼        │
│  CollectResults()              RecordGPUTime()        RecordMeasurement()│
│         │                              │                       │        │
│         ▼                              ▼                       ▼        │
│     LOGGED                    UTILIZATION             COST LEARNING     │
│         │                     TRACKED                      │            │
│         ▼                              │                       │        │
│  GetLastDispatchMs()                   ▼                       ▼        │
│         │                     BudgetEvents            GetEstimatedCostNs()│
│         │                              │                       │        │
│         └──────────── ✗ ───────────────┴──────── ✗ ────────────┘        │
│                    NO CONNECTION BETWEEN THEM                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Current State by System

**GPUPerformanceLogger** (Used by 4 nodes):
- ComputeDispatchNode.cpp:169
- GeometryRenderNode.cpp:168
- TraceRaysNode.cpp:177
- VoxelGridNode.cpp:139 (memory only)

```cpp
// Current: Collects but doesn't share
gpuPerfLogger_->CollectResults(currentFrameIndex);
auto avgMs = gpuPerfLogger_->GetAverageDispatchMs();
// Data stops here - logged but not used for learning
```

**TimelineCapacityTracker** (Used by 1 node):
- MultiDispatchNode.cpp:329

```cpp
// Current: Tracks utilization but no cost learning
capacityTracker_->RecordGPUTime(measuredNs);
// Publishes BudgetEvents but ITaskProfile never learns
```

**ITaskProfile::RecordMeasurement()** (Used by 0 nodes):
- Only called in tests
- Production nodes NEVER call this

### 2.3 Missing Integration Points

| Gap | Source | Destination | Impact |
|-----|--------|-------------|--------|
| GPU timing → Profile | GPUPerformanceLogger | ITaskProfile | Profiles never learn |
| Estimate vs Actual | Any | PredictionErrorTracker | No adaptive correction |
| Profile → VirtualTask | ITaskProfile | VirtualTask.profiles | Estimates stale |

### 2.4 Recommended Fix

**Connect the pipeline in profiled nodes:**

```cpp
// ComputeDispatchNode.cpp - AFTER CollectResults()
void ComputeDispatchNode::ExecuteImpl(ExecuteContext& ctx) {
    // ... existing dispatch code ...

    // EXISTING: Collect GPU timing
    gpuPerfLogger_->CollectResults(currentFrameIndex);
    uint64_t gpuTimeNs = gpuPerfLogger_->GetLastDispatchNs();

    // NEW: Feed to capacity tracker
    if (auto* tracker = GetTimelineCapacityTracker()) {
        tracker->RecordGPUTime(gpuTimeNs);
    }

    // NEW: Feed to task profile for cost learning
    if (auto* profile = GetTaskProfile("compute_dispatch")) {
        profile->RecordMeasurement(gpuTimeNs);
    }

    // NEW: Record prediction error for adaptive correction
    if (auto* tracker = GetTimelineCapacityTracker()) {
        uint64_t estimated = EstimateTaskCost(0);
        tracker->RecordPrediction("compute_dispatch", estimated, gpuTimeNs);
    }
}
```

---

## 3. Event Architecture Analysis

### 3.1 Event Flow (Working Correctly)

```
Frame Execution
       │
       ▼
RenderGraph publishes FrameEndEvent
       │
       ▼
TimelineCapacityTracker receives FrameEndEvent
       │
       ▼
TimelineCapacityTracker.EndFrame()
       │
       ▼
TimelineCapacityTracker.PublishBudgetEvents()
       │
       ├─► IF over budget → BudgetOverrunEvent
       └─► IF under threshold → BudgetAvailableEvent
       │
       ▼
TaskProfileRegistry receives Budget event
       │
       ▼
TaskProfileRegistry sets deferred flag (pendingDecrease_/pendingIncrease_)
       │
       ▼
RenderGraph.RenderFrame() continues
       │
       ▼
taskProfileRegistry_.ProcessDeferredActions()  ◄── Sprint 6.5 fix
       │
       ├─► DecreaseLowestPriority() OR
       └─► IncreaseHighestPriority()
```

### 3.2 Dead Code: WorkUnitChangeCallback

**Definition** (ITaskProfile.h:600):
```cpp
using WorkUnitChangeCallback = std::function<void(
    const std::string& taskId,
    int32_t oldWorkUnits,
    int32_t newWorkUnits
)>;
```

**Registration** (TaskProfileRegistry.h:412):
```cpp
void SetChangeCallback(WorkUnitChangeCallback callback) {
    changeCallback_ = std::move(callback);
}
```

**Usage**: ONLY in test_task_profile.cpp:526

**Production usage**: NONE

**Recommendation**: Remove or document as "testing/debugging only"

### 3.3 Unused Application Lifecycle Events

| Event | Defined | Published | Consumed |
|-------|---------|-----------|----------|
| ApplicationInitializedEvent | Message.h:565 | ❌ Not found | ❌ None |
| ApplicationShuttingDownEvent | Message.h:591 | ❌ External | CalibrationStore |

**Issue**: CalibrationStore expects shutdown event but RenderGraph doesn't publish it

---

## 4. Node Profile Coverage

### 4.1 Currently Profiled (4 nodes)

| Node | Profile System | Measurement Type |
|------|---------------|------------------|
| ComputeDispatchNode | GPUPerformanceLogger | GPU timestamps |
| TraceRaysNode | GPUPerformanceLogger | GPU timestamps |
| GeometryRenderNode | GPUPerformanceLogger | GPU timestamps |
| MultiDispatchNode | TaskQueue + TimelineCapacityTracker | GPU timing |

### 4.2 Needs Profiling (8 nodes)

| Node | Priority | Profile Type | Phase | TaskId Pattern | Rationale |
|------|----------|--------------|-------|----------------|-----------|
| ComputePipelineNode | MEDIUM | SimpleTaskProfile | Compile | `compute_pipeline_compile` | Pipeline creation 50-500ms |
| GraphicsPipelineNode | MEDIUM | SimpleTaskProfile | Compile | `graphics_pipeline_compile` | Pipeline creation via cacher |
| RayTracingPipelineNode | MEDIUM | SimpleTaskProfile | Compile | `rt_pipeline_compile` | RT pipeline + SBT creation |
| AccelerationStructureNode | MEDIUM | Custom | Compile | `accel_struct_build_{count}` | Scales with primitive count |
| VoxelGridNode | MEDIUM | Custom | Compile | `voxel_grid_gen_{res}^3` | Scales with resolution cubed |
| ShaderLibraryNode | MEDIUM | Custom | Compile | `shader_compile_{name}` | SPIRV compilation |
| DepthBufferNode | MEDIUM | ResolutionTaskProfile | Compile | `depth_buffer_{w}x{h}` | Scales with resolution |
| TextureLoaderNode | LOW | SimpleTaskProfile | Compile | `texture_load` | Image loading + mipmaps |

### 4.3 No Profiling Needed (19 nodes)

Configuration/trivial nodes: SwapChainNode, PresentNode, FrameSyncNode, DescriptorSetNode, DescriptorResourceGathererNode, PushConstantGathererNode, VertexBufferNode, DebugBufferReaderNode, CameraNode, InputNode, WindowNode, DeviceNode, CommandPoolNode, InstanceNode, BoolOpNode, LoopBridgeNode, StructSpreaderNode, FramebufferNode, RenderPassNode

---

## 5. API Over-Robustness

### 5.1 TimelineCapacityTracker (35 methods → ~15)

**Current public method count**: 35

**Methods to REMOVE** (unused/test-only):

```cpp
// Never called in production - only tests
uint32_t SuggestAdditionalTasks(uint64_t estimatedCostPerTaskNs) const;
float ComputeTaskCountScale() const;

// Redundant per-device variants (use GetTimeline() instead)
uint64_t GetGPURemainingBudget(uint32_t queueIndex) const;
uint64_t GetGPURemainingBudget() const;
uint64_t GetMinGPURemainingBudget() const;
uint64_t GetCPURemainingBudget(uint32_t threadIndex) const;
uint64_t GetCPURemainingBudget() const;
uint64_t GetMinCPURemainingBudget() const;

// Consolidate to:
uint64_t GetRemainingBudget() const { return GetMinGPURemainingBudget(); }
```

**Methods to KEEP** (essential):

```cpp
// Frame lifecycle
void BeginFrame();
void EndFrame();

// Measurement recording
void RecordGPUTime(uint64_t nanoseconds);
void RecordCPUTime(uint64_t nanoseconds);

// Budget queries (consolidated)
uint64_t GetRemainingBudget() const;
bool IsOverBudget() const;
bool CanScheduleMoreWork() const;

// Timeline access (for advanced users)
const SystemTimeline& GetCurrentTimeline() const;
const Config& GetConfig() const;

// Event subscription
void SubscribeToFrameEvents(EventBus::MessageBus* messageBus);
```

### 5.2 Config Struct Simplification

**Current** (11 parameters):
```cpp
struct Config {
    uint32_t numGPUQueues = 1;
    uint32_t numCPUThreads = 1;
    uint64_t gpuTimeBudgetNs = 16'666'666;
    uint64_t cpuTimeBudgetNs = 8'000'000;
    uint32_t historyDepth = 60;
    uint32_t maxHistoryDepth = 300;
    float adaptiveThreshold = 0.90f;
    bool enableAdaptiveScheduling = true;
    float hysteresisDamping = 0.10f;
    float hysteresisDeadband = 0.05f;
};
```

**Recommended** (2 parameters for 95% of use cases):
```cpp
struct Config {
    // Essential - most users only need this
    uint64_t targetFrameTimeNs = 16'666'666;  // 60 FPS default

    // Advanced - for tuning (can be internal with setter methods)
    uint32_t numGPUQueues = 1;
    float adaptiveThreshold = 0.90f;
    // ... rest as internal defaults
};
```

### 5.3 VirtualTask Creation Helper

**Current boilerplate** (9 lines per node):
```cpp
std::vector<VirtualTask> GetExecutionTasks(VirtualTaskPhase phase) override {
    if (phase != VirtualTaskPhase::Execute)
        return NodeInstance::GetExecutionTasks(phase);

    std::vector<VirtualTask> tasks;
    for (uint32_t i = 0; i < DetermineTaskCount(); ++i) {
        VirtualTask task;
        task.id = {this, i};
        task.execute = [this, i]() { ExecuteBundle(i); };
        task.estimatedCostNs = EstimateTaskCost(i);
        tasks.push_back(std::move(task));
    }
    return tasks;
}
```

**Recommended helper** (1 line per node):
```cpp
// Add to NodeInstance.h
protected:
    std::vector<VirtualTask> CreateParallelTasks(
        VirtualTaskPhase phase,
        std::function<void(uint32_t)> executeBundle
    ) {
        std::vector<VirtualTask> tasks;
        for (uint32_t i = 0; i < DetermineTaskCount(); ++i) {
            VirtualTask task;
            task.id = {this, i};
            task.execute = [=]() { executeBundle(i); };
            task.estimatedCostNs = EstimateTaskCost(i);
            task.profiles = GetPhaseProfiles(phase);
            tasks.push_back(std::move(task));
        }
        return tasks;
    }

// Usage:
return CreateParallelTasks(phase, [this](uint32_t i) { ExecuteBundle(i); });
```

---

## 6. Implementation Plan

### Phase 1: Critical Fixes (COMPLETED 2026-01-09)

| Task | Status | Files Modified |
|------|--------|----------------|
| 1. Remove `VirtualTask.estimatedCostNs` field | ✅ | VirtualTask.h, NodeInstance.cpp, TypedNodeInstance.h, VariadicTypedNode.h, TBBVirtualTaskExecutor.cpp, tests |
| 2. Bridge `NodeInstance::EstimateTaskCost()` to profiles | ✅ | NodeInstance.h/cpp |
| 3. Connect GPUPerfLogger → TaskProfile in 3 nodes | ✅ | ComputeDispatchNode, GeometryRenderNode, TraceRaysNode (.h/.cpp) |
| 4. Add `Sampler::Finalize()` API for GPU timing | ✅ | ITaskProfile.h, SimpleTaskProfile.h |
| 5. Add `CreateParallelTasks()` helper | ✅ | NodeInstance.h/cpp, TypedNodeInstance.h, VariadicTypedNode.h |
| 6. Keep `WorkUnitChangeCallback` | ✅ | Kept for adaptive workload (Phase 2) |

**New APIs Added:**
```cpp
// Unified timing API (GPU or external sources)
auto sample = profile->Sample();
sample.Finalize(gpuTimeNs);  // External measurement

// Parallel task helper (reduces boilerplate from 15 lines to 5)
return CreateParallelTasks(phase, [this](uint32_t i) {
    TypedExecuteContext ctx(this, i);
    ExecuteImpl(ctx);
});
```

### Phase 2: Profile Coverage (COMPLETED 2026-01-09)

| Task | Status | Files Modified |
|------|--------|----------------|
| 1. Add SimpleTaskProfile to ComputePipelineNode | ✅ | ComputePipelineNode.h/cpp |
| 2. Add SimpleTaskProfile to GraphicsPipelineNode | ✅ | GraphicsPipelineNode.h/cpp |
| 3. Add SimpleTaskProfile to RayTracingPipelineNode | ✅ | RayTracingPipelineNode.h/cpp |
| 4. Add SimpleTaskProfile to AccelerationStructureNode | ✅ | AccelerationStructureNode.h/cpp |
| 5. Add SimpleTaskProfile to VoxelGridNode | ✅ | VoxelGridNode.h/cpp |
| 6. Add SimpleTaskProfile to ShaderLibraryNode | ✅ | ShaderLibraryNode.h/cpp |
| 7. Add SimpleTaskProfile to DepthBufferNode | ✅ | DepthBufferNode.h/cpp |
| 8. Add SimpleTaskProfile to TextureLoaderNode | ✅ | TextureLoaderNode.h/cpp |

**Commit**: `03c32ef` - feat(Sprint6.5): Add compile-time profiling to 8 pipeline nodes

### Phase 3: API Cleanup (COMPLETED 2026-01-09)

| Task | Status | Notes |
|------|--------|-------|
| 1. Consolidate TimelineCapacityTracker methods | ✅ | Documented test-only vs production methods |
| 2. Simplify Config struct | ✅ | Added `Config::ForTargetFPS()` factory, reorganized Essential/Advanced |
| 3. Multi-device tracking | ✅ KEPT | Preserved for future multi-GPU support |
| ~~4. Add file-based Save/Load~~ | ✅ EXISTS | CalibrationStore.h already implements |

**Changes Made:**
- Config struct reorganized: Essential params (gpuTimeBudgetNs, adaptiveThreshold) vs Advanced
- Factory methods: `Config::ForTargetFPS(60.0f)`, `Config::Default()`
- Documented `SuggestAdditionalTasks()` and `ComputeTaskCountScale()` as test-only
- Multi-device tracking kept - needed for future multi-GPU / async compute

---

## 7. Success Metrics

### After Phase 1-2 Completion

| Metric | Before | After Phase 1 | After Phase 2 |
|--------|--------|---------------|---------------|
| Cost estimation mechanisms | 4 | 2 | 2 (profiles + TaskQueue internal) |
| Measurement pipeline connections | 0 | 4 | 4 |
| WorkUnitChangeCallback | Present | Kept | Kept (for adaptive workload) |
| VirtualTask creation boilerplate | 9 lines | 1 line | 1 line |
| Nodes with TaskProfile | 0 | 3 (GPU) | 11 (3 GPU + 8 pipeline) |

### Verification Tests

After Phase 1, these tests should demonstrate end-to-end flow:

1. **Cost estimation flows through profiles**:
   - Call `NodeInstance::EstimateTaskCost()` → Returns profile estimate

2. **Measurements feed cost learning**:
   - Execute profiled node → `ITaskProfile::GetSampleCount()` increases

3. **Prediction tracking works**:
   - Execute with estimate → `PredictionErrorTracker::GetCorrectionFactor()` adjusts

---

## 8. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Breaking existing tests | Medium | Low | Update test expectations |
| Performance regression | Low | Medium | Profile before/after |
| API break for external users | Low | High | Keep deprecated aliases temporarily |

---

## Appendix A: File Reference

### Core Files to Modify

```
libraries/RenderGraph/include/Core/
├── VirtualTask.h              # Remove estimatedCostNs field
├── NodeInstance.h             # Bridge EstimateTaskCost to profiles
├── ITaskProfile.h             # Remove WorkUnitChangeCallback
├── TaskProfileRegistry.h      # Remove WorkUnitChangeCallback
└── TimelineCapacityTracker.h  # (Phase 3) Consolidate methods

libraries/RenderGraph/src/Core/
├── NodeInstance.cpp           # Implement profile bridge
└── TBBVirtualTaskExecutor.cpp # Use GetEstimatedCostFromProfiles()

libraries/RenderGraph/src/Nodes/
├── ComputeDispatchNode.cpp    # Connect GPUPerfLogger → Profile
├── TraceRaysNode.cpp          # Connect GPUPerfLogger → Profile
├── GeometryRenderNode.cpp     # Connect GPUPerfLogger → Profile
└── MultiDispatchNode.cpp      # Already connected, verify
```

### Test Files to Update

```
libraries/RenderGraph/tests/
├── test_virtual_task.cpp              # Update for removed field
├── test_task_profile.cpp              # Update for removed callback
├── test_virtual_task_integration.cpp  # Verify end-to-end flow
└── test_timeline_capacity_tracker.cpp # Verify measurement flow
```

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **ITaskProfile** | Abstract interface for task-specific cost models |
| **SimpleTaskProfile** | Linear cost model: baseline + (workUnits * costPerUnit) |
| **ResolutionTaskProfile** | Quadratic cost model based on resolution |
| **TaskProfileRegistry** | Central registry owning all profiles |
| **TimelineCapacityTracker** | System-wide budget tracking and utilization |
| **GPUPerformanceLogger** | Per-node GPU timing via timestamp queries |
| **PredictionErrorTracker** | Tracks estimate vs actual for adaptive correction |
| **VirtualTask** | Execution unit for task-level parallelism |

---

*Document generated: 2026-01-09*
*Last updated: 2026-01-09 (All phases + integration gaps fixed)*
*Author: Claude Code Audit*
*Sprint: 6.5 Timeline Foundation*
