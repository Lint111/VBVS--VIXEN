---
title: TaskQueue - Budget-Aware Task Scheduling
aliases: [TaskQueue, Budget Queue, Priority Queue]
tags: [rendergraph, taskqueue, timeline, sprint-6-2]
created: 2026-01-06
sprint: Sprint 6.2
related:
  - "[[../MultiDispatchNode]]"
  - "[[../RenderGraph]]"
  - "[[../../05-Progress/features/Sprint6.2-TaskQueue-System]]"
---

# TaskQueue<T> - Budget-Aware Task Scheduling

**Header:** `libraries/RenderGraph/include/Core/TaskQueue.h`
**Sprint:** 6.2 - Timeline Foundation
**Purpose:** Generic priority-based task queue with GPU time/memory budget enforcement

---

## Overview

`TaskQueue<T>` is a template-based task scheduler that combines priority-based execution with resource budget tracking. It enables frame budget enforcement by rejecting or warning about over-budget tasks before execution.

### Key Features

- **Priority-based scheduling** - Tasks execute from highest (255) to lowest (0) priority
- **Stable sort** - Equal-priority tasks preserve insertion order
- **Budget enforcement** - Strict (reject) vs lenient (warn) overflow modes
- **Zero-cost bypass** - Tasks with `estimatedCostNs = 0` bypass budget checks
- **Overflow protection** - Safe arithmetic prevents uint64_t wrap-around
- **Generic template** - Works with any task data type `T`

---

## Template Signature

```cpp
template<typename TTaskData>
class TaskQueue {
public:
    struct TaskSlot {
        TTaskData data;
        uint64_t estimatedCostNs;   // GPU time estimate (0 = bypass budget)
        uint8_t priority;            // 0 (lowest) to 255 (highest)
        uint64_t insertionOrder;     // For stable sort
    };

    // Budget-aware enqueue (returns false if rejected in strict mode)
    bool TryEnqueue(TaskSlot&& slot);

    // Unchecked enqueue (always succeeds, updates budget tracking)
    void EnqueueUnchecked(TaskSlot&& slot);

    // Execute tasks in priority order
    void Execute(const std::function<void(const TTaskData&)>& executor);
    void ExecuteWithMetadata(const std::function<void(const TaskSlot&)>& executor);

    // Budget management
    void SetBudget(const TaskBudget& budget);
    [[nodiscard]] uint64_t GetRemainingBudget() const;
    [[nodiscard]] bool IsBudgetExhausted() const;

    // Queue state
    [[nodiscard]] size_t GetQueuedCount() const;
    void Clear();
};
```

---

## Usage Patterns

### Pattern 1: Strict Budget Enforcement

Reject tasks that would exceed frame budget:

```cpp
TaskQueue<DispatchPass> queue;

// Set 16.67ms budget (60 FPS) with strict rejection
queue.SetBudget(BudgetPresets::FPS60_Strict);

// Enqueue tasks with cost estimates
bool accepted1 = queue.TryEnqueue({prefilter, 5'000'000, 200});  // 5ms, high priority
bool accepted2 = queue.TryEnqueue({mainPass, 8'000'000, 128});    // 8ms, med priority
bool accepted3 = queue.TryEnqueue({postfilter, 6'000'000, 100});  // 6ms - REJECTED (over budget)

if (!accepted3) {
    LOG_WARNING("Task rejected: budget exhausted");
}

// Execute accepted tasks in priority order
queue.Execute([](const DispatchPass& pass) {
    vkCmdDispatch(cmdBuffer, pass.workGroupCount.x, .y, .z);
});

queue.Clear(); // Reset for next frame
```

### Pattern 2: Lenient Mode with Warnings

Accept over-budget tasks but log warnings:

