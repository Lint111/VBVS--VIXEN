# Architectural Phases Implementation Checkpoint

**Project**: VIXEN RenderGraph Architecture Overhaul
**Started**: October 31, 2025
**Updated**: November 1, 2025 (Critical architecture review - Phase 0 added)
**Status**: Phase 0 REQUIRED - Critical correctness issues identified
**Total Scope**: 90-130 hours across 8 phases (0, A-G)

---

## CRITICAL UPDATE: Phase 0 Required Before All Other Work

**Comprehensive architectural review (November 1, 2025) identified THREE P0 correctness bugs**:
1. 🔴 **Per-frame resource management missing** - UBO updates have race conditions
2. 🔴 **Frame-in-flight synchronization missing** - no CPU-GPU fences
3. 🔴 **Command buffer recording strategy undefined** - unclear when buffers are recorded

**Plus TWO P0 fundamental architecture gaps**:
1. 🔴 **Multi-rate update loop missing** - blocks gameplay features
2. 🔴 **Template method pattern missing** - repetitive boilerplate across 15+ nodes

**Decision**: **HALT Phase A (Persistent Cache)** until Phase 0 complete. Cache correctness depends on proper frame synchronization.

---

## Implementation Path (REVISED)

**New Path**: **0 → A → B → C → F → D → E → G**

This path now prioritizes:
1. **Correctness** (0): Fix P0 bugs in execution model
2. **Foundation** (A-C): Cache, encapsulation, API stability
3. **Performance** (F): Data-parallel workloads
4. **Scalability** (D): Execution waves for 500+ nodes
5. **Polish** (E): Hot reload
6. **Tooling** (G): Visual editor

---

## Phase Status Overview

| Phase | Priority | Time Est | Status | Completion |
|-------|----------|----------|--------|------------|
| **0: Execution Model Correctness** | 🔴 CRITICAL | 6-10 days | ⏳ PENDING | 0% |
| **A: Persistent Cache** | ⭐⭐⭐ HIGH | 5-8h | ⏸️ PAUSED | 50% (2.5h done) |
| **B: Encapsulation + Thread Safety** | ⭐⭐⭐ HIGH | 5-7h | ⏳ PENDING | 0% |
| **C: Event Processing + Validation** | ⭐⭐⭐ HIGH | 2-3h | ⏳ PENDING | 0% |
| **F: Array Processing** | ⭐⭐⭐ HIGH | 10-14h | ⏳ PENDING | 0% |
| **D: Execution Waves** | ⭐⭐ MEDIUM | 8-12h | ⏳ PENDING | 0% |
| **E: Hot Reload** | ⭐ LOW | 17-22h | ⏳ PENDING | 0% |
| **G: Visual Editor** | ⭐⭐ MED-HIGH | 40-60h | ⏳ PENDING | 0% |

**Total Progress**: 2.5 hours / 90-130 hours (2%)

---

## Phase 0: Execution Model Correctness 🔴

**Priority**: 🔴 CRITICAL (Blocks all other work - correctness bugs)
**Time Estimate**: 6-10 days (48-80 hours)
**Status**: ⏳ NOT STARTED
**Prerequisites**: None - must be done FIRST

### Goal

Fix critical correctness bugs in execution model before they're hidden under layers of complexity. All identified issues have well-known solutions used by Unity, Unreal, and Frostbite.

### Critical Issues Identified

**Correctness Bugs** (cause validation errors, crashes, flickering):
1. Per-frame resource management missing (UBO race conditions)
2. Frame-in-flight synchronization missing (CPU-GPU sync)
3. Command buffer recording strategy undefined (static vs dynamic)

**Fundamental Gaps** (block gameplay features):
4. Multi-rate update loop missing (physics, logic, render separation)
5. Template method pattern missing (boilerplate across 15+ nodes)

### Implementation Plan

#### **0.1: Per-Frame Resource Pattern** (2-3 days)

