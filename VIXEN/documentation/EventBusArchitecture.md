# Event Bus Architecture

## Overview

The Event Bus is a centralized event system for the RenderGraph that handles resource invalidation, application state changes, and execution events. It prevents "event spaghetti" by providing a single, structured communication channel between nodes.

## Design Principles

1. **Single Event Bus Per Graph**: One EventBus instance owned by RenderGraph
2. **Type-Safe Events**: Strongly-typed event payloads (no void* casting)
3. **Queue-Based Processing**: Events processed at safe points, not during Compile/Execute
4. **Subscription Model**: Nodes subscribe to specific event types
5. **Cascade Invalidation**: Events trigger recompilation of dependent nodes
6. **Thread-Safe Design**: Prepared for future multi-threaded graph execution

## Event Categories

### Resource Invalidation Events (0-99)
Events that trigger node recompilation:
- `WindowResize`: Window dimensions changed → SwapChain needs recreation
- `SwapChainInvalidated`: Swapchain recreated → Framebuffers need rebuild
- `ShaderReloaded`: Shader file changed → Pipeline needs recompilation
- `TextureReloaded`: Texture file changed → Descriptor sets need update
- `PipelineInvalidated`: Pipeline needs rebuild
- `DescriptorInvalidated`: Descriptor sets need update

### Application State Events (100-199)
Events that update scene/rendering state:
- `SceneChanged`: Scene graph modified
- `CameraUpdated`: Camera transform/projection changed
- `LightingChanged`: Light parameters changed
- `MaterialChanged`: Material parameters changed

### Execution Events (200-299)
Rarely used - prefer SynchronizationManager for Vulkan sync:
- `FrameComplete`: Frame rendering finished
- `RenderPassBegin`: Entering render pass
- `RenderPassEnd`: Exiting render pass

## Architecture

```
┌─────────────────────────────────────────┐
│          RenderGraph                    │
│  ┌───────────────────────────────────┐  │
│  │         EventBus                  │  │
│  │  - Event queue                    │  │
│  │  - Listener registry              │  │
│  │  - Callback registry              │  │
│  │  - Statistics                     │  │
│  └───────────────┬───────────────────┘  │
│                  │                       │
│     ┌────────────┼────────────┐          │
│     ▼            ▼            ▼          │
│  ┌──────┐   ┌──────┐   ┌──────┐        │
│  │ Node │   │ Node │   │ Node │        │
│  │  A   │   │  B   │   │  C   │        │
│  └──────┘   └──────┘   └──────┘        │
│  (Window)   (SwapCh)  (Framebuf)       │
└─────────────────────────────────────────┘

Event Flow:
1. WindowNode emits WindowResize event
2. EventBus queues event
3. RenderGraph::ProcessEvents() processes queue
4. SwapChainNode receives event via OnEvent()
5. SwapChainNode marks itself for recompile
6. SwapChainNode emits SwapChainInvalidated
7. FramebufferNode receives cascade event
8. RenderGraph::RecompileDirtyNodes() recompiles
```

## Core Components

### EventBus Class

**Location**: `RenderGraph/Core/EventBus.h`

**Responsibilities**:
- Manage event subscriptions (both interface-based and lambda-based)
- Queue events for async processing
- Dispatch events to listeners
- Track statistics (events emitted, processed, queued)

**Key Methods**:
```cpp
// Subscription
void Subscribe(EventType type, IEventListener* listener);
void Subscribe(const std::vector<EventType>& types, IEventListener* listener);
void Unsubscribe(EventType type, IEventListener* listener);
void UnsubscribeAll(IEventListener* listener);

// Lambda subscriptions (no interface needed)
uint32_t Subscribe(EventType type, EventCallback callback);
void Unsubscribe(uint32_t callbackId);

// Event emission
void Emit(std::unique_ptr<Event> event);              // Queue for later
void EmitImmediate(const Event& event);                // Dispatch now

// Helper emitters (type-safe)
void EmitWindowResize(NodeHandle source, uint32_t newW, uint32_t newH,
                      uint32_t oldW, uint32_t oldH);
void EmitShaderReload(NodeHandle source, const std::string& path,
                      NodeHandle shaderNode);
void EmitResourceInvalidation(NodeHandle source, NodeHandle invalidated,
                              const std::string& reason);

// Processing
void ProcessEvents();                                  // Process queue
void ClearQueue();                                     // Discard all queued
size_t GetQueuedEventCount() const;

// Debug/stats
EventStats GetStats() const;
void ResetStats();
void SetLoggingEnabled(bool enabled);
```

