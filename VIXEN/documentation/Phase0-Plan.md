# Phase 0: Production-Ready Foundation

**Goal**: Fix critical production blockers before feature development

**Status**: 2/5 complete (Phase 0.1-0.2 done)

---

## ✅ Phase 0.1: Per-Frame Resources (COMPLETE)

**Problem**: CPU writing to UBO while GPU reading it → race condition
**Solution**: PerFrameResources ring buffer pattern (3 UBO copies, one per swapchain image)

**Implemented**:
- `RenderGraph/include/Core/PerFrameResources.h` - Ring buffer helper
- DescriptorSetNode uses ring buffer for per-frame UBOs
- Wired IMAGE_INDEX connections throughout graph
- Verified 3 distinct UBO buffers created

**Result**: CPU writes frame N while GPU reads frame N-1 ✅

---

## ✅ Phase 0.2: Frame-in-Flight Synchronization (COMPLETE)

**Problem**: CPU can race infinitely ahead of GPU → memory exhaustion, stuttering
**Solution**: MAX_FRAMES_IN_FLIGHT=2 pattern with fences

**Implemented**:
- FrameSyncNode managing 2 fences + 2 semaphore pairs (per-flight pattern)
- GeometryRenderNode waits on fence before recording, signals on submit
- SwapChainNode uses per-flight semaphores (not per-image)
- PresentNode waits on GeometryRenderNode output

**Result**: CPU limited to 2 frames ahead of GPU, continuous rendering ✅

---

## 🔴 Phase 0.3: Command Buffer Recording Strategy (NEXT)

**Problem**: Unclear when command buffers should be re-recorded

**Current Behavior**:
- GeometryRenderNode re-records command buffers **every frame** (line 136 in GeometryRenderNode.cpp)
- This works but is suboptimal for static scenes

**Goal**: Define clear recording strategy with dirty tracking

### Option 1: Always Re-Record (Current)
```cpp
void GeometryRenderNode::Execute(VkCommandBuffer cmd) {
    RecordDrawCommands(cmdBuffer, imageIndex);  // Every frame
    vkQueueSubmit(...);
}
```

**Pros**: Simple, always up-to-date
**Cons**: Unnecessary CPU work for static scenes
**Use Case**: Dynamic scenes, animated UBOs

### Option 2: Record Once + Dirty Tracking
```cpp
class GeometryRenderNode {
    bool commandBuffersDirty = true;

    void Execute(VkCommandBuffer cmd) {
        if (commandBuffersDirty) {
            RecordDrawCommands(cmdBuffer, imageIndex);
            commandBuffersDirty = false;
        }
        vkQueueSubmit(...);
    }

    void MarkDirty() { commandBuffersDirty = true; }
};
```

**Pros**: Saves CPU work for static scenes
**Cons**: Requires dirty tracking, must invalidate on parameter changes
**Use Case**: Static scenes, baked command buffers

### Option 3: Hybrid (Recommended)
```cpp
enum class RecordingMode {
    STATIC,   // Record once, re-record on dirty
    DYNAMIC   // Re-record every frame
};

class GeometryRenderNode {
    RecordingMode recordingMode = RecordingMode::DYNAMIC;  // Default safe
```

**Pros**: Flexibility, performance when needed
**Cons**: More complexity
**Use Case**: Mix of static and dynamic nodes

### Implementation Plan

**Tasks**:
1. Add `recordingMode` parameter to GeometryRenderNodeConfig (1 hour)
2. Add dirty tracking to GeometryRenderNode (1 hour)
3. Implement MarkDirty() when parameters change (2 hours)
4. Test both modes (static scene + dynamic UBO updates) (1 hour)
5. Document recording strategy guidelines (1 hour)

**Total Estimate**: 1 day (6 hours)

**Decision Point**: Do we need Option 3 now, or is Option 1 sufficient for MVP?

**Recommendation**: **Keep Option 1 (always re-record) for now**. Optimize in Phase 0.3.1 later if profiling shows command recording is a bottleneck.

---

## 🔴 Phase 0.4: Multi-Rate Update Loop

**Problem**: Physics tied to render rate → gameplay broken

**Current Behavior**:
```cpp
while (!shouldClose) {
    ProcessInput();        // Tied to frame rate
    UpdatePhysics();       // 🔴 WRONG: Variable timestep
    RenderFrame();
}
```

**Goal**: Decouple physics (fixed 60Hz) from rendering (variable)

### Recommended Pattern: Fixed Timestep with Accumulator
```cpp
const double PHYSICS_DT = 1.0 / 60.0;  // 60 FPS physics
double physicsAccumulator = 0.0;

while (!shouldClose) {
    double frameTime = timer.GetDeltaTime();
    physicsAccumulator += frameTime;

    // Fixed timestep physics updates
    while (physicsAccumulator >= PHYSICS_DT) {
        UpdatePhysics(PHYSICS_DT);  // Always 1/60 second steps
        physicsAccumulator -= PHYSICS_DT;
    }

    // Variable timestep rendering
    RenderFrame();  // Can be 30, 60, 144, 240 FPS
}
```