**Problem**:
```cpp
// DescriptorSetNode::Execute() - BUGGY: Single UBO for all frames
void DescriptorSetNode::Execute() {
    Draw_Shader::BufferVals ubo;
    ubo.mvp = projection * view * model;
    memcpy(uniformBufferMemory, &ubo, sizeof(ubo));  // ❌ Race condition!
}
```

**Issue**: GPU reading frame N's UBO while CPU overwrites it for frame N+1.

**Solution**:
```cpp
// Per-frame resources pattern
struct PerFrameData {
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformMemory;
    void* mappedMemory;
    VkDescriptorSet descriptorSet;
};

class DescriptorSetNode {
    std::vector<PerFrameData> perFrameData;  // One per swapchain image

    void Compile() {
        uint32_t imageCount = swapchainInfo->swapChainImageCount;
        perFrameData.resize(imageCount);

        for (auto& frame : perFrameData) {
            // Allocate per-frame uniform buffer
            CreateBuffer(device, sizeof(UBO), &frame.uniformBuffer, &frame.uniformMemory);
            vkMapMemory(device, frame.uniformMemory, 0, sizeof(UBO), 0, &frame.mappedMemory);
        }
    }

    void Execute() {
        uint32_t imageIndex = In(IMAGE_INDEX);
        auto& frame = perFrameData[imageIndex];  // ✅ Use correct frame buffer
        memcpy(frame.mappedMemory, &ubo, sizeof(ubo));
    }

    void Cleanup() {
        for (auto& frame : perFrameData) {
            vkDestroyBuffer(device, frame.uniformBuffer, nullptr);
            vkFreeMemory(device, frame.uniformMemory, nullptr);
        }
    }
};
```

**Tasks**:
1. Create `PerFrameResources` helper class (1 day)
2. Refactor `DescriptorSetNode` to use per-frame UBOs (0.5 days)
3. Refactor `GeometryRenderNode` to use per-frame command buffers (0.5 days)
4. Document per-frame resource ownership rules (0.5 days)

**Files to Modify**:
- NEW: `RenderGraph/include/Core/PerFrameResources.h`
- EDIT: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`
- EDIT: `RenderGraph/src/Nodes/GeometryRenderNode.cpp`

**Success Criteria**:
- [ ] PerFrameResources abstraction created
- [ ] DescriptorSetNode uses per-frame UBOs
- [ ] GeometryRenderNode uses per-frame command buffers
- [ ] Zero validation errors under stress test (1000+ frames)

---

#### **0.2: Frame-in-Flight Synchronization** (1-2 days)

**Problem**:
```cpp
// VulkanGraphApplication::Render() - BUGGY: No frame throttling
bool Render() {
    renderGraph->RenderFrame();  // ❌ No wait for GPU
    currentFrame++;              // ❌ Infinite acceleration
    return true;
}
```

**Issue**: CPU can submit frames infinitely faster than GPU can consume.

**Solution**:
```cpp
const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameSyncData {
    VkFence inFlightFence;
    VkSemaphore imageAvailable;
    VkSemaphore renderComplete;
};

class RenderGraph {
    std::vector<FrameSyncData> frameSyncData;  // Size = MAX_FRAMES_IN_FLIGHT
    uint32_t currentFrameIndex = 0;

