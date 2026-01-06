---
tags: [feature, proposal, architecture, active]
created: 2026-01-06
status: active
priority: high
complexity: medium
sprint: Sprint 6.3
design-element: 38
parent-work-item: 338
related:
  - "[[timeline-execution-system]]"
  - "[[Sprint6.2-TaskQueue-System]]"
  - "[[../Production-Roadmap-2026]]"
---

# Feature Proposal: Timeline Capacity Tracker & Adaptive Scheduling

**Author:** User + Claude
**Date:** 2026-01-06
**Status:** Active Proposal
**Sprint:** 6.3 - Phase 3 of Timeline Foundation

---

## Executive Summary

**Objective:** Implement runtime performance tracking and adaptive task scheduling by measuring actual CPU/GPU execution times and dynamically adjusting workload based on available timeline capacity.

**Current Gap:** TaskQueue (Sprint 6.2) uses **estimated** costs for budget enforcement, but has no feedback mechanism to:
1. Measure actual GPU/CPU time consumed
2. Compare estimates vs. reality
3. Adapt future scheduling based on measurements
4. Visualize timeline utilization

**Proposed Solution:** TimelineCapacityTracker that bridges the gap between budget planning (estimates) and runtime execution (measurements), enabling:
- **Adaptive Scheduling:** Add more work when GPU idle, reduce when over budget
- **Predictive Estimates:** Learn from actual measurements to improve future predictions
- **Runtime Visibility:** Clear representation of timeline utilization
- **Multi-Device Awareness:** Identify bottlenecks (GPU vs. CPU vs. Transfer)

**Key Benefit:** Transform static budget enforcement into dynamic, self-tuning performance management.

---

## 1. Problem Statement

### 1.1 Current System (Sprint 6.2 - TaskQueue)

**What we have:**
```cpp
TaskQueue<DispatchPass> queue;
queue.SetFrameBudget(16'666'666);  // 16.67ms budget

TaskSlot slot;
slot.estimatedCostNs = 2'000'000;  // Estimate: 2ms
queue.TryEnqueue(std::move(slot));  // Accept if estimate fits
```

**Limitations:**
1. **Estimates are static** - No feedback from actual execution
2. **No adaptation** - Can't increase work when GPU is idle
3. **No learning** - Same estimate used even if consistently wrong
4. **No bottleneck identification** - Don't know if GPU or CPU is limiting factor

### 1.2 Gap Analysis

| Feature | Budget System (Current) | Measurement System (Needed) |
|---------|------------------------|----------------------------|
| Planning | ✅ Estimate-based budgets | ❌ No actual measurement |
| Enforcement | ✅ Reject over-budget tasks | ❌ No runtime adaptation |
| Feedback | ❌ No measurement loop | ✅ Measure actual cost |
| Prediction | ❌ Static estimates | ✅ Learn from history |
| Visibility | ❌ No utilization metrics | ✅ Timeline representation |
| Adaptation | ❌ Fixed task count | ✅ Dynamic scheduling |

### 1.3 Motivating Use Cases

**Use Case 1: Variable Workload (Game)**
```
Frame 1: 100 objects visible → Queue 100 draw tasks → GPU 60% utilized → Idle time wasted
Frame 2: 200 objects visible → Queue 200 draw tasks → GPU 120% utilized → Frame drop
```
**Solution:** Adaptive scheduler detects idle GPU (Frame 1) and adds extra quality passes (SSAO, shadows). Detects overload (Frame 2) and reduces quality.

**Use Case 2: Performance Prediction (Research)**
```
Benchmark: Run 180 configurations, estimate total time needed
Problem: Static estimates say "2 hours", actually takes 4 hours → Wasted time
```
**Solution:** Measure first 10 configs, update estimates, predict remaining time accurately.

**Use Case 3: Multi-Device Balancing**
```
CPU: 40% utilized, GPU: 95% utilized → GPU-bound
Issue: Can't add more GPU work, but CPU idle
```
**Solution:** Identify GPU bottleneck, shift work to CPU (physics, culling, data prep).