```cpp
TaskQueue<DispatchPass> queue;

// Lenient mode: accept all, warn on overflow
queue.SetBudget(TaskBudget{16'666'666, BudgetOverflowMode::Lenient});

// Set warning callback
queue.SetWarningCallback([](uint64_t newTotal, uint64_t budget, uint64_t taskCost) {
    LOG_WARNING("Budget overflow: {}ns total ({}ns budget), task={}ns",
                newTotal, budget, taskCost);
});

// All tasks accepted, warnings fired if over-budget
queue.TryEnqueue({pass1, 10'000'000, 255});  // Accepted
queue.TryEnqueue({pass2, 12'000'000, 128});  // Accepted + WARNING (22ms > 16.67ms)

queue.Execute([](const DispatchPass& pass) { /* execute */ });
```

### Pattern 3: Zero-Cost Bypass (Backward Compatibility)

Tasks with `estimatedCostNs = 0` bypass budget checks:

```cpp
// Legacy code using QueueDispatch() (zero-cost)
TaskSlot legacySlot;
legacySlot.data = createDispatch();
legacySlot.estimatedCostNs = 0;  // Zero-cost = always accepted
legacySlot.priority = 128;

queue.EnqueueUnchecked(std::move(legacySlot));  // Never rejected
```

---

## Budget Enforcement

### Strict Mode

**Behavior:** Rejects tasks that would exceed budget

```cpp
queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Strict});

queue.TryEnqueue({task1, 6'000'000, 255});  // Accepted (6ms used, 4ms remaining)
queue.TryEnqueue({task2, 5'000'000, 128});  // REJECTED (would exceed 10ms budget)

EXPECT_EQ(queue.GetRemainingBudget(), 4'000'000);  // 4ms remaining
EXPECT_EQ(queue.GetQueuedCount(), 1);              // Only task1 queued
```

### Lenient Mode

**Behavior:** Accepts over-budget tasks, fires warning callback

```cpp
queue.SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Lenient});

queue.SetWarningCallback([](uint64_t newTotal, uint64_t budget, uint64_t taskCost) {
    // newTotal = 16'000'000 (total after adding task2)
    // budget = 10'000'000 (configured limit)
    // taskCost = 10'000'000 (task2's cost)
});

queue.TryEnqueue({task1, 6'000'000, 255});   // Accepted
queue.TryEnqueue({task2, 10'000'000, 128});  // Accepted + WARNING

EXPECT_EQ(queue.GetQueuedCount(), 2);  // Both tasks accepted
```

### Zero Budget Edge Case

```cpp
queue.SetBudget(TaskBudget{0, BudgetOverflowMode::Strict});

queue.TryEnqueue({task, 1'000'000, 255});  // REJECTED (strict mode, zero budget)

// BUT:
queue.EnqueueUnchecked({task, 0, 255});  // ACCEPTED (zero-cost bypass)
```

---

## Priority-Based Execution

Tasks execute from highest (255) to lowest (0) priority. Equal priorities maintain insertion order (stable sort).

```cpp
queue.TryEnqueue({task_low1, 1ms, 50});    // Priority 50
queue.TryEnqueue({task_high1, 1ms, 200});  // Priority 200
queue.TryEnqueue({task_med, 1ms, 128});    // Priority 128
queue.TryEnqueue({task_high2, 1ms, 200});  // Priority 200 (same as task_high1)
queue.TryEnqueue({task_low2, 1ms, 50});    // Priority 50 (same as task_low1)

// Execution order:
// 1. task_high1 (200, inserted first)
// 2. task_high2 (200, inserted second) <- stable sort
// 3. task_med (128)
// 4. task_low1 (50, inserted first)
// 5. task_low2 (50, inserted second) <- stable sort
```

---

## API Reference

### Budget Management

#### `SetBudget(const TaskBudget& budget)`

Configure frame budget and overflow mode.

**Parameters:**
- `budget` - Budget configuration (time, memory, mode)

