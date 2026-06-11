---
title: MultiDispatchNode - Group-Based Compute Dispatch
aliases: [MultiDispatch, Group Dispatch, Batch Dispatch, Budget-Aware Dispatch]
tags: [rendergraph, compute, vulkan, sprint-6-1, sprint-6-2, taskqueue]
created: 2026-01-06
updated: 2026-01-06
sprint: Sprint 6.2
related:
  - "[[RenderGraph]]"
  - "[[RenderGraph/TaskQueue]]"
  - "[[../05-Progress/features/accumulation-slot-proper-design]]"
  - "[[../05-Progress/features/variadic-modifier-api]]"
  - "[[../05-Progress/features/Sprint6.2-TaskQueue-System]]"
---

# MultiDispatchNode - Group-Based Compute Dispatch

**Type ID:** 108
**Sprint:** 6.1
**Purpose:** Efficiently record multiple compute dispatches with optional group-based partitioning

---

## Overview

`MultiDispatchNode` records multiple compute dispatches into a single command buffer. Sprint 6.1 adds **group-based dispatch**, allowing dispatches to be partitioned by group ID and processed independently with automatic barrier insertion.

### Key Features

- **Multiple dispatch recording** - Record N dispatches in single command buffer
- **Group-based partitioning** (Sprint 6.1) - Partition by `groupId` for independent processing
- **Automatic barriers** - Insert memory/execution barriers between groups
- **Backward compatible** - Legacy `QueueDispatch()` API still works
- **Statistics tracking** - Dispatch count, barrier count, work group totals

---

## API Overview

### Two Usage Patterns

#### Pattern 1: GROUP_INPUTS (Sprint 6.1 - Recommended)

```cpp
// Generator creates dispatch passes
batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId));
```

**When to use:** Multiple passes known at compile-time, need grouping

#### Pattern 2: QueueDispatch() (Legacy)

```cpp
// Imperative dispatch queuing
void MyNode::Execute() {
    multiDispatch->QueueDispatch(pass1);
    multiDispatch->QueueDispatch(pass2);
}
```

**When to use:** Dynamic dispatches at runtime, no grouping needed

---

## Configuration Slots

### Inputs

| Slot | Type | Required | Purpose |
|------|------|----------|---------|
| `VULKAN_DEVICE_IN` | `VulkanDevice*` | ✅ | Device for command pool |
| `COMMAND_POOL` | `VkCommandPool` | ✅ | Pool for command buffer allocation |
| `SWAPCHAIN` | `VkSwapchainKHR` | ✅ | Swapchain for frame count |
| `IMAGE_INDEX` | `uint32_t` | ✅ | Current frame image index |
| `CURRENT_FRAME_INDEX` | `uint32_t` | ✅ | Current frame-in-flight index |
| `GROUP_INPUTS` | `std::vector<DispatchPass>` | ❌ | **Sprint 6.1:** Accumulation slot for grouped passes |

### Outputs

| Slot | Type | Purpose |
|------|------|---------|
| `COMMAND_BUFFER` | `VkCommandBuffer` | Recorded command buffer with all dispatches |
| `VULKAN_DEVICE_OUT` | `VulkanDevice*` | Passthrough for downstream nodes |

---

## GROUP_INPUTS Slot (Sprint 6.1)

### Concept

`GROUP_INPUTS` is an **accumulation slot** that collects `DispatchPass` elements from multiple sources. Each pass can have an optional `groupId` field. Passes are partitioned by group ID and processed independently.

### Lifecycle

**Compile-time only.** Data is read during `CompileImpl()` and cached in `groupedDispatches_` map. Use `QueueDispatch()` for per-frame dynamic dispatch.

### Type Definition

```cpp
ACCUMULATION_INPUT_SLOT_V2(
    GROUP_INPUTS,
    std::vector<DispatchPass>,  // Container type
    DispatchPass,                // Element type
    5,                           // Slot index
    SlotNullability::Optional,   // Not required
    SlotRole::Dependency,        // Affects compile order
    SlotStorageStrategy::Value   // Copies passes (safe for cross-frame use)
);
```

### Storage Strategy: Value vs Reference