---

## 2. Proposed Architecture

### 2.1 Core Components

```cpp
// New file: libraries/RenderGraph/include/Core/TimelineCapacityTracker.h

namespace Vixen::RenderGraph {

/**
 * @brief Device-specific timeline tracking
 */
struct DeviceTimeline {
    // Configuration (static per frame)
    uint64_t budgetNs;              // Target frame time (e.g., 16.67ms)

    // Measurement (runtime, updated during frame)
    uint64_t measuredNs;            // Actual time consumed
    uint64_t remainingNs;           // budgetNs - measuredNs

    // Utilization (computed)
    float utilization;              // measuredNs / budgetNs (0.0-1.0+)
    bool exceededBudget;            // True if utilization > 1.0

    // Tracking
    uint32_t frameNumber;
    uint32_t taskCount;             // Tasks executed this frame
};

/**
 * @brief System-wide timeline capacity tracking
 */
struct SystemTimeline {
    DeviceTimeline gpu;             // GPU execution time
    DeviceTimeline cpu;             // CPU overhead time
    DeviceTimeline transfer;        // DMA/transfer time (future)

    // Bottleneck identification
    enum class Bottleneck { None, GPU, CPU, Transfer };
    Bottleneck GetBottleneck() const;
};

/**
 * @brief Runtime performance tracker with adaptive scheduling
 *
 * Sprint 6.3: Timeline Capacity Tracker
 * Design Element: #38 Timeline Capacity Tracker
 *
 * Bridges budget planning (estimates) with runtime execution (measurements).
 * Provides feedback loop for adaptive scheduling and predictive estimation.
 *
 * Key Features:
 * - Measures actual GPU/CPU time via timestamp queries + profiler
 * - Tracks utilization (% of frame budget used)
 * - Learns from measurements to improve future estimates
 * - Suggests additional tasks when capacity available
 * - Identifies bottlenecks (GPU vs. CPU)
 *
 * @see TaskQueue for budget-aware task scheduling (estimates)
 * @see DeviceBudgetManager for memory budget tracking
 * @see GPUTimestampQuery for GPU time measurement
 */
class TimelineCapacityTracker {
public:
    /**
     * @brief Configuration for capacity tracking
     */
    struct Config {
        uint64_t gpuTimeBudgetNs = 16'666'666;      // 60 FPS target
        uint64_t cpuTimeBudgetNs = 8'000'000;       // Half frame for CPU
        uint32_t historyDepth = 60;                 // Frames to track
        float adaptiveThreshold = 0.90f;            // Add work if < 90% utilized
        bool enableAdaptiveScheduling = true;       // Auto-adjust task count
    };

    TimelineCapacityTracker(const Config& config = Config{});
    ~TimelineCapacityTracker() = default;

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin new frame, reset measurements
     */
    void BeginFrame();

    /**
     * @brief End frame, compute utilization and store in history
     */
    void EndFrame();

    // =========================================================================
    // Measurement Recording
    // =========================================================================

    /**
     * @brief Record actual GPU time for a task
     *
     * Called after task execution with measured time from GPUTimestampQuery.
     *
     * @param nanoseconds Measured GPU time
     */
    void RecordGPUTime(uint64_t nanoseconds);

    /**
     * @brief Record actual CPU time for a task
     *
     * Called after task execution with measured time from Profiler.
     *
     * @param nanoseconds Measured CPU time
     */
    void RecordCPUTime(uint64_t nanoseconds);

    // =========================================================================
    // Capacity Queries
    // =========================================================================

    /**
     * @brief Get current frame timeline state
     */
    [[nodiscard]] const SystemTimeline& GetCurrentTimeline() const;

    /**
     * @brief Get remaining GPU budget
     * @return Nanoseconds remaining (0 if over budget)
     */
    [[nodiscard]] uint64_t GetGPURemainingBudget() const;

    /**
     * @brief Get remaining CPU budget
     * @return Nanoseconds remaining (0 if over budget)
     */
    [[nodiscard]] uint64_t GetCPURemainingBudget() const;

    /**
     * @brief Check if system can schedule more work
     *
     * @return true if utilization < adaptiveThreshold (default 90%)
     */
    [[nodiscard]] bool CanScheduleMoreWork() const;

    /**
     * @brief Check if system is over budget
     *
     * @return true if GPU or CPU utilization > 100%
     */
    [[nodiscard]] bool IsOverBudget() const;

    // =========================================================================
    // Adaptive Scheduling
    // =========================================================================

    /**
     * @brief Suggest number of additional tasks to schedule
     *
     * Uses remaining budget and task cost estimate to compute how many
     * additional tasks can fit in the current frame.
     *
     * @param estimatedCostPerTaskNs Average cost per task
     * @return Number of additional tasks to schedule (0 if none)
     */
    [[nodiscard]] uint32_t SuggestAdditionalTasks(
        uint64_t estimatedCostPerTaskNs
    ) const;

    /**
     * @brief Compute scale factor for next frame's task count
     *
     * Returns multiplier for task count based on current utilization:
     * - < 80%: return 1.2 (increase by 20%)
     * - 80-95%: return 1.0 (maintain)
     * - > 95%: return 0.8 (decrease by 20%)
     *
     * @return Scale factor for task count (0.5 - 1.5 range)
     */
    [[nodiscard]] float ComputeTaskCountScale() const;

    // =========================================================================
    // Prediction & Learning
    // =========================================================================

    /**
     * @brief Record actual vs. estimated cost for learning
     *
     * Tracks prediction error to improve future estimates.
     *
     * @param estimatedNs Original estimate
     * @param actualNs Measured actual time
     */
    void RecordPredictionError(uint64_t estimatedNs, uint64_t actualNs);

    /**
     * @brief Get average prediction error ratio
     *
     * @return Ratio of actual / estimated (1.0 = perfect, >1.0 = underestimate)
     */
    [[nodiscard]] float GetAveragePredictionError() const;

    /**
     * @brief Adjust estimate based on learned error
     *
     * @param estimate Original estimate
     * @return Adjusted estimate (estimate * avgPredictionError)
     */
    [[nodiscard]] uint64_t AdjustEstimate(uint64_t estimate) const;

    // =========================================================================
    // Historical Statistics
    // =========================================================================

    /**
     * @brief Get average GPU utilization over recent frames
     *
     * @param frameCount Number of frames to average (default 60)
     * @return Average utilization (0.0-1.0+)
     */
    [[nodiscard]] float GetAverageGPUUtilization(uint32_t frameCount = 60) const;

    /**
     * @brief Get average CPU utilization over recent frames
     */
    [[nodiscard]] float GetAverageCPUUtilization(uint32_t frameCount = 60) const;

    /**
     * @brief Get frame history for visualization/analysis
     */
    [[nodiscard]] const std::deque<SystemTimeline>& GetHistory() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update GPU budget target
     */
    void SetGPUBudget(uint64_t nanoseconds);

    /**
     * @brief Update CPU budget target
     */
    void SetCPUBudget(uint64_t nanoseconds);

    /**
     * @brief Enable/disable adaptive scheduling
     */
    void SetAdaptiveScheduling(bool enabled);

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const Config& GetConfig() const { return config_; }

private:
    Config config_;
    SystemTimeline currentFrame_;
    std::deque<SystemTimeline> history_;

    // Prediction learning
    std::vector<float> predictionErrors_;  // Ratio: actual / estimated
    float avgPredictionError_ = 1.0f;

    // Internal helpers
    void UpdatePredictionModel();
    float ComputeAverage(const std::vector<float>& values) const;
};

} // namespace Vixen::RenderGraph
```

