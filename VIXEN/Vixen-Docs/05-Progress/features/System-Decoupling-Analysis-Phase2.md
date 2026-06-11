# System Decoupling Analysis - Phase 2

**Date**: 2026-01-08
**Sprint**: 6.3 - Timeline Capacity System (Continuation)
**Purpose**: Comprehensive mapping of RenderGraph, VulkanDevice, and Host systems for event-driven refactoring

---

## Executive Summary

This analysis maps interactions across three system layers:
1. **RenderGraph Core** - Graph compilation, node execution, resource lifecycle
2. **VulkanDevice/GPU Layer** - Device management, memory allocation, queue submission
3. **Host Layer** - Window, input, application loop

**Key Finding**: Many direct method calls can become event-driven, and several RenderGraph-managed operations can become implicit based on graph state.

---

## 1. Current System Coupling Map

### 1.1 RenderGraph Direct Dependencies

| Subsystem | Current Call Pattern | Coupling Level |
|-----------|---------------------|----------------|
| TimelineCapacityTracker | `BeginFrame()`/`EndFrame()` | ✅ **DECOUPLED** (Sprint 6.3) |
| TaskProfileRegistry | `DecreaseLowestPriority()` | ✅ **DECOUPLED** (Sprint 6.3) |
| LifetimeScopeManager | `BeginFrame()`/`EndFrame()` | 🔴 Direct call |
| DeferredDestructionQueue | `ProcessFrame(frameIndex)` | 🔴 Direct call |
| GPUQueryManager | `BeginFrame()` per-consumer | 🟡 Scattered calls |
| CleanupStack | `ExecuteAll()` at shutdown | 🟡 Implicit dependency |
| ResourceDependencyTracker | `RegisterResourceProducer()` | 🟡 Implicit during connect |
| MainCacher | `Load/Save` persistent caches | 🔴 Direct call |
| LoopManager | `UpdateLoops()` | 🔴 Direct call in Execute |

### 1.2 VulkanDevice Direct Dependencies

| Consumer | Access Pattern | Coupling Level |
|----------|---------------|----------------|
| DeviceNode | Creates/owns VulkanDevice | Owner |
| ComputeDispatchNode | `vulkanDevice->device`, `->queue` | 🔴 Direct member access |
| MultiDispatchNode | `vulkanDevice_->device`, query manager | 🔴 Direct member access |
| CommandPoolNode | `vulkanDevice->device` for pool creation | 🔴 Direct member access |
| GeometryRenderNode | `vkQueueSubmit(vulkanDevice->queue)` | 🔴 Direct queue access |
| TraceRaysNode | Same pattern | 🔴 Direct queue access |
| 83+ other files | Various direct access | 🔴 Scattered |

### 1.3 Host Layer Dependencies

| System | Trigger | Current Handler |
|--------|---------|-----------------|
| Window Close | WM_CLOSE → WindowNode | Publish WindowCloseEvent → App |
| Window Resize | WM_SIZE → WindowNode | Publish WindowResizedMessage → SwapChain |
| Input | GetAsyncKeyState polling | InputNode polls each frame |
| Frame Loop | main.cpp while loop | App.Update() → App.Render() |

---

## 2. Event-Driven Conversion Candidates

### 2.1 HIGH PRIORITY - Frame Lifecycle Systems

#### 2.1.1 LifetimeScopeManager (RECOMMENDED)

**Current:**
```cpp
// RenderGraph::RenderFrame()
if (scopeManager_) {
    scopeManager_->BeginFrame();  // Line 599
}
// ... node execution ...
if (scopeManager_) {
    scopeManager_->EndFrame();    // Line 692
}
```

**Proposed:**
```cpp
// LifetimeScopeManager subscribes to frame events
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

**Impact**: Remove 2 direct calls from RenderGraph, system self-manages

#### 2.1.2 DeferredDestructionQueue (RECOMMENDED)

**Current:**
```cpp
// RenderGraph::RenderFrame()
deferredDestruction.ProcessFrame(globalFrameIndex);  // Line 595
```

**Proposed:**
```cpp
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

**Impact**: Remove 1 direct call, destruction happens automatically

#### 2.1.3 GPUQueryManager (RECOMMENDED)

**Current:**
```cpp
// Scattered across nodes
queryManager_->BeginFrame();  // MultiDispatchNode line 368
queryManager_->BeginFrame();  // GeometryRenderNode line 286
queryManager_->BeginFrame();  // TraceRaysNode line 276
// Internal guard prevents multiple init, but pattern is confusing
```

