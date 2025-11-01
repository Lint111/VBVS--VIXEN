# Phase 0.4: Graph-Based Multi-Rate Loop System

**Goal**: Implement graph-native loop management with configurable execution rates and explicit dependency tracking

**Status**: Planning → Implementation

**Priority**: CRITICAL - Required for any gameplay features (movement, physics, animations)

---

## Problem Statement

**Current Behavior** (BROKEN):
```cpp
while (!shouldClose) {
    graph->Execute(commandBuffer);  // All nodes execute every frame
    // Physics/logic tied to variable framerate
    // 60 FPS → 16.6ms, 144 FPS → 6.9ms, 30 FPS → 33.3ms
}
```

**Issues**:
- Physics simulation depends on framerate (30 FPS = slow motion, 144 FPS = fast forward)
- Movement speeds inconsistent across different machines
- Collision detection unreliable (tunneling at high framerates)
- No way to express "this group of nodes runs at 60Hz"
- Loop logic hidden in application layer (not graph-visible)

---

## Solution: Graph-Based Loop System

**Architecture**: Loop management as graph-native system with node-based bridges

### Key Design Principles

1. **LoopManager = RenderGraph-scoped system** (like ShaderLibrary)
2. **LoopBridgeNode = System access point** (publishes loop state to graph)
3. **Loop propagation via node connections** (LOOP_OUT → LOOP_IN chains)
4. **Auto-generated loop slots on all nodes** (zero config boilerplate)
5. **Configurable catch-up behavior** (fire-and-forget, single step, multiple steps)
6. **Multi-loop support via BoolOpNode** (AND/OR/XOR graph-side logic)

---

## Architecture Components

### 1. Timer (High-Resolution Clock)

```cpp
// RenderGraph/include/Core/Timer.h
class Timer {
public:
    Timer();

    // Get time since last call in seconds
    double GetDeltaTime();

    // Get total elapsed time since creation
    double GetElapsedTime() const;

    // Reset timer state
    void Reset();

private:
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;
};
```

### 2. LoopReference (Shared State)

```cpp
// RenderGraph/include/Core/LoopManager.h
struct LoopReference {
    uint32_t loopID;
    bool shouldExecuteThisFrame;
    double deltaTime;
    uint64_t stepCount;
    uint64_t lastExecutedFrame;      // For freshness detection
    double lastExecutionTimeMs;      // Profiling data
    LoopCatchupMode catchupMode;     // How to handle missed steps
};
```

### 3. LoopManager (Graph-Owned System)

```cpp
enum class LoopCatchupMode : uint8_t {
    FireAndForget,        // Execute once with accumulated dt
    SingleCorrectiveStep, // Execute once with fixed dt, log debt
    MultipleSteps         // Execute N times with fixed dt (default)
};

struct LoopConfig {
    double fixedTimestep;          // 1/60.0 for 60Hz, 0.0 for variable
    std::string name;
    LoopCatchupMode catchupMode = LoopCatchupMode::MultipleSteps;
    double maxCatchupTime = 0.25;  // Spiral of death prevention
};

class LoopManager {
public:
    // Register new loop
    uint32_t RegisterLoop(const LoopConfig& config);

    // Get stable pointer to loop state
    const LoopReference* GetLoopReference(uint32_t loopID);

    // Update all loop states (called by RenderGraph::Execute)
    void UpdateLoops(double frameTime);

private:
    struct LoopState {
        LoopConfig config;
        LoopReference reference;  // Stable memory address
        double accumulator = 0.0;
    };

    Timer timer;
    std::unordered_map<uint32_t, LoopState> loops;
    uint32_t nextLoopID = 0;
};
```

### 4. NodeInstance - Auto Loop Slots

```cpp
// RenderGraph/include/Core/NodeInstance.h
class NodeInstance {
public:
    // Reserved slot indices (auto-generated, not in config)
    static constexpr uint32_t AUTO_LOOP_IN_SLOT = UINT32_MAX - 1;
    static constexpr uint32_t AUTO_LOOP_OUT_SLOT = UINT32_MAX - 2;

    // Loop connection API
    void SetLoopInput(const LoopReference* loopRef);
    const LoopReference* GetLoopOutput() const;

    // Execution gating
    bool ShouldExecuteThisFrame() const;

    // Loop data access (for nodes that need it)
    double GetLoopDeltaTime() const;
    uint64_t GetLoopStepCount() const;

private:
    std::vector<const LoopReference*> connectedLoops;  // Supports multiple loops
};
```

### 5. LoopBridgeNode (System Access Point)

