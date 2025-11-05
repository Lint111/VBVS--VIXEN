# IGraphCompilable Architecture

## Overview

The `IGraphCompilable` interface provides a mechanism for nodes to discover and configure dynamic slots during graph compilation, before deferred connections are processed.

## Current Implementation (Phase 1)

### ✅ Completed
1. **IGraphCompilable Interface** (`RenderGraph/include/Core/IGraphCompilable.h`)
   - Pure virtual interface with `GraphCompileSetup(Context& ctx)` method
   - Documented execution order and use cases

2. **VariadicTypedNode Integration** (`RenderGraph/include/Core/VariadicTypedNode.h`)
   - Now inherits `IGraphCompilable`
   - Default implementation calls `SetupImpl()` during graph compilation
   - All variadic nodes automatically get graph-compile-time setup

3. **Workaround: PreRegisterVariadicSlots()**
   - `DescriptorResourceGathererNode` uses `PreRegisterVariadicSlots()` during graph construction
   - Registers slots eagerly using compile-time metadata from `Names.h`
   - Enables `ConnectVariadic` to work immediately
   - See: `DescriptorResourceGathererNode.h` lines 62-74, 102-132

### Current Execution Flow

```
Graph Construction:
  1. AddNode("DescriptorResourceGatherer")
  2. PreRegisterVariadicSlots(ComputeTest::outputImage)  ← Workaround
  3. ConnectVariadic(gatherer, binding, source, slot)   ← Works because slots exist

Graph Compilation (renderGraph->Compile()):
  1. Validate()
  2. AnalyzeDependencies()
  3. AllocateResources()
  4. GeneratePipelines():
      - For each node:
        - Setup()  ← VariadicTypedNode::GraphCompileSetup() calls SetupImpl()
        - Compile()
  5. BuildExecutionOrder()

Execute Loop:
  - ExecuteImpl() on each node
```

## Future Improvements (Phase 2)

### TODO: Deferred Connection Processing

Currently `ConnectVariadic` executes immediately via a lambda in `TypedConnection.h`. For true deferred processing:

1. **Add `ProcessDeferredConnections()` to RenderGraph**
   ```cpp
   void RenderGraph::Compile() {
       // ... existing phases ...
       AnalyzeDependencies();

       // NEW: Process deferred connections after Setup completes
       ProcessDeferredConnections();

       AllocateResources();
       // ...
   }
   ```

2. **Store deferred connections in RenderGraph**
   ```cpp
   class RenderGraph {
   private:
       std::vector<std::function<void()>> deferredConnections_;

   public:
       void AddDeferredConnection(std::function<void()> conn) {
           deferredConnections_.push_back(std::move(conn));
       }

       void ProcessDeferredConnections() {
           for (auto& conn : deferredConnections_) {
               conn();
           }
           deferredConnections_.clear();
       }
   };
   ```

3. **Remove PreRegisterVariadicSlots workaround**
   - Let `SetupImpl()` discover slots from shader bundle input
   - `ConnectVariadic` will resolve after Setup completes

### TODO: StructUnpackerNode

Create a node for type-safe struct member extraction:

```cpp
// Future usage:
auto unpacker = graph->AddNode("StructUnpacker", "sc_unpack");

batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
              unpacker, StructUnpackerConfig::INPUT)

     .ConnectMember(unpacker, &SwapChainPublicVariables::colorBuffers,
                    descriptorGatherer, ComputeTest::outputImage);

// unpacker->GraphCompileSetup() discovers SwapChainPublicVariables type
// and registers member outputs automatically
```

**Implementation:**
```cpp
class StructUnpackerNode : public TypedNode<StructUnpackerConfig>,
                          public IGraphCompilable {
    void GraphCompileSetup(Context& ctx) override {
        auto input = ctx.In(StructUnpackerConfig::INPUT);
        auto variant = input->GetHandleVariant();

        if (auto* scPtr = std::get_if<SwapChainPublicVariables*>(&variant)) {
            RegisterMemberOutput("colorBuffers",
                                offsetof(SwapChainPublicVariables, colorBuffers));
            RegisterMemberOutput("swapChainImageCount",
                                offsetof(SwapChainPublicVariables, swapChainImageCount));
            // ...
        }
    }
};
```

### TODO: ConnectMember with Pointer-to-Member

Type-safe member connection using C++ pointer-to-member:

```cpp
template<typename StructType, typename MemberType>
ConnectionBatch& ConnectMember(
    NodeHandle unpackerNode,
    MemberType StructType::*memberPtr,  // Type-safe!
    NodeHandle targetNode,
    auto targetSlot
) {
    size_t offset = offsetof_member(memberPtr);

    // Deferred: finds output slot by offset after GraphCompileSetup
    graph->AddDeferredConnection([=]() {
        auto* unpacker = dynamic_cast<StructUnpackerNode*>(
            graph->GetInstance(unpackerNode));
        size_t outputSlot = unpacker->GetOutputSlotByOffset(offset);
        graph->ConnectNodes(unpackerNode, outputSlot, targetNode, targetSlot.index);
    });

    return *this;
}
```

## Benefits

### Current (Phase 1)
- ✅ Variadic connections work via `PreRegisterVariadicSlots()`
- ✅ Foundation laid with `IGraphCompilable` interface
- ✅ All variadic nodes automatically inherit graph-compile-time setup
- ✅ Clear execution order documented

### Future (Phase 2)
- 🎯 Automatic type discovery (no manual `PreRegisterVariadicSlots()`)
- 🎯 Type-safe struct member access
- 🎯 Compile-time validation via pointer-to-member
- 🎯 Self-configuring graph based on connected types
- 🎯 Recompile support (re-discover on hot-reload)

## Migration Path

1. **Now**: Use `PreRegisterVariadicSlots()` for new variadic nodes
2. **Phase 2a**: Implement deferred connection queue in RenderGraph
3. **Phase 2b**: Create `StructUnpackerNode` with type discovery
4. **Phase 2c**: Add `ConnectMember()` with pointer-to-member syntax
5. **Phase 2d**: Remove `PreRegisterVariadicSlots()` workarounds

## Files Modified

- `RenderGraph/include/Core/IGraphCompilable.h` (new)
- `RenderGraph/include/Core/VariadicTypedNode.h` (added interface inheritance)
- `RenderGraph/include/Nodes/DescriptorResourceGathererNode.h` (added PreRegisterVariadicSlots)
- `RenderGraph/src/Nodes/DescriptorResourceGathererNode.cpp` (SwapChainPublicVariables handling)
- `source/VulkanGraphApplication.cpp` (calls PreRegisterVariadicSlots)

## Related Discussions

See session transcript for detailed design discussion on:
- Execution flow and timing
- Type discovery mechanisms
- Member pointer syntax for type safety
- Deferred connection processing