### Event Hierarchy

**Base Event**:
```cpp
struct Event {
    EventType type;
    uint64_t timestamp;
    NodeHandle sourceNode = 0;  // Which node emitted
    virtual ~Event() = default;
};
```

**Derived Events**:
```cpp
struct WindowResizeEvent : public Event {
    uint32_t newWidth, newHeight;
    uint32_t oldWidth, oldHeight;
};

struct ShaderReloadEvent : public Event {
    std::string shaderPath;
    NodeHandle shaderNodeHandle;
};

struct ResourceInvalidationEvent : public Event {
    NodeHandle invalidatedNode;
    std::string reason;
};
```

### IEventListener Interface

**Location**: `RenderGraph/Core/EventBus.h`

```cpp
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void OnEvent(const Event& event) = 0;
    virtual const std::string& GetListenerName() const = 0;
};
```

**NodeInstance Implementation**:
```cpp
class NodeInstance : public IEventListener {
public:
    void OnEvent(const Event& event) override;
    const std::string& GetListenerName() const override { return instanceName; }

    // Virtual handlers (override in specific nodes)
    virtual void OnWindowResize(const WindowResizeEvent& event) {}
    virtual void OnShaderReload(const ShaderReloadEvent& event) {}
    virtual void OnResourceInvalidated(const ResourceInvalidationEvent& event) {}

protected:
    EventBus* eventBus = nullptr;  // Set by RenderGraph
    bool needsRecompile = false;

    void EmitEvent(std::unique_ptr<Event> event);
};
```

## Event Flow Examples

### Example 1: Window Resize Cascade

```
User resizes window
    ↓
WindowNode::Execute() detects resize (WM_SIZE message)
    ↓
WindowNode emits WindowResize event
    eventBus->EmitWindowResize(handle, newW, newH, oldW, oldH)
    ↓
Event queued in EventBus
    ↓
Main Loop: renderGraph->ProcessEvents()
    ↓
EventBus dispatches to all WindowResize listeners
    ↓
SwapChainNode::OnWindowResize(event)
    - Marks self for recompile: MarkForRecompile()
    - Emits SwapChainInvalidated event
    ↓
FramebufferNode::OnEvent(SwapChainInvalidated)
    - Marks self for recompile
    ↓
RenderGraph collects dirty nodes: {SwapChain, Framebuffer}
    ↓
RenderGraph::RecompileDirtyNodes()
    - Sort by execution order
    - For each: Cleanup() → Compile()
    - Clear recompile flags
    ↓
Rendering continues with new dimensions
```

### Example 2: Shader Hot Reload

```
File watcher detects shader change (future feature)
    ↓
ShaderNode emits ShaderReloaded event
    eventBus->EmitShaderReload(handle, shaderPath, shaderHandle)
    ↓
Event queued
    ↓
ProcessEvents() dispatches
    ↓
GraphicsPipelineNode::OnShaderReload(event)
    - Marks self for recompile
    - Emits PipelineInvalidated
    ↓
DescriptorSetNode::OnEvent(PipelineInvalidated) (if layout changed)
    - Marks self for recompile
    ↓
RecompileDirtyNodes() rebuilds pipeline
    ↓
Next frame uses new shader
```

## Integration with RenderGraph

### RenderGraph Ownership

```cpp
class RenderGraph {
private:
    std::unique_ptr<EventBus> eventBus;
    std::set<NodeHandle> dirtyNodes;

public:
    RenderGraph() : eventBus(std::make_unique<EventBus>()) {
        #ifdef _DEBUG
        eventBus->SetLoggingEnabled(true);
        #endif
    }

    EventBus* GetEventBus() { return eventBus.get(); }
};
```

### Node Registration

```cpp
NodeHandle RenderGraph::AddNode(const std::string& typeName,
                                const std::string& instanceName) {
    // ... create instance ...

    // Set event bus reference
    instance->SetEventBus(eventBus.get());

    // Instance can now subscribe to events in Setup()

    return handle;
}
```

### Main Loop Integration

