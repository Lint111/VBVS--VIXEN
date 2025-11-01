# Loop System Architecture

**Phase**: 0.4
**Status**: Implementation Ready
**Category**: Core System

---

## Overview

The Loop System provides graph-native multi-rate execution control, allowing different subgraphs to run at independent update frequencies (e.g., physics at 60Hz, rendering at 144Hz, AI at 30Hz).

**Key Insight**: Loop management is a RenderGraph-scoped system (like ShaderLibrary), accessed via LoopBridgeNodes that publish loop state into the graph.

---

## Core Components

### 1. Timer

High-resolution clock for accurate delta time measurement.

```cpp
class Timer {
public:
    double GetDeltaTime();        // Time since last call
    double GetElapsedTime() const; // Time since creation
    void Reset();

private:
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;
};
```

**Usage**: Internal to LoopManager, not directly exposed to nodes.

---

### 2. LoopReference

Shared state container representing a single loop's current state. Passed as const pointer to all nodes connected to that loop.

```cpp
struct LoopReference {
    uint32_t loopID;
    bool shouldExecuteThisFrame;   // Accumulator triggered this frame
    double deltaTime;               // Fixed timestep (e.g., 1/60.0)
    uint64_t stepCount;             // Total steps executed
    uint64_t lastExecutedFrame;     // RenderGraph frame counter
    double lastExecutionTimeMs;     // Profiling data
    LoopCatchupMode catchupMode;    // How to handle missed steps
};
```

**Lifetime**: Stored in LoopManager's internal map, pointer remains stable.

---

### 3. LoopCatchupMode

Defines behavior when frame time exceeds fixed timestep (e.g., 100ms frame with 16.6ms physics timestep).

```cpp
enum class LoopCatchupMode : uint8_t {
    FireAndForget,        // Execute once with accumulated dt (dt=100ms)
    SingleCorrectiveStep, // Execute once with fixed dt (dt=16.6ms), log 83.4ms debt
    MultipleSteps         // Execute 6 times with fixed dt (6 * 16.6ms)
};
```

**Default**: `MultipleSteps` (most common for physics/gameplay)

---

### 4. LoopManager

RenderGraph-owned system managing all registered loops.

```cpp
class LoopManager {
public:
    uint32_t RegisterLoop(const LoopConfig& config);
    const LoopReference* GetLoopReference(uint32_t loopID);
    void UpdateLoops(double frameTime);  // Called by RenderGraph::Execute

private:
    struct LoopState {
        LoopConfig config;
        LoopReference reference;
        double accumulator = 0.0;
    };

    std::unordered_map<uint32_t, LoopState> loops;
    Timer timer;
};
```

**Lifecycle**:
1. Application calls `graph->RegisterLoop()` (delegates to LoopManager)
2. Returns `loopID` (used to create LoopBridgeNodes)
3. `UpdateLoops()` called once per frame by RenderGraph
4. LoopReferences updated based on accumulator state

---

### 5. NodeInstance Loop Slots

All nodes automatically gain loop connection capability via reserved slot indices.

```cpp
class NodeInstance {
public:
    static constexpr uint32_t AUTO_LOOP_IN_SLOT = UINT32_MAX - 1;
    static constexpr uint32_t AUTO_LOOP_OUT_SLOT = UINT32_MAX - 2;

    void SetLoopInput(const LoopReference* loopRef);
    const LoopReference* GetLoopOutput() const;
    bool ShouldExecuteThisFrame() const;
    double GetLoopDeltaTime() const;

private:
    std::vector<const LoopReference*> connectedLoops;
};
```

**Design**:
- No config boilerplate needed
- Nodes without loop connections always execute (default behavior)
- Multi-loop support: node executes if ANY loop active (OR logic)

---

### 6. LoopBridgeNode

System access node that publishes LoopReference to the graph.

```cpp
CONSTEXPR_NODE_CONFIG(LoopBridgeNodeConfig, 0, 2, false) {
    CONSTEXPR_OUTPUT(LOOP_OUT, const LoopReference*, 0, false);
    CONSTEXPR_OUTPUT(SHOULD_EXECUTE, bool, 1, false);
    PARAM_DEFINITION(LOOP_ID, uint32_t);
};

class LoopBridgeNode : public TypedNode<LoopBridgeNodeConfig> {
    void Setup() override {
        loopID = GetParameter<uint32_t>("LOOP_ID");
        loopManager = &GetGraph()->GetLoopManager();
    }

    void Execute(VkCommandBuffer commandBuffer) override {
        const LoopReference* loopRef = loopManager->GetLoopReference(loopID);
        Out(LoopBridgeNodeConfig::LOOP_OUT, loopRef);
        Out(LoopBridgeNodeConfig::SHOULD_EXECUTE, loopRef->shouldExecuteThisFrame);
    }
};
```

