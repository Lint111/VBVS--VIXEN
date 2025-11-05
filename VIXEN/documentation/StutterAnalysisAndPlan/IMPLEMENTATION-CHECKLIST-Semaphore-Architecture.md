# Implementation Checklist: Semaphore-Based Smooth Recompilation

**Start Date**: [To be filled]  
**Target Completion**: [+4 weeks]  
**Status**: Ready for execution

---

## Phase 1: Infrastructure Setup (Week 1)

### 1.1 Create FrameSnapshot Header
- [ ] Create file: `RenderGraph/include/Core/FrameSnapshot.h`
- [ ] Define `FrameSnapshot` struct with fields:
  - [ ] `frameIndex: uint64_t`
  - [ ] `timestamp: std::chrono::high_resolution_clock::time_point`
  - [ ] `executedNodes: std::vector<NodeInstance*>`
  - [ ] `gpuTimeNs: uint64_t`
  - [ ] `cpuTimeNs: uint64_t`
- [ ] Add `FrameSnapshot::ContainsNode(NodeInstance*)` method
- [ ] Add `FrameSnapshot::Clear()` method

### 1.2 Implement FrameHistoryRing Class
- [ ] Define `FrameHistoryRing` class in header
  - [ ] Constructor: `FrameHistoryRing(uint32_t bufferSize = 3)`
  - [ ] `Record(nodes, gpuTime, cpuTime)` method
  - [ ] `WasNodeExecutedRecently(node)` method
  - [ ] `GetOldestFrame()` method
  - [ ] `GetCurrentFrame()` method
  - [ ] `GetFrameCount()` method
  - [ ] `GetBufferSize()` method
  - [ ] `Clear()` method
- [ ] Private helper: `GetOldestSlot()` method

### 1.3 Implement FrameSnapshot CPP
- [ ] Create file: `RenderGraph/src/Core/FrameSnapshot.cpp`
- [ ] Implement `FrameHistoryRing::Record()`
  - [ ] Advance `currentSlot_`
  - [ ] Increment `frameCount_`
  - [ ] Store snapshot data
  - [ ] Set timestamp
- [ ] Implement `FrameHistoryRing::WasNodeExecutedRecently()`
  - [ ] Iterate through all buffer slots
  - [ ] Check each snapshot's `ContainsNode()`
  - [ ] Return true if found in any frame

### 1.4 Update CMakeLists.txt
- [ ] Add to `RenderGraph/CMakeLists.txt`:
  - [ ] `src/Core/FrameSnapshot.cpp` to source list
  - [ ] `include/Core/FrameSnapshot.h` to header list
- [ ] Verify build succeeds: `cmake --build build --config Debug`

### 1.5 Verification
- [ ] Code compiles without errors
- [ ] No new compiler warnings
- [ ] Unused code detected and documented (if any)

---

## Phase 2: NodeInstance Extension (Week 2, Days 1-2)

### 2.1 Extend NodeInstance Header
**File**: `RenderGraph/include/Core/NodeInstance.h`

Add to public section:
- [ ] Add public enum `ExecutionState`:
  ```cpp
  enum class ExecutionState {
      Never,       // Never submitted
      Submitted,   // GPU work in-flight
      Complete     // GPU work finished
  };
  ```

Add public methods:
- [ ] `VkSemaphore GetExecutionCompletionSemaphore() const`
- [ ] `void SignalExecutionSubmitted(VkSemaphore semaphore)`
- [ ] `bool IsExecutionComplete() const`
- [ ] `uint64_t GetLastSubmittedFrameIndex() const`

Add private members:
- [ ] `VkSemaphore executionCompleteSemaphore_ = VK_NULL_HANDLE`
- [ ] `uint64_t lastSubmittedFrameIndex_ = 0`
- [ ] `ExecutionState executionState_ = ExecutionState::Never`

### 2.2 Implement NodeInstance Methods
**File**: `RenderGraph/src/Core/NodeInstance.cpp`

Implement `NodeInstance::IsExecutionComplete()`:
```cpp
bool NodeInstance::IsExecutionComplete() const {
    if (executionState_ == ExecutionState::Never) {
        return true;  // Never executed
    }
    
    if (executionState_ != ExecutionState::Submitted) {
        return true;  // Already complete
    }
    
    // TODO (Phase H): VK_KHR_synchronization2 semaphore status check
    if (GetDevice() && executionCompleteSemaphore_ != VK_NULL_HANDLE) {
        return false;  // Conservative: assume still in-flight
    }
    
    return true;
}
```