### 2.2 Integration with TaskQueue

**Feedback Loop Architecture:**

```cpp
// Modified: libraries/RenderGraph/include/Core/TaskQueue.h

template<typename TTaskData>
class TaskQueue {
public:
    // EXISTING: Budget-aware enqueue with estimates
    bool TryEnqueue(TaskSlot&& slot);

    // NEW: Record actual execution cost for learning
    void RecordActualCost(const TaskSlot& slot, uint64_t actualNs) {
        // Update capacity tracker
        if (capacityTracker_) {
            capacityTracker_->RecordGPUTime(actualNs);
            capacityTracker_->RecordPredictionError(
                slot.estimatedCostNs,
                actualNs
            );
        }
    }

    // NEW: Set capacity tracker for feedback
    void SetCapacityTracker(TimelineCapacityTracker* tracker) {
        capacityTracker_ = tracker;
    }

    // NEW: Enqueue with measured budget (uses actual remaining capacity)
    [[nodiscard]] bool CanEnqueueWithMeasuredBudget(
        const TaskSlot& slot
    ) const {
        if (!capacityTracker_) {
            return TryEnqueue(std::move(slot));  // Fallback to estimates
        }

        // Use adjusted estimate based on learned error
        uint64_t adjustedCost = capacityTracker_->AdjustEstimate(slot.estimatedCostNs);
        return adjustedCost <= capacityTracker_->GetGPURemainingBudget();
    }

private:
    TimelineCapacityTracker* capacityTracker_ = nullptr;  // Optional feedback
};
```