**Pattern**: Same as ShaderLibraryNode (zero input slots, accesses graph-owned system)

---

### 7. BoolOpNode

Enables multi-loop logic composition (AND/OR/XOR/NOT).

```cpp
enum class BoolOp { AND, OR, XOR, NOT };

CONSTEXPR_NODE_CONFIG(BoolOpNodeConfig, 2, 1, false) {
    CONSTEXPR_INPUT(INPUT_A, bool, 0, false);
    CONSTEXPR_INPUT(INPUT_B, bool, 1, false);
    CONSTEXPR_OUTPUT(OUTPUT, bool, 0, false);
    PARAM_DEFINITION(OPERATION, BoolOp);
};
```

**Use Case**: Node executes only when BOTH physics (60Hz) AND network (30Hz) loops active.

---

## Execution Flow

### Frame N

```
1. RenderGraph::Execute() called
2. frameTimer.GetDeltaTime() → frameTime (e.g., 16.6ms at 60 FPS)
3. loopManager.UpdateLoops(frameTime)
   ├─ PhysicsLoop (60Hz): accumulator += 16.6ms → trigger (shouldExecute = true)
   ├─ NetworkLoop (30Hz): accumulator += 16.6ms → no trigger (need 33.3ms)
   └─ RenderLoop (variable): always trigger (fixedTimestep = 0.0)

4. Loop propagation (iterate connections)
   └─ Find AUTO_LOOP_OUT → AUTO_LOOP_IN connections
   └─ Call dest->SetLoopInput(source->GetLoopOutput())

5. Node execution (iterate executionOrder)
   └─ if (node->ShouldExecuteThisFrame())
        node->Execute(commandBuffer)
```

### Catch-Up Example (100ms Frame Spike)

**MultipleSteps Mode**:
```
PhysicsLoop (16.6ms fixed timestep):
accumulator = 100ms
while (accumulator >= 16.6ms):
    shouldExecuteThisFrame = true
    Execute all physics nodes (6 times total)
    accumulator -= 16.6ms
accumulator = 0.4ms (remainder for next frame)
```

**FireAndForget Mode**:
```
PhysicsLoop:
deltaTime = 100ms (accumulated)
shouldExecuteThisFrame = true
Execute all physics nodes once with dt=100ms
accumulator = 0
```

---

## Graph Wiring Patterns

### Pattern 1: Simple Fixed Timestep

```cpp
// Application code
uint32_t physicsLoopID = graph->RegisterLoop({
    .fixedTimestep = 1.0/60.0,
    .name = "Physics"
});

auto physicsLoop = graph->CreateNode<LoopBridgeNode>("PhysicsLoop");
physicsLoop->SetParameter("LOOP_ID", physicsLoopID);

auto physicsNode = graph->CreateNode<PhysicsNode>("PhysicsNode");

// Loop chain
graph->Connect(physicsLoop, NodeInstance::AUTO_LOOP_OUT_SLOT,
               physicsNode, NodeInstance::AUTO_LOOP_IN_SLOT);
```

**Result**: PhysicsNode executes at 60Hz regardless of render framerate.

---

### Pattern 2: Loop Branching

```cpp
// One loop drives multiple independent chains
graph->Connect(physicsLoop, NodeInstance::AUTO_LOOP_OUT_SLOT,
               collisionNode, NodeInstance::AUTO_LOOP_IN_SLOT);
graph->Connect(physicsLoop, NodeInstance::AUTO_LOOP_OUT_SLOT,
               animationNode, NodeInstance::AUTO_LOOP_IN_SLOT);

// Both receive same LoopReference (shared state)
```

---

### Pattern 3: Multi-Loop AND Logic

```cpp
auto andNode = graph->CreateNode<BoolOpNode>("PhysicsAndNetwork");
andNode->SetParameter("OPERATION", BoolOp::AND);

graph->Connect(physicsLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_A);
graph->Connect(networkLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_B);
graph->Connect(andNode, BoolOpNodeConfig::OUTPUT,
               syncNode, SyncNodeConfig::CUSTOM_SHOULD_EXECUTE);
```

**Result**: SyncNode executes only when both loops trigger.

---

### Pattern 4: Cross-Loop Data Flow

```cpp
// Physics at 60Hz, render at 144Hz
PhysicsNode (60Hz) ──[POSITION_OUT]──> RenderNode (144Hz)

// RenderNode reads stale position between physics updates
// Phase 0.4: Acceptable (passive read)
// Phase 0.4.1: Add LerpNode for interpolation
```

---

## Implementation Details

### LoopManager::UpdateLoops

```cpp
void LoopManager::UpdateLoops(double frameTime) {
    // Cap frame time (spiral of death protection)
    if (frameTime > 0.25) frameTime = 0.25;
    if (frameTime <= 0.0) frameTime = 0.001;

    for (auto& [id, state] : loops) {
        if (state.config.fixedTimestep == 0.0) {
            // Variable rate loop (always execute)
            state.reference.shouldExecuteThisFrame = true;
            state.reference.deltaTime = frameTime;
        } else {
            // Fixed timestep accumulator
            state.accumulator += frameTime;

            if (state.accumulator >= state.config.fixedTimestep) {
                state.reference.shouldExecuteThisFrame = true;
                state.reference.deltaTime = state.config.fixedTimestep;
                state.reference.stepCount++;
                state.accumulator -= state.config.fixedTimestep;

                // MultipleSteps handled by RenderGraph (execute node multiple times)
            } else {
                state.reference.shouldExecuteThisFrame = false;
            }
        }

        state.reference.lastExecutedFrame = currentFrameIndex;
    }
}
```

### NodeInstance::ShouldExecuteThisFrame

```cpp
bool NodeInstance::ShouldExecuteThisFrame() const {
    if (connectedLoops.empty()) return true;  // No loops = always execute

    // OR logic: execute if ANY loop active
    for (const auto* loop : connectedLoops) {
        if (loop->shouldExecuteThisFrame) return true;
    }
    return false;
}
```

---

## Edge Cases

### 1. Spiral of Death

**Problem**: Frame takes 300ms → accumulator grows infinitely → more physics steps → longer frame

**Solution**: Cap frameTime at 250ms (maxCatchupTime)
```cpp
if (frameTime > state.config.maxCatchupTime) {
    frameTime = state.config.maxCatchupTime;
    Logger::Warn("Loop '{}' capped frame time: {:.2f}ms", name, frameTime * 1000.0);
}
```

**Effect**: Physics runs in slow-motion but doesn't freeze.

---

### 2. Zero Delta Time

**Problem**: Timer precision issues, or paused application

**Solution**: Enforce minimum delta
```cpp
if (frameTime <= 0.0) frameTime = 0.001;  // 1ms minimum
```

---

### 3. No Loop Connection

**Problem**: Node created but never connected to LoopBridgeNode

**Behavior**: `connectedLoops.empty() == true` → executes every frame (default)

---

### 4. Multiple Loop Connections

**Problem**: Node receives loop inputs from multiple bridges

**Behavior**: OR logic - executes if ANY loop active
```cpp
// Example: Node connected to both 60Hz physics and 30Hz AI loops
// Executes at 60Hz (physics triggers more frequently)
```

---

## Performance Considerations

### Memory

- **LoopReference**: 48 bytes per loop (negligible)
- **Node overhead**: 8 bytes per node (vector<const LoopReference*>)
- **Connection overhead**: Zero (uses existing connection system)

### CPU

- **UpdateLoops**: O(n) where n = number of loops (typically 2-5)
- **Loop propagation**: O(m) where m = number of loop connections (typically 10-50)
- **ShouldExecuteThisFrame**: O(k) where k = loops per node (typically 1-2)

**Total overhead**: <1% of frame time

---

## Future Enhancements

### Phase 0.4.1: Interpolation

```cpp
struct LoopReference {
    // ... existing fields ...
    double interpolationAlpha;  // accumulator / fixedTimestep
};

// LerpNode uses alpha to smooth cross-loop data
Vector3 interpolated = Lerp(prevPos, currPos, loopRef->interpolationAlpha);
```

### Phase 0.5: Wave Execution

```cpp
// All LoopBridgeNodes execute in Wave 0 (no dependencies)
// Enables parallel loop updates
Wave 0: [PhysicsLoop, NetworkLoop, RenderLoop] (parallel)
Wave 1: [Nodes depending on loops]
```

### Phase 0.6: Dynamic Reconfiguration

```cpp
loopManager.SetLoopTimescale(physicsLoopID, 0.5);  // Slow-motion
loopManager.PauseLoop(physicsLoopID);              // Pause
loopManager.ResumeLoop(physicsLoopID);             // Resume
```

---

## Testing

### Unit Tests

```cpp
TEST(LoopManager, FixedTimestep60Hz) {
    LoopManager manager;
    uint32_t loopID = manager.RegisterLoop({1.0/60.0, "Test"});

    // Simulate 1 second at 60 FPS
    for (int i = 0; i < 60; i++) {
        manager.UpdateLoops(1.0/60.0);
        const LoopReference* ref = manager.GetLoopReference(loopID);
        EXPECT_TRUE(ref->shouldExecuteThisFrame);
    }

    EXPECT_EQ(manager.GetLoopReference(loopID)->stepCount, 60);
}
```

### Integration Tests

```cpp
TEST(RenderGraph, LoopGatedExecution) {
    auto graph = std::make_unique<RenderGraph>();
    uint32_t loopID = graph->RegisterLoop({1.0/60.0, "Physics"});

    auto loopBridge = graph->CreateNode<LoopBridgeNode>("PhysicsLoop");
    loopBridge->SetParameter("LOOP_ID", loopID);

    auto testNode = graph->CreateNode<TestNode>("TestNode");
    graph->Connect(loopBridge, NodeInstance::AUTO_LOOP_OUT_SLOT,
                   testNode, NodeInstance::AUTO_LOOP_IN_SLOT);

    graph->Compile();

    // Execute 120 frames at 60 FPS
    for (int i = 0; i < 120; i++) {
        graph->Execute(nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // TestNode should execute ~60 times (loop at 60Hz, render at 60Hz)
    EXPECT_NEAR(testNode->GetExecutionCount(), 60, 5);
}
```

---

## Comparison to Traditional Approach

### Traditional (Application-Layer Loop)

```cpp
// Hidden in VulkanGraphApplication::Run()
while (gameLoop.Tick()) {
    UpdatePhysics(dt);  // 60Hz
    graph->Execute();   // All nodes execute every frame
}
```

**Issues**:
- Loop logic hidden from graph
- No way to express "this node group runs at 60Hz"
- Can't compose multiple loops (physics + network + AI)
- Cross-loop dependencies unclear

### Graph-Based Loop System

```cpp
// Explicit in graph topology
PhysicsLoop (60Hz) → PhysicsNodeA → PhysicsNodeB
NetworkLoop (30Hz) → NetworkNodeA
RenderLoop (var)   → RenderNode
```

**Benefits**:
- Loop membership visible in graph connections
- Composable (BoolOpNode for multi-loop logic)
- Inspectable (query which nodes in which loop)
- Future-proof (supports wave execution, interpolation)

---

## API Summary

### Application API

```cpp
// Register loops
uint32_t loopID = graph->RegisterLoop({
    .fixedTimestep = 1.0/60.0,
    .name = "Physics",
    .catchupMode = LoopCatchupMode::MultipleSteps,
    .maxCatchupTime = 0.25
});

// Create bridge
auto bridge = graph->CreateNode<LoopBridgeNode>("PhysicsLoop");
bridge->SetParameter("LOOP_ID", loopID);

// Connect nodes to loop
graph->Connect(bridge, NodeInstance::AUTO_LOOP_OUT_SLOT,
               node, NodeInstance::AUTO_LOOP_IN_SLOT);
```

### Node API

```cpp
// In custom node Execute()
if (!ShouldExecuteThisFrame()) return;  // Skip if loop inactive

double dt = GetLoopDeltaTime();  // Fixed timestep (e.g., 1/60.0)
uint64_t step = GetLoopStepCount();  // Total steps since start
```

---

## References

- **Gaffer on Games**: "Fix Your Timestep" (accumulator pattern inspiration)
- **Phase 0.1**: Per-frame resources (ring buffer pattern)
- **Phase 0.2**: Frame-in-flight synchronization (fence-based CPU throttling)
- **Phase 0.3**: Command buffer dirty tracking (StatefulContainer)