    void RenderFrame() {
        auto& sync = frameSyncData[currentFrameIndex];

        // ✅ Wait for GPU to finish frame N-2
        vkWaitForFences(device, 1, &sync.inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &sync.inFlightFence);

        // Acquire image
        vkAcquireNextImageKHR(..., sync.imageAvailable, ...);

        // Submit with fence
        VkSubmitInfo submitInfo = {
            .pWaitSemaphores = &sync.imageAvailable,
            .pSignalSemaphores = &sync.renderComplete,
        };
        vkQueueSubmit(..., sync.inFlightFence);  // ✅ Signal fence

        // Present
        vkQueuePresentKHR(..., &sync.renderComplete);

        currentFrameIndex = (currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }
};
```

**Tasks**:
1. Add `FrameSyncData` struct to RenderGraph (0.5 days)
2. Implement fence-based waiting in RenderFrame() (0.5 days)
3. Update SwapChainNode to use per-flight semaphores (0.5 days)
4. Test under high load (verify no GPU stalls) (0.5 days)

**Files to Modify**:
- EDIT: `RenderGraph/include/Core/RenderGraph.h`
- EDIT: `RenderGraph/src/Core/RenderGraph.cpp`
- EDIT: `RenderGraph/src/Nodes/SwapChainNode.cpp`

**Success Criteria**:
- [ ] MAX_FRAMES_IN_FLIGHT = 2 enforced
- [ ] Fence-based CPU-GPU sync working
- [ ] Per-flight semaphores created
- [ ] CPU never runs more than 2 frames ahead of GPU

---

#### **0.3: Command Buffer Recording Strategy** (1 day)

**Problem**: Unclear when/how command buffers are recorded (once? every frame? when dirty?).

**Solution**:
```cpp
enum class CommandBufferStrategy {
    Static,       // Record once in Compile(), reuse
    Dynamic,      // Re-record every Execute()
    Conditional   // Re-record when dirty flag set
};

class NodeInstance {
    virtual CommandBufferStrategy GetRecordingStrategy() const {
        return CommandBufferStrategy::Conditional;  // Default
    }

    void Execute(VkCommandBuffer cmd) final {
        auto strategy = GetRecordingStrategy();

        if (strategy == CommandBufferStrategy::Dynamic ||
            (strategy == CommandBufferStrategy::Conditional && isDirty)) {
            RecordCommandBuffer(cmd);
            isDirty = false;
        } else if (strategy == CommandBufferStrategy::Static) {
            // Use pre-recorded command buffer
        }

        ExecuteImpl(cmd);
    }

protected:
    virtual void RecordCommandBuffer(VkCommandBuffer cmd) {}
};
```

**Tasks**:
1. Add `CommandBufferStrategy` enum (0.25 days)
2. Implement conditional recording in NodeInstance (0.5 days)
3. Document strategy per node type (0.25 days)

**Files to Modify**:
- EDIT: `RenderGraph/include/Core/NodeInstance.h`
- EDIT: `RenderGraph/src/Core/NodeInstance.cpp`
- EDIT: `documentation/CommandBufferStrategies.md` (NEW)

**Success Criteria**:
- [ ] CommandBufferStrategy enum defined
- [ ] Conditional recording implemented
- [ ] All node types document their strategy

---

#### **0.4: Multi-Rate Update Loop (LoopManager)** (2-3 days)

**Problem**: Single execution loop, no support for multi-rate updates (physics at 90Hz, render at vsync).

**Solution**:
```cpp
class LoopManager {
public:
    enum class UpdateRate {
        Asap,           // Uncapped (logic updates)
        AsapVsync,      // Vsync-capped (rendering)
        Fixed60Hz,      // 16.67ms
        Fixed90Hz,      // 11.11ms (physics)
        Fixed120Hz,     // 8.33ms (simulation)
    };

    void RegisterLoop(std::string name, UpdateRate rate,
                     std::function<void(float)> callback,
                     uint32_t priority);

    void Tick(float realDelta);  // Main frame tick

private:
    struct Loop {
        std::string name;
        std::function<void(float)> callback;
        UpdateRate rate;
        float accumulator = 0.0f;
        uint32_t priority = 0;
    };
    std::vector<Loop> loops;
};

// Fixed-timestep accumulator
void LoopManager::Tick(float realDelta) {
    for (auto& loop : loops) {
        if (loop.rate.isFixed) {
            loop.accumulator += realDelta;
            while (loop.accumulator >= loop.rate.targetDelta) {
                loop.callback(loop.rate.targetDelta);  // Fixed delta
                loop.accumulator -= loop.rate.targetDelta;
            }
        } else {
            loop.callback(realDelta);  // Variable delta
        }
    }
}
```

**Integration with RenderGraph**:
```cpp
class RenderGraph {
    LoopManager loopManager;