### 2.3 Measurement Integration

**GPU Time Measurement (via GPUTimestampQuery):**

```cpp
// Existing infrastructure: libraries/VulkanResources/include/GPUTimestampQuery.h
// We leverage this existing class

void MultiDispatchNode::ExecuteImpl(TypedExecuteContext& ctx) {
    auto& tracker = ctx.GetCapacityTracker();

    for (auto& task : queuedTasks_) {
        // Record GPU timestamp before dispatch
        uint32_t startQuery = gpuQuery_.BeginQuery(cmdBuffer);

        // Execute task
        RecordDispatch(cmdBuffer, task);

        // Record GPU timestamp after dispatch
        uint32_t endQuery = gpuQuery_.EndQuery(cmdBuffer);

        // After frame, retrieve results
        // (Note: Results available 1-2 frames later due to GPU latency)
        if (gpuQuery_.ResultsAvailable(startQuery)) {
            uint64_t actualGPUTime = gpuQuery_.GetElapsedNanoseconds(startQuery, endQuery);
            taskQueue_.RecordActualCost(task, actualGPUTime);
        }
    }
}
```

**CPU Time Measurement (via Profiler):**

```cpp
// Existing infrastructure: libraries/Profiler/include/Timer.h

void MultiDispatchNode::CompileImpl(TypedCompileContext& ctx) {
    Profiler::Timer cpuTimer;
    cpuTimer.Start();

    // CPU work (pipeline creation, descriptor updates, etc.)
    CreatePipelines();
    UpdateDescriptors();

    cpuTimer.Stop();

    auto& tracker = ctx.GetCapacityTracker();
    tracker.RecordCPUTime(cpuTimer.ElapsedNanoseconds());
}
```

---

## 3. Adaptive Scheduling Examples

### 3.1 Example 1: Dynamic Quality Adjustment

```cpp
void RenderQualityManager::AdjustQuality(TimelineCapacityTracker& tracker) {
    float gpuUtil = tracker.GetCurrentTimeline().gpu.utilization;

    if (gpuUtil < 0.80f) {
        // GPU has spare capacity - increase quality
        EnableSSAO();
        EnableHighResShadows();
        NODE_LOG_INFO("Increasing quality - GPU utilization: " +
                     std::to_string(gpuUtil * 100) + "%");
    } else if (gpuUtil > 0.95f) {
        // GPU overloaded - decrease quality
        DisableSSAO();
        ReduceShadowResolution();
        NODE_LOG_WARNING("Reducing quality - GPU utilization: " +
                        std::to_string(gpuUtil * 100) + "%");
    }
}
```

### 3.2 Example 2: Adaptive Task Count