```cpp
// RenderGraph/include/Nodes/LoopBridgeNodeConfig.h
CONSTEXPR_NODE_CONFIG(LoopBridgeNodeConfig, 0, 2, false) {
    // Outputs
    CONSTEXPR_OUTPUT(LOOP_OUT, const LoopReference*, 0, false);
    CONSTEXPR_OUTPUT(SHOULD_EXECUTE, bool, 1, false);

    // Parameters
    PARAM_DEFINITION(LOOP_ID, uint32_t);
};

// RenderGraph/include/Nodes/LoopBridgeNode.h
class LoopBridgeNode : public TypedNode<LoopBridgeNodeConfig> {
public:
    void Setup() override {
        loopID = GetParameter<uint32_t>("LOOP_ID");
        loopManager = &GetGraph()->GetLoopManager();
    }

    void Execute(VkCommandBuffer commandBuffer) override {
        const LoopReference* loopRef = loopManager->GetLoopReference(loopID);

        Out(LoopBridgeNodeConfig::LOOP_OUT, loopRef);
        Out(LoopBridgeNodeConfig::SHOULD_EXECUTE, loopRef->shouldExecuteThisFrame);
    }

private:
    uint32_t loopID;
    LoopManager* loopManager;
};
```

### 6. BoolOpNode (Multi-Loop Logic)

```cpp
// RenderGraph/include/Nodes/BoolOpNodeConfig.h
enum class BoolOp { AND, OR, XOR, NOT };

CONSTEXPR_NODE_CONFIG(BoolOpNodeConfig, 2, 1, false) {
    CONSTEXPR_INPUT(INPUT_A, bool, 0, false);
    CONSTEXPR_INPUT(INPUT_B, bool, 1, false);
    CONSTEXPR_OUTPUT(OUTPUT, bool, 0, false);

    PARAM_DEFINITION(OPERATION, BoolOp);
};

// Example: Node executes only when BOTH physics AND network loops active
graph->Connect(physicsLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_A);
graph->Connect(networkLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_B);
graph->Connect(andNode, BoolOpNodeConfig::OUTPUT,
               syncNode, SyncNodeConfig::SHOULD_EXECUTE_CUSTOM);
```

---

## Graph Wiring Examples

### Simple 60Hz Physics Loop

```cpp
// Register loop with graph
uint32_t physicsLoopID = graph->RegisterLoop({
    .fixedTimestep = 1.0/60.0,
    .name = "Physics",
    .catchupMode = LoopCatchupMode::MultipleSteps
});

// Create bridge node
auto physicsLoop = graph->CreateNode<LoopBridgeNode>("PhysicsLoop");
physicsLoop->SetParameter("LOOP_ID", physicsLoopID);

// Create physics nodes
auto nodeA = graph->CreateNode<PhysicsNodeA>("NodeA");
auto nodeB = graph->CreateNode<PhysicsNodeB>("NodeB");

// Loop chain (propagates loop state)
graph->Connect(physicsLoop, NodeInstance::AUTO_LOOP_OUT_SLOT,
               nodeA, NodeInstance::AUTO_LOOP_IN_SLOT);
graph->Connect(nodeA, NodeInstance::AUTO_LOOP_OUT_SLOT,
               nodeB, NodeInstance::AUTO_LOOP_IN_SLOT);

// Resource dependencies (separate from loop membership)
graph->Connect(nodeA, PhysicsNodeAConfig::VELOCITY_OUT,
               nodeB, PhysicsNodeBConfig::VELOCITY_IN);
```

### Cross-Loop Data Flow

```cpp
// Physics nodes run at 60Hz
PhysicsNode (60Hz) ──[POSITION_OUT]──> RenderNode (144Hz)

// RenderNode reads physics output passively
// Data stays same between physics updates (acceptable for Phase 0.4)
// Future: Add LerpNode for interpolation
```

### Multi-Loop AND Logic

```cpp
// Create two loops
uint32_t physicsLoopID = graph->RegisterLoop({1.0/60.0, "Physics"});
uint32_t networkLoopID = graph->RegisterLoop({1.0/30.0, "Network"});

// Create bridges
auto physicsLoop = graph->CreateNode<LoopBridgeNode>("PhysicsLoop");
auto networkLoop = graph->CreateNode<LoopBridgeNode>("NetworkLoop");

// Create AND node
auto andNode = graph->CreateNode<BoolOpNode>("PhysicsAndNetwork");
andNode->SetParameter("OPERATION", BoolOp::AND);

// Wire: syncNode executes only when BOTH loops active
graph->Connect(physicsLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_A);
graph->Connect(networkLoop, LoopBridgeNodeConfig::SHOULD_EXECUTE,
               andNode, BoolOpNodeConfig::INPUT_B);
graph->Connect(andNode, BoolOpNodeConfig::OUTPUT,
               syncNode, SyncNodeConfig::SHOULD_EXECUTE);
```