    void InitializeLoops() {
        loopManager.RegisterLoop("Input", UpdateRate::Asap,
            [this](float dt) { ProcessInput(dt); }, 0);

        loopManager.RegisterLoop("Physics", UpdateRate::Fixed90Hz,
            [this](float dt) { UpdatePhysics(dt); }, 100);

        loopManager.RegisterLoop("Update", UpdateRate::Asap,
            [this](float dt) { UpdateNodes(dt); }, 300);

        loopManager.RegisterLoop("Render", UpdateRate::AsapVsync,
            [this](float dt) { RenderFrame(); }, 400);
    }

    void RunFrame() {
        float deltaTime = time.GetDeltaTime();
        loopManager.Tick(deltaTime);
    }
};
```

**Node Update Support**:
```cpp
class NodeInstance {
public:
    void Update(float deltaTime) final {
        UpdateImpl(deltaTime);
    }

protected:
    virtual void UpdateImpl(float deltaTime) {}  // Override in subclasses
};
```

**Tasks**:
1. Implement LoopManager class (1 day)
2. Add Update(float) to NodeInstance (0.5 days)
3. Integrate with RenderGraph::RunFrame() (0.5 days)
4. Update VulkanGraphApplication to use RunFrame() (0.5 days)
5. Test multi-rate loops (physics at 90Hz, render at 60Hz) (0.5 days)

**Files to Modify**:
- NEW: `RenderGraph/include/Core/LoopManager.h`
- NEW: `RenderGraph/src/Core/LoopManager.cpp`
- EDIT: `RenderGraph/include/Core/NodeInstance.h`
- EDIT: `RenderGraph/include/Core/RenderGraph.h`
- EDIT: `RenderGraph/src/Core/RenderGraph.cpp`
- EDIT: `source/VulkanGraphApplication.cpp`

**Success Criteria**:
- [ ] LoopManager implemented
- [ ] Fixed-timestep accumulator working
- [ ] Priority-based execution order
- [ ] NodeInstance::UpdateImpl() available
- [ ] VulkanGraphApplication::RunFrame() simplifies main loop

---

#### **0.5: Template Method Pattern Refactor** (1-2 days)

**Problem**: Every node manually calls boilerplate (RegisterCleanup, etc.). Easy to forget.

**Solution**:
```cpp
class NodeInstance {
public:
    // Public interface (final - cannot override)
    void Setup() final {
        LOG_DEBUG("Setting up: " + instanceName);
        SetupImpl();
        SubscribeToEvents();  // ✅ Automatic
    }

    void Compile() final {
        LOG_DEBUG("Compiling: " + instanceName);
        CompileImpl();
        RegisterCleanup();  // ✅ Automatic
    }

    void Execute(VkCommandBuffer cmd) final {
        PROFILE_SCOPE(instanceName);
        ExecuteImpl(cmd);
    }

    void Cleanup() final {
        LOG_DEBUG("Cleaning up: " + instanceName);
        CleanupImpl();
        UnregisterFromCleanupStack();  // ✅ Automatic
        ResetCleanupFlag();  // ✅ Automatic
    }