Implement `NodeInstance::SignalExecutionSubmitted()`:
```cpp
void NodeInstance::SignalExecutionSubmitted(VkSemaphore semaphore) {
    executionCompleteSemaphore_ = semaphore;
    lastSubmittedFrameIndex_ = owningGraph_->GetGlobalFrameIndex();
    executionState_ = ExecutionState::Submitted;
}
```

### 2.3 Update RenderGraph.h
**File**: `RenderGraph/include/Core/RenderGraph.h`

Add include:
- [ ] `#include "Core/FrameSnapshot.h"`

Add to public section:
- [ ] `uint64_t GetGlobalFrameIndex() const { return globalFrameIndex_; }`

Add to private section:
- [ ] `FrameHistoryRing frameHistory_{3};`
- [ ] `uint64_t globalFrameIndex_ = 0;`

### 2.4 Verify Integration
- [ ] Code compiles
- [ ] No circular includes
- [ ] RenderGraph can access FrameHistoryRing
- [ ] NodeInstance can signal execution state

---

## Phase 2: RenderGraph Core Modifications (Week 2, Days 3-5)

### 3.1 Modify RecompileDirtyNodes() - Part 1: Frame History Check

**File**: `RenderGraph/src/Core/RenderGraph.cpp`

**Location**: Around line 1110-1140 (current vkDeviceWaitIdle section)

**Action**: Replace aggressive sync with selective sync

Original code (to replace):
```cpp
std::unordered_set<VkDevice> devicesToWait;
for (NodeInstance* node : nodesToRecompile) {
    if (!node) continue;
    auto* vdev = node->GetDevice();
    if (vdev && vdev->device != VK_NULL_HANDLE) {
        devicesToWait.insert(vdev->device);
    }
}

for (VkDevice dev : devicesToWait) {
    if (dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(dev);  // ⚠️ AGGRESSIVE
    }
}
```

Replace with:
```cpp
// Collect nodes to recompile set
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
            vkDeviceWaitIdle(dev);
        }
    }
}
```

Checklist:
- [ ] Old code identified and marked for replacement
- [ ] New code preserves dirtyNodes iteration logic
- [ ] Compilation succeeds
- [ ] No logic regressions

### 3.2 Modify Execute() - Add Frame Recording

**File**: `RenderGraph/src/Core/RenderGraph.cpp`

**Location**: End of `Execute()` method (around line 1250-1300)

**Action**: Record executed nodes to frame history

Code to add (at end of Execute()):
```cpp
// ===== NEW: Record frame snapshot for synchronization tracking =====

// Collect nodes that executed successfully
std::vector<NodeInstance*> executedNodes;
for (const auto& nodePtr : instances) {
    if (!nodePtr) continue;
    
    NodeInstance* node = nodePtr.get();
    if (node && node->GetState() == NodeState::Complete) {
        executedNodes.push_back(node);
    }
}

// Collect performance metrics
uint64_t gpuTime = 0;
uint64_t cpuTime = 0;
// TODO: Aggregate from nodes' PerformanceStats if available
// for (auto& node : executedNodes) {
//     gpuTime += node->GetPerformanceStats().gpuTimeNs;
//     cpuTime += node->GetPerformanceStats().cpuTimeNs;
// }

// Record frame snapshot
frameHistory_.Record(executedNodes, gpuTime, cpuTime);

// Increment global frame counter
globalFrameIndex_++;
```

Checklist:
- [ ] Added after all execution logic
- [ ] Collects all successfully executed nodes
- [ ] Records to frameHistory_
- [ ] Increments frame counter

### 3.3 Modify ProcessEvents() - Call Order

**File**: `RenderGraph/src/Core/RenderGraph.cpp` (or wherever ProcessEvents is)

**Location**: ProcessEvents() method

**Action**: Ensure RecompileDirtyNodes() called after ProcessEvents

Current (if separate calls):
```cpp
void Update() {
    // ...
    renderGraph->ProcessEvents();  // ← Should already be here
}

void ProcessEvents() {
    if (!messageBus) return;
    messageBus->ProcessMessages();
    // ← RecompileDirtyNodes() should be called here
}
```

Verify:
- [ ] ProcessEvents() is in RenderGraph::ProcessEvents()
- [ ] RecompileDirtyNodes() is called from ProcessEvents()
- [ ] Order is: Process messages → Recompile dirty nodes
- [ ] Execute() called after ProcessEvents()