| Strategy | Behavior | When to Use |
|----------|----------|-------------|
| **Value** | Copies `DispatchPass` data | Safe for cross-frame persistence (GROUP_INPUTS uses this) |
| **Reference** | Stores pointer to source data | Source data must outlive usage |
| **Span** | Non-owning view | Temporary iteration only |

**Why Value for GROUP_INPUTS:** DispatchPass data is read once at compile-time and cached. Copying ensures source nodes can be destroyed without affecting MultiDispatchNode.

---

## GroupKeyModifier

### Purpose

Partition accumulated `DispatchPass` elements by extracting a group ID from each element.

### Syntax

```cpp
batch.Connect(generator, GeneratorConfig::OUTPUT,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId));
```

### How It Works

1. **PreValidation:** Verifies target slot is accumulation slot
2. **Runtime (CompileImpl):** Extracts `groupId` from each `DispatchPass`
3. **Partitioning:** Builds `std::map<uint32_t, vector<DispatchPass>>`
4. **Execution:** Records each group independently with barriers between groups

### Supported Field Types

```cpp
// Optional uint32_t (preferred)
std::optional<uint32_t> DispatchPass::groupId;
GroupKey(&DispatchPass::groupId);

// Plain uint32_t (always has group)
uint32_t MyStruct::groupIndex;
GroupKey(&MyStruct::groupIndex);
```

### Current Limitation (Sprint 6.1)

**GroupKeyModifier stores extractor function but doesn't use it yet.** MultiDispatchNode hard-codes extraction of `DispatchPass::groupId`. Future work will make this generic by reading the extractor from connection metadata.

---

## DispatchPass Structure

### Definition

```cpp
struct DispatchPass {
    // Required fields
    VkPipeline pipeline;           ///< Compute pipeline to bind
    VkPipelineLayout layout;       ///< Pipeline layout for binding
    glm::uvec3 workGroupCount;     ///< Dispatch dimensions (X, Y, Z)

    // Optional fields
    std::vector<VkDescriptorSet> descriptorSets;  ///< Descriptor sets to bind
    uint32_t firstSet = 0;                         ///< First set number
    std::optional<PushConstantData> pushConstants; ///< Push constant data
    std::string debugName;                         ///< Debug label

    // Sprint 6.1: Group-based dispatch
    std::optional<uint32_t> groupId;               ///< Group ID for partitioning

    // Validation
    [[nodiscard]] bool IsValid() const {
        return pipeline != VK_NULL_HANDLE &&
               layout != VK_NULL_HANDLE &&
               workGroupCount.x > 0 &&
               workGroupCount.y > 0 &&
               workGroupCount.z > 0;
    }

    // Helpers
    [[nodiscard]] uint64_t TotalWorkGroups() const {
        return static_cast<uint64_t>(workGroupCount.x) *
               workGroupCount.y * workGroupCount.z;
    }
};
```

### Field Descriptions

| Field | Required | Description |
|-------|----------|-------------|
| `pipeline` | ✅ | VkPipeline handle - must not be VK_NULL_HANDLE |
| `layout` | ✅ | VkPipelineLayout for descriptor/push constant binding |
| `workGroupCount` | ✅ | Dispatch dimensions - all must be > 0 |
| `descriptorSets` | ❌ | Descriptor sets to bind (if any) |
| `firstSet` | ❌ | Starting set index (default: 0) |
| `pushConstants` | ❌ | Push constant data (if pipeline uses it) |
| `debugName` | ❌ | Debug label for profiling/debugging |
| `groupId` | ❌ | Group ID for partitioned processing |

### Validation

`IsValid()` checks:
- ✅ `pipeline != VK_NULL_HANDLE`
- ✅ `layout != VK_NULL_HANDLE`
- ✅ `workGroupCount.x > 0`
- ✅ `workGroupCount.y > 0`
- ✅ `workGroupCount.z > 0`

Optional fields (`groupId`, `descriptorSets`, `pushConstants`) do not affect validity.

---

## Group-Based Dispatch Logic

### Partitioning Algorithm

```cpp
// MultiDispatchNode::CompileImpl()
std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches_;

for (const auto& pass : groupInputs) {
    if (pass.groupId.has_value()) {
        groupedDispatches_[pass.groupId.value()].push_back(pass);
    } else {
        groupedDispatches_[0].push_back(pass);  // Default: group 0
    }
}
```