    void Update(float deltaTime) final {
        UpdateImpl(deltaTime);
    }

protected:
    // Implementation interface (override in subclasses)
    virtual void SetupImpl() {}
    virtual void CompileImpl() = 0;  // Pure virtual - must implement
    virtual void ExecuteImpl(VkCommandBuffer cmd) {}
    virtual void CleanupImpl() {}
    virtual void UpdateImpl(float deltaTime) {}
};
```

**Migration Example**:
```cpp
// Before
void MyNode::Compile() override {
    // do work
    RegisterCleanup();  // ❌ Manual
}

// After
void MyNode::CompileImpl() override {
    // do work
    // RegisterCleanup() automatic!
}
```

**Tasks**:
1. Add *Impl() virtual methods to NodeInstance (0.5 days)
2. Migrate all 15+ nodes to use *Impl() pattern (1 day)
3. Remove manual RegisterCleanup() calls (0.5 days)

**Files to Modify**:
- EDIT: `RenderGraph/include/Core/NodeInstance.h`
- EDIT: `RenderGraph/src/Core/NodeInstance.cpp`
- EDIT: All 15+ node implementations (*.cpp in RenderGraph/src/Nodes/)

**Success Criteria**:
- [ ] All lifecycle methods use template pattern
- [ ] All nodes migrated to *Impl()
- [ ] Zero manual RegisterCleanup() calls
- [ ] Impossible to forget cleanup registration

---

### Phase 0 Summary

**Estimated Time**: 6-10 days (48-80 hours)

**Breakdown**:
- 0.1: Per-Frame Resources (2-3 days)
- 0.2: Frame-in-Flight Sync (1-2 days)
- 0.3: Command Buffer Strategy (1 day)
- 0.4: Multi-Rate Update Loop (2-3 days)
- 0.5: Template Method Refactor (1-2 days)

**Impact**: Fixes all P0 correctness bugs, unblocks gameplay features, eliminates boilerplate.

**Next**: Resume Phase A (Persistent Cache) after Phase 0 complete.

---

## Phase A: Persistent Cache Infrastructure (PAUSED)

**Priority**: ⭐⭐⭐ HIGH (Critical for production)
**Time Estimate**: 5-8 hours
**Status**: ⏸️ PAUSED at 50% (2.5h done)
**Prerequisites**: **Phase 0 complete** (cache correctness depends on frame-in-flight sync)

### Pause Reason

Persistent cache correctness depends on proper frame synchronization. If cache is saved while GPU is still using resources (because no fence-based sync), corruption can occur.

**Resume after Phase 0.2 (Frame-in-Flight Sync) complete.**

### Completed Work ✅

**1. ShaderModuleCacher Serialization** ✅ (2.5h)
- Binary cache with versioning
- SPIR-V bytecode serialization
- Automatic VkShaderModule recreation on load
- Thread-safe insertion

**Files**:
- `CashSystem/src/shader_module_cacher.cpp:336-543`

### Remaining Work ⏳

**2. PipelineCacher Serialization** (~1h)
- Use Vulkan's `vkGetPipelineCacheData()` / `vkCreatePipelineCache()`

**3. Application Integration** (~0.5h)
- Hook `SaveAll()` / `LoadAll()` into VulkanGraphApplication

**4. Testing** (~1h)
- Cold start, warm start, performance comparison

**Resume Criteria**: Phase 0.2 complete (frame-in-flight fences working).

---

## Phase B: Encapsulation + Thread Safety

**Priority**: ⭐⭐⭐ HIGH (Architectural debt + threading support)
**Time Estimate**: 5-7 hours (expanded from 3-4h to include thread safety)
**Status**: ⏳ PENDING
**Prerequisites**: Phase 0 complete

### Original Goal (INodeWiring)

Replace `friend class RenderGraph` with narrow `INodeWiring` interface. Fixes encapsulation violation.

### NEW: Thread Safety Documentation/Implementation

**Option 1: Document "Not Thread-Safe"** (1 hour)
```cpp
/**
 * @brief RenderGraph execution model
 *
 * THREAD SAFETY: RenderGraph is **not thread-safe**.
 * - All methods must be called from the same thread.
 * - LoopManager loops execute sequentially, not in parallel.
 */
class RenderGraph { ... };
```

**Option 2: Add Thread Safety** (2-3 hours)
```cpp
class RenderGraph {
    std::mutex graphMutex;

    void UpdateNodes(float dt) {
        std::lock_guard<std::mutex> lock(graphMutex);
        // ... update logic
    }

