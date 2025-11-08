# RenderGraph Execution Lifecycle Schema

## Overview
The RenderGraph system processes nodes through several distinct phases. Each phase has specific responsibilities and access to different types of information.

---

## Phase Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                   GRAPH CONSTRUCTION PHASE                       │
│  (User code in VulkanGraphApplication.cpp)                      │
├──────────────────────────────────────────────────────────────────┤
│ - AddNode()           : Create node instances                    │
│ - ConnectNodes()      : Connect static inputs/outputs            │
│ - ConnectVariadic()   : Queue deferred variadic connections      │
│ - ConnectMember()     : Queue deferred member connections        │
│                                                                   │
│ Available Information:                                            │
│   ✓ Node type metadata (static config)                          │
│   ✓ Compile-time constants (Names.h)                            │
│   ✗ Connected input data (not yet available)                    │
│   ✗ Resource handles (not yet created)                          │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│              GRAPH COMPILATION - RenderGraph::Compile()          │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│  Step 1: ANALYZE DEPENDENCIES - AnalyzeDependencies()           │
├──────────────────────────────────────────────────────────────────┤
│ - Topological sort of node graph                                 │
│ - Build execution order                                          │
│ - Assign execution indices                                       │
│                                                                   │
│ Available Information:                                            │
│   ✓ Graph topology                                               │
│   ✓ Node dependencies                                            │
│   ✗ Input data (connections not processed)                      │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│  Step 2: ALLOCATE RESOURCES - AllocateResources()               │
├──────────────────────────────────────────────────────────────────┤
│ - Currently placeholder (nodes allocate in Compile)              │
│ - Future: Memory aliasing, resource pooling                      │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│  Step 3: GENERATE PIPELINES - GeneratePipelines()               │
│                                                                   │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Phase 1: GRAPH COMPILE SETUP                               │ │
│  │  - IGraphCompilable::GraphCompileSetup()                   │ │
│  ├────────────────────────────────────────────────────────────┤ │
│  │ Purpose: Early setup before connections are processed       │ │
│  │                                                              │ │
│  │ Available Information:                                       │ │
│  │   ✓ Node type metadata                                      │ │
│  │   ✓ Compile-time constants (Names.h)                        │ │
│  │   ✗ Connected input data (NOT AVAILABLE - connections not  │ │
│  │     processed yet!)                                         │ │
│  │   ✗ GetInput() - INVALID (no connections)                  │ │
│  │   ✗ Context::In() - INVALID (no context yet)               │ │
│  │                                                              │ │
│  │ Use Cases:                                                   │ │
│  │   ✓ PreRegisterVariadicSlots with compile-time metadata    │ │
│  │   ✓ Register dynamic slots from Names.h constants          │ │
│  │   ✗ Read shader bundle from input (NOT POSSIBLE)           │ │
│  │   ✗ Discover descriptors from connected data (NOT POSSIBLE)│ │
│  │                                                              │ │
│  │ Example (DescriptorResourceGathererNode):                   │ │
│  │   gatherer->PreRegisterVariadicSlots(                      │ │
│  │       ComputeTest::outputImage,    // From Names.h         │ │
│  │       ComputeTest::uniformBuffer   // Compile-time known   │ │
│  │   );                                                         │ │
│  └────────────────────────────────────────────────────────────┘ │
│                            ↓                                     │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Phase 2: PROCESS DEFERRED CONNECTIONS                      │ │
│  │  - ProcessDeferredConnections() [TODO: Not implemented]    │ │
│  ├────────────────────────────────────────────────────────────┤ │
│  │ Purpose: Resolve ConnectVariadic, ConnectMember calls       │ │
│  │                                                              │ │
│  │ Available Information:                                       │ │
│  │   ✓ Variadic slot registrations (from Phase 1)             │ │
│  │   ✓ Source node outputs (static connections exist)         │ │
│  │   ✗ Input data values (not compiled yet)                   │ │
│  └────────────────────────────────────────────────────────────┘ │
│                            ↓                                     │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Phase 3: SETUP AND COMPILE                                 │ │
│  │  For each node in execution order:                         │ │
│  │    - NodeInstance::Setup()                                 │ │
│  │    - NodeInstance::Compile()                               │ │
│  ├────────────────────────────────────────────────────────────┤ │
│  │ Purpose: Initialize nodes and create Vulkan resources       │ │
│  │                                                              │ │
│  │ Setup Phase:                                                 │ │
│  │ ──────────                                                  │ │
│  │ Available Information:                                       │ │
│  │   ✓ All connections (static + deferred)                    │ │
│  │   ✓ Context::In() - READ connected input data              │ │
│  │   ✓ GetInput() - Access connected resources                │ │
│  │   ✓ Dependency inputs (upstream nodes already Setup)       │ │
│  │   ✗ Vulkan resources (not created until Compile)           │ │
│  │                                                              │ │
│  │ Use Cases:                                                   │ │
│  │   ✓ Read shader bundle and discover descriptors            │ │
│  │   ✓ Register variadic slots from shader metadata           │ │
│  │   ✓ Validate input resources                               │ │
│  │   ✓ Cache persistent state                                 │ │
│  │                                                              │ │
│  │ Example (DescriptorResourceGathererNode::SetupImpl):       │ │
│  │   auto shaderBundle = ctx.In(SHADER_DATA_BUNDLE);         │ │
│  │   for (auto& binding : shaderBundle->descriptorLayout) {   │ │
│  │       RegisterVariadicSlot(...);                            │ │
│  │   }                                                          │ │
│  │                                                              │ │
│  │ ─────────────────────────────────────────────────────      │ │
│  │                                                              │ │
│  │ Compile Phase:                                               │ │
│  │ ──────────────                                              │ │
│  │ Available Information:                                       │ │
│  │   ✓ All Setup-phase information                            │ │
│  │   ✓ Dependency Vulkan resources (upstream compiled)        │ │
│  │   ✓ Create Vulkan objects (pipelines, descriptors, etc.)   │ │
│  │   ✓ Context::Out() - WRITE output resources                │ │
│  │                                                              │ │
│  │ Use Cases:                                                   │ │
│  │   ✓ Validate variadic inputs                               │ │
│  │   ✓ Create descriptor sets                                 │ │
│  │   ✓ Create pipelines                                       │ │
│  │   ✓ Allocate buffers/images                                │ │
│  │                                                              │ │
│  │ Example (DescriptorResourceGathererNode::CompileImpl):     │ │
│  │   ValidateVariadicInputsImpl(ctx);                         │ │
│  │   GatherResources(ctx);                                     │ │
│  │   ctx.Out(DESCRIPTOR_RESOURCES, resourceArray_);           │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│  Step 4: BUILD EXECUTION ORDER - BuildExecutionOrder()          │
├──────────────────────────────────────────────────────────────────┤
│ - Currently placeholder (order already built in step 1)          │
│ - Future: Batching, parallel execution, GPU timeline optimization│
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│                    RUNTIME EXECUTION PHASE                       │
│                 RenderGraph::Execute(frameIndex)                 │
├──────────────────────────────────────────────────────────────────┤
│ For each frame:                                                   │
│   - RecompileDirtyNodes() : Handle runtime recompilation         │
│   - ProcessEvents()       : Handle window resize, etc.           │
│   - Execute nodes in order:                                      │
│       NodeInstance::Execute(taskIndex)                           │
│                                                                   │
│ Available Information:                                            │
│   ✓ All compiled Vulkan resources                               │
│   ✓ Per-frame index                                              │
│   ✓ Command buffers                                              │
│   ✓ Synchronization primitives                                   │
└──────────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────────┐
│                      CLEANUP PHASE                               │
│  (On window close, graph reset, or node recompilation)          │
├──────────────────────────────────────────────────────────────────┤
│ - vkDeviceWaitIdle()  : Sync GPU                                │
│ - NodeInstance::Cleanup() : Destroy Vulkan resources            │
│ - CleanupStack execution : Dependency-ordered cleanup            │
│                                                                   │
│ Available Information:                                            │
│   ✓ All node state                                               │
│   ✓ Dependency graph                                             │
└──────────────────────────────────────────────────────────────────┘
```

---

## Key Distinctions

### GraphCompileSetup vs Setup/Compile

| Aspect | GraphCompileSetup | Setup | Compile |
|--------|-------------------|-------|---------|
| **When** | Before connections processed | After connections processed | After Setup |
| **Access to inputs** | ❌ NO | ✅ YES | ✅ YES |
| **GetInput()** | ❌ Invalid | ✅ Valid | ✅ Valid |
| **Context parameter** | ❌ No | ✅ Yes | ✅ Yes |
| **Purpose** | Pre-register slots | Discover runtime metadata | Create Vulkan resources |
| **Use compile-time metadata** | ✅ YES (Names.h) | ❌ Rarely | ❌ No |
| **Read connected data** | ❌ NO | ✅ YES | ✅ YES |

### Connection-Agnostic vs Connection-Dependent Operations

**Connection-Agnostic** (GraphCompileSetup):
- Uses compile-time constants
- `PreRegisterVariadicSlots(ComputeTest::outputImage)`
- No dependency on graph connections

**Connection-Dependent** (Setup/Compile):
- Uses runtime-connected data
- `ctx.In(SHADER_DATA_BUNDLE)->descriptorLayout`
- Requires graph connections to be processed

---

## Current Architecture Issues

### Problem: GraphCompileSetup using GetInput()
```cpp
// ❌ INCORRECT - GraphCompileSetup tries to read connected input
void DescriptorResourceGathererNode::GraphCompileSetup() {
    Resource* shaderBundleResource = GetInput(...);  // NOT AVAILABLE YET!
    auto shaderBundle = shaderBundleResource->GetHandle<ShaderDataBundle*>();
    // Discover descriptors...
}
```

### Solution Options

**Option 1: Remove GraphCompileSetup override, use existing Setup pattern**
```cpp
// ✅ CORRECT - Setup reads connected shader bundle
void DescriptorResourceGathererNode::SetupImpl(Context& ctx) {
    auto shaderBundle = ctx.In(SHADER_DATA_BUNDLE);  // Available here!
    DiscoverDescriptors(ctx);  // Already implemented
}
```

**Option 2: Keep GraphCompileSetup for compile-time registration only**
```cpp
// ✅ CORRECT - GraphCompileSetup uses compile-time metadata
void DescriptorResourceGathererNode::GraphCompileSetup() {
    // Only use compile-time metadata from Names.h
    if (!descriptorSlots_.empty()) {
        return;  // Already pre-registered via PreRegisterVariadicSlots
    }
    // No automatic discovery here - connections not available
}
```

---

## Recommended Approach

For `DescriptorResourceGathererNode`:

1. **GraphCompileSetup**: Keep as no-op (default behavior)
   - Only used when user explicitly calls `PreRegisterVariadicSlots()`

2. **SetupImpl**: Keep existing implementation
   - Reads shader bundle from `Context::In()`
   - Discovers descriptors via `DiscoverDescriptors(ctx)`
   - Registers variadic slots at runtime

3. **CompileImpl**: Keep existing implementation
   - Validates variadic inputs
   - Gathers resources
   - Outputs resource array

This preserves the existing working architecture where discovery happens during **Setup phase** after connections are available.

---

## Future Work

### Phase 2: ProcessDeferredConnections
Currently a TODO. Will enable:
- Deferred `ConnectVariadic()` resolution
- Deferred `ConnectMember()` resolution
- Dynamic connection validation

### Recompilation Flow
When nodes need recompilation (e.g., window resize):
```
RecompileDirtyNodes()
  ↓
vkDeviceWaitIdle()
  ↓
node->Cleanup()      // Destroy old Vulkan resources
  ↓
node->Setup()        // Re-discover metadata (if needed)
  ↓
node->Compile()      // Recreate Vulkan resources
  ↓
Mark dependents dirty
```

---

## Summary

- **GraphCompileSetup**: Connection-agnostic, uses compile-time metadata only
- **Setup**: Connection-dependent, reads input data, discovers runtime metadata
- **Compile**: Creates Vulkan resources using discovered metadata
- **Execute**: Per-frame execution with compiled resources
- **Cleanup**: Dependency-ordered resource destruction

The existing `DescriptorResourceGathererNode` architecture with `SetupImpl` reading the shader bundle is **correct** for runtime discovery. GraphCompileSetup should only be used for compile-time pre-registration.