### Default Group Behavior

**Passes without `groupId`** (nullopt) are assigned to **group 0**.

```cpp
DispatchPass pass1{};
pass1.groupId = std::nullopt;  // Goes to group 0

DispatchPass pass2{};
pass2.groupId = 0;  // Also goes to group 0

DispatchPass pass3{};
pass3.groupId = 5;  // Goes to group 5
```

Result: Group 0 contains `pass1` and `pass2`, Group 5 contains `pass3`.

### Deterministic Ordering

`std::map` guarantees **deterministic iteration order** (sorted by key).

```cpp
groupedDispatches_[5] = {...};  // Insert order: 5, 1, 10, 3
groupedDispatches_[1] = {...};
groupedDispatches_[10] = {...};
groupedDispatches_[3] = {...};

// Iteration order: 1, 3, 5, 10 (always sorted)
```

**Why this matters:** Ensures groups execute in predictable order for debugging and profiling.

---

## Execution Flow

### Group-Based Path (Sprint 6.1)

```cpp
// MultiDispatchNode::ExecuteImpl()
if (!groupedDispatches_.empty()) {
    for (const auto& [groupId, passes] : groupedDispatches_) {
        // Insert barrier between groups (if not first)
        if (autoBarriers_ && !firstGroup) {
            InsertAutoBarrier(cmdBuffer);
        }

        // Record all passes in this group
        for (const auto& pass : passes) {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);

            if (!pass.descriptorSets.empty()) {
                vkCmdBindDescriptorSets(cmdBuffer, ...);
            }

            if (pass.pushConstants.has_value()) {
                vkCmdPushConstants(cmdBuffer, ...);
            }

            vkCmdDispatch(cmdBuffer, pass.workGroupCount.x, pass.workGroupCount.y, pass.workGroupCount.z);
        }
    }
}
```

### Legacy Path (Backward Compatible)

```cpp
// MultiDispatchNode::ExecuteImpl()
else {
    // Use dispatchQueue_ (legacy QueueDispatch() API)
    for (const auto& pass : dispatchQueue_) {
        // Same recording logic as group-based
    }
}
```

**Fallback condition:** `groupedDispatches_.empty()` (no GROUP_INPUTS connected)

---

## Barrier Insertion

### Automatic Barriers

When `autoBarriers_` is enabled (default):
- **Between groups:** Full memory/execution barrier
- **Within group:** Barrier between passes

### Barrier Configuration

```cpp
VkMemoryBarrier barrier{};
barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

vkCmdPipelineBarrier(
    cmdBuffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &barrier, 0, nullptr, 0, nullptr
);
```

**Purpose:** Ensures writes from previous pass/group are visible to next pass/group.

---

## Usage Examples

### Example 1: Multi-Pass Blur

```cpp
// Generator node creates 3 dispatch passes for horizontal, vertical, and final blur
class BlurPassGenerator : public TypedNode<BlurGenConfig> {
    void CompileImpl(TypedCompileContext& ctx) override {
        std::vector<DispatchPass> passes;

        // Horizontal blur pass (group 0)
        DispatchPass hBlur{};
        hBlur.pipeline = horizontalBlurPipeline_;
        hBlur.layout = blurLayout_;
        hBlur.workGroupCount = {width / 16, height / 16, 1};
        hBlur.groupId = 0;
        hBlur.debugName = "HorizontalBlur";
        passes.push_back(hBlur);

        // Vertical blur pass (group 1)
        DispatchPass vBlur{};
        vBlur.pipeline = verticalBlurPipeline_;
        vBlur.layout = blurLayout_;
        vBlur.workGroupCount = {width / 16, height / 16, 1};
        vBlur.groupId = 1;
        vBlur.debugName = "VerticalBlur";
        passes.push_back(vBlur);

        // Output blur passes
        ctx.Out(BlurGenConfig::DISPATCH_PASS) = passes;
    }
};

// Connect to MultiDispatchNode
batch.Connect(blurGen, BlurGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId));
```

**Result:**
- Group 0: Horizontal blur (1 pass)
- Group 1: Vertical blur (1 pass)
- Barrier inserted between groups

### Example 2: Dynamic Dispatch (Legacy)

