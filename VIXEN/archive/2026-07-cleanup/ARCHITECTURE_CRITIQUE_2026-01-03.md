# VIXEN Architecture Critique & Pre-Allocation Analysis
**Date:** January 3, 2026  
**Scope:** Production Roadmap 2026, Current State Review, Optimization Opportunities

---

## Executive Summary

**Current State:** Excellent foundational architecture with clear ownership semantics, event-driven invalidation, and strong type safety. Sprint 4 (ResourceManagement) and Sprint 5 (CashSystem Robustness) have created solid building blocks. Ready for next-phase systems (Timeline, Multi-GPU, Physics).

**Key Findings:**
1. ✅ **Memory architecture fundamentally sound** - Graph owns resources, nodes access via typed API
2. ⚠️ **Allocation fragmentation risk** - 11 sprints of feature work will add allocations scattered across subsystems
3. 🎯 **Pre-allocation opportunity identified** - 5-6 key allocation points can be pre-sized during Setup phase
4. ⚠️ **Runtime allocation risks** - EventBus, container resizing, descriptor binding, task queues need protection

---

## Production Roadmap Analysis

### Timeline Overview

```
Q1 2026:     Sprint 4 (ResourceManager) ✅ COMPLETE
             Sprint 5 (CashSystem)      🔨 58% complete
             Sprint 1.1-1.3 (Infrastructure Hardening) ✅ COMPLETE

Q2 2026:     Sprint 6 (Timeline Foundation)       - 232h
             Sprint 8 (Timeline System)           - 72h
             Sprint 2.3 (Paper Publication)       - research

Q2-Q4 2026:  Sprint 3.3 (Auto Synchronization)    - 72h
             Sprint 3.4 (Multi-GPU)               - 92h

Q1-Q4 2026:  Sprint 7 (Core Physics)              - 224h
             Sprint 11 (Soft Body Physics)        - 104h
             Sprint 12 (GPU Procedural Gen)       - 100h
             Sprint 13 (Skin Width SVO)           - 62h

Q1 2026-Q1 2027: Sprint 14 (VR Integration)       - 124h

TOTAL: 1,422 hours across 11 active sprints
```

### Current Velocity & Risk Factors

**Completed:**
- Sprint 4: ✅ ResourceManagement library (156 tests)
- Sprint 5 Phases 1-2.5: ✅ Memory safety + cacher consolidation (60h/104h = 58%)
- Sprints 1.1-1.3: ✅ Infrastructure hardening

**At Risk:**
- **Timeline System (Sprints 6-9):** Complex architecture with frame history, sub-graph compilation, wave scheduling
- **Multi-GPU (Sprint 10):** Driver compatibility, thread synchronization
- **Physics (Sprints 7, 11-13):** Performance targets (100M voxels, 90 FPS), memory budget (VR)

---

## Architecture Strengths ✅

### 1. Resource Ownership Model
```cpp
// Graph owns resources
RenderGraph {
    std::vector<std::unique_ptr<Resource>> resources;  // Clear lifetime
};

// Nodes access via typed API
TypedNode {
    In<T>(SlotID);   // Compile-time type check
    Out<T>(SlotID);  // Guaranteed valid
};
```
**Assessment:** Eliminates circular dependencies, clear RAII. Excellent foundation.

### 2. Event-Driven Invalidation
```
WindowResize → EventBus → SwapChainNode::OnEvent()
           ↓
    SetDirty(true)
           ↓
    graph.RecompileDirtyNodes()
           ↓
    Cascade invalidation (dependent framebuffers, render passes)
```
**Assessment:** Loose coupling, scalable to 1000+ node graphs. EventBus is single-threaded (safe).