```cpp
void MultiDispatchNode::ExecuteImpl(TypedExecuteContext& ctx) {
    auto& tracker = ctx.GetCapacityTracker();

    // Execute queued tasks
    uint32_t executedTasks = 0;
    for (auto& task : queuedTasks_) {
        if (!tracker.CanScheduleMoreWork()) {
            NODE_LOG_WARNING("Budget exhausted after " +
                           std::to_string(executedTasks) + " tasks");
            break;  // Stop to avoid frame drop
        }

        RecordDispatch(ctx.GetCommandBuffer(), task);
        ++executedTasks;
    }

    // Adaptive: Add extra work if capacity available
    if (tracker.CanScheduleMoreWork()) {
        uint32_t additionalTasks = tracker.SuggestAdditionalTasks(
            averageTaskCostNs_
        );

        auto extraTasks = optionalTaskQueue_.DequeueBatch(additionalTasks);
        for (auto& task : extraTasks) {
            RecordDispatch(ctx.GetCommandBuffer(), task);
        }

        NODE_LOG_INFO("Added " + std::to_string(additionalTasks) +
                     " optional tasks (GPU utilization: " +
                     std::to_string(tracker.GetCurrentTimeline().gpu.utilization * 100) +
                     "%)");
    }
}
```

### 3.3 Example 3: Bottleneck-Aware Scheduling

```cpp
void HybridScheduler::BalanceWorkload(TimelineCapacityTracker& tracker) {
    auto bottleneck = tracker.GetCurrentTimeline().GetBottleneck();

    switch (bottleneck) {
        case SystemTimeline::Bottleneck::GPU:
            // GPU-bound: Shift work to CPU
            EnableCPUCulling();
            DisableGPUPostProcessing();
            NODE_LOG_INFO("GPU-bound - shifting work to CPU");
            break;

        case SystemTimeline::Bottleneck::CPU:
            // CPU-bound: Shift work to GPU
            EnableGPUCulling();
            AddGPUPostProcessing();
            NODE_LOG_INFO("CPU-bound - shifting work to GPU");
            break;

        case SystemTimeline::Bottleneck::None:
            // Balanced - both idle
            AddOptionalWork();
            NODE_LOG_INFO("Balanced - adding optional work");
            break;
    }
}
```

---

## 4. Implementation Roadmap

### Phase 1: Core Measurement Infrastructure (1-2 weeks)

**Goals:**
- TimelineCapacityTracker class operational
- GPU/CPU time measurement integrated
- Basic tracking (no adaptation yet)

**Deliverables:**
1. `TimelineCapacityTracker.h/.cpp` - Core tracker class
2. GPU timestamp integration via `GPUTimestampQuery`
3. CPU timer integration via `Profiler::Timer`
4. Frame lifecycle hooks (`BeginFrame()`, `EndFrame()`)
5. Utilization computation
6. Tests: measurement accuracy, history tracking
7. Documentation: "Capacity Tracking Guide"

**Success Criteria:**
- Can measure GPU time with ±5% accuracy
- Can measure CPU time with ±2% accuracy
- Can track 60 frames of history
- Can compute utilization correctly

**Effort:** 12-16 hours

**Files Created:**
- `libraries/RenderGraph/include/Core/TimelineCapacityTracker.h` (~300 lines)
- `libraries/RenderGraph/src/Core/TimelineCapacityTracker.cpp` (~200 lines)
- `libraries/RenderGraph/tests/test_capacity_tracker.cpp` (~400 lines, 15 tests)

---

### Phase 2: Feedback Loop & Learning (1 week)

**Goals:**
- Prediction error tracking
- Estimate adjustment based on measurements
- Integration with TaskQueue

**Deliverables:**
1. `RecordPredictionError()` implementation
2. `AdjustEstimate()` with learned correction
3. `TaskQueue::RecordActualCost()` integration
4. Exponential moving average for predictions
5. Tests: prediction accuracy, learning convergence
6. Documentation: "Predictive Estimation Guide"