### 3.4 Compilation & Link Test
- [ ] Code compiles without errors
- [ ] No new linker errors
- [ ] FrameHistoryRing object created and accessed correctly
- [ ] No memory access violations (check with Debug build)

---

## Phase 3: Testing (Week 3)

### 4.1 Unit Tests: FrameHistoryRing

**File**: `tests/RenderGraph/FrameHistoryTests.cpp` (NEW)

Create tests:
- [ ] `TestFrameHistoryRecordsNodes` - Verify Record() stores nodes
- [ ] `TestWasNodeExecutedRecently` - Verify detection works
- [ ] `TestRingBufferWrapAround` - Verify circular buffer behavior
- [ ] `TestOldestFrameCorrect` - Verify oldest slot calculation
- [ ] `TestClear` - Verify Clear() works

Checklist:
- [ ] All tests pass
- [ ] Code coverage > 90%
- [ ] Edge cases handled (empty history, single frame, full buffer)

### 4.2 Unit Tests: Node Synchronization

**File**: `tests/RenderGraph/NodeSyncTests.cpp` (NEW)

Create tests:
- [ ] `TestSignalExecutionSubmitted` - Verify state changes
- [ ] `TestIsExecutionComplete` - Verify state query
- [ ] `TestGetLastSubmittedFrameIndex` - Verify frame tracking

Checklist:
- [ ] All tests pass
- [ ] State transitions correct
- [ ] Frame index tracking accurate

### 4.3 Integration Tests: Selective Synchronization

**File**: `tests/RenderGraph/SelectiveSyncTests.cpp` (NEW)

Create tests:
- [ ] `TestNoSyncForUnexecutedNodes` - vkDeviceWaitIdle NOT called
- [ ] `TestSyncForRecentlyExecutedNodes` - vkDeviceWaitIdle IS called
- [ ] `TestMultiDeviceSync` - Multiple devices sync correctly
- [ ] `TestFallbackSyncIfHistoryEmpty` - Safe path works

Checklist:
- [ ] All tests pass
- [ ] Mock VkDevice behaves correctly
- [ ] Sync decisions correct

### 4.4 Performance Benchmark

**File**: `tests/RenderGraph/StutterBenchmark.cpp` (NEW)

Create benchmark:
```cpp
TEST(StutterBenchmark, AnimationTimeSmoothing) {
    ComputeShaderTestHarness harness;
    harness.Initialize();
    
    std::vector<double> frameTimes;
    
    for (int frame = 0; frame < 300; ++frame) {
        double time = harness.GetComputeShaderTime();
        frameTimes.push_back(time);
        
        if (frame == 150) {
            harness.TriggerShaderRecompilation();  // Force recompile
        }
        
        harness.RenderFrame();
    }
    
    // Analyze: max time delta should be < 2ms
    double maxDelta = 0.0;
    for (size_t i = 1; i < frameTimes.size(); ++i) {
        double delta = std::abs(frameTimes[i] - frameTimes[i-1]);
        maxDelta = std::max(maxDelta, delta);
    }
    
    EXPECT_LT(maxDelta, 2.0);  // < 2ms stutter
}
```

Checklist:
- [ ] Benchmark compiles
- [ ] Runs without errors
- [ ] Measures time discontinuity correctly
- [ ] Shows improvement over baseline

### 4.5 Regression Tests

**File**: `tests/RenderGraph/PerfRegressionTests.cpp` (NEW)

Create tests:
- [ ] `TestRecompilationLatency` - < 5ms per recompile
- [ ] `TestFrameHistoryOverhead` - < 1ms recording time
- [ ] `TestMemoryOverhead` - < 10KB frame history
- [ ] `TestCacheKeyAccuracy` - Hash computation unchanged

Checklist:
- [ ] All performance tests pass
- [ ] No regressions from baseline
- [ ] Overhead acceptable

### 4.6 Run Full Test Suite
- [ ] `ctest --build-dir build` succeeds
- [ ] All new tests pass
- [ ] All existing tests still pass
- [ ] Code coverage acceptable (> 85%)

---

## Phase 4: Validation & Performance (Week 4)

### 5.1 Real-World Testing

**Scenario 1: Shader Reload During Animation**
- [ ] Start compute shader with moving cube
- [ ] Measure time values frame-by-frame
- [ ] Modify shader file
- [ ] Monitor for time discontinuities
- [ ] Expected: No visible stutter

**Scenario 2: Window Resize Event**
- [ ] Start rendering with active animation
- [ ] Resize window (triggers SwapChain recompile)
- [ ] Monitor animation smoothness
- [ ] Expected: Seamless resize, no animation interruption

**Scenario 3: EventBus Cascade Recompilation**
- [ ] Trigger event that invalidates multiple nodes
- [ ] Observe cascade recompilation
- [ ] Monitor GPU synchronization points
- [ ] Expected: Selective sync, minimal stalls

### 5.2 Performance Profiling

Tools: PIX, GPU Debugger, or equivalent

Measure:
- [ ] CPU time in vkDeviceWaitIdle() before/after
- [ ] GPU idle time before/after
- [ ] Frame time variance during recompilation
- [ ] Peak memory usage

Expected results:
- [ ] 90-99% reduction in CPU stall time
- [ ] GPU keeps executing during recompilation
- [ ] Frame time variance < 2ms (vs. 100-500ms before)

### 5.3 Validation on Multiple Targets
- [ ] NVIDIA GPU (if available)
- [ ] AMD GPU (if available)
- [ ] Integrated GPU (Intel/AMD APU)

Checklist per target:
- [ ] No crashes or validation errors
- [ ] Frame time improvement visible
- [ ] Time values monotonically increasing
- [ ] No temporal artifacts in output

### 5.4 Documentation

Create or update:
- [ ] `IMPLEMENTATION-LOG.md` - What was done, lessons learned
- [ ] `PERFORMANCE-REPORT.md` - Before/after metrics
- [ ] `KNOWN-ISSUES.md` - Any issues found (and resolutions)
- [ ] `DEBUG-GUIDE.md` - How to debug if issues arise

### 5.5 Code Review Checklist
- [ ] Code follows C++23 standards (PascalCase, <20 instructions)
- [ ] Comments explain WHY, not WHAT
- [ ] No raw new/delete (smart pointers only)
- [ ] RAII principles followed
- [ ] No circular includes
- [ ] Memory properly freed

---

## Phase 5: Future Work (Post-Launch)

### Planned Enhancements
- [ ] VK_KHR_synchronization2 integration (Phase H)
- [ ] Timeline semaphore support (Phase H)
- [ ] Async compilation background thread (Phase I)
- [ ] Multi-GPU load balancing (Phase I)

### Known Limitations (Document)
- [ ] Non-blocking semaphore query not yet implemented (requires KHR extension)
- [ ] Async compilation not yet supported
- [ ] No per-queue synchronization (whole device sync)
- [ ] Frame history only tracks execution, not GPU timeline

---

## Daily Standup Template

Use this during implementation:

```
Date: [___]
Status: ON TRACK / AT RISK / BLOCKED

Completed:
- [ ] [Task from checklist]
- [ ] [Task from checklist]

Blockers:
- [If any]

Next Steps:
- [ ] [Next task]
- [ ] [Next task]

Notes:
[Any interesting findings or challenges]
```

---

## Rollback Plan

If major issues discovered:

1. **Immediate**: Disable frame history check
   - Comment out `if (frameHistory_.WasNodeExecutedRecently(node))` check
   - Always call vkDeviceWaitIdle (revert to old behavior)

2. **Short-term**: Revert ProcessEvents() changes
   - Remove frame history recording
   - Restore old sync logic

3. **Long-term**: Create new branch
   - `git checkout -b phase-g-sync-revert`
   - Full rollback if needed

---

## Success Criteria

✅ **Milestone 1: Compilation** (End of Week 1)
- [ ] All new code compiles
- [ ] No new warnings
- [ ] CMakeLists.txt updated

✅ **Milestone 2: Integration** (End of Week 2)
- [ ] NodeInstance extended
- [ ] RenderGraph modified
- [ ] Code compiles and runs

✅ **Milestone 3: Testing** (End of Week 3)
- [ ] All unit tests pass
- [ ] Integration tests pass
- [ ] Performance benchmark shows improvement

✅ **Milestone 4: Validation** (End of Week 4)
- [ ] Real-world testing successful
- [ ] Performance profiling complete
- [ ] Documentation updated

---

## Sign-Off

- [ ] Code reviewed by team lead
- [ ] All tests passing on CI/CD
- [ ] Performance metrics approved
- [ ] Documentation complete
- [ ] Ready for production

**Approved by**: [Name]  
**Date**: [Date]

---

**Checklist Version**: 1.0  
**Last Updated**: November 3, 2025  
**Status**: Ready for execution