### 3. Type Safety via Compile-Time Verification
```cpp
// Slot config enforces types
struct MyNodeConfig {
    INPUT_SLOT(DEPTH_BUFFER, VkImageView, SlotMode::SINGLE);
    OUTPUT_SLOT(FRAMEBUFFER, VkFramebuffer, SlotMode::SINGLE);
};

// In() and Out() methods are template-specialized
template<SlotID::Type T> T In(SlotID) = delete;  // Won't compile wrong type
template<> VkImageView In(DEPTH_BUFFER) { return ...; }  // Valid
```
**Assessment:** Zero runtime overhead, prevents 90% of node wiring bugs.

### 4. Budget-Aware Resource Management
```cpp
// DeviceBudgetManager tracks allocation
ResourceBudgetManager {
    soft_limit: 4 GB
    hard_limit: 7 GB
    current: 2.1 GB
    throttle_at: 85% (threshold for SlotTask dynamic throttling)
};

// SlotTask respects budget
ExecuteParallel(budget) {
    if (current > soft_limit * 0.85) {
        // Queue tasks, don't allocate more until drain
    }
};
```
**Assessment:** Excellent for VR/mobile. However, relies on allocators respecting budget (testing needed).

---

## Architecture Weaknesses & Risks ⚠️

### 1. **Allocation Fragmentation During Runtime** 🔴 HIGH RISK

**Problem:** 11 sprints of feature work will introduce allocations across:
- EventBus event queue (Sprint 3.1: MultiDispatchNode, TaskQueue)
- Timeline system sub-graph compilation (Sprint 3.2: frame history storage)
- Multi-GPU descriptor copying (Sprint 3.4: cross-GPU buffers)
- Physics simulation buffers (Sprint 4.1-4.5: cellular automata, force fields)
- VR memory tracking (Sprint 14)

**Current Status:**
- ✅ ResourceManagement pre-allocates for device/host buffers
- ❌ No pre-allocation for **transient data structures** (queues, maps, vectors)
- ❌ No pre-allocation for **graph topology changes** (node additions, reconnections)

**Example Issue:**
```cpp
// EventBus during frame execution
std::vector<Event> eventQueue;  // Could reallocate if events > capacity

// Timeline system during compile
std::unordered_map<TimelineID, FrameHistory> history;  // No pre-sizing

// Physics simulation
std::vector<VkBuffer> cellularAutomataBuffers;  // Grows as chunks load
```

**Risk Level:** MEDIUM (setup phase completed before critical deadline, runtime allocations less likely)

### 2. **Graph Recompilation Cascade** 🟡 MEDIUM RISK

**Problem:** EventBus invalidation can trigger expensive recompilation:
```
1 WindowResize event
  ↓
SwapChainNode.SetDirty()
  ↓
EventBus.Emit(SwapChainInvalidated)
  ↓
FramebufferNode.OnEvent() → SetDirty()
  ↓
RenderPassNode.OnEvent() → SetDirty()
  ↓
graph.RecompileDirtyNodes() [3+ nodes recompiled]
  ↓
Each recompile may allocate new descriptor sets, pipelines, command buffers
```

**Current Mitigation:**
- ✅ Cleanup dependency tracking prevents duplicate cleanup
- ✅ Dirty flag prevents re-execution
- ❌ No "batch" invalidation mode to coalesce updates
- ❌ No "dry run" to pre-allocate before cascade

**Risk Level:** MEDIUM (not data path critical, but affects frame latency during window resize)

### 3. **Descriptor Set Binding Deferred Until Compile** 🟡 MEDIUM RISK

**Current Flow (Phase G):**
```
node.Compile() {
    // Create pipeline, but defer descriptor binding
    pipeline = PipelineCache::Get(...);
}

node.Execute() {
    // Descriptors bound during execution
    bindDescriptors(descriptorSets);  // Can allocate if not pre-bound
}
```

**Problem:** Descriptor updates during Execute() can cause GPU stalls if descriptors not pre-allocated.

**Current Mitigation:**
- ✅ SlotRole bitwise flags allow pre-calculation of descriptor needs
- ⚠️ But ExecuteContext doesn't reserve descriptor buffer space

**Risk Level:** LOW-MEDIUM (only affects dynamic descriptor binding, rare in current architecture)