**Success Criteria:**
- Prediction error converges within 50 frames
- Adjusted estimates within 10% of actual
- Learning persists across frames

**Effort:** 8-12 hours

**Files Modified:**
- `libraries/RenderGraph/include/Core/TaskQueue.h` (+30 lines)
- `libraries/RenderGraph/include/Core/TimelineCapacityTracker.h` (+50 lines)
- `libraries/RenderGraph/src/Core/TimelineCapacityTracker.cpp` (+100 lines)
- `libraries/RenderGraph/tests/test_task_queue.cpp` (+8 tests)

---

### Phase 3: Adaptive Scheduling (1-2 weeks)

**Goals:**
- Dynamic task count adjustment
- Capacity-based work scheduling
- Integration with MultiDispatchNode

**Deliverables:**
1. `SuggestAdditionalTasks()` implementation
2. `ComputeTaskCountScale()` with hysteresis
3. MultiDispatchNode adaptive execution
4. Bottleneck identification logic
5. Tests: adaptive behavior, stability
6. Documentation: "Adaptive Scheduling Tutorial"

**Success Criteria:**
- Maintains 90-95% GPU utilization
- No thrashing (stable task count ±10%)
- Handles sudden workload changes smoothly

**Effort:** 12-16 hours

**Files Modified:**
- `libraries/RenderGraph/include/Nodes/MultiDispatchNode.h` (+40 lines)
- `libraries/RenderGraph/src/Nodes/MultiDispatchNode.cpp` (+80 lines)
- `libraries/RenderGraph/tests/test_adaptive_scheduling.cpp` (NEW, 12 tests)

---

### Phase 4: Visualization & Tooling (Optional, 3-5 days)

**Goals:**
- Real-time timeline view (ImGui)
- Profiler integration
- Export to Chrome tracing format

**Deliverables:**
1. ImGui timeline widget (like Chrome DevTools)
2. Real-time utilization graphs
3. Bottleneck visualization
4. Chrome tracing export (JSON)
5. Documentation: "Timeline Visualization Guide"

**Success Criteria:**
- Can visualize 120 frames in real-time
- Can identify bottlenecks visually
- Can export traces for offline analysis

**Effort:** 8-12 hours (optional)

---

## 5. Integration with Existing Systems

### 5.1 Sprint 6.2 (TaskQueue) Integration

**Current State (Sprint 6.2):**
```cpp
TaskQueue<DispatchPass> queue;
queue.SetFrameBudget(16'666'666);  // Static budget

TaskSlot slot{.estimatedCostNs = 2'000'000};
queue.TryEnqueue(std::move(slot));  // Static estimate
```

**Enhanced State (Sprint 6.3):**
```cpp
TimelineCapacityTracker tracker;
tracker.SetGPUBudget(16'666'666);

TaskQueue<DispatchPass> queue;
queue.SetCapacityTracker(&tracker);  // Link feedback

TaskSlot slot{.estimatedCostNs = 2'000'000};

// Option 1: Use static estimate (backward compatible)
queue.TryEnqueue(std::move(slot));

// Option 2: Use measured capacity (adaptive)
if (queue.CanEnqueueWithMeasuredBudget(slot)) {
    queue.EnqueueUnchecked(std::move(slot));
}

// After execution, record actual cost
uint64_t actualCost = MeasureGPUTime();
queue.RecordActualCost(slot, actualCost);  // Feedback loop
```

### 5.2 DeviceBudgetManager Integration

**Memory + Time Budgets:**

```cpp
DeviceBudgetManager memoryBudget(...);
TimelineCapacityTracker timeBudget(...);

// Check both constraints before scheduling
bool CanScheduleTask(const Task& task) {
    bool hasMemory = memoryBudget.GetAvailableDeviceMemory() >= task.memoryNeed;
    bool hasTime = timeBudget.CanScheduleMoreWork();
    return hasMemory && hasTime;
}
```

### 5.3 Timeline Execution System Integration

**Composable Timelines with Capacity Tracking:**