**Proposed:**
```cpp
class GPUQueryManager {
    ScopedSubscriptions subscriptions_;

    void SubscribeToFrameEvents(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(
            [this](const auto& e) { BeginFrame(); }  // Single init
        );
    }
};
```

**Impact**: Remove scattered BeginFrame() calls from nodes, single initialization point

---

### 2.2 MEDIUM PRIORITY - Resource Management

#### 2.2.1 Queue Submission Coordination

**Current:** Direct `vkQueueSubmit()` calls scattered across nodes:
- ComputeDispatchNode
- MultiDispatchNode
- GeometryRenderNode
- TraceRaysNode
- BatchedUploader

**Proposed Event Pattern:**
```cpp
struct QueueSubmitRequestEvent : BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    VkQueue queue;
    uint32_t submitCount;
    const VkSubmitInfo2* pSubmits;
    VkFence fence;
    uint32_t priority;  // For ordering
};

// Nodes publish instead of calling vkQueueSubmit directly
messageBus->Publish(std::make_unique<QueueSubmitRequestEvent>(...));

// Central SubmissionCoordinator handles actual submission
class SubmissionCoordinator {
    void OnSubmitRequest(const QueueSubmitRequestEvent& e) {
        // Optional: reorder by priority, batch submissions
        vkQueueSubmit2(e.queue, e.submitCount, e.pSubmits, e.fence);
    }
};
```

**Benefits:**
- Central bottleneck for synchronization
- Opportunity for submission batching/reordering
- Clear audit trail
- Easier multi-GPU extension

#### 2.2.2 Memory Allocation Events

**Current:** Direct calls to `vulkanDevice->AllocateBuffer()`

**Proposed:**
```cpp
struct BufferAllocationRequestEvent : BaseEventMessage {
    BufferAllocationRequest request;
    std::function<void(const BufferAllocation&)> callback;
};

// Benefits:
// - Central allocation coordinator
// - Deferred allocation for batching
// - Global fragmentation awareness
// - Memory pressure event trigger
```

**Complexity:** Medium - requires callback pattern or futures

---

### 2.3 LOW PRIORITY - Cleanup & Lifecycle

#### 2.3.1 MainCacher Persistent Cache Loading

**Current:**
```cpp
// RenderGraph::GeneratePipelines()
mainCacher->LoadPerNodeCaches(node);  // After Setup
mainCacher->SaveAllCaches();          // At end
```

**Proposed:**
```cpp
// MainCacher subscribes to compilation events
subscriptions_.Subscribe<NodeSetupCompleteEvent>(
    [this](const auto& e) { LoadPerNodeCaches(e.node); }
);
subscriptions_.Subscribe<GraphCompilationCompleteEvent>(
    [this](const auto& e) { SaveAllCaches(); }
);
```

**Impact:** Minor - currently works fine, but decoupling enables easier testing

---

## 3. Implicit Operations (Graph State-Driven)

These operations should happen automatically based on graph state changes, not explicit calls:

### 3.1 Resource Cleanup on Node Removal

**Current:**
```cpp
// Explicit call required
graph.RemoveNode(handle);
// Node cleanup happens, but dependents may be orphaned
```

**Proposed:**
```cpp
// NodeRemovedEvent triggers cascade cleanup
graph.RemoveNode(handle);
// Internally publishes NodeRemovedEvent
// CleanupStack subscribes and auto-cleans dependents
```

### 3.2 Recompilation Cascade

**Current:**
```cpp
// RenderGraph::RecompileDirtyNodes()
for each dirty node:
    node.Cleanup();
    node.Setup();
    node.Compile();
    // Mark dependents dirty
```

**Proposed Event Chain:**
```cpp
// NodeDirtyEvent → triggers dependent cascade
// Each node subscribes to upstream dirty events
// Recompilation happens in topological order automatically
```

### 3.3 Device Invalidation Cascade

**Current:** DeviceNode publishes DeviceInvalidationEvent, but handlers are inconsistent

**Proposed:**
```cpp
// All device-dependent nodes subscribe to DeviceInvalidationEvent
// Auto-cleanup and auto-recompile when device changes
subscriptions_.Subscribe<DeviceInvalidationEvent>(
    [this](const auto& e) {
        InvalidateDeviceResources();
        MarkNeedsRecompile();
    }
);
```

---

## 4. Systems That Should STAY Direct

Not everything benefits from event-driven architecture:

| System | Reason to Keep Direct |
|--------|----------------------|
| **Node.Execute()** | Hot path, virtual dispatch already minimal |
| **GraphTopology.TopologicalSort()** | One-time during compile, no runtime benefit |
| **Resource slot access** | Per-frame hot path, direct pointer is fastest |
| **Vulkan API calls** | Must be direct, events would add overhead |
| **CleanupStack registration** | Happens during compile, not runtime |

---

## 5. Implementation Priority

### Phase 1: Frame Lifecycle Systems (Sprint 6.3 continuation)
1. ✅ TimelineCapacityTracker → FrameStart/End events (DONE)
2. ✅ TaskProfileRegistry → Budget events (DONE)
3. 🔲 LifetimeScopeManager → FrameStart/End events
4. 🔲 DeferredDestructionQueue → FrameStart event
5. 🔲 GPUQueryManager → FrameStart event

### Phase 2: Resource Coordination (Sprint 6.4 candidate)
6. 🔲 QueueSubmitRequestEvent pattern
7. 🔲 BufferAllocationRequestEvent pattern (if multi-GPU needed)

### Phase 3: Lifecycle Cascade (Sprint 7 candidate)
8. 🔲 NodeDirtyEvent cascade
9. 🔲 DeviceInvalidationEvent auto-handlers

---

## 6. Migration Pattern

For each system moving to event-driven:

```cpp
// Step 1: Add subscription support without removing direct calls
class LifetimeScopeManager {
    ScopedSubscriptions subscriptions_;

    void SubscribeToFrameEvents(MessageBus* bus) {
        subscriptions_.SetBus(bus);
        subscriptions_.Subscribe<FrameStartEvent>(...);
        subscriptions_.Subscribe<FrameEndEvent>(...);
    }
};

// Step 2: RenderGraph initializes subscription
void RenderGraph::InitializeEventDrivenSystems() {
    if (scopeManager_) {
        scopeManager_->SubscribeToFrameEvents(messageBus_);
    }
}

// Step 3: Remove direct calls from RenderFrame()
// Before: scopeManager_->BeginFrame();
// After: (removed - handled by event subscription)

// Step 4: Test thoroughly
// Step 5: Remove old code paths
```

---

## 7. New Event Types Needed

| Event | Publisher | Subscribers | Purpose |
|-------|-----------|-------------|---------|
| `NodeSetupCompleteEvent` | RenderGraph | MainCacher | Trigger cache loading |
| `GraphCompilationCompleteEvent` | RenderGraph | MainCacher | Trigger cache saving |
| `NodeRemovedEvent` | RenderGraph | CleanupStack | Cascade cleanup |
| `DeviceCreatedEvent` | DeviceNode | All device-dependent | Initialize resources |
| `QueueSubmitRequestEvent` | Nodes | SubmissionCoordinator | Centralize submissions |

---

## 8. Benefits Summary

| Change | Lines Removed from RenderGraph | New Capability |
|--------|-------------------------------|----------------|
| LifetimeScopeManager events | ~6 lines | Self-managing lifetime |
| DeferredDestruction events | ~2 lines | Self-triggering cleanup |
| GPUQueryManager events | ~3 scattered | Single init point |
| Queue submission events | 0 (nodes change) | Central coordination |
| **Total** | **~11 lines** | **4 new capabilities** |

---

## 9. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Event ordering issues | Use priority-based event dispatch if needed |
| Performance overhead | Events are synchronous, no async cost |
| Debugging difficulty | Add event logging/tracing |
| Migration regressions | Incremental migration, one system at a time |
| Over-engineering | Only convert systems with clear benefit |

---

## 10. Decision Matrix

| System | Event-Driven? | Reason |
|--------|--------------|--------|
| LifetimeScopeManager | ✅ Yes | Clean separation, self-managing |
| DeferredDestruction | ✅ Yes | Clean separation, frame-aware |
| GPUQueryManager | ✅ Yes | Eliminates scattered init calls |
| Queue Submission | 🟡 Maybe | Useful for multi-GPU, overkill for single |
| Memory Allocation | 🟡 Maybe | Only if multi-GPU or advanced pooling needed |
| Node Execution | ❌ No | Hot path, keep direct |
| Vulkan API calls | ❌ No | Must be direct |
| Graph topology | ❌ No | Compile-time only |

---

## Next Steps

1. **Review this document** - Validate priorities and approach
2. **Implement Phase 1** - Frame lifecycle systems (LifetimeScopeManager, DeferredDestruction, GPUQueryManager)
3. **Evaluate Phase 2** - Queue submission coordination if multi-GPU is on roadmap
4. **Document patterns** - Update architecture docs with event-driven patterns