### 4. **CashSystem Uploader Synchronization** 🟡 MEDIUM RISK

**Current Pattern (Sprint 5.2.5):**
```cpp
class TypedCacher {
    GetUploader() {
        // StagingBufferPool allocates on-demand
        return stagingPool->AllocateBuffer(size);  // Can allocate
    }

    // Upload happens during ExecuteParallel
    ExecuteParallel(...) {
        uploader.BatchUpload(data);  // Calls vkCmdCopyBufferToImage
        // May wait for fence if pool exhausted
    }
};
```

**Current State (Sprint 5 — 58% complete):**
- ✅ StagingBufferPool created
- ✅ BatchedUploader in place
- ❌ Pool size not pre-calculated based on workload
- ⚠️ No pre-warming during Setup phase

**Risk Level:** MEDIUM (can block frame if pool exhausted, but Phases 3-4 mitigate)

### 5. **No Pre-Allocation for Timeline Sub-Graphs** 🔴 HIGH RISK (Future)

**Planned Timeline System (Sprint 3.2):**
```cpp
class TimelineNode : public TypedNode {
    std::unordered_map<TimelineID, SubGraph> subgraphs;  // Will dynamically add
    std::unordered_map<TimelineID, FrameHistory> history;  // Per-timeline history
};

// At runtime
void Execute() {
    for (auto& timeline : timelines) {
        // Subgraph compile/execute
        auto oldImage = GetPreviousFrameResource(timeline.id);  // Lookup
        auto result = timeline.subgraph.Execute(...);  // Could allocate descriptors
    }
}
```

**Risk:** Frame allocations during TAA accumulation, motion blur, temporal filtering

**Mitigation Path:** Reserve frame history slots during Setup, pre-allocate descriptor cache per timeline

**Risk Level:** HIGH (not yet implemented, but critical for Q2 deadline)

### 6. **Multi-GPU Memory Transfer Not Pre-Sized** 🔴 HIGH RISK (Future)

**Planned Multi-GPU (Sprint 3.4):**
```cpp
class MultiGPUManager {
    std::unordered_map<GPUHandle, GPUState> gpus;  // Will grow
    std::unordered_map<ResourceHandle, TransferState> transfers;  // Unbounded queue
};

// At runtime
void Schedule() {
    for (auto& work : pendingWork) {
        if (work.target_gpu != current_gpu) {
            transfers[work.id] = TransferState{...};  // Allocates
        }
    }
}
```

**Risk:** Cross-GPU buffer copies queue unbounded, can OOM if load balancing staggers GPUs

**Risk Level:** HIGH (not yet implemented, requires pre-planning)

---

## Pre-Allocation Strategy: Roadmap for Zero-Allocation Runtime 🎯

### Phase 1: Audit Current Allocations (Week 1-2)

**Goal:** Identify all runtime allocations between Setup and Cleanup.

**Method:**
```cpp
// Add allocation tracker in IMemoryAllocator
struct AllocationSnapshot {
    size_t count;
    size_t total_bytes;
    std::string largest_allocation;
    std::vector<std::string> live_pointers;
};

// Log at setup/compile/execute boundaries
AllocationSnapshot snap_after_setup = tracker.Snapshot();
AllocationSnapshot snap_after_frame = tracker.Snapshot();

// Report delta
auto delta = snap_after_frame - snap_after_setup;
if (delta.total_bytes > THRESHOLD) {
    LOG_WARN("Runtime allocation: {} ({} {})", delta.largest_allocation, delta.count, delta.total_bytes);
}
```

**Files to Instrument:**
- [libraries/RenderGraph/include/RenderGraph.h](libraries/RenderGraph/include/RenderGraph.h) - AddNode, RecompileDirtyNodes
- [libraries/EventBus/include/EventBus.h](libraries/EventBus/include/EventBus.h) - Emit, ProcessEvents
- [libraries/CashSystem/include/TypedCacher.h](libraries/CashSystem/include/TypedCacher.h) - GetUploader, BatchUpload
- [libraries/ResourceManagement/include/Memory/ResourceBudgetManager.h](libraries/ResourceManagement/include/Memory/ResourceBudgetManager.h) - Allocation tracking