```cpp
void RenderLoop() {
    while (!shouldClose) {
        // 1. Process events (window messages, hot reload, etc.)
        renderGraph->ProcessEvents();

        // 2. Recompile invalidated nodes (resize, shader reload)
        renderGraph->RecompileDirtyNodes();

        // 3. Wait for previous frame (Vulkan sync via SynchronizationManager)
        syncManager->WaitForFrame(currentFrame);

        // 4. Execute graph
        renderGraph->Execute(currentFrame);

        // 5. Present
        presentNode->Present(currentFrame);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
}
```

### ProcessEvents Implementation

```cpp
void RenderGraph::ProcessEvents() {
    // Process all queued events
    eventBus->ProcessEvents();

    // Collect nodes marked for recompilation
    dirtyNodes.clear();
    for (auto& [handle, instance] : nodeInstances) {
        if (instance->NeedsRecompile()) {
            dirtyNodes.insert(handle);

            // Cascade: invalidate dependents
            InvalidateDependents(handle);
        }
    }
}

void RenderGraph::InvalidateDependents(NodeHandle nodeHandle) {
    NodeInstance* node = GetInstance(nodeHandle);

    for (auto& [handle, instance] : nodeInstances) {
        if (instance->DependsOn(node)) {
            NODE_LOG_DEBUG("Cascade invalidation: " + instance->GetInstanceName());
            instance->MarkForRecompile();
            dirtyNodes.insert(handle);
        }
    }
}
```

### RecompileDirtyNodes Implementation

```cpp
void RenderGraph::RecompileDirtyNodes() {
    if (dirtyNodes.empty()) return;

    NODE_LOG_INFO("Recompiling " + std::to_string(dirtyNodes.size()) + " nodes");

    // Sort by execution order (respect dependencies)
    std::vector<NodeInstance*> sortedDirty;
    for (NodeHandle handle : dirtyNodes) {
        sortedDirty.push_back(GetInstance(handle));
    }
    std::sort(sortedDirty.begin(), sortedDirty.end(),
              [](NodeInstance* a, NodeInstance* b) {
                  return a->GetExecutionOrder() < b->GetExecutionOrder();
              });

    // Cleanup old resources, then recompile
    for (NodeInstance* node : sortedDirty) {
        NODE_LOG_INFO("Recompiling: " + node->GetInstanceName());
        node->Cleanup();
        node->Compile();
        node->ClearRecompileFlag();
    }

    dirtyNodes.clear();
}
```

## Node Implementation Patterns

### Pattern 1: Subscribe in Setup()

```cpp
class SwapChainNode : public TypedNode<SwapChainNodeConfig> {
public:
    void Setup() override {
        // Subscribe after eventBus is set by RenderGraph
        if (eventBus) {
            eventBus->Subscribe(EventType::WindowResize, this);
        }
    }

    void OnWindowResize(const WindowResizeEvent& event) override {
        NODE_LOG_INFO("Handling resize: " + std::to_string(event.newWidth) +
                      "x" + std::to_string(event.newHeight));

        MarkForRecompile();

        // Emit downstream invalidation
        auto invalidEvent = std::make_unique<ResourceInvalidationEvent>(
            GetHandle(), "Swapchain invalidated by resize");
        invalidEvent->type = EventType::SwapChainInvalidated;
        EmitEvent(std::move(invalidEvent));
    }

    ~SwapChainNode() {
        // Unsubscribe on destruction
        if (eventBus) {
            eventBus->UnsubscribeAll(this);
        }
    }
};
```

### Pattern 2: Lambda Subscription (No Interface)

```cpp
class RenderGraph {
    void SetupInternalListeners() {
        // Subscribe to all resource invalidation events for logging
        eventBus->Subscribe(EventType::SwapChainInvalidated,
            [](const Event& event) {
                LOG_WARNING("SwapChain invalidated - full recompilation required");
            });
    }
};
```

### Pattern 3: Multiple Event Types

```cpp
class FramebufferNode : public TypedNode<FramebufferNodeConfig> {
public:
    void Setup() override {
        if (eventBus) {
            eventBus->Subscribe({
                EventType::SwapChainInvalidated,
                EventType::WindowResize
            }, this);
        }
    }

    void OnWindowResize(const WindowResizeEvent& event) override {
        MarkForRecompile();
    }

    void OnResourceInvalidated(const ResourceInvalidationEvent& event) override {
        if (event.type == EventType::SwapChainInvalidated) {
            MarkForRecompile();
        }
    }
};
```