### Implementation Plan

**Tasks**:
1. Add Timer class (high-resolution clock) (1 hour)
2. Implement fixed timestep accumulator in main loop (2 hours)
3. Separate Update() into UpdatePhysics() and UpdateLogic() (2 hours)
4. Add interpolation for rendering (smooth between physics steps) (3 hours)
5. Test at different frame rates (60, 144, 30 FPS) (2 hours)

**Total Estimate**: 2-3 days (10 hours)

**Why Critical**: Required for any gameplay features (movement, collision, animations)

---

## 🔴 Phase 0.5: Template Method Pattern (Boilerplate Elimination)

**Problem**: Repetitive boilerplate across 15+ node types

**Current Pattern** (repeated in every node):
```cpp
class FooNode : public TypedNode<FooConfig> {
public:
    void Setup() override {
        // Get device
        vulkanDevice = In(FooConfig::VULKAN_DEVICE);
        if (!vulkanDevice) throw std::runtime_error("...");

        // Get other inputs...
    }

    void Compile() override {
        // Allocate resources...
        RegisterCleanup();  // Easy to forget!
    }

    void Execute(VkCommandBuffer cmd) override {
        // Do work...
    }

protected:
    void CleanupImpl() override {
        // Free resources...
    }
};
```

**Goal**: Template method pattern to eliminate common boilerplate

### Proposed Pattern
```cpp
template<typename ConfigType>
class BaseRenderNode : public TypedNode<ConfigType> {
public:
    void Setup() final {  // Non-virtual (can't override)
        // Common setup
        vulkanDevice = In(ConfigType::VULKAN_DEVICE);
        ValidateDevice();

        // Call derived class hook
        OnSetup();
    }

    void Compile() final {
        // Common compilation
        AllocateResources();

        // Call derived class hook
        OnCompile();

        // Automatic cleanup registration (can't forget!)
        RegisterCleanup();
    }

protected:
    // Hooks for derived classes (less boilerplate)
    virtual void OnSetup() {}
    virtual void OnCompile() = 0;
    virtual void OnExecute(VkCommandBuffer cmd) = 0;

    // Automatic resource tracking
    template<typename T>
    T RegisterResource(T resource) {
        trackedResources.push_back(resource);
        return resource;
    }
};
```

**Benefits**:
- Can't forget RegisterCleanup() (automatic)
- Less duplicated validation code
- Consistent error handling
- Easier to add cross-cutting concerns (profiling, logging)

### Implementation Plan

**Tasks**:
1. Design template method hierarchy (2 hours)
2. Create BaseRenderNode template (3 hours)
3. Refactor 3-4 nodes as pilot (GeometryRenderNode, SwapChainNode) (4 hours)
4. Verify no regressions (2 hours)
5. Document pattern in systemPatterns.md (1 hour)

**Total Estimate**: 1-2 days (12 hours)

**Decision Point**: Do we refactor all 15+ nodes now, or just establish pattern?

**Recommendation**: Pilot with 3-4 nodes, document pattern. Refactor others gradually.

---

## Phase 0 Summary

| Phase | Status | Time Estimate | Blocking? |
|-------|--------|---------------|-----------|
| 0.1 Per-Frame Resources | ✅ COMPLETE | - | YES (race conditions) |
| 0.2 Frame-in-Flight Sync | ✅ COMPLETE | - | YES (CPU-GPU race) |
| 0.3 Command Recording | 🔴 NEXT | 1 day | NO (optimization) |
| 0.4 Multi-Rate Loop | 🔴 TODO | 2-3 days | YES (gameplay) |
| 0.5 Template Method | 🔴 TODO | 1-2 days | NO (code quality) |

**Critical Path**: 0.1 → 0.2 → **0.4** (Phase 0.3 and 0.5 are optimizations)

**Recommendation**:
- **Phase 0.3**: Defer optimization, keep always-re-record for now
- **Phase 0.4**: Implement next (required for gameplay)
- **Phase 0.5**: Implement after 0.4 (quality of life)

---

## Next Session Plan

**Option A: Optimize Command Recording (Phase 0.3)**
- Add dirty tracking
- Implement STATIC/DYNAMIC recording modes
- Profile before/after

**Option B: Enable Gameplay (Phase 0.4 - RECOMMENDED)**
- Implement fixed timestep loop
- Decouple physics from rendering
- Test at different frame rates

**Recommendation**: **Phase 0.4** - More valuable than 0.3 optimization. Command recording performance is acceptable for now.