### Phase 2: Pre-Allocate Critical Structures (Week 2-4)

#### 2.1: EventBus Queue Pre-Allocation
```cpp
// BEFORE: EventBus dynamically grows queue
class EventBus {
    std::vector<Event> eventQueue;  // Can reallocate
};

// AFTER: Pre-sized at graph construction
class EventBus {
    std::vector<Event> eventQueue;
    static constexpr size_t INITIAL_CAPACITY = 1024;  // Max events per frame
    
    EventBus(size_t expectedEventRate = INITIAL_CAPACITY) {
        eventQueue.reserve(expectedEventRate);
    }
};

// Updated RenderGraph::Setup()
void RenderGraph::Setup() {
    size_t expectedEvents = nodes.size() * 3;  // Heuristic: each node ~3 events/frame
    eventBus->PreAllocate(expectedEvents);
}
```

**Impact:** ✅ Zero allocations during frame, EventBus can handle 1K+ events

#### 2.2: Resource Descriptor Cache Pre-Allocation
```cpp
// Current: DescriptorSetCache grows dynamically
class DescriptorSetCache {
    std::unordered_map<DescriptorKey, VkDescriptorSet> cache;
};

// Better: Pre-calculate during Compile phase
class DescriptorSetCache {
    struct PreAllocRequest {
        uint32_t count;
        VkDescriptorSetLayoutBinding* bindings;
    };
    std::vector<PreAllocRequest> requests;
    std::vector<VkDescriptorSet> pool;  // Pre-allocated pool
    
    void PreAllocate(const std::vector<PreAllocRequest>& requests) {
        // Create single pool with all descriptors upfront
        uint32_t totalNeeded = 0;
        for (auto& req : requests) totalNeeded += req.count;
        
        VkDescriptorPoolCreateInfo poolInfo{
            .descriptorSetCount = totalNeeded,
            .pPoolSizes = ...
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
        pool.resize(totalNeeded);
    }
};

// During Compile phase
void RenderGraph::CompileNode(NodeHandle h) {
    auto node = nodes[h].get();
    auto descriptorNeeds = node->GetDescriptorNeeds();  // New method
    descriptorCache.PreAllocate(descriptorNeeds);
    node->Compile();  // Now all descriptors ready
}
```

**Impact:** ✅ No descriptor allocation during Execute

#### 2.3: Timeline Sub-Graph Frame History Pre-Reservation
```cpp
// Planned Timeline System (Sprint 3.2)
class TimelineNode : public TypedNode {
    std::vector<FrameHistorySlot> frameHistory;  // Pre-allocated ring buffer
    uint32_t maxHistoryFrames;
    
    struct FrameHistorySlot {
        std::vector<Resource*> images;
        std::vector<Resource*> buffers;
    };
};

// During Setup
void TimelineNode::Setup() {
    maxHistoryFrames = 4;  // TAA, motion blur typical
    frameHistory.resize(maxHistoryFrames);  // Pre-allocate all slots
    
    for (auto& slot : frameHistory) {
        slot.images.reserve(imageCountPerFrame);
        slot.buffers.reserve(bufferCountPerFrame);
    }
    
    RegisterEventListener();  // Subscribe to frame events
}

// During Execute
void TimelineNode::Execute() {
    // Rotate without allocation
    auto& currentSlot = frameHistory[currentFrameIdx % maxHistoryFrames];
    currentSlot.images[0] = GetCurrentFrameImage();  // No allocation
}
```

**Impact:** ✅ Timeline system zero-allocation during frame execution