    void Execute(VkCommandBuffer cmd) {
        std::lock_guard<std::mutex> lock(graphMutex);
        // ... execute logic
    }
};
```

**Recommendation**: Start with Option 1 (documentation), add Option 2 if parallel loops needed later.

### Implementation Plan

**B.1: INodeWiring Interface** (1-2h)
- Create narrow interface for graph wiring
- Remove friend declarations

**B.2: Thread Safety Documentation** (1h)
- Document threading model explicitly
- Add thread safety notes to all public methods

**Total**: 5-7 hours (original 3-4h + 2-3h thread safety)

---

## Phase C: Event Processing + Validation

**Priority**: ⭐⭐⭐ HIGH (API usability + correctness)
**Time Estimate**: 2-3 hours (expanded from 1-2h to include validation)
**Status**: ⏳ PENDING
**Prerequisites**: Phase 0 complete

### Original Goal (Event Processing API)

Single `RenderFrame()` method that guarantees correct sequencing.

### NEW: Validation Systems

**C.1: Explicit Event Processing API** (1h)
- Already exists: `RenderGraph::RenderFrame()`
- Ensure it calls: ProcessEvents() → RecompileDirtyNodes() → Execute() → Present()

**C.2: Slot Lifetime Validation** (1h)
```cpp
enum class SlotLifetime {
    Static,   // Set once in Compile(), never changes
    Dynamic,  // Changes in Execute()
    Mutable   // Can change, triggers recompilation
};

void RenderGraph::ConnectNodes(...) {
    if (fromLifetime == Dynamic && toLifetime == Static) {
        throw std::runtime_error("Cannot connect dynamic to static!");
    }
}
```

**C.3: Render Pass Compatibility Validation** (1h)
```cpp
void RenderGraph::Validate() {
    // Check: Pipeline's render pass matches framebuffer's render pass
    if (!AreRenderPassesCompatible(pipelineRP, framebufferRP)) {
        throw ValidationError("Render pass mismatch!");
    }
}
```

**Total**: 2-3 hours

---

## Phases D-G: Continue As Planned

**Phase D**: Execution Waves (8-12h)
**Phase E**: Hot Reload (17-22h)
**Phase F**: Array Processing (10-14h)
**Phase G**: Visual Editor (40-60h)

**No changes to these phases** - proceed as originally planned after Phases 0-C complete.

---

## Key Decisions Made

**November 1, 2025**:
1. ✅ **Phase 0 added as CRITICAL prerequisite** - must fix correctness bugs first
2. ✅ **Phase A paused** - resume after Phase 0.2 (frame-in-flight sync)
3. ✅ **Phase B expanded** - added thread safety documentation/implementation
4. ✅ **Phase C expanded** - added validation systems
5. ✅ **Revised timeline** - 90-130 hours total (was 74-108h)

**October 31, 2025**:
1. Complete Architecture Path selected: 0 → A → B → C → F → D → E → G
2. ShaderModuleCacher format: Binary with versioning
3. PipelineCacher strategy: Vulkan's built-in serialization
4. Cache directory: `binaries/cache/`

---

## Next Session Checklist

**Phase 0.1: Per-Frame Resources** (Start Here)
- [ ] Create `PerFrameResources` helper class
- [ ] Refactor DescriptorSetNode for per-frame UBOs
- [ ] Refactor GeometryRenderNode for per-frame command buffers
- [ ] Document per-frame ownership rules

**Then Phase 0.2: Frame-in-Flight Sync**
- [ ] Add `FrameSyncData` to RenderGraph
- [ ] Implement fence-based waiting
- [ ] Update SwapChainNode for per-flight semaphores
- [ ] Test under high load

**Continue with 0.3-0.5 before resuming Phase A**

---

## Reference Documents

- **NEW: Architectural Review 2025-11-01**: `documentation/ArchitecturalReview-2025-11-01.md` - Comprehensive blind spot analysis
- **Active Context**: `memory-bank/activeContext.md` - Detailed phase architectures
- **Progress**: `memory-bank/progress.md` - Overall project status
- **System Patterns**: `memory-bank/systemPatterns.md` - Design patterns

---

## Notes

**CRITICAL**: Phase 0 is **not optional**. These are correctness bugs that will cause:
- Validation errors under load
- Flickering/artifacts (UBO race conditions)
- GPU stalls (no frame-in-flight limiting)
- Inability to add gameplay features (no multi-rate updates)

**All issues have proven solutions** from Unity, Unreal, Frostbite. Implementation is straightforward but must be done **before** adding complexity.

**Estimated Phase 0 Completion**: +6-10 days from start
**Estimated Full Architecture Completion**: +87.5-127.5 hours from Phase 0 start