---

## RenderGraph Integration

```cpp
// RenderGraph.h
class RenderGraph {
public:
    // Loop registration API
    uint32_t RegisterLoop(const LoopManager::LoopConfig& config) {
        return loopManager.RegisterLoop(config);
    }

    // System access (like GetShaderLibrary())
    LoopManager& GetLoopManager() { return loopManager; }

    void Execute(VkCommandBuffer commandBuffer) {
        // 1. Update all loop states
        loopManager.UpdateLoops(frameTime);

        // 2. Propagate loop references through graph
        for (auto& conn : connections) {
            if (conn.sourceSlot == NodeInstance::AUTO_LOOP_OUT_SLOT) {
                const LoopReference* loopRef = conn.source->GetLoopOutput();
                conn.dest->SetLoopInput(loopRef);
            }
        }

        // 3. Execute nodes (each checks ShouldExecuteThisFrame)
        for (NodeInstance* node : executionOrder) {
            if (node->ShouldExecuteThisFrame()) {
                node->Execute(commandBuffer);
            }
        }
    }

private:
    LoopManager loopManager;
    Timer frameTimer;
    double frameTime = 0.0;
};
```

---

## Implementation Plan

### Task 1: Create Timer Class
**Files**: `RenderGraph/include/Core/Timer.h`, `RenderGraph/src/Core/Timer.cpp`
**Time**: 30 minutes
**Verification**: Test GetDeltaTime() accuracy with manual sleeps

### Task 2: Create LoopManager
**Files**: `RenderGraph/include/Core/LoopManager.h`, `RenderGraph/src/Core/LoopManager.cpp`
**Time**: 1.5 hours
**Features**: RegisterLoop, UpdateLoops with accumulator, catch-up modes, logging

### Task 3: Add Loop Slots to NodeInstance
**Files**: `RenderGraph/include/Core/NodeInstance.h`, `RenderGraph/src/Core/NodeInstance.cpp`
**Time**: 1 hour
**Features**: AUTO_LOOP_IN/OUT constants, SetLoopInput, GetLoopOutput, ShouldExecuteThisFrame

### Task 4: Create LoopBridgeNode
**Files**:
- `RenderGraph/include/Nodes/LoopBridgeNodeConfig.h`
- `RenderGraph/include/Nodes/LoopBridgeNode.h`
- `RenderGraph/src/Nodes/LoopBridgeNode.cpp`
**Time**: 1 hour
**Features**: Accesses graph's LoopManager, publishes LoopReference + bool

### Task 5: Create BoolOpNode
**Files**:
- `RenderGraph/include/Nodes/BoolOpNodeConfig.h`
- `RenderGraph/include/Nodes/BoolOpNode.h`
- `RenderGraph/src/Nodes/BoolOpNode.cpp`
**Time**: 45 minutes
**Features**: AND, OR, XOR, NOT operations on bool inputs

### Task 6: Integrate with RenderGraph
**Files**: `RenderGraph/include/Core/RenderGraph.h`, `RenderGraph/src/Core/RenderGraph.cpp`
**Time**: 1.5 hours
**Changes**: Add LoopManager member, RegisterLoop API, UpdateLoops in Execute, loop propagation

### Task 7: Wire in VulkanGraphApplication
**Files**: `source/VulkanGraphApplication.cpp`
**Time**: 1 hour
**Changes**: Register loops, create LoopBridgeNodes, connect to existing nodes

### Task 8: Testing & Validation
**Time**: 1.5 hours
**Tests**:
- 60Hz physics loop at 30/60/144 FPS render rates
- Verify 600 physics steps over 10 seconds
- Test MultipleSteps catch-up mode (simulate lag spike)
- Test FireAndForget mode (variable timestep)
- Test multi-loop AND logic (two loops → one node)

### Task 9: Documentation & Commit
**Files**:
- `documentation/GraphArchitecture/09-loop-system.md` (new architecture doc)
- `memory-bank/activeContext.md` (update current focus)
- `memory-bank/progress.md` (mark Phase 0.4 complete)
**Time**: 1 hour

**Total Estimated Time**: 8-10 hours

---

## Testing Strategy

### Manual Tests

1. **60Hz Physics at Variable Render Rates**
   ```cpp
   // Register 60Hz physics loop
   // Create physics nodes connected to loop
   // Run for 10 seconds at 30/60/144 FPS
   // Verify: ~600 physics steps regardless of render FPS
   ```

2. **Catch-Up Mode: MultipleSteps**
   ```cpp
   // Simulate 100ms frame (spike)
   // Physics should execute 6 times (100ms / 16.6ms)
   // Verify: 6 Execute() calls on physics nodes
   ```

3. **Catch-Up Mode: FireAndForget**
   ```cpp
   // Variable timestep loop
   // 100ms frame → single Execute() with dt=100ms
   ```

4. **Multi-Loop AND Logic**
   ```cpp
   // Create 60Hz physics + 30Hz network loops
   // Wire BoolOpNode(AND) between them
   // Verify: SyncNode executes only when BOTH active
   ```

5. **Loop Profiling**
   ```cpp
   // Check logs for budget warnings
   // Verify lastExecutionTimeMs updated
   ```

### Edge Cases

1. **Spiral of Death**: Frame > 250ms → capped, slow-motion effect
2. **Zero Delta Time**: Minimum 0.001s enforced
3. **No Loop Connection**: Node executes every frame (default behavior)
4. **Multiple Loop Inputs**: Node executes if ANY loop active (OR logic)

---

## Success Criteria

- ✅ LoopManager manages multiple loops with independent accumulators
- ✅ Loop state propagates through LOOP_IN→LOOP_OUT connections
- ✅ Nodes gate execution based on ShouldExecuteThisFrame()
- ✅ 60Hz physics runs at fixed rate regardless of render FPS
- ✅ Catch-up modes work correctly (MultipleSteps, FireAndForget, SingleCorrectiveStep)
- ✅ BoolOpNode enables multi-loop AND/OR logic
- ✅ Loop profiling logs budget warnings
- ✅ Zero validation errors
- ✅ Documentation updated

---

## Files to Create/Modify

**NEW Files**:
- `RenderGraph/include/Core/Timer.h`
- `RenderGraph/src/Core/Timer.cpp`
- `RenderGraph/include/Core/LoopManager.h`
- `RenderGraph/src/Core/LoopManager.cpp`
- `RenderGraph/include/Nodes/LoopBridgeNodeConfig.h`
- `RenderGraph/include/Nodes/LoopBridgeNode.h`
- `RenderGraph/src/Nodes/LoopBridgeNode.cpp`
- `RenderGraph/include/Nodes/BoolOpNodeConfig.h`
- `RenderGraph/include/Nodes/BoolOpNode.h`
- `RenderGraph/src/Nodes/BoolOpNode.cpp`
- `documentation/GraphArchitecture/09-loop-system.md`

**Modified Files**:
- `RenderGraph/include/Core/NodeInstance.h` - Add AUTO_LOOP_IN/OUT, loop methods
- `RenderGraph/src/Core/NodeInstance.cpp` - Implement loop propagation
- `RenderGraph/include/Core/RenderGraph.h` - Add LoopManager member, RegisterLoop API
- `RenderGraph/src/Core/RenderGraph.cpp` - Integrate loop update + propagation
- `source/VulkanGraphApplication.cpp` - Wire LoopBridgeNodes
- `memory-bank/activeContext.md` - Update current focus
- `memory-bank/progress.md` - Mark Phase 0.4 complete

**Total**: 11 new files, 7 modified files

---

## Future Enhancements (Post-0.4)

### Phase 0.4.1: Interpolation Support
- Add LerpNode for smooth cross-loop data interpolation
- Implement GetInterpolationAlpha() based on loop accumulator
- Use lastExecutedFrame for freshness detection

### Phase 0.5: Wave-Based Execution
- Group nodes into execution waves based on dependencies
- All LoopBridgeNodes execute in Wave 0 (no dependencies)
- Enables parallel execution within waves

### Phase 0.6: Dynamic Loop Reconfiguration
- Runtime timescale modification (pause, slow-motion)
- Loop enable/disable at runtime
- Hot-reload loop configurations

---

## Open Questions

1. **CMake**: Add Timer/LoopManager to RenderGraph library or create separate Core library?
   - **Recommendation**: Add to RenderGraph lib (same pattern as ShaderLibrary)

2. **Logging**: Use existing Logger or create loop-specific logging?
   - **Recommendation**: Use existing Logger with "LoopManager" category

3. **Unit Tests**: Write now or defer?
   - **Recommendation**: Manual tests for Phase 0.4, unit tests in Phase 1 refactor

---

## Next Steps

1. ✅ Review architecture with user
2. Implement Task 1 (Timer) - 30 minutes
3. Implement Task 2 (LoopManager) - 1.5 hours
4. Implement Tasks 3-5 (Node integration) - 2.75 hours
5. Implement Task 6-7 (RenderGraph + App wiring) - 2.5 hours
6. Test all scenarios - 1.5 hours
7. Document and commit - 1 hour

**Ready to proceed!**