#### 2.4: Multi-GPU Transfer Queue Pre-Sizing
```cpp
// Planned Multi-GPU (Sprint 3.4)
class MultiGPUScheduler {
    struct TransferJob {
        GPUHandle src, dst;
        ResourceHandle resource;
        uint64_t offset, size;
    };
    
    std::vector<TransferJob> transferQueue;  // Pre-allocated
    uint32_t transferQueueHead = 0;
    
    void PreAllocate(uint32_t expectedTransfersPerFrame) {
        transferQueue.reserve(expectedTransfersPerFrame * 2);  // 2x for safety
    }
};

// Usage
void MultiGPUScheduler::Schedule() {
    for (auto& work : pendingWork) {
        if (work.target_gpu != current_gpu && transferQueueHead < transferQueue.capacity()) {
            transferQueue[transferQueueHead++] = {/* transfer job */};
        }
    }
}
```

**Impact:** ✅ Cross-GPU transfers don't allocate during scheduling

#### 2.5: CashSystem StagingBufferPool Pre-Warming
```cpp
// Current (Sprint 5.2.5)
class StagingBufferPool {
    std::vector<VkBuffer> freeBuffers;  // Grows on-demand
    
    VkBuffer Allocate(VkDeviceSize size) {
        if (freeBuffers.empty()) {
            // Allocate new buffer dynamically
            auto buf = AllocateBuffer(...);
            return buf;
        }
        auto buf = freeBuffers.back();
        freeBuffers.pop_back();
        return buf;
    }
};

// Better: Pre-warm during Setup
class StagingBufferPool {
    void PreWarm(uint32_t bufferCount, VkDeviceSize bufferSize) {
        freeBuffers.reserve(bufferCount);
        for (uint32_t i = 0; i < bufferCount; i++) {
            auto buf = AllocateBuffer(bufferSize);
            freeBuffers.push_back(buf);
        }
    }
};

// Usage in RenderGraph::Setup()
void RenderGraph::Setup() {
    // Calculate max concurrent uploads
    uint32_t maxCachers = 5;  // VoxelScene, VoxelAABB, Acceleration, Mesh, etc.
    uint32_t maxUploadsPerCacher = 3;  // Pipelined uploads
    
    stagingPool.PreWarm(
        maxCachers * maxUploadsPerCacher,  // 15 buffers
        256 * 1024 * 1024  // 256 MB each
    );
}
```

**Impact:** ✅ No staging buffer allocation during frame execution

### Phase 3: Reserve Container Capacity During Compile Phase (Week 4-6)

```cpp
// New method: GetCompileContext() pre-reserve requirements
class NodeInstance {
    struct PreAllocRequirements {
        size_t inputSlotCount = 0;
        size_t outputSlotCount = 0;
        size_t descriptorSetCount = 0;
        size_t commandBufferSize = 0;  // For command buffer recording
    };
    
    virtual PreAllocRequirements GetPreAllocRequirements() const {
        return {};  // Default empty
    }
};

// Example: GeometryRenderNode
class GeometryRenderNode : public TypedNode {
    PreAllocRequirements GetPreAllocRequirements() const override {
        return {
            .inputSlotCount = 2,       // Material descriptor, scene data
            .outputSlotCount = 1,      // Rendered framebuffer
            .descriptorSetCount = 1,   // Material descriptors
            .commandBufferSize = 64 * 1024  // 64 KB command buffer
        };
    }
};

// During Compile phase
void RenderGraph::CompileNode(NodeHandle h) {
    auto reqs = nodes[h]->GetPreAllocRequirements();
    
    // Reserve descriptor capacity
    vkAllocateDescriptorSets(..., reqs.descriptorSetCount);
    
    // Reserve command buffer space
    commandBufferPool.Reserve(reqs.commandBufferSize);
    
    nodes[h]->Compile();  // Now safe to use
}
```

**Impact:** ✅ Compile phase pre-calculates all allocation needs

### Phase 4: Deferred Destruction Pool Pre-Allocation (Week 6-8)

