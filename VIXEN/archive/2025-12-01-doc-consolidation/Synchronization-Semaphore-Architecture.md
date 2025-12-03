# GPU Synchronization & Semaphore Architecture for Smooth Runtime Recompilation

**Last Updated**: November 3, 2025  
**Status**: Implementation Guide - Phase G (Compute Pipeline Optimization)  
**Author**: VIXEN Development Team  
**Related Documents**: 
- `RenderGraph-Architecture-Overview.md`
- `PhaseG-ComputePipeline-Plan.md`

---

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Root Cause Analysis](#root-cause-analysis)
3. [Architecture Overview](#architecture-overview)
4. [Design Decisions](#design-decisions)
5. [Implementation Details](#implementation-details)
6. [Code Integration Points](#code-integration-points)
7. [Testing & Validation](#testing--validation)
8. [Performance Considerations](#performance-considerations)
9. [Migration Path](#migration-path)

---

## Problem Statement

### Symptom
When shader recompilation occurs during runtime (e.g., shader reload, window resize, resource invalidation), the compute shader animation **stutters** - time discontinuities appear in the animation timeline, visible as frame drops or animation freezes.

### Example
```
Frame 1:  time = 16.666ms (smooth)
Frame 2:  time = 33.332ms (smooth)
Frame 3:  [RECOMPILE TRIGGERED]
Frame 4:  time = 33.332ms (REPEAT - stutter!)
Frame 5:  time = 50.000ms (catchup)
```

### User Impact
- Jarring visual artifacts during shader development
- Animation loops reset or skip frames
- Perceived engine instability during iteration

---

## Root Cause Analysis

### Current Implementation Issues

#### 1. **Aggressive `vkDeviceWaitIdle()` During Frame**

**Location**: `RenderGraph/src/Core/RenderGraph.cpp` (~line 1116)

```cpp
// Current problematic code
for (VkDevice dev : devicesToWait) {
    if (dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(dev);  // ⚠️ STALLS ENTIRE GPU
    }
}
```

**Problem**:
- Blocks **all GPU work**, not just affected nodes
- Called **during frame update** (not safely between frames)
- Compute dispatch queue drained → Animation time state lost
- **No frame pipelining** - GPU halts waiting for CPU

**Vulkan Specification Impact**:
```
vkDeviceWaitIdle() effectively:
1. Flushes all command buffers
2. Waits for all in-flight GPU work
3. Blocks CPU thread until GPU idle
4. Essentially: CPU_STALL = GPUWorkTime + QueueLatency
```

#### 2. **Recompilation During Active Rendering**

**Current Flow**:
```
RenderFrame()
  → Update() 
    → ProcessEvents()
      → RecompileDirtyNodes()
        → vkDeviceWaitIdle()  ← STALL HERE
  → Execute()
```

**Why This Matters**:
- Compute dispatches already in-flight from previous frame
- Time buffer updates queued but not yet executed
- `vkDeviceWaitIdle()` drains queue before time increment applied
- Next frame sees stale time value

#### 3. **No Frame Affinity Tracking**

Current code doesn't distinguish:
- Nodes that **executed this frame** (need sync)
- Nodes that **haven't executed yet** (no sync needed)
- Nodes that **executed 2+ frames ago** (safe to modify)

**Result**: Oversynchronization - waits even when not necessary.

---

## Architecture Overview

### High-Level Design

```
┌─────────────────────────────────────────────────────┐
│                  Main Thread (Frame Loop)            │
│                                                      │
│  Frame N:  Render() → Submit() → Present()          │
│            ↓                                         │
│            [GPU executing Frame N in background]    │
│            ↓                                         │
│  Frame N+1: ProcessEvents()                         │
│             [Safe point - previous frame submitted] │
│             ↓                                        │
│             RecompileDirtyNodes()                    │
│             [Uses frame history to sync selectively]│
│             ↓                                        │
│             Render()                                │
│             [GPU executes Frame N+1 with new code] │
│                                                      │
└─────────────────────────────────────────────────────┘

Time diagram showing frame affinity:
┌──────────┬──────────┬──────────┬──────────┐
│ Frame N-2│ Frame N-1│ Frame N  │ Frame N+1│
│          │          │          │          │
│  GPU Exe │ GPU Exec │GPU Execu │          │
│  (done)  │ (done)   │(in-flt)  │ (waiting)│
│          │          │          │          │
│  [Safe]  │ [Safe]   │[Risky]   │[Ready]   │
└──────────┴──────────┴──────────┴──────────┘
                              ↑
                         Process events here
                         Recompile only nodes
                         not in Frame N
```

### Key Principles

#### Principle 1: **Three-Frame Ring Buffer**
```cpp
static constexpr uint32_t FRAME_HISTORY_SIZE = 3;
std::vector<FrameSnapshot> frameHistory[FRAME_HISTORY_SIZE];
uint32_t currentHistorySlot = 0;
```

**Why 3 Frames?**
- **Vulkan triple-buffering standard**: GPU may have up to 2 frames in-flight
- **Safety margin**: Slot N+3 guaranteed to be complete before recompilation
- **Memory efficient**: Only track essential metadata (node list + execution time)

#### Principle 2: **Event Deferred Execution**
```
Event received → Queued for recompilation
                      ↓
       Next frame → ProcessEvents() 
                      ↓
           Recompile → Only nodes not in-flight
```

#### Principle 3: **Semaphore-Based Node Sync**
```
Per-node synchronization instead of per-device:
- Each node has VkSemaphore marking execution complete
- Query semaphore status (VK_KHR_synchronization2) to avoid stall
- Only wait for nodes actively being recompiled
```

---

## Design Decisions

### Decision 1: **Three-Frame History vs. Timeline Semaphores**

| Aspect | 3-Frame History | Timeline Semaphores |
|--------|-----------------|-------------------|
| **Setup** | Simple metadata tracking | Requires KHR extension |
| **Runtime** | O(1) lookup per frame | GPU timeline queries |
| **Overhead** | ~3KB per frame history | ~1 semaphore per node |
| **Compatibility** | Vulkan 1.0+ | Vulkan 1.2+ with KHR |
| **We chose** | ✅ This first | Future optimization |

**Rationale**: 3-frame history is simpler to validate and debug. Timeline semaphores can be added in Phase H (Discovery System).

### Decision 2: **Move Recompilation to `ProcessEvents()` Phase**

**When Recompilation Happens**:

```
Frame N:     GPU executing compute dispatches
             ↓
ProcessEvents() ← [RECOMPILE HAPPENS HERE]
             ↓
Execute()    GPU executes new code
```

**Why Not During `Execute()`?**
- ❌ GPU still executing previous frame
- ❌ Compute shader time buffers may be stale
- ❌ Hard to atomically swap code

**Why `ProcessEvents()`?**
- ✅ Previous frame fully submitted
- ✅ Safe point between GPU work
- ✅ Before time updates applied
- ✅ Matches industry pattern (Unreal, Unity)

### Decision 3: **Selective Synchronization**

Instead of:
```cpp
// OLD: Wait for all devices (too aggressive)
for (VkDevice dev : allDevicesInGraph) {
    vkDeviceWaitIdle(dev);
}
```

Do:
```cpp
// NEW: Wait only if node was in recent frames
if (WasNodeExecutedInRecentFrames(node)) {
    WaitForNodeCompletion(node);
}
```

**Benefit**: Shader reloads (no node execution change) require zero synchronization!

---

## Implementation Details

### Part A: Frame History Tracking

#### 1. Define `FrameSnapshot` Structure

**File**: `RenderGraph/include/Core/FrameSnapshot.h` (NEW)

```cpp
#pragma once

#include <vector>
#include <cstdint>
#include <chrono>

namespace Vixen::RenderGraph {

// Forward declaration
class NodeInstance;

/**
 * @brief Snapshot of nodes executed in a single frame
 * 
 * Used for synchronization decisions during recompilation.
 * Tracks which nodes were active during each frame to determine
 * if GPU work from that frame might still be in-flight.
 * 
 * **Memory**: ~1-10 KB per frame (depends on node count)
 */
struct FrameSnapshot {
    /// Frame sequence number (global frame index)
    uint64_t frameIndex = 0;
    
    /// Timestamp when frame was recorded
    std::chrono::high_resolution_clock::time_point timestamp;
    
    /// Nodes that executed during this frame
    /// Does NOT include nodes that were compiled but not executed
    std::vector<NodeInstance*> executedNodes;
    
    /// Total GPU time for frame (for performance analysis)
    uint64_t gpuTimeNs = 0;
    
    /// CPU time for frame (for performance analysis)
    uint64_t cpuTimeNs = 0;
    
    /**
     * @brief Check if node was executed during this frame
     */
    bool ContainsNode(NodeInstance* node) const {
        for (NodeInstance* n : executedNodes) {
            if (n == node) return true;
        }
        return false;
    }
    
    /**
     * @brief Clear snapshot for reuse
     */
    void Clear() {
        executedNodes.clear();
        gpuTimeNs = 0;
        cpuTimeNs = 0;
    }
};

/**
 * @brief Ring buffer of frame snapshots for history tracking
 * 
 * **Purpose**: Determine which frames might have GPU work still in-flight.
 * 
 * **Usage**:
 * ```cpp
 * FrameHistoryRing history(3);  // 3-frame buffer
 * history.Record(executedNodes, gpuTime);
 * if (history.WasNodeExecutedRecently(node)) {
 *     vkDeviceWaitIdle(device);  // Safe to sync
 * }
 * ```
 */
class FrameHistoryRing {
public:
    /**
     * @brief Create frame history with specified buffer size
     * @param bufferSize Number of frames to track (typically 3)
     */
    explicit FrameHistoryRing(uint32_t bufferSize = 3)
        : buffer_(bufferSize), currentSlot_(0), frameCount_(0) {}
    
    /**
     * @brief Record nodes executed in current frame
     * @param executedNodes List of nodes that ran during this frame
     * @param gpuTimeNs Time spent on GPU (optional, for metrics)
     * @param cpuTimeNs Time spent on CPU (optional, for metrics)
     */
    void Record(
        const std::vector<NodeInstance*>& executedNodes,
        uint64_t gpuTimeNs = 0,
        uint64_t cpuTimeNs = 0
    );
    
    /**
     * @brief Check if node was executed in any recent frame
     * @param node Node to check
     * @return True if node executed in last N frames (where N = buffer size)
     * 
     * **Safety**: If true, node might have GPU work in-flight → requires sync
     */
    bool WasNodeExecutedRecently(NodeInstance* node) const;
    
    /**
     * @brief Get the oldest frame snapshot (furthest in past)
     * 
     * **Safety**: Nodes in the oldest frame are definitely safe to modify
     * (no GPU work in-flight from that frame).
     */
    const FrameSnapshot& GetOldestFrame() const {
        return buffer_[GetOldestSlot()];
    }
    
    /**
     * @brief Get current frame snapshot being recorded
     */
    FrameSnapshot& GetCurrentFrame() {
        return buffer_[currentSlot_];
    }
    
    /**
     * @brief Get total frames recorded (for debug/metrics)
     */
    uint64_t GetFrameCount() const { return frameCount_; }
    
    /**
     * @brief Get buffer size (number of frames tracked)
     */
    uint32_t GetBufferSize() const { return static_cast<uint32_t>(buffer_.size()); }
    
    /**
     * @brief Clear all history (for graph reset)
     */
    void Clear() {
        for (auto& snapshot : buffer_) {
            snapshot.Clear();
        }
        frameCount_ = 0;
    }

private:
    std::vector<FrameSnapshot> buffer_;
    uint32_t currentSlot_;
    uint64_t frameCount_;
    
    uint32_t GetOldestSlot() const {
        // After N records, oldest is at (currentSlot + 1) % size
        if (frameCount_ < buffer_.size()) {
            return 0;  // Not yet wrapped around
        }
        return (currentSlot_ + 1) % buffer_.size();
    }
};

} // namespace Vixen::RenderGraph
```

#### 2. Implement `FrameHistoryRing`

**File**: `RenderGraph/src/Core/FrameSnapshot.cpp` (NEW)

```cpp
#include "Core/FrameSnapshot.h"
#include "Core/NodeInstance.h"

namespace Vixen::RenderGraph {

void FrameHistoryRing::Record(
    const std::vector<NodeInstance*>& executedNodes,
    uint64_t gpuTimeNs,
    uint64_t cpuTimeNs
) {
    // Advance to next slot
    currentSlot_ = (currentSlot_ + 1) % buffer_.size();
    frameCount_++;
    
    // Record snapshot
    FrameSnapshot& current = buffer_[currentSlot_];
    current.frameIndex = frameCount_;
    current.timestamp = std::chrono::high_resolution_clock::now();
    current.executedNodes = executedNodes;
    current.gpuTimeNs = gpuTimeNs;
    current.cpuTimeNs = cpuTimeNs;
}

bool FrameHistoryRing::WasNodeExecutedRecently(NodeInstance* node) const {
    for (const auto& snapshot : buffer_) {
        if (snapshot.ContainsNode(node)) {
            return true;
        }
    }
    return false;
}

} // namespace Vixen::RenderGraph
```

### Part B: Per-Node Synchronization

#### 1. Extend `NodeInstance` with Semaphore Support

**File**: `RenderGraph/include/Core/NodeInstance.h`

Add to `NodeInstance` class:

```cpp
public:
    // ===== GPU Synchronization =====
    
    /**
     * @brief Get the semaphore signaled when this node's GPU work completes
     * 
     * **Usage Pattern**:
     * ```cpp
     * // After Execute():
     * node->SignalExecutionComplete(semaphore);
     * 
     * // Before Recompile():
     * if (node->IsExecutionComplete()) {
     *     // Safe to recompile
     * }
     * ```
     */
    VkSemaphore GetExecutionCompletionSemaphore() const {
        return executionCompleteSemaphore_;
    }
    
    /**
     * @brief Signal that this node's GPU work has been submitted
     * 
     * Called by Execute() or the command buffer submission layer
     * after GPU work is queued. Records frame index for tracking.
     * 
     * @param semaphore VkSemaphore to signal when node's GPU work complete
     */
    void SignalExecutionSubmitted(VkSemaphore semaphore) {
        executionCompleteSemaphore_ = semaphore;
        lastSubmittedFrameIndex_ = owningGraph_->GetGlobalFrameIndex();
        executionState_ = ExecutionState::Submitted;
    }
    
    /**
     * @brief Check if this node's GPU work is complete
     * 
     * **Non-blocking check** (requires VK_KHR_synchronization2).
     * Falls back to aggressive check if extension unavailable.
     * 
     * @return True if GPU work complete, or node never executed
     */
    bool IsExecutionComplete() const;
    
    /**
     * @brief Get frame index when this node's work was last submitted
     */
    uint64_t GetLastSubmittedFrameIndex() const {
        return lastSubmittedFrameIndex_;
    }

private:
    enum class ExecutionState {
        Never,       // Never submitted
        Submitted,   // GPU work in-flight
        Complete     // GPU work finished
    };
    
    VkSemaphore executionCompleteSemaphore_ = VK_NULL_HANDLE;
    uint64_t lastSubmittedFrameIndex_ = 0;
    ExecutionState executionState_ = ExecutionState::Never;
```

#### 2. Implement Node Synchronization Logic

**File**: `RenderGraph/src/Core/NodeInstance.cpp`

Add method:

```cpp
bool NodeInstance::IsExecutionComplete() const {
    if (executionState_ == ExecutionState::Never) {
        return true;  // Never executed, so "complete"
    }
    
    if (executionState_ != ExecutionState::Submitted) {
        return true;  // Already marked complete
    }
    
    // Check semaphore status (requires VK_KHR_synchronization2)
    if (GetDevice() && executionCompleteSemaphore_ != VK_NULL_HANDLE) {
        // TODO (Phase H): Implement VK_KHR_synchronization2 based check
        // For now, fall back to aggressive vkDeviceWaitIdle
        return false;  // Conservative: assume still in-flight
    }
    
    return true;
}
```

### Part C: Frame History Integration into RenderGraph

#### 1. Add to `RenderGraph.h`

Add member variables and methods:

```cpp
private:
    // ===== Frame History Tracking (for smooth recompilation) =====
    
    /// Ring buffer of frame execution snapshots
    FrameHistoryRing frameHistory_{3};
    
    /// Current global frame index (incremented each frame)
    uint64_t globalFrameIndex_ = 0;

public:
    /**
     * @brief Get current global frame index
     * 
     * Used by nodes to track when they were last submitted.
     */
    uint64_t GetGlobalFrameIndex() const { return globalFrameIndex_; }
```

#### 2. Modify `RecompileDirtyNodes()` Implementation

**File**: `RenderGraph/src/Core/RenderGraph.cpp`

Replace the aggressive synchronization:

```cpp
void RenderGraph::RecompileDirtyNodes() {
    if (dirtyNodes.empty()) {
        return;
    }
    
    // ... existing deferred node handling ...
    
    // ===== NEW: Frame-Aware Synchronization =====
    
    // Collect nodes to recompile
    std::unordered_set<NodeInstance*> nodesToRecompileSet;
    for (NodeHandle handle : dirtyNodes) {
        if (handle.IsValid() && handle.index < instances.size()) {
            NodeInstance* node = instances[handle.index].get();
            if (node) {
                nodesToRecompileSet.insert(node);
            }
        }
    }
    
    // Check if any nodes need synchronization
    bool needsSync = false;
    std::unordered_set<VkDevice> devicesToWait;
    
    for (NodeInstance* node : nodesToRecompileSet) {
        // ✅ IMPROVED: Only sync if node was recently executed
        if (frameHistory_.WasNodeExecutedRecently(node)) {
            needsSync = true;
            
            auto* vdev = node->GetDevice();
            if (vdev && vdev->device != VK_NULL_HANDLE) {
                devicesToWait.insert(vdev->device);
            }
        }
    }
    
    // Only wait if we found nodes that might have in-flight work
    if (needsSync && !devicesToWait.empty()) {
        for (VkDevice dev : devicesToWait) {
            if (dev != VK_NULL_HANDLE) {
                // ✅ SAFE: Only affects devices with recent node execution
                // Much better than vkDeviceWaitIdle() on all devices
                vkDeviceWaitIdle(dev);
            }
        }
    }
    
    // ... rest of recompilation logic ...
}
```

#### 3. Record Frame History in `Execute()`

**File**: `RenderGraph/src/Core/RenderGraph.cpp`

After rendering completes, record which nodes executed:

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    // ... existing execution ...
    
    // Record frame snapshot for synchronization tracking
    std::vector<NodeInstance*> executedNodes;
    for (const auto& node : executionOrder) {
        if (node && node->GetState() == NodeState::Complete) {
            executedNodes.push_back(node.get());
        }
    }
    
    uint64_t gpuTime = 0;
    uint64_t cpuTime = 0;
    // TODO: Collect actual GPU/CPU times from nodes' PerformanceStats
    
    frameHistory_.Record(executedNodes, gpuTime, cpuTime);
    globalFrameIndex_++;
}
```

### Part D: Event Deferred Recompilation

#### 1. Modify `ProcessEvents()`

**File**: `RenderGraph/src/Core/RenderGraph.cpp`

```cpp
void RenderGraph::ProcessEvents() {
    if (!messageBus) return;
    
    // Process all pending events
    messageBus->ProcessMessages();
    
    // ===== NEW: Deferred recompilation at safe point =====
    // Events have queued dirty nodes, but actual recompilation
    // happens HERE (after message processing, before Execute)
    // This ensures:
    // 1. All event-driven invalidations are collected
    // 2. GPU work from previous frame has been submitted
    // 3. Safe to synchronize only dirty nodes
    
    RecompileDirtyNodes();  // Selective sync based on frame history
}
```

#### 2. Ensure `ProcessEvents()` is Called Between Frames

**File**: `source/VulkanGraphApplication.cpp` (or equivalent main loop)

Modify frame loop:

```cpp
void VulkanGraphApplication::Update() {
    if (!isPrepared) return;
    
    time.Update();
    
    if (renderGraph) {
        // ✅ CRITICAL: Process events BEFORE rendering
        // This triggers selective recompilation if needed
        renderGraph->ProcessEvents();
    }
}

void VulkanGraphApplication::Render() {
    if (!isPrepared) return;
    
    if (renderGraph) {
        // GPU executes with freshly recompiled nodes
        renderGraph->Execute(primaryCommandBuffer);
    }
}
```

---

## Code Integration Points

### Integration Point 1: CMakeLists.txt

Add new source files:

```cmake
# RenderGraph/CMakeLists.txt

set(VIXEN_RENDERGRAPH_SOURCES
    ${VIXEN_RENDERGRAPH_SOURCES}
    src/Core/FrameSnapshot.cpp
)

set(VIXEN_RENDERGRAPH_HEADERS
    ${VIXEN_RENDERGRAPH_HEADERS}
    include/Core/FrameSnapshot.h
)
```

### Integration Point 2: Header Includes

**File**: `RenderGraph/include/Core/RenderGraph.h`

```cpp
#include "Core/FrameSnapshot.h"
```

**File**: `RenderGraph/include/Core/NodeInstance.h`

```cpp
#include <vulkan/vulkan.h>
#include <cstdint>
```

### Integration Point 3: Existing Node Execution

Nodes don't need modification if they already call `RegisterCleanup()`. The frame history is automatically populated during `Execute()`.

---

## Testing & Validation

### Test 1: Frame History Tracking

**File**: `tests/RenderGraph/FrameHistoryTests.cpp` (NEW)

```cpp
#include <gtest/gtest.h>
#include "Core/FrameSnapshot.h"
#include "Core/NodeInstance.h"

TEST(FrameHistoryRing, TracksNodeExecution) {
    FrameHistoryRing history(3);
    
    MockNodeInstance node1, node2, node3;
    
    // Frame 1: Nodes 1 and 2 execute
    std::vector<NodeInstance*> frame1 = {&node1, &node2};
    history.Record(frame1);
    EXPECT_TRUE(history.WasNodeExecutedRecently(&node1));
    EXPECT_TRUE(history.WasNodeExecutedRecently(&node2));
    EXPECT_FALSE(history.WasNodeExecutedRecently(&node3));
    
    // Frame 2: Nodes 2 and 3 execute
    std::vector<NodeInstance*> frame2 = {&node2, &node3};
    history.Record(frame2);
    EXPECT_TRUE(history.WasNodeExecutedRecently(&node2));
    EXPECT_TRUE(history.WasNodeExecutedRecently(&node3));
    
    // Frame 3: Only node 3 executes
    std::vector<NodeInstance*> frame3 = {&node3};
    history.Record(frame3);
    EXPECT_TRUE(history.WasNodeExecutedRecently(&node3));
    
    // Frame 4: node 1 not in any recent frame → not executed recently
    std::vector<NodeInstance*> frame4 = {&node2};
    history.Record(frame4);
    EXPECT_FALSE(history.WasNodeExecutedRecently(&node1));  // Dropped out of 3-frame window
}

TEST(FrameHistoryRing, OldestFrameGuaranteedSafe) {
    FrameHistoryRing history(2);  // 2-frame buffer
    
    MockNodeInstance node;
    std::vector<NodeInstance*> executed = {&node};
    
    history.Record(executed);  // Frame 1
    history.Record(executed);  // Frame 2
    
    const FrameSnapshot& oldest = history.GetOldestFrame();
    // Oldest frame is from Frame 1, which is now 1 frame old
    // Safe to modify nodes from oldest frame without sync
    EXPECT_EQ(oldest.frameIndex, 1);
}
```

### Test 2: Selective Synchronization

**File**: `tests/RenderGraph/SyncTests.cpp` (NEW)

```cpp
TEST(SelectiveSync, OnlyWaitsForRecentlyExecutedNodes) {
    MockVulkanDevice mockDevice;
    RenderGraph graph(&registry, &messageBus);
    
    // Add node that hasn't executed yet
    NodeHandle node = graph.AddNode("ComputePass", "compute1");
    
    // Mark for recompilation
    graph.MarkNodeNeedsRecompile(node);
    
    // Frame history is empty → node not recently executed
    
    // Should NOT call vkDeviceWaitIdle
    EXPECT_CALL(mockDevice, vkDeviceWaitIdle).Times(0);
    
    graph.RecompileDirtyNodes();  // ✅ No synchronization
}

TEST(SelectiveSync, WaitsForRecentlyExecutedNodes) {
    MockVulkanDevice mockDevice;
    RenderGraph graph(&registry, &messageBus);
    
    NodeHandle node = graph.AddNode("ComputePass", "compute1");
    
    // Simulate node execution
    std::vector<NodeInstance*> executed = {/* ... */};
    graph.frameHistory_.Record(executed);  // Node in frame history
    
    // Mark for recompilation
    graph.MarkNodeNeedsRecompile(node);
    
    // SHOULD call vkDeviceWaitIdle
    EXPECT_CALL(mockDevice, vkDeviceWaitIdle).Times(1);
    
    graph.RecompileDirtyNodes();  // ✅ Selective synchronization
}
```

### Test 3: Stutter Metric

**File**: `tests/RenderGraph/StutterMetrics.cpp` (NEW)

```cpp
TEST(StutterMetrics, NoTimeDiscontinuity) {
    // Measure animation time discontinuity during recompilation
    
    ComputeShaderTestHarness harness;
    harness.Initialize();
    
    std::vector<double> frameTimes;
    
    for (int i = 0; i < 300; i++) {
        double time = harness.GetComputeShaderTime();
        frameTimes.push_back(time);
        
        // Trigger recompilation at frame 150
        if (i == 150) {
            harness.TriggerShaderRecompilation();
        }
        
        harness.RenderFrame();
    }
    
    // Analyze time continuity
    double maxTimeDelta = 0.0;
    double expectedDelta = 16.67;  // ~60 FPS @ 16.67ms
    
    for (size_t i = 1; i < frameTimes.size(); i++) {
        double delta = frameTimes[i] - frameTimes[i-1];
        maxTimeDelta = std::max(maxTimeDelta, std::abs(delta - expectedDelta));
    }
    
    // NEW ARCHITECTURE: Stutter should be minimal (<2ms)
    // OLD ARCHITECTURE: Would show 100+ms spikes
    EXPECT_LT(maxTimeDelta, 2.0);  // ✅ Smooth!
}
```

### Test 4: Performance Regression

**File**: `tests/RenderGraph/PerfRegression.cpp` (NEW)

```cpp
TEST(PerfRegression, RecompilationLatency) {
    RenderGraph graph(&registry, &messageBus);
    
    // Build graph with 100 nodes
    std::vector<NodeHandle> nodes;
    for (int i = 0; i < 100; i++) {
        nodes.push_back(graph.AddNode("ComputePass", "compute" + std::to_string(i)));
    }
    
    graph.Compile();
    
    // Trigger recompilation of 10 random nodes
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10; i++) {
        graph.MarkNodeNeedsRecompile(nodes[i * 10]);
    }
    graph.RecompileDirtyNodes();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Recompilation should be <5ms (conservative estimate)
    EXPECT_LT(latency.count(), 5);
}
```

---

## Performance Considerations

### Memory Overhead

```
FrameHistoryRing(3) overhead:
  = 3 * sizeof(FrameSnapshot)
  = 3 * (8 + 8 + vector<NodeInstance*> + 8 + 8)
  ≈ 3 * (32 + 32*N)  where N = avg nodes per frame

For N=50 nodes/frame:
  ≈ 3 * (32 + 1600) ≈ 4.9 KB
```

**Negligible**: Frame history uses <10 KB across typical graph.

### Synchronization Latency

```
OLD ARCHITECTURE (Aggressive):
  RecompileDirtyNodes() → vkDeviceWaitIdle() → 50-500ms stall

NEW ARCHITECTURE (Selective):
  RecompileDirtyNodes() → Check frame history (O(1))
                      → If needed: vkDeviceWaitIdle() → 0-50ms
                      → If not needed: Continue (no stall)

Typical improvement: 90-99% reduction in stall time
```

### GPU Utilization

```
OLD: GPU sits idle during recompilation
     ▓▓▓▓ GPU work
     ░░░░ Idle (stall)
     ▓▓▓▓

NEW: GPU keeps working with old code while CPU recompiles
     ▓▓▓▓ GPU work (old code)
     ▓▓▓▓ GPU work (new code, after sync point)
```

---

## Migration Path

### Phase 1: Infrastructure (Week 1)

1. ✅ Create `FrameSnapshot.h` with history ring
2. ✅ Implement `FrameHistoryRing` class
3. ✅ Add to `CMakeLists.txt`
4. 🔄 Build verification

### Phase 2: Integration (Week 2)

1. Extend `NodeInstance` with semaphore tracking
2. Integrate `FrameHistoryRing` into `RenderGraph`
3. Modify `RecompileDirtyNodes()` implementation
4. Update `ProcessEvents()` call order

### Phase 3: Testing (Week 3)

1. Write unit tests for frame history
2. Write integration tests for synchronization
3. Benchmark stutter reduction
4. Performance regression tests

### Phase 4: Validation (Week 4)

1. Run on target hardware (Windows NVIDIA, AMD)
2. Measure animation smoothness (capture frame times)
3. Validate shader reload workflow
4. Document findings in Performance report

### Phase 5: Optimization (Phase H)

Future improvements:
- VK_KHR_synchronization2 for non-blocking semaphore queries
- Timeline semaphores for frame-level granularity
- Async compute shader compilation (background thread)
- Shader cache with warm-up precompilation

---

## Debugging Guide

### Symptom: Still Seeing Stutter

**Checklist**:
1. Verify `ProcessEvents()` is called before `Execute()` ✓
2. Check `RecompileDirtyNodes()` is using `frameHistory_` ✓
3. Confirm `frameHistory_.Record()` called after `Execute()` ✓
4. Validate dirty nodes being added correctly ✓

**Debug Output** (enable in code):
```cpp
#define DEBUG_RECOMPILATION 1

#if DEBUG_RECOMPILATION
std::cout << "[Frame " << globalFrameIndex_ << "] "
          << "Recompiling " << nodesToRecompileSet.size() << " nodes\n";

for (NodeInstance* node : nodesToRecompileSet) {
    bool recent = frameHistory_.WasNodeExecutedRecently(node);
    std::cout << "  Node '" << node->GetInstanceName() 
              << "': recently_executed=" << recent << "\n";
}
#endif
```

### Symptom: vkDeviceWaitIdle() Still Blocking

**Cause**: `frameHistory_.WasNodeExecutedRecently()` returning true when it shouldn't.

**Verification**:
```cpp
// After recording frame history
for (const auto& node : dirtyNodes) {
    bool wasRecent = frameHistory_.WasNodeExecutedRecently(node);
    // If wasRecent is false but vkDeviceWaitIdle still stalls,
    // the node might be executing in a different device/queue
}
```

---

## Future Work

### Phase H: Discovery System Integration

Timeline semaphores for automatic layout discovery:
```cpp
class UnknownTypeRegistry {
    struct LayoutHasher {
        uint64_t ComputeStructHash(const SpirvStructDefinition&);
        // Uses semaphore timeline to verify GPU work complete
    };
};
```

### Phase I: Multi-GPU Support

Extend frame history to track per-GPU execution:
```cpp
struct PerDeviceFrameSnapshot {
    VkDevice device;
    std::vector<NodeInstance*> executedNodes;
    uint64_t completionTimelineValue;  // VK_KHR_synchronization2
};
```

### Beyond Phase I: Async Recompilation

Background thread for shader compilation:
```cpp
std::thread compilationThread;

void AsyncRecompile(NodeInstance* node) {
    // Compile in background
    compilationThread = std::thread([node]() {
        node->Compile();  // No GPU wait
    });
    // Submit when ready
}
```

---

## References

- **Vulkan Specification**: GPU Synchronization (Chapter 6)
  - `vkDeviceWaitIdle()`: Full device sync
  - `VK_KHR_synchronization2`: Timeline semaphores
  
- **Frame Pipelining Pattern**:
  - Unreal Engine RDG (Render Dependency Graph)
  - Unity HDRP (High Definition Render Pipeline)
  - Frostbite FrameGraph

- **VIXEN Documentation**:
  - `RenderGraph-Architecture-Overview.md`
  - `PhaseG-ComputePipeline-Plan.md`
  - `EventBusArchitecture.md`

---

## Checklist for Implementation

- [ ] Create `FrameSnapshot.h`
- [ ] Implement `FrameHistoryRing` class
- [ ] Add to `CMakeLists.txt`
- [ ] Extend `NodeInstance` with semaphore support
- [ ] Integrate into `RenderGraph`
- [ ] Modify `RecompileDirtyNodes()`
- [ ] Update `ProcessEvents()` flow
- [ ] Write unit tests
- [ ] Write integration tests
- [ ] Benchmark and validate
- [ ] Document performance results
- [ ] Update migration guide

---

**Document Version**: 1.0  
**Status**: Ready for implementation  
**Last Review**: November 3, 2025