## Synchronization vs. Events

**Events are NOT for Vulkan synchronization**. Use SynchronizationManager for that:

```cpp
// ❌ WRONG: Don't use events for Vulkan sync
eventBus->EmitImmediate(FrameCompleteEvent{...});

// ✅ CORRECT: Use SynchronizationManager
syncManager->WaitForFence(frameInFlightFence[currentFrame]);
```

**Event Use Cases**:
- Resource invalidation (resize, hot reload)
- Application state changes (camera, scene)
- Debug/profiling hooks

**SynchronizationManager Use Cases**:
- VkSemaphore signaling (image available, render complete)
- VkFence waiting (frame in flight)
- Pipeline barriers (layout transitions)

## Statistics and Debugging

### Enable Logging

```cpp
// In debug builds
#ifdef _DEBUG
renderGraph->GetEventBus()->SetLoggingEnabled(true);
#endif
```

**Output**:
```
[EventBus] main_window subscribed to event type 0 (WindowResize)
[EventBus] Event queued (type=0, queue_size=1)
[EventBus] Processing 1 queued events
[SwapChainNode] Handling resize: 1280x720
[EventBus] Event queued (type=1, queue_size=1)
[FramebufferNode] Cascade invalidation triggered
```

### Query Statistics

```cpp
auto stats = eventBus->GetStats();
std::cout << "Total emitted: " << stats.totalEmitted << "\n";
std::cout << "Total processed: " << stats.totalProcessed << "\n";
std::cout << "Queue size: " << stats.currentQueueSize << "\n";

for (const auto& [type, count] : stats.emittedByType) {
    std::cout << "Type " << static_cast<uint32_t>(type)
              << ": " << count << " events\n";
}
```

## Future Enhancements

### Thread-Safe Event Bus
For multi-threaded graph execution:
```cpp
std::mutex queueMutex;
std::mutex listenerMutex;

void EventBus::Emit(std::unique_ptr<Event> event) {
    std::lock_guard<std::mutex> lock(queueMutex);
    eventQueue.push(std::move(event));
}
```

### Event Filtering
```cpp
// Subscribe with predicate filter
eventBus->Subscribe(EventType::WindowResize, this,
    [](const Event& e) {
        auto& resize = static_cast<const WindowResizeEvent&>(e);
        return resize.newWidth > 800; // Only large windows
    });
```

### Event Priority
```cpp
struct Event {
    EventType type;
    uint64_t timestamp;
    uint32_t priority = 0; // Higher = process first
};

// Priority queue instead of FIFO queue
std::priority_queue<std::unique_ptr<Event>,
                   std::vector<std::unique_ptr<Event>>,
                   EventComparator> eventQueue;
```

## Implementation Checklist

- [ ] Create `RenderGraph/Core/EventBus.h`
- [ ] Create `RenderGraph/Core/EventBus.cpp`
- [ ] Add `IEventListener` interface to `NodeInstance.h`
- [ ] Implement `NodeInstance::OnEvent()` dispatcher
- [ ] Add `EventBus* eventBus` member to `NodeInstance`
- [ ] Modify `RenderGraph::AddNode()` to call `SetEventBus()`
- [ ] Implement `RenderGraph::ProcessEvents()`
- [ ] Implement `RenderGraph::RecompileDirtyNodes()`
- [ ] Add `NeedsRecompile()`, `MarkForRecompile()` to `NodeInstance`
- [ ] Update `WindowNode` to emit `WindowResize` events
- [ ] Update `SwapChainNode` to handle resize and emit invalidation
- [ ] Update `FramebufferNode` to handle swapchain invalidation
- [ ] Integrate event processing into main render loop
- [ ] Add unit tests for event dispatch and cascade invalidation
- [ ] Document event-driven patterns in memory bank

## Summary

The EventBus provides:
1. **Centralized communication** - single event channel for entire graph
2. **Type safety** - strongly-typed event payloads
3. **Decoupling** - nodes don't need direct references
4. **Automatic cascade** - invalidation propagates through dependencies
5. **Safe processing** - events processed between frames, not during execution
6. **Debuggability** - statistics, logging, introspection

This architecture prevents event spaghetti while maintaining flexibility for complex event-driven rendering scenarios.