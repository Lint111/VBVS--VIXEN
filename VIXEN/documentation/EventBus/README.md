# EventBus Documentation

**Status**: Complete - Integrated with RenderGraph

## Overview

Type-safe event system enabling decoupled communication between nodes. Supports cascade invalidation pattern for automatic graph recompilation.

## Core Documents

- **[EventBusArchitecture.md](EventBusArchitecture.md)** - Event-driven invalidation, cascade recompilation
- **[EventBus-ResourceManagement-Integration.md](EventBus-ResourceManagement-Integration.md)** - Integration with resource management

## Key Features

- ✅ Type-safe event payloads (`Event<PayloadType>`)
- ✅ Queue-based processing (prevents recursion)
- ✅ Cascade invalidation pattern (WindowResize → SwapChainInvalidated → FramebufferDirty)
- ✅ Nodes subscribe to events via `IEventListener`
- ✅ CleanupStack provides dependency graph for cascade propagation

## Cascade Invalidation Flow

```
WindowResize event
    ↓
SwapChainNode::OnEvent() → SetDirty(true) → Emit(SwapChainInvalidated)
    ↓
FramebufferNode::OnEvent() → SetDirty(true) → Emit(FramebufferDirty)
    ↓
GeometryRenderNode::OnEvent() → SetDirty(true)
    ↓
RenderGraph::ProcessEvents() → RecompileDirtyNodes()
```

## Usage Example

```cpp
// Node subscribes to events
void SwapChainNode::Setup() override {
    eventBus->Subscribe(EventType::WindowResize, this);
}

// Node handles event, marks dirty
void SwapChainNode::OnEvent(const Event& event) override {
    if (event.type == EventType::WindowResize) {
        SetDirty(true);
        eventBus->Emit(SwapChainInvalidatedEvent{});  // Cascade
    }
}

// Graph recompiles dirty nodes
graph.ProcessEvents();       // Drain event queue
graph.RecompileDirtyNodes(); // Recompile affected nodes
```

## Recompilation Pattern

```cpp
// RenderGraph.cpp lines 1060-1075
node->Cleanup();              // Destroy old resources
node->Setup();                // Re-subscribe to events
node->Compile();              // Create new resources
node->RegisterCleanup();      // Register new cleanup callback
cleanupStack.ResetExecuted(handle);
```

## Related

- See `memory-bank/systemPatterns.md` - EventBus Invalidation Pattern (Pattern 4)
- See `RenderGraph/src/Core/RenderGraph.cpp` - Recompilation implementation
- See `CleanupStack.h` - Dependency tracking for cascade propagation