```cpp
void MyNode::ExecuteImpl(TypedExecuteContext& ctx) {
    auto* multiDispatch = graph->GetNode<MultiDispatchNode>("dispatcher");

    // Queue dispatches dynamically
    for (int i = 0; i < dynamicCount; ++i) {
        DispatchPass pass{};
        pass.pipeline = computePipelines_[i];
        pass.layout = pipelineLayout_;
        pass.workGroupCount = CalculateWorkGroups(i);
        pass.debugName = "DynamicPass_" + std::to_string(i);

        multiDispatch->QueueDispatch(pass);
    }
}
```

### Example 3: Mixed Sources

```cpp
// Multiple generators contribute to same GROUP_INPUTS
batch.Connect(generator1, Gen1Config::PASSES,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId));

batch.Connect(generator2, Gen2Config::PASSES,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId));
```

**Result:** All passes from both generators are accumulated and partitioned by `groupId`.

---

## Statistics

### MultiDispatchStats

**Sprint 6.1: Task #314 - Enhanced with per-group statistics**

```cpp
struct GroupDispatchStats {
    uint32_t dispatchCount = 0;      // Number of dispatches in this group
    uint64_t totalWorkGroups = 0;    // Sum of work groups in this group
    double recordTimeMs = 0.0;       // CPU time to record this group's commands
};

struct MultiDispatchStats {
    // Overall statistics
    uint32_t dispatchCount = 0;      // Total number of dispatches recorded
    uint32_t barrierCount = 0;       // Number of barriers inserted
    uint64_t totalWorkGroups = 0;    // Sum of all work groups across all groups
    double recordTimeMs = 0.0;       // Total CPU time to record commands

    // Per-group statistics (Sprint 6.1: Task #314)
    std::map<uint32_t, GroupDispatchStats> groupStats;  // group ID -> statistics

    // Helper methods
    [[nodiscard]] uint32_t GetGroupCount() const;
    [[nodiscard]] const GroupDispatchStats* GetGroupStats(uint32_t groupId) const;
};
```

### Access

```cpp
const auto& stats = multiDispatchNode->GetStats();
std::cout << "Recorded " << stats.dispatchCount << " dispatches\n";
std::cout << "Inserted " << stats.barrierCount << " barriers\n";
std::cout << "Total work groups: " << stats.totalWorkGroups << "\n";
std::cout << "Record time: " << stats.recordTimeMs << "ms\n";
```

#### Per-Group Statistics (Sprint 6.1: Task #314)

```cpp
const auto& stats = multiDispatchNode->GetStats();

// Check if group-based dispatch was used
if (stats.GetGroupCount() > 0) {
    std::cout << "Group breakdown (" << stats.GetGroupCount() << " groups):\n";

    for (const auto& [groupId, groupStat] : stats.groupStats) {
        std::cout << "  Group " << groupId << ": "
                  << groupStat.dispatchCount << " dispatches, "
                  << groupStat.totalWorkGroups << " work groups, "
                  << groupStat.recordTimeMs << "ms\n";
    }
}

// Query specific group
const auto* group0Stats = stats.GetGroupStats(0);
if (group0Stats) {
    std::cout << "Group 0 recorded " << group0Stats->dispatchCount << " dispatches\n";
}
```

#### Example Output

```
[MultiDispatchNode] Frame 120: 10 dispatches, 2 barriers, 1.6ms record time | 3 groups: G0(3d/0.5ms), G1(5d/0.8ms), G2(2d/0.3ms)
```

**Statistics are reset every frame after ExecuteImpl.**

**Note:** Per-group statistics are only populated when using GROUP_INPUTS. When using the legacy QueueDispatch() API, `groupStats` will be empty.

---

## Limitations & Future Work

### Current Limitations (Sprint 6.1)

1. **Hard-coded group key extraction**
   - GroupKeyModifier stores extractor but doesn't use it
   - MultiDispatchNode hard-codes `DispatchPass::groupId`
   - **Future:** Generic extraction using stored function

2. **No sorting within groups**
   - Passes within a group maintain insertion order
   - **Future:** AccumulationSortModifier (see [sort-modifier-strategy-pattern](../05-Progress/features/sort-modifier-strategy-pattern.md))