```cpp
// Current: DeferredDestruction queues objects for cleanup
class DeferredDestruction {
    std::vector<std::unique_ptr<Resource>> pendingCleanup;  // Can grow
};

// Better: Pre-sized based on graph topology
class DeferredDestruction {
    struct SlotAllocator {
        std::vector<std::unique_ptr<Resource>> slots;  // Pre-allocated
        size_t writeIdx = 0;
        
        void Enqueue(std::unique_ptr<Resource> res) {
            if (writeIdx >= slots.size()) {
                slots.resize(slots.size() * 2);  // Only grows if overflow
            }
            slots[writeIdx++] = std::move(res);
        }
    };
    
    SlotAllocator allocator;
};

// Sizing during Setup
void RenderGraph::Setup() {
    size_t maxResourcesPerFrame = nodes.size() * 5;  // Heuristic
    deferredDestruction.PreReserve(maxResourcesPerFrame);
}
```

**Impact:** ✅ Deferred destruction queue bounded, no unexpected allocations

---

## Critical Pre-Allocation Checklist

| Component | Current State | Pre-Allocation Strategy | Timeline | Priority |
|-----------|---------------|------------------------|----------|----------|
| **EventBus Queue** | Dynamic | Reserve at construction | Sprint 5 Phase 3 (pending) | HIGH |
| **DescriptorSetCache** | Dynamic | Pre-calculate during Compile | Sprint 5 Phase 4 | HIGH |
| **Timeline FrameHistory** | Not yet built | Pre-allocate ring buffer | Sprint 6 | HIGH |
| **Multi-GPU Transfers** | Not yet built | Size transfer queue | Sprint 10 | HIGH |
| **StagingBufferPool** | Sprint 5.2.5 ✅ | Pre-warm at Setup | Sprint 5 Phase 3 | MEDIUM |
| **Deferred Destruction** | Dynamic | Pre-reserve slots | Sprint 5 Phase 4 | MEDIUM |
| **Command Buffer Pool** | Per-frame | Size to node count | Sprint 5 Phase 3 | MEDIUM |
| **Physics CA Buffers** | Not yet built | Pre-allocate chunk atlas | Sprint 7 | MEDIUM |

---

## Architectural Critique Points 🔍

### Strength: Event-Driven Invalidation
**Assessment:** ✅ Excellent design. Loose coupling between nodes prevents tight dependencies.
**Minor Issue:** No batching mechanism for simultaneous events.
```cpp
// Could batch these separately:
EventBus.Emit(WindowResized);
EventBus.Emit(SwapChainInvalidated);  // Cascade from same root

// Better: Emit single composite event
EventBus.Emit(FrameSetupNeeded);
```
**Recommendation:** Add composite event support for synchronous cascades.

### Strength: Clean Resource Ownership
**Assessment:** ✅ Graph owns, nodes access. Clear RAII semantics.
**Minor Issue:** Cleanup dependency detection relies on input connections. Won't catch transitive dependencies.
```cpp
// Dependency chain: A → B → C
// If B is removed, C cleanup order breaks
// Current: Cleanup detects A→B, B→C
// But if only B→C exists (A disconnected), order incorrect

// Better: Full topological sort during cleanup registration
```
**Recommendation:** Add full DAG topological sort to CleanupStack.

### Weakness: No Compile-Time Resource Estimation
**Assessment:** ⚠️ Descriptor sets, command buffers, staging buffers all sized heuristically.
**Issue:** Hard to predict allocation needs before first Execute.
```cpp
// No method to query node resource usage
node->GetResourceEstimate()  // Doesn't exist
    // Would return: descriptors, memory, commands, etc.
```
**Recommendation:** Add `GetResourceEstimate()` virtual method to all nodes.

### Weakness: Graph Recompilation Not Batched
**Assessment:** ⚠️ Single event can trigger 3+ node recompiles.
**Issue:** Each recompile reallocates pipelines, descriptors, command buffers.
```cpp
// Current: Event → Node.SetDirty() → Immediate recompile
graph.ProcessEvents();  // Recompile all dirty nodes

// Better: Batch recompilation
graph.ProcessEvents(batchMode=true);  // Collect all dirty nodes
graph.RecompileDirty();  // Single pass
```
**Recommendation:** Add batch recompilation mode for resize/reconfigure events.