```cpp
// Future (Timeline Execution System - Sprint 6.4+)
class TimelineNode : public TypedNode<TimelineConfig> {
    void ExecuteImpl(TypedExecuteContext& ctx) override {
        auto& tracker = ctx.GetCapacityTracker();

        // Execute sub-graph with capacity tracking
        tracker.BeginFrame();
        subGraph_->Execute();
        tracker.EndFrame();

        // Report to parent timeline
        parentTracker_->RecordGPUTime(tracker.GetCurrentTimeline().gpu.measuredNs);
    }
};
```

---

## 6. Success Metrics

### 6.1 Performance Metrics

| Metric | Baseline | Target | Measurement |
|--------|----------|--------|-------------|
| GPU Utilization | 60-70% (static) | 85-95% (adaptive) | Avg over 1000 frames |
| Frame Time Variance | ±30% | ±10% | Std deviation |
| Prediction Accuracy | N/A | ±10% after 50 frames | RMSE |
| Adaptation Latency | N/A | <5 frames | Time to stabilize |

### 6.2 Usability Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| API Simplicity | <10 lines for basic usage | Code review |
| Documentation Coverage | 100% public API | Doc audit |
| Learning Curve | <1 hour to understand | User study |

### 6.3 Correctness Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Measurement Accuracy | ±5% GPU, ±2% CPU | Validation vs. NSight |
| Budget Enforcement | 100% compliance | Validation layer |
| Memory Safety | 0 leaks/crashes | Valgrind / ASan |

---

## 7. Cost-Benefit Analysis

### 7.1 Development Cost

| Phase | Duration | Complexity | Effort |
|-------|----------|------------|--------|
| Phase 1: Measurement | 1-2 weeks | Medium | 12-16h |
| Phase 2: Learning | 1 week | Medium | 8-12h |
| Phase 3: Adaptation | 1-2 weeks | Medium | 12-16h |
| Phase 4: Visualization | 3-5 days (optional) | Low | 8-12h |
| **Total** | **3-5 weeks** | **Medium** | **40-56h** |

**Team Requirements:**
- 1 engineer (familiar with RenderGraph, profiling)

**Estimated Full-Time Equivalent:** 0.5 FTE over 1 month

### 7.2 Performance Benefits

| Scenario | Current | With Adaptive | Improvement |
|----------|---------|---------------|-------------|
| Variable workload (game) | 60% util | 90% util | +50% throughput |
| Overload protection | Frame drops | Graceful degradation | Stable framerate |
| Benchmark prediction | ±50% error | ±10% error | 5x accuracy |

### 7.3 Memory Cost

| Component | Per-Instance Overhead |
|-----------|----------------------|
| TimelineCapacityTracker | ~50 KB (60-frame history) |
| Per-frame SystemTimeline | ~100 bytes |
| Prediction error tracking | ~4 KB (1000 samples) |

**Total Overhead:** ~54 KB (negligible)

---

## 8. Risks & Mitigation

### 8.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| GPU timestamp latency | High | Medium | Accept 1-2 frame delay, use smoothing |
| Prediction oscillation | Medium | High | Add hysteresis (±10% band) |
| Measurement overhead | Low | Medium | Batch queries, sample subset of tasks |
| Driver variance | Medium | Low | Test on NVIDIA, AMD, Intel |

### 8.2 Integration Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| TaskQueue API break | Low | High | Additive API, backward compatible |
| Performance regression | Low | High | Benchmark before/after |
| Learning instability | Medium | Medium | Exponential smoothing, cap adjustments |

---

## 9. Alternatives Considered

### 9.1 Alternative 1: Static Over-Provisioning

**Description:** Always assume worst-case, allocate max budget.

**Pros:**
- Simple, no runtime overhead

**Cons:**
- Wastes GPU capacity (idle 30-40%)
- No adaptation to varying workloads

**Verdict:** ❌ Rejected - leaves performance on table

### 9.2 Alternative 2: Manual Tuning

**Description:** User manually adjusts task counts based on profiling.

**Pros:**
- No complexity
- User has full control