3. **Fixed barrier strategy**
   - Always inserts full memory barrier
   - **Future:** Configurable barrier types per group

### Future Enhancements

**Completed in Sprint 6.1:**
- ✅ Per-group statistics (dispatch count, work groups per group) - Task #314

**Planned for Sprint 6.2+:**
- Custom barrier strategies
- Group-level GPU timestamping for profiling
- Generic group key extraction

---

## Best Practices

### ✅ Do

- **Use GROUP_INPUTS for compile-time passes** - Known passes at compile time
- **Use QueueDispatch() for runtime passes** - Dynamic dispatch counts
- **Always validate passes** - Call `pass.IsValid()` before queuing
- **Use meaningful debugName** - Helps with profiling and debugging
- **Group related passes** - Minimize barrier overhead
- **Use deterministic group IDs** - Predictable execution order

### ❌ Don't

- **Don't mix GROUP_INPUTS and QueueDispatch()** - Choose one pattern per node
- **Don't forget work group count** - Must be > 0 in all dimensions
- **Don't assume nullopt means no group** - It means group 0
- **Don't rely on insertion order across groups** - Use explicit group IDs

---

## Sprint 6.2: Budget-Aware Task Scheduling

**Sprint:** 6.2 - Phase 2 Timeline Foundation
**Status:** ✅ Complete (2026-01-06)

Sprint 6.2 integrates `TaskQueue<DispatchPass>` into MultiDispatchNode, replacing the basic `std::deque` with a priority-based scheduler that supports GPU time budgets.

### Key Changes

**Replaced:**
```cpp
std::deque<DispatchPass> dispatchQueue_;  // Sprint 6.1
```

**With:**
```cpp
TaskQueue<DispatchPass> taskQueue_;  // Sprint 6.2
```

### New API: TryQueueDispatch()

Budget-aware dispatch queuing with priority support:

```cpp
bool TryQueueDispatch(
    DispatchPass&& pass,
    uint64_t estimatedCostNs,  // GPU time estimate
    uint8_t priority = 128      // 0 (low) to 255 (high)
) -> bool;
```

**Returns:**
- `true` if task accepted
- `false` if rejected (strict mode only)

**Example:**
```cpp
DispatchPass prefilter = createPrefilter();
bool accepted = multiDispatch->TryQueueDispatch(
    std::move(prefilter),
    5'000'000,  // 5ms GPU time estimate
    200         // High priority
);

if (!accepted) {
    LOG_WARNING("Prefilter rejected: frame budget exhausted");
}
```

### Backward Compatibility

**QueueDispatch() unchanged:**
```cpp
size_t QueueDispatch(DispatchPass&& pass);  // Sprint 6.1 API - still works!
```

**Implementation:** Internally calls `EnqueueUnchecked()` with zero cost:
```cpp
// Sprint 6.2 implementation (backward compatible)
size_t MultiDispatchNode::QueueDispatch(DispatchPass&& pass) {
    TaskQueue::TaskSlot slot;
    slot.data = std::move(pass);
    slot.estimatedCostNs = 0;   // Zero-cost = bypass budget
    slot.priority = 128;         // Default priority

    taskQueue_.EnqueueUnchecked(std::move(slot));
    return taskQueue_.GetQueuedCount() - 1;
}
```

**Zero-cost tasks bypass budget checks** - ensures 100% Sprint 6.1 compatibility.

### Configuration Parameters

#### FRAME_BUDGET_NS (uint32_t, default: 16'666'666)

Frame budget in nanoseconds. Default is 16.67ms (60 FPS).

**Note:** Uses `uint32_t` due to parameter system limitations. Max value ~4.29 seconds is sufficient for realistic frame budgets (<1 second).

**Example:**
```cpp
multiDispatch->SetParameter("frameBudgetNs", 16'666'666);  // 16.67ms (60 FPS)
multiDispatch->SetParameter("frameBudgetNs", 33'333'333);  // 33.33ms (30 FPS)
```

#### BUDGET_OVERFLOW_MODE (string, default: "strict")

Controls behavior when tasks exceed budget:

| Mode | Behavior | Use Case |
|------|----------|----------|
| `"strict"` | Reject over-budget tasks | Fixed frame time targets |
| `"lenient"` | Accept with warning callback | Graceful degradation, debugging |