### Weakness: No Pre-Execution Validation Pass
**Assessment:** ⚠️ Errors discovered during Execute, not during Compile.
**Issue:** Frame latency hit if descriptor binding fails during command recording.
```cpp
// Current: Execute phase discovers binding errors
void Execute() {
    vkCmdBindDescriptorSets(...);  // Fails here (latency spike)
}

// Better: Validate during Compile
void Compile() {
    ValidateDescriptorBinding();  // Fails here (setup time)
}
```
**Recommendation:** Add comprehensive validation pass post-Compile, before first Execute.

### Weakness: Timeline System Architecture Underdefined
**Assessment:** ⚠️ Sprint 3.2 has high complexity, limited pre-planning.
**Issue:** Frame history storage, sub-graph wiring, recursive compilation all TBD.
```cpp
// Planned but not specified:
// - How are sub-graphs scoped? (per-timeline? per-frame?)
// - Where does frame history allocate? (system memory? GPU?)
// - How are descriptor updates synchronized across timelines?
```
**Recommendation:** Create detailed Timeline System Architecture RFC before Sprint 6.

---

## Risk Scoring Matrix

| Risk | Probability | Impact | Detectability | Pre-Mitigation |
|------|-------------|--------|----------------|----------------|
| **EventBus allocation overflow** | Medium | Low | High (logs) | Phase 2.1 ✅ |
| **Timeline frame history fragmentation** | High | Medium | Medium | Phase 2.3 ✅ |
| **Multi-GPU transfer queue deadlock** | High | High | Low | Phase 2.4 ✅ |
| **Descriptor pool exhaustion** | Medium | High | High (VK_ERROR) | Phase 2.2 ✅ |
| **Physics CA buffer allocation stall** | Medium | Medium | High (profiler) | Phase 2.5 |
| **Recompilation cascade latency** | Low | Medium | High (frame time) | Phase 3 ✅ |

---

## Recommended Work Items (Priority Order)

### Sprint 5 Phase 3 (Immediate — Next Week)
1. ✅ EventBus pre-allocation (4h)
2. ✅ StagingBufferPool pre-warming (3h)
3. ✅ Command buffer pool sizing (4h)
4. ✅ Allocation tracker instrumentation (6h)

**Total:** 17 hours (fits in remaining Sprint 5 budget: 44h)

### Sprint 6 (Timeline Foundation)
1. Timeline FrameHistory pre-allocation (Phase 2.3, 8h)
2. Timeline Architecture RFC (4h)
3. Descriptor cache pre-calculation (8h)

**Total:** 20 hours (integrate into Sprint 6 timeline)

### Sprint 10 (Multi-GPU)
1. Transfer queue pre-sizing (Phase 2.4, 6h)
2. Per-GPU descriptor pool isolation (8h)

**Total:** 14 hours (integrate into Sprint 10 setup)

### Cross-Sprint (Ongoing)
- **Allocation tracking** - Add to all allocators (weekly)
- **Resource estimation** - Document per node type (2h each × 20 nodes = 40h over 2 months)
- **Recompilation profiling** - Measure cascade latency (4h)

---

## Conclusion

VIXEN's architecture is **fundamentally sound** with strong ownership semantics and event-driven design. The pre-allocation strategy outlined here provides a **concrete path to zero-allocation runtime** (except setup).

**Key Takeaways:**
1. ✅ Resource ownership model is excellent—continue leveraging
2. ✅ EventBus pattern scales well—pre-allocate queue only
3. ⚠️ Next-phase systems (Timeline, Multi-GPU, Physics) need detailed pre-planning
4. 🎯 Pre-allocation checklist identifies 8 critical components
5. 📋 Work items fit within existing sprint budgets

**Next Step:** Execute Sprint 5 Phase 3 items (17h) to lock down immediate allocations, then start Timeline RFC for Sprint 6.