**Example:**
```cpp
queue.SetBudget(TaskBudget{16'666'666, BudgetOverflowMode::Strict});
queue.SetBudget(BudgetPresets::FPS60_Lenient);  // Using preset
```

#### `GetRemainingBudget() -> uint64_t`

Query remaining budget capacity in nanoseconds.

**Returns:** Budget remaining (0 if exhausted or over budget)

**Example:**
```cpp
EXPECT_EQ(queue.GetRemainingBudget(), 10'000'000);  // 10ms remaining
```

#### `IsBudgetExhausted() -> bool`

Check if budget is fully consumed.

**Returns:** `true` if remaining budget is 0

**Example:**
```cpp
if (queue.IsBudgetExhausted()) {
    LOG_WARNING("Frame budget exhausted");
}
```

### Task Enqueue

#### `TryEnqueue(TaskSlot&& slot) -> bool`

Enqueue task with budget checking.

**Parameters:**
- `slot` - Task slot (data, cost, priority)

**Returns:** `true` if accepted, `false` if rejected (strict mode only)

**Behavior:**
- **Zero-cost tasks (estimatedCostNs == 0):** Always accepted
- **Strict mode:** Rejects if budget would be exceeded
- **Lenient mode:** Accepts with warning callback

**Example:**
```cpp
TaskSlot slot{dispatchPass, 5'000'000, 200};
if (!queue.TryEnqueue(std::move(slot))) {
    LOG_ERROR("Task rejected: budget exhausted");
}
```

#### `EnqueueUnchecked(TaskSlot&& slot)`

Enqueue task without budget checking (always succeeds).

**Parameters:**
- `slot` - Task slot (data, cost, priority)

**Use cases:**
- Mandatory tasks that must execute regardless of budget
- Zero-cost tasks (backward compatibility with QueueDispatch)
- External budget management

**Example:**
```cpp
// Mandatory safety task (no budget check)
queue.EnqueueUnchecked({criticalTask, 2'000'000, 255});
```

### Task Execution

#### `Execute(const std::function<void(const TTaskData&)>& executor)`

Execute all queued tasks in priority order.

**Parameters:**
- `executor` - Function called for each task's data

**Example:**
```cpp
queue.Execute([](const DispatchPass& pass) {
    vkCmdDispatch(cmdBuffer, pass.workGroupCount.x, .y, .z);
});
```

#### `ExecuteWithMetadata(const std::function<void(const TaskSlot&)>& executor)`

Execute tasks with access to full slot metadata.

**Parameters:**
- `executor` - Function called with full task slot (data + cost + priority)

**Example:**
```cpp
queue.ExecuteWithMetadata([](const TaskSlot& slot) {
    LOG_DEBUG("Executing: cost={}ns, priority={}", slot.estimatedCostNs, slot.priority);
    processTask(slot.data);
});
```

### Queue State

#### `GetQueuedCount() -> size_t`

Get number of tasks currently queued.

**Returns:** Task count

**Example:**
```cpp
EXPECT_EQ(queue.GetQueuedCount(), 5);
```

#### `Clear()`

Remove all tasks and reset state (budget, insertion order).

**Example:**
```cpp
queue.Clear();  // Prepare for next frame
EXPECT_EQ(queue.GetQueuedCount(), 0);
```

---

## Integration with MultiDispatchNode

Sprint 6.2 integrates TaskQueue into MultiDispatchNode for budget-aware compute dispatch scheduling.

### API Changes

**Sprint 6.1 (Backward Compatible):**
```cpp
multiDispatch->QueueDispatch(dispatchPass);  // Zero-cost, always accepted
```

**Sprint 6.2 (Budget-Aware):**
```cpp
bool accepted = multiDispatch->TryQueueDispatch(
    dispatchPass,
    5'000'000,  // 5ms GPU time estimate
    200         // High priority
);
```

### Configuration Parameters

MultiDispatchNode supports budget configuration via node parameters:

```cpp
multiDispatch->SetParameter("frameBudgetNs", 16'666'666);     // 16.67ms (60 FPS)
multiDispatch->SetParameter("budgetOverflowMode", "strict");  // or "lenient"
```

**Defaults:**
- `frameBudgetNs`: 16'666'666 (16.67ms for 60 FPS)
- `budgetOverflowMode`: "strict"

---

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `TryEnqueue()` | O(1) | Constant time enqueue + budget check |
| `EnqueueUnchecked()` | O(1) | No budget validation |
| `Execute()` | O(n log n) | Stable sort on first execution |
| `GetRemainingBudget()` | O(1) | Cached total cost |
| `Clear()` | O(1) | Vector clear |

**Optimization:** Sorting is lazy - only performed on first `Execute()` call after enqueue operations.

---

## Budget Presets

`TaskBudget.h` provides constexpr presets for common frame rates:

```cpp
namespace BudgetPresets {
    inline constexpr TaskBudget FPS60_Strict{16'666'666, BudgetOverflowMode::Strict};
    inline constexpr TaskBudget FPS30_Strict{33'333'333, BudgetOverflowMode::Strict};
    inline constexpr TaskBudget FPS120_Strict{8'333'333, BudgetOverflowMode::Strict};
    inline constexpr TaskBudget FPS60_Lenient{16'666'666, BudgetOverflowMode::Lenient};
    inline constexpr TaskBudget Unlimited{};  // max uint64_t, no enforcement
}
```

**Usage:**
```cpp
queue.SetBudget(BudgetPresets::FPS60_Strict);   // 16.67ms strict
queue.SetBudget(BudgetPresets::Unlimited);      // No budget limits
```

---

## Thread Safety

**Not thread-safe.** TaskQueue assumes single-threaded access (typical for RenderGraph nodes executing on main thread).

For multi-threaded scenarios:
- Use separate TaskQueue per thread
- Synchronize access with mutexes
- Use lock-free queue implementation

---

## Error Handling

### Invalid Pass Validation

TaskQueue does NOT validate task data. Responsibility belongs to task generator:

```cpp
// Validation happens BEFORE enqueue
if (!dispatchPass.IsValid()) {
    throw std::runtime_error("Invalid dispatch: null pipeline");
}

queue.TryEnqueue({dispatchPass, cost, priority});  // Assumes valid data
```

### Budget Overflow

**Strict mode:** Returns `false`, task not queued
```cpp
if (!queue.TryEnqueue(slot)) {
    handleRejection();  // Task was rejected
}
```

**Lenient mode:** Returns `true`, fires warning callback
```cpp
queue.SetWarningCallback([](uint64_t newTotal, uint64_t budget, uint64_t taskCost) {
    logWarning(newTotal, budget, taskCost);
});

queue.TryEnqueue(slot);  // Always true in lenient mode
```

---

## Testing

**Unit Tests:** `libraries/RenderGraph/tests/test_task_queue.cpp` (28 tests)
**Integration Tests:** `libraries/RenderGraph/tests/test_multidispatch_integration.cpp` (15 tests)

**Coverage:**
- Strict mode budget enforcement
- Lenient mode warning callbacks
- Zero-cost bypass
- Priority-based execution order
- Overflow protection
- Budget presets
- Queue state management

---

## Related Documentation

- [[../MultiDispatchNode]] - MultiDispatchNode Sprint 6.2 integration
- [[../../05-Progress/features/Sprint6.2-TaskQueue-System]] - Sprint 6.2 implementation details
- [[../../01-Architecture/RenderGraph-System]] - RenderGraph architecture

**Header Files:**
- `libraries/RenderGraph/include/Core/TaskQueue.h` - Template implementation
- `libraries/RenderGraph/include/Data/TaskBudget.h` - Budget structures and presets

---

**Created:** 2026-01-06
**Sprint:** 6.2 - Phase 2 Timeline Foundation
**Status:** Complete