**Example:**
```cpp
multiDispatch->SetParameter("budgetOverflowMode", "strict");   // Reject over-budget
multiDispatch->SetParameter("budgetOverflowMode", "lenient");  // Accept with warning
```

### Budget Management API

#### SetBudget()

Configure frame budget programmatically:

```cpp
void SetBudget(const TaskBudget& budget);
```

**Example:**
```cpp
// Using constexpr presets
multiDispatch->SetBudget(BudgetPresets::FPS60_Strict);
multiDispatch->SetBudget(BudgetPresets::FPS120_Lenient);

// Custom budget
TaskBudget custom{10'000'000, BudgetOverflowMode::Strict};
multiDispatch->SetBudget(custom);
```

#### GetRemainingBudget()

Query available budget capacity:

```cpp
[[nodiscard]] uint64_t GetRemainingBudget() const;
```

**Returns:** Nanoseconds remaining (0 if exhausted)

**Example:**
```cpp
if (multiDispatch->GetRemainingBudget() < 2'000'000) {
    LOG_WARNING("Less than 2ms budget remaining");
}
```

#### GetBudget()

Retrieve current budget configuration:

```cpp
[[nodiscard]] const TaskBudget& GetBudget() const;
```

**Example:**
```cpp
const TaskBudget& budget = multiDispatch->GetBudget();
LOG_INFO("Budget: {}ns, mode={}", budget.gpuTimeBudgetNs,
         budget.IsStrict() ? "strict" : "lenient");
```

### Priority-Based Execution

Tasks execute from highest (255) to lowest (0) priority. Equal priorities preserve insertion order (stable sort).

**Example:**
```cpp
// Queue tasks with different priorities
multiDispatch->TryQueueDispatch(prefilter, 2ms, 200);   // High priority
multiDispatch->TryQueueDispatch(mainPass, 10ms, 128);   // Medium priority
multiDispatch->TryQueueDispatch(postfilter, 3ms, 100);  // Low priority

// Execution order:
// 1. prefilter  (priority 200)
// 2. mainPass   (priority 128)
// 3. postfilter (priority 100)
```

### Strict vs Lenient Mode Behavior

#### Strict Mode Example

```cpp
multiDispatch->SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Strict});

bool ok1 = multiDispatch->TryQueueDispatch(pass1, 6'000'000, 255);  // Accepted (6ms)
bool ok2 = multiDispatch->TryQueueDispatch(pass2, 5'000'000, 128);  // REJECTED (would exceed 10ms)

EXPECT_TRUE(ok1);
EXPECT_FALSE(ok2);  // Task rejected, not queued
EXPECT_EQ(multiDispatch->GetQueueSize(), 1);  // Only pass1 queued
```

#### Lenient Mode Example

```cpp
multiDispatch->SetBudget(TaskBudget{10'000'000, BudgetOverflowMode::Lenient});

// Set warning callback (via TaskQueue API)
multiDispatch->GetTaskQueue().SetWarningCallback(
    [](uint64_t newTotal, uint64_t budget, uint64_t taskCost) {
        LOG_WARNING("Budget overflow: {}ns total (budget: {}ns), task: {}ns",
                    newTotal, budget, taskCost);
    }
);

bool ok1 = multiDispatch->TryQueueDispatch(pass1, 6'000'000, 255);   // Accepted
bool ok2 = multiDispatch->TryQueueDispatch(pass2, 10'000'000, 128);  // Accepted + WARNING

EXPECT_TRUE(ok1);
EXPECT_TRUE(ok2);  // Both accepted in lenient mode
EXPECT_EQ(multiDispatch->GetQueueSize(), 2);
```

### Migration Guide (Sprint 6.1 → 6.2)

#### Pattern 1: No Changes Required

If using basic `QueueDispatch()`, no code changes needed:

```cpp
// Sprint 6.1 code - works in Sprint 6.2 unchanged
multiDispatch->QueueDispatch(std::move(pass1));
multiDispatch->QueueDispatch(std::move(pass2));
// Zero-cost bypass = always accepted
```

#### Pattern 2: Add Budget Awareness

Opt-in to budget enforcement:

```cpp
// Sprint 6.2: Budget-aware queuing
multiDispatch->SetBudget(BudgetPresets::FPS60_Strict);

bool ok = multiDispatch->TryQueueDispatch(pass1, 5'000'000, 200);
if (!ok) {
    handleRejection();  // Task was rejected due to budget
}
```

#### Pattern 3: Priority-Based Scheduling

Control execution order:

```cpp
// High-priority critical passes execute first
multiDispatch->TryQueueDispatch(criticalPass, 3ms, 255);   // Priority 255
multiDispatch->TryQueueDispatch(optionalPass, 2ms, 50);    // Priority 50
// criticalPass executes before optionalPass
```

### Integration with Sprint 6.3 (Future)

Sprint 6.2 provides the foundation for Sprint 6.3's runtime capacity tracking:

- **Sprint 6.2:** Estimate-based budgets (pre-execution)
- **Sprint 6.3:** Measured runtime costs (post-execution feedback)
- **Future:** Adaptive scheduling based on historical performance

See [[../05-Progress/feature-proposal-plans/timeline-capacity-tracker|Timeline Capacity Tracker]] for Sprint 6.3 proposal.

---

## Testing

### Sprint 6.1 Tests

See [test_group_dispatch.cpp](../../libraries/RenderGraph/tests/test_group_dispatch.cpp):
- GroupKeyModifier validation (4 tests)
- DispatchPass validation (5 tests)
- Group partitioning logic (4 tests)
- Backward compatibility (2 tests)
- Invalid pass handling (6 tests)
- Complex scenarios (3 tests)
- Helper functions (2 tests)
- Field combinations (3 tests)
- Statistics API (5 tests)

**Sprint 6.1 Total: 41 tests, all passing**

### Sprint 6.2 Tests

See [test_task_queue.cpp](../../libraries/RenderGraph/tests/test_task_queue.cpp):
- Strict mode budget enforcement (8 tests)
- Lenient mode with warnings (6 tests)
- Budget API (remaining budget, exhaustion, queries) (5 tests)
- TaskBudget structure (constructors, helpers, presets) (4 tests)
- Integration (Clear(), EnqueueUnchecked(), ExecuteWithMetadata()) (3 tests)
- Overflow protection (2 tests)

**Sprint 6.2 Unit Tests: 28 tests, all passing**

See [test_multidispatch_integration.cpp](../../libraries/RenderGraph/tests/test_multidispatch_integration.cpp):
- Backward compatibility (QueueDispatch zero-cost bypass) (2 tests)
- Budget enforcement (strict/lenient modes) (4 tests)
- Priority-based execution order (2 tests)
- Warning callbacks (2 tests)
- Budget exhaustion (2 tests)
- Configuration presets (1 test)
- Edge cases (empty queue, mid-frame budget changes) (2 tests)

**Sprint 6.2 Integration Tests: 15 tests, all passing**

**Combined Total: 84 tests across 3 test files, all passing**

---

## Related Documentation

### Core Documentation

- [[RenderGraph]] - Main RenderGraph documentation
- [[RenderGraph/TaskQueue]] - TaskQueue template (Sprint 6.2)
- [[../01-Architecture/RenderGraph-System|RenderGraph System Architecture]] - System architecture

### Sprint Documentation

- [[../05-Progress/features/Sprint6.2-TaskQueue-System|Sprint 6.2: TaskQueue System]] - Implementation details
- [[../05-Progress/features/accumulation-slot-proper-design|Accumulation Slot Design]] - V2 accumulation slots (Sprint 6.1)
- [[../05-Progress/features/variadic-modifier-api|Variadic Modifier API]] - Sprint 6.0.1 unified Connect()
- [[../05-Progress/feature-proposal-plans/timeline-capacity-tracker|Timeline Capacity Tracker]] - Sprint 6.3 proposal

### Future Enhancements

- [[../05-Progress/features/sort-modifier-strategy-pattern|Sort Modifier Pattern]] - Future sorting enhancement

---

**Created:** 2026-01-06
**Last Updated:** 2026-01-06 (Sprint 6.2)
**Status:** ✅ Implemented and tested
**Test Coverage:** 84 tests (41 Sprint 6.1, 28 Sprint 6.2 unit, 15 Sprint 6.2 integration), all passing