**Cons:**
- Requires expertise
- Doesn't adapt to runtime conditions
- Breaks with hardware/workload changes

**Verdict:** ❌ Rejected - not self-tuning

### 9.3 Alternative 3: External Profiler Integration

**Description:** Use NSight/RenderDoc/PIX for feedback.

**Pros:**
- Professional tools
- Rich visualizations

**Cons:**
- External dependency
- Not runtime-adaptive
- Offline analysis only

**Verdict:** ⚠️ Complementary - use for validation, not runtime

---

## 10. Open Questions

### 10.1 Design Questions

1. **GPU Timestamp Latency:** How to handle 1-2 frame delay in query results?
   - **Proposal:** Use exponential moving average over 10 frames for stability

2. **Hysteresis Band:** What tolerance for utilization before adapting?
   - **Proposal:** ±10% band (adapt only if utilization changes >10%)

3. **Multi-Queue Tracking:** Track compute/graphics/transfer separately?
   - **Proposal:** Phase 1 = GPU total, Phase 2 = per-queue breakdown

4. **Cross-Timeline Aggregation:** How to track nested timeline budgets?
   - **Proposal:** Hierarchical tracking (parent = sum of children)

### 10.2 Implementation Questions

1. **Measurement Sampling:** Profile every task or sample subset?
   - **Proposal:** Sample 10% of tasks (statistical accuracy sufficient)

2. **Prediction Model:** Exponential vs. linear moving average?
   - **Proposal:** Exponential (weights recent frames higher)

3. **Adaptation Rate:** How fast to adjust task count?
   - **Proposal:** Max ±20% per frame (prevents oscillation)

---

## 11. Dependencies

### 11.1 Prerequisites

- ✅ Sprint 6.2 Complete (TaskQueue with budget enforcement)
- ✅ GPUTimestampQuery operational
- ✅ Profiler::Timer operational
- ⏳ Sprint 6.1 Complete (MultiDispatchNode foundation)

### 11.2 Blocks

- Sprint 6.4: Timeline Node System (needs capacity tracking for nested timelines)
- Sprint 6.5: Multi-GPU Scheduling (needs per-device capacity tracking)

---

## 12. Related Documentation

- [[Sprint6.2-TaskQueue-System]] - Budget enforcement (estimates)
- [[timeline-execution-system]] - Timeline Foundation architecture
- [[../Production-Roadmap-2026]] - Master roadmap integration
- [[../../Libraries/RenderGraph]] - RenderGraph API reference
- [[../../Libraries/Profiler]] - CPU profiling infrastructure

**HacknPlan:**
- Design Element: [#38 Timeline Capacity Tracker](hacknplan://design-element/38)
- Parent: [#32 Timeline Execution System](hacknplan://design-element/32)

---

## 13. Conclusion

TimelineCapacityTracker bridges the gap between static budget planning and dynamic runtime adaptation. By measuring actual execution times and learning from predictions, it enables:

**Key Benefits:**
- **+30-50% GPU utilization** through adaptive scheduling
- **Stable framerates** through graceful degradation
- **Accurate benchmarking** through predictive estimates
- **Clear visibility** into timeline capacity

**Key Challenges:**
- GPU timestamp query latency (1-2 frames)
- Prediction stability (hysteresis needed)
- Integration complexity (backward compatibility)

**Recommendation:** Proceed with phased implementation (3 phases, skip optional Phase 4 initially). Re-evaluate after Phase 2 based on prediction accuracy.

**Next Steps:**
1. Create HacknPlan tasks for Sprint 6.3
2. Begin Phase 1 (Measurement Infrastructure)
3. Integrate with existing MultiDispatchNode
4. Benchmark adaptive vs. static scheduling
5. Update Production Roadmap with Sprint 6.3 timeline

---

**Created:** 2026-01-06
**Sprint:** 6.3 - Timeline Capacity Tracker
**Estimated Effort:** 40-56 hours
**Status:** Active Proposal - Ready for Implementation
