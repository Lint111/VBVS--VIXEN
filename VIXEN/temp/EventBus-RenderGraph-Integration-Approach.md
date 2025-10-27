# Event Bus Integration into RenderGraph: Detailed Approach

## Executive Summary

This document outlines a comprehensive approach to integrate the EventBus system into the RenderGraph architecture. The integration enables:

- **Event-driven resource invalidation** (window resize, shader hot-reload)
- **Decoupled node communication** (publish-subscribe model)
- **Automatic cascade recompilation** (dependency-aware invalidation)
- **Worker thread integration** (async compilation, resource loading)
- **Zero-stutter hot-reload** (background work with main thread safety)

**Integration Complexity**: Medium
**Estimated Lines Changed**: ~500-800 (new code + modifications)
**Breaking Changes**: Minimal (additive architecture)
**Performance Impact**: Negligible (event processing at safe points)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Current State Analysis](#2-current-state-analysis)
3. [Integration Strategy](#3-integration-strategy)
4. [Detailed Implementation Plan](#4-detailed-implementation-plan)
5. [Event Message Catalog](#5-event-message-catalog)
6. [Node Integration Patterns](#6-node-integration-patterns)
7. [Main Loop Integration](#7-main-loop-integration)
8. [Hot-Reload Implementation](#8-hot-reload-implementation)
9. [Migration Guide](#9-migration-guide)
10. [Testing Strategy](#10-testing-strategy)
11. [Future Enhancements](#11-future-enhancements)

---

## 1. Architecture Overview

### 1.1 System Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Application Layer                            │
│  ┌────────────┐  ┌──────────────┐  ┌────────────────────────┐       │
│  │FileWatcher │  │ WindowSystem │  │ ShaderCompilationQueue │       │
│  └─────┬──────┘  └──────┬───────┘  └────────┬───────────────┘       │
└────────┼────────────────┼──────────────────┼─────────────────────────┘
         │                │                  │
         │ Publish        │ Publish          │ SubmitWork
         ▼                ▼                  ▼
┌──────────────────────────────────────────────────────────────────────┐
│                          EventBus Library                             │
│  ┌────────────────────────────────────────────────────────────┐     │
│  │  MessageBus (Thread-Safe Queue)                            │     │
│  │    - Subscription management by message type               │     │
│  │    - FIFO queue for async processing                       │     │
│  │    - Statistics tracking                                   │     │
│  └────────────────┬───────────────────────────────────────────┘     │
│                   │                                                   │
│  ┌────────────────┴───────────────────────────────────────────┐     │
│  │  WorkerThreadBridge<TResult>                               │     │
│  │    - Background thread pool                                │     │
│  │    - Work queue + result publishing                        │     │
│  │    - Exception-safe worker lifecycle                       │     │
│  └────────────────────────────────────────────────────────────┘     │
└────────────────────────┬─────────────────────────────────────────────┘
                         │
                         │ ProcessMessages()
                         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         RenderGraph Layer                             │
│  ┌────────────────────────────────────────────────────────────┐     │
│  │  RenderGraph                                               │     │
│  │    - Owns MessageBus* (injected)                           │     │
│  │    - Subscribes to: CleanupRequested, WindowResized, etc.  │     │
│  │    - Processes events → marks nodes dirty                  │     │
│  │    - Recompiles dirty nodes at safe points                 │     │
│  └────────────────┬───────────────────────────────────────────┘     │
│                   │                                                   │
│                   │ InjectEventBus(eventBus)                          │
│                   ▼                                                   │
│  ┌────────────────────────────────────────────────────────────┐     │
│  │  NodeInstance (Base Class)                                 │     │
│  │    - MessageBus* messageBus (nullable)                     │     │
│  │    - SubscriptionID[] subscriptions                        │     │
│  │    - bool needsRecompile flag                              │     │
│  │    - Virtual OnEvent() handlers                            │     │
│  └────────────────┬───────────────────────────────────────────┘     │
│                   │                                                   │
│                   │ Inheritance                                       │
│                   ▼                                                   │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐     │
│  │ SwapChainNode│ WindowNode   │ PipelineNode │ GeometryNode │     │
│  │              │              │              │              │     │
│  │ - Subscribe  │ - Publish    │ - Subscribe  │ - Subscribe  │     │
│  │   WindowResize│  WindowResize│  ShaderReload│  CameraUpdate│     │
│  │              │              │              │              │     │
│  │ - Emit       │              │ - Emit       │              │     │
│  │   SwapChain  │              │   Pipeline   │              │     │
│  │   Invalidated│              │   Invalidated│              │     │
│  └──────────────┴──────────────┴──────────────┴──────────────┘     │
└──────────────────────────────────────────────────────────────────────┘
```

### 1.2 Message Flow Example: Window Resize

```
[1] User resizes window
        ↓
[2] WindowNode::Execute() detects WM_SIZE message
        ↓
[3] WindowNode publishes WindowResizedMessage
        messageBus->Publish(std::make_unique<WindowResizedMessage>(...))
        ↓
[4] Message queued (thread-safe)
        ↓
[5] Main loop: messageBus.ProcessMessages()
        ↓
[6] MessageBus dispatches to all subscribers
        ↓
[7] SwapChainNode receives message via subscription callback
        - Marks self: needsRecompile = true
        - Publishes SwapChainInvalidatedMessage
        ↓
[8] FramebufferNode receives SwapChainInvalidatedMessage
        - Marks self: needsRecompile = true
        ↓
[9] RenderGraph::RecompileDirtyNodes() called at safe point
        - Collects all nodes with needsRecompile = true
        - Topological sort (respect dependencies)
        - For each dirty node:
            1. node->CleanupImpl() (destroy old Vulkan resources)
            2. node->Compile() (recreate with new dimensions)
            3. node->RegisterCleanup() (update cleanup stack)
            4. needsRecompile = false
        ↓
[10] Next frame renders with updated swapchain/framebuffers
```

### 1.3 Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **MessageBus Ownership** | Injected into RenderGraph (not owned) | Allows shared bus across systems (ShaderManagement, ResourceManagement) |
| **Node Event Access** | Optional (MessageBus* can be nullptr) | Nodes work standalone for testing, events are opt-in feature |
| **Subscription Lifetime** | NodeInstance manages SubscriptionIDs | Automatic unsubscribe in destructor prevents dangling callbacks |
| **Event Processing Timing** | Safe points only (between frames) | Prevents mid-frame Vulkan state corruption |
| **Dirty Flag Location** | Per-node (`needsRecompile` bool) | Enables granular recompilation, avoids full graph rebuild |
| **Cleanup Integration** | Event-driven via CleanupRequestedMessage | Decouples application from graph topology, tag-based bulk ops |
| **Worker Thread Pattern** | WorkerThreadBridge<TResult> | Zero-stutter async compilation with main thread safety |
| **Subscription Keying** | Primary key by event message type (emT) with category & tag filters | Fast lookup by emT, then cheap bit-flag checks; tag checks as optional, more expensive filter |

---

## 2. Current State Analysis

### 2.1 RenderGraph Current Architecture

**Files:**
- `RenderGraph/Core/RenderGraph.h` - Main orchestrator
- `RenderGraph/Core/NodeInstance.h` - Base node class
- `RenderGraph/Core/TypedNodeInstance.h` - Type-safe CRTP wrapper
- `RenderGraph/Core/GraphTopology.h` - Dependency analysis
- `RenderGraph/Core/CleanupStack.h` - Resource cleanup

**Current Lifecycle:**
1. **Construction**: AddNode() → CreateInstance() → Setup()
2. **Compilation**: Compile() → 4-phase build (dependencies → resources → pipelines → execution order)
3. **Execution**: RenderFrame() → topological order traversal → Execute(cmdBuf)
4. **Cleanup**: ExecuteCleanup() → reverse dependency order

**Current Limitations:**
- ❌ No event-driven invalidation (must manually call CleanupSubgraph)
- ❌ No hot-reload support (shader changes require full restart)
- ❌ Tight coupling (application must know node names for cleanup)
- ❌ No async compilation (shader compilation blocks main thread)
- ❌ No cascade invalidation (window resize requires manual multi-node cleanup)

### 2.2 EventBus Current Status

**Implemented Libraries:**
- ✅ `EventBus/MessageBus.h` - Pub/sub message bus (thread-safe)
- ✅ `EventBus/Message.h` - Base message types
- ✅ `EventBus/WorkerThreadBridge.h` - Worker thread integration
- ✅ `ShaderManagement/ShaderEvents.h` - Shader-specific messages

**Message Types:**
- TextMessage (type 1)
- WorkerResultMessage (type 2)
- ShaderCompilation* (types 200-206)

**Integration Status:**
- ✅ EventBus library compiled and tested
- ✅ ShaderManagement uses EventBus for compilation events
- ❌ RenderGraph NOT integrated yet
- ❌ No GraphMessages.h (RenderGraph-specific messages)
- ❌ NodeInstance does not have MessageBus reference

---

## 3. Integration Strategy

### 3.1 Phased Rollout

**Phase 1: Foundation (Core Integration)**
- Add MessageBus injection to RenderGraph constructor
- Add `MessageBus* messageBus` to NodeInstance
- Implement `InjectEventBus()` in RenderGraph::AddNode()
- Create `RenderGraph/Core/GraphMessages.h` with core message types

**Phase 2: Basic Event Flow (Window Resize)**
- Implement WindowResizedMessage
- Modify WindowNode to publish resize events
- Implement SwapChainNode subscription + invalidation
- Add RenderGraph::RecompileDirtyNodes() method
- Integrate ProcessMessages() into main loop

**Phase 3: Cleanup Integration (Tag-Based)**
- Implement CleanupRequestedMessage + CleanupCompletedMessage
- Add RenderGraph subscription to CleanupRequested
- Implement HandleCleanupRequest() dispatcher
- Migrate existing CleanupSubgraph to event-driven

**Phase 4: Hot-Reload (Shader Pipeline)**
- Implement ShaderReloadedMessage
- Add PipelineNode subscription
- Integrate WorkerThreadBridge for async compilation
- Implement RM<VkPipeline> state tracking

**Phase 5: Advanced Features (Camera, Lighting)**
- Implement CameraUpdatedMessage
- Implement LightingChangedMessage
- Add GeometryNode subscriptions
- Optimize: Only recompile affected nodes

### 3.2 Backward Compatibility

**Non-Breaking Additions:**
- MessageBus parameter defaults to `nullptr` in RenderGraph constructor
- Nodes check `if (messageBus)` before publishing/subscribing
- Existing direct CleanupSubgraph() calls still work
- No changes to NodeType templates required

**Migration Path:**
```cpp
// Old code (still works)
RenderGraph graph(&registry, nullptr, &logger);
graph.CleanupSubgraph("SwapChain");

// New code (event-driven)
RenderGraph graph(&registry, &messageBus, &logger);
auto msg = CleanupRequestedMessage::ByTag(0, "swapchain-dependent");
messageBus.Publish(std::move(msg));
```

### 3.3 Testing Approach

**Unit Tests:**
- EventBus message dispatch correctness
- NodeInstance subscription lifecycle
- RecompileDirtyNodes topological correctness
- Cleanup request handling (by name, tag, type)

**Integration Tests:**
- Window resize → swapchain recreation
- Shader hot-reload → pipeline rebuild
- Tag-based bulk cleanup
- Worker thread → main thread message flow

**Manual Tests:**
- Resize window (visual validation)
- Edit shader file (live reload)
- Dynamic light addition/removal
- Performance profiling (event overhead)

---

## 4. Detailed Implementation Plan

### 4.1 File Structure Changes

**New Files:**
```
RenderGraph/Core/
    GraphMessages.h            # RenderGraph-specific messages (100-199)

RenderGraph/Core/
    EventIntegration.h         # Helper utilities for event-driven nodes
    EventIntegration.cpp

temp/
    EventBus-Integration-Notes.md    # This document
    EventBus-Testing-Scenarios.md    # Test cases
    EventBus-Migration-Checklist.md  # Step-by-step migration
```

**Modified Files:**
```
RenderGraph/Core/
    RenderGraph.h              # Add MessageBus*, subscription management
    RenderGraph.cpp            # ProcessEvents(), RecompileDirtyNodes()

    NodeInstance.h             # Add MessageBus*, needsRecompile, subscriptions
    NodeInstance.cpp           # InjectEventBus(), subscription helpers

source/RenderGraph/Nodes/
    WindowNode.h/.cpp          # Publish WindowResizedMessage
    SwapChainNode.h/.cpp       # Subscribe to WindowResized, publish invalidation
    FramebufferNode.h/.cpp     # Subscribe to SwapChainInvalidated
    PipelineNode.h/.cpp        # Subscribe to ShaderReloaded

main.cpp                       # Create MessageBus, inject, ProcessMessages()
```

### 4.2 Core Classes Modification

#### 4.2.1 RenderGraph.h Changes

```cpp
// RenderGraph/Core/RenderGraph.h

#pragma once

#include "EventBus/MessageBus.h"
#include "GraphMessages.h"
#include <set>

namespace Vixen::RenderGraph {

class RenderGraph {
public:
    // Constructor with optional MessageBus
    RenderGraph(
        NodeTypeRegistry* registry,
        EventBus::MessageBus* messageBus = nullptr,  // NEW: Optional event bus
        Logger* logger = nullptr
    );

    ~RenderGraph();

    // NEW: Event-driven methods
    void ProcessEvents();                 // Call once per frame
    void RecompileDirtyNodes();          // Recompile nodes marked dirty
    EventBus::MessageBus* GetMessageBus() { return messageBus; }

    // Existing methods (unchanged)
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);
    void ConnectNodes(NodeHandle from, uint32_t outSlot, NodeHandle to, uint32_t inSlot);
    void Compile();
    void Execute(VkCommandBuffer cmd);
    void RenderFrame();

    // Cleanup (existing + event-driven)
    void ExecuteCleanup();
    void CleanupSubgraph(const std::string& rootNodeName);  // Still supported
    void CleanupByTag(const std::string& tag);             // Still supported
    void CleanupByType(const std::string& typeName);       // Still supported

private:
    // NEW: Event handling
    void SetupEventSubscriptions();       // Subscribe to CleanupRequested, etc.
    void HandleCleanupRequest(const CleanupRequestedMessage& msg);
    void HandleWindowResize(const WindowResizedMessage& msg);

    // NEW: Dirty tracking
    std::set<NodeHandle> dirtyNodes;
    void MarkNodeDirty(NodeHandle handle);
    void CollectDirtyNodes();             // Scan all nodes for needsRecompile flag

    // Existing members
    NodeTypeRegistry* nodeTypeRegistry;
    EventBus::MessageBus* messageBus;     // NEW: Injected dependency (not owned)
    Logger* logger;

    std::unordered_map<NodeHandle, std::unique_ptr<NodeInstance>> nodeInstances;
    std::unordered_map<std::string, NodeHandle> nodeNameToHandle;
    std::unique_ptr<GraphTopology> topology;
    std::unique_ptr<CleanupStack> cleanupStack;

    std::vector<NodeHandle> executionOrder;
    bool isCompiled = false;

    // NEW: Subscription tracking
    std::vector<EventBus::SubscriptionID> graphSubscriptions;
};

} // namespace Vixen::RenderGraph
```

#### 4.2.2 NodeInstance.h Changes

```cpp
// RenderGraph/Core/NodeInstance.h

#pragma once

#include "EventBus/MessageBus.h"
#include "GraphMessages.h"
#include <vector>
#include <string>

namespace Vixen::RenderGraph {

class NodeInstance {
public:
    NodeInstance(const std::string& instanceName);
    virtual ~NodeInstance();

    // Lifecycle (unchanged)
    virtual void Setup() {}
    virtual void Compile() = 0;
    virtual void Execute(VkCommandBuffer cmd) = 0;

    // Cleanup (unchanged)
    void RegisterCleanup(const std::string& cleanupName, std::function<void()> callback);
    virtual void CleanupImpl() {}

    // NEW: Event integration
    void InjectEventBus(EventBus::MessageBus* bus);
    EventBus::MessageBus* GetMessageBus() const { return messageBus; }

    // NEW: Event publishing helpers
    void PublishMessage(std::unique_ptr<EventBus::Message> msg);
    void PublishImmediate(const EventBus::Message& msg);

    // NEW: Subscription helpers (called in derived Setup())
    EventBus::SubscriptionID Subscribe(EventBus::MessageType type, EventBus::MessageHandler handler);
    void Unsubscribe(EventBus::SubscriptionID id);
    void UnsubscribeAll();

    // NEW: Virtual event handlers (override in derived classes)
    virtual void OnWindowResize(const WindowResizedMessage& msg) {}
    virtual void OnShaderReload(const ShaderReloadedMessage& msg) {}
    virtual void OnSwapChainInvalidated(const SwapChainInvalidatedMessage& msg) {}
    virtual void OnCameraUpdate(const CameraUpdatedMessage& msg) {}

    // NEW: Dirty flag management
    bool NeedsRecompile() const { return needsRecompile; }
    void MarkForRecompile() { needsRecompile = true; }
    void ClearRecompileFlag() { needsRecompile = false; }

    // Existing methods (unchanged)
    const std::string& GetInstanceName() const { return instanceName; }
    NodeHandle GetHandle() const { return handle; }
    void SetHandle(NodeHandle h) { handle = h; }

    void AddTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    const std::vector<std::string>& GetTags() const { return tags; }

    // Resource management (unchanged)
    void SetInput(uint32_t slot, uint32_t index, Resource* resource);
    void SetOutput(uint32_t slot, uint32_t index, Resource* resource);
    Resource* GetInput(uint32_t slot, uint32_t index) const;
    Resource* GetOutput(uint32_t slot, uint32_t index) const;

protected:
    std::string instanceName;
    NodeHandle handle = 0;
    std::vector<std::string> tags;

    // NEW: Event bus integration
    EventBus::MessageBus* messageBus = nullptr;
    std::vector<EventBus::SubscriptionID> subscriptions;
    bool needsRecompile = false;

    // Resource storage (unchanged)
    std::vector<std::vector<Resource*>> inputs;
    std::vector<std::vector<Resource*>> outputs;

    // Cleanup (unchanged)
    std::vector<std::pair<std::string, std::function<void()>>> cleanupCallbacks;
};

} // namespace Vixen::RenderGraph
```

#### 4.2.3 GraphMessages.h (New File)

```cpp
// RenderGraph/Core/GraphMessages.h

#pragma once

#include "EventBus/Message.h"
#include "ResourceDescriptor.h"
#include <string>
#include <vector>
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief Message type IDs for RenderGraph events
 *
 * Range: 100-199 reserved for RenderGraph
 */
enum class GraphMessageType : uint32_t {
    // Resource Invalidation (100-119)
    WindowResized           = 100,
    SwapChainInvalidated    = 101,
    FramebufferInvalidated  = 102,
    PipelineInvalidated     = 103,
    DescriptorInvalidated   = 104,

    // Application State (120-139)
    CameraUpdated           = 120,
    LightingChanged         = 121,
    SceneChanged            = 122,
    MaterialChanged         = 123,

    // Graph Management (140-159)
    CleanupRequested        = 140,
    CleanupCompleted        = 141,
    NodeAdded               = 142,
    NodeRemoved             = 143,
    GraphRecompiled         = 144,

    // Debug/Profiling (160-179)
    FrameComplete           = 160,
    NodeExecutionStart      = 161,
    NodeExecutionEnd        = 162,
    CompilationStart        = 163,
    CompilationEnd          = 164
};

// ============================================================================
// Resource Invalidation Messages
// ============================================================================

/**
 * @brief Window resized - triggers swapchain recreation
 */
struct WindowResizedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::WindowResized);

    uint32_t newWidth;
    uint32_t newHeight;
    uint32_t oldWidth;
    uint32_t oldHeight;

    WindowResizedMessage(
        EventBus::SenderID sender,
        uint32_t newW,
        uint32_t newH,
        uint32_t oldW,
        uint32_t oldH
    )
        : Message(sender, TYPE)
        , newWidth(newW)
        , newHeight(newH)
        , oldWidth(oldW)
        , oldHeight(oldH) {}
};

/**
 * @brief SwapChain invalidated - triggers framebuffer recreation
 */
struct SwapChainInvalidatedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::SwapChainInvalidated);

    std::string swapChainNodeName;
    std::string reason;  // "Window resize", "Surface lost", etc.

    SwapChainInvalidatedMessage(
        EventBus::SenderID sender,
        std::string nodeName,
        std::string why
    )
        : Message(sender, TYPE)
        , swapChainNodeName(std::move(nodeName))
        , reason(std::move(why)) {}
};

/**
 * @brief Pipeline invalidated - triggers descriptor set updates
 */
struct PipelineInvalidatedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::PipelineInvalidated);

    std::string pipelineNodeName;
    std::string reason;  // "Shader reload", "Layout changed", etc.
    bool interfaceChanged;  // If true, descriptor sets need rebuild

    PipelineInvalidatedMessage(
        EventBus::SenderID sender,
        std::string nodeName,
        std::string why,
        bool changed = false
    )
        : Message(sender, TYPE)
        , pipelineNodeName(std::move(nodeName))
        , reason(std::move(why))
        , interfaceChanged(changed) {}
};

// ============================================================================
// Application State Messages
// ============================================================================

/**
 * @brief Camera updated - triggers uniform buffer update
 */
struct CameraUpdatedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::CameraUpdated);

    // Optional: Embed camera data directly
    // Or just notify and nodes pull from CameraSystem
    std::string cameraName;
    bool projectionChanged;  // If true, may need pipeline recreation
    bool transformChanged;   // If true, only uniform update

    CameraUpdatedMessage(
        EventBus::SenderID sender,
        std::string name,
        bool projChanged = false,
        bool transChanged = true
    )
        : Message(sender, TYPE)
        , cameraName(std::move(name))
        , projectionChanged(projChanged)
        , transformChanged(transChanged) {}
};

/**
 * @brief Lighting changed - triggers shadow map recreation or uniform update
 */
struct LightingChangedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::LightingChanged);

    enum class ChangeType {
        LightAdded,
        LightRemoved,
        LightMoved,
        LightIntensityChanged,
        LightColorChanged,
        GlobalAmbientChanged
    };

    ChangeType changeType;
    std::string lightName;
    uint32_t lightIndex;  // For array indexing

    LightingChangedMessage(
        EventBus::SenderID sender,
        ChangeType type,
        std::string name = "",
        uint32_t index = 0
    )
        : Message(sender, TYPE)
        , changeType(type)
        , lightName(std::move(name))
        , lightIndex(index) {}
};

// ============================================================================
// Graph Management Messages
// ============================================================================

/**
 * @brief Cleanup requested - event-driven subgraph cleanup
 */
struct CleanupRequestedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::CleanupRequested);

    enum class Scope {
        Specific,  // Cleanup specific node by name
        ByTag,     // Cleanup all nodes with tag
        ByType,    // Cleanup all nodes of type
        Full       // Full graph cleanup
    };

    Scope scope;
    std::optional<std::string> targetNodeName;  // For Specific
    std::optional<std::string> tag;             // For ByTag
    std::optional<std::string> typeName;        // For ByType
    std::string reason;                         // Debug/logging

    CleanupRequestedMessage(EventBus::SenderID sender, std::string nodeName)
        : Message(sender, TYPE)
        , scope(Scope::Specific)
        , targetNodeName(std::move(nodeName)) {}

    static std::unique_ptr<CleanupRequestedMessage> ByTag(
        EventBus::SenderID sender,
        std::string tagName,
        std::string reason = ""
    ) {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender, "");
        msg->scope = Scope::ByTag;
        msg->tag = std::move(tagName);
        msg->targetNodeName = std::nullopt;
        msg->reason = std::move(reason);
        return msg;
    }

    static std::unique_ptr<CleanupRequestedMessage> ByType(
        EventBus::SenderID sender,
        std::string type,
        std::string reason = ""
    ) {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender, "");
        msg->scope = Scope::ByType;
        msg->typeName = std::move(type);
        msg->targetNodeName = std::nullopt;
        msg->reason = std::move(reason);
        return msg;
    }

    static std::unique_ptr<CleanupRequestedMessage> Full(
        EventBus::SenderID sender,
        std::string reason = ""
    ) {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender, "");
        msg->scope = Scope::Full;
        msg->targetNodeName = std::nullopt;
        msg->reason = std::move(reason);
        return msg;
    }
};

/**
 * @brief Cleanup completed - feedback for debugging/logging
 */
struct CleanupCompletedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::CleanupCompleted);

    std::vector<std::string> cleanedNodes;
    size_t cleanedCount;

    CleanupCompletedMessage(EventBus::SenderID sender, std::vector<std::string> nodes)
        : Message(sender, TYPE)
        , cleanedNodes(std::move(nodes))
        , cleanedCount(cleanedNodes.size()) {}
};

/**
 * @brief Graph recompiled - full recompilation complete
 */
struct GraphRecompiledMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::GraphRecompiled);

    size_t nodeCount;
    std::chrono::milliseconds compilationTime;
    std::string reason;

    GraphRecompiledMessage(
        EventBus::SenderID sender,
        size_t count,
        std::chrono::milliseconds time,
        std::string why = ""
    )
        : Message(sender, TYPE)
        , nodeCount(count)
        , compilationTime(time)
        , reason(std::move(why)) {}
};

// ============================================================================
// Debug/Profiling Messages (Optional - for performance tracking)
// ============================================================================

/**
 * @brief Frame complete - for frame time tracking
 */
struct FrameCompleteMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE =
        static_cast<uint32_t>(GraphMessageType::FrameComplete);

    uint64_t frameNumber;
    std::chrono::microseconds frameTime;
    uint32_t drawCallCount;

    FrameCompleteMessage(
        EventBus::SenderID sender,
        uint64_t frame,
        std::chrono::microseconds time,
        uint32_t drawCalls
    )
        : Message(sender, TYPE)
        , frameNumber(frame)
        , frameTime(time)
        , drawCallCount(drawCalls) {}
};

} // namespace Vixen::RenderGraph
```

---

## 5. Event Message Catalog

### 5.1 Message Type Ranges

| Range | Owner | Purpose |
|-------|-------|---------|
| 0-99 | EventBus | Base types (Text, WorkerResult) |
| 100-199 | RenderGraph | Graph management, invalidation |
| 200-299 | ShaderManagement | Compilation events, hot-reload |
| 300-399 | ResourceManagement | Resource state changes |
| 400-499 | Application | Custom application events |

### 5.2 RenderGraph Messages (100-199)

#### Resource Invalidation (100-119)

| Type | Name | Trigger | Subscribers | Effect |
|------|------|---------|-------------|--------|
| 100 | WindowResized | Window WM_SIZE message | SwapChainNode | Recreate swapchain |
| 101 | SwapChainInvalidated | Swapchain recreation | FramebufferNode, PresentNode | Recreate framebuffers |
| 102 | FramebufferInvalidated | Framebuffer recreation | RenderPassNode | Recreate render passes |
| 103 | PipelineInvalidated | Shader hot-reload | DescriptorSetNode, GeometryNode | Rebind pipeline |
| 104 | DescriptorInvalidated | Texture reload | GeometryNode | Update descriptor sets |

#### Application State (120-139)

| Type | Name | Trigger | Subscribers | Effect |
|------|------|---------|-------------|--------|
| 120 | CameraUpdated | Camera movement | GeometryNode, ShadowNode | Update uniform buffers |
| 121 | LightingChanged | Light add/remove/move | ShadowNode, GeometryNode | Recreate shadow maps or update UBOs |
| 122 | SceneChanged | Scene graph modified | GeometryNode | Rebuild command buffers |
| 123 | MaterialChanged | Material property changed | GeometryNode | Update descriptor sets |

#### Graph Management (140-159)

| Type | Name | Trigger | Subscribers | Effect |
|------|------|---------|-------------|--------|
| 140 | CleanupRequested | Manual or event-driven | RenderGraph | Execute subgraph cleanup |
| 141 | CleanupCompleted | After cleanup | Application (logging) | Feedback/debugging |
| 142 | NodeAdded | Dynamic node creation | Application | Update UI/state |
| 143 | NodeRemoved | Dynamic node removal | Application | Update UI/state |
| 144 | GraphRecompiled | Full recompilation | Application | Profiling/logging |

#### Debug/Profiling (160-179)

| Type | Name | Trigger | Subscribers | Effect |
|------|------|---------|-------------|--------|
| 160 | FrameComplete | End of frame | Profiler | Track frame time |
| 161 | NodeExecutionStart | Node Execute() entry | Profiler | Node profiling |
| 162 | NodeExecutionEnd | Node Execute() exit | Profiler | Node profiling |
| 163 | CompilationStart | Compile() entry | Profiler | Track compile time |
| 164 | CompilationEnd | Compile() exit | Profiler | Track compile time |

### 5.3 Shader Management Messages (200-299)

| Type | Name | Purpose |
|------|------|---------|
| 200 | ShaderCompilationStarted | Async compilation began |
| 201 | ShaderCompilationProgress | Compilation progress update |
| 202 | ShaderCompilationCompleted | Compilation success (contains ShaderDataBundle) |
| 203 | ShaderCompilationFailed | Compilation error |
| 204 | SdiGenerated | SDI header generated |
| 205 | SdiRegistryUpdated | Central SDI registry updated |
| 206 | ShaderHotReloadReady | Hot-reload ready (interface compatibility checked) |

---

## 6. Node Integration Patterns

### 6.1 Pattern 1: Event Publisher (WindowNode)

**Use Case**: Publish events when state changes

```cpp
// source/RenderGraph/Nodes/WindowNode.h

#pragma once

#include "RenderGraph/Core/TypedNodeInstance.h"
#include "RenderGraph/Core/GraphMessages.h"

class WindowNode : public TypedNode<WindowNodeConfig> {
public:
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer cmd) override;

private:
    uint32_t currentWidth = 0;
    uint32_t currentHeight = 0;
};
```

```cpp
// source/RenderGraph/Nodes/WindowNode.cpp

void WindowNode::Execute(VkCommandBuffer cmd) {
    // Poll window messages
    MSG msg;
    while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_SIZE) {
            uint32_t newWidth = LOWORD(msg.lParam);
            uint32_t newHeight = HIWORD(msg.lParam);

            if (newWidth != currentWidth || newHeight != currentHeight) {
                // Publish resize event
                if (messageBus) {
                    auto resizeMsg = std::make_unique<WindowResizedMessage>(
                        GetHandle(),
                        newWidth,
                        newHeight,
                        currentWidth,
                        currentHeight
                    );

                    PublishMessage(std::move(resizeMsg));

                    LOG_INFO("Window resized: " + std::to_string(newWidth) +
                             "x" + std::to_string(newHeight));
                }

                currentWidth = newWidth;
                currentHeight = newHeight;
            }
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
```

### 6.2 Pattern 2: Event Subscriber (SwapChainNode)

**Use Case**: React to upstream events and cascade invalidation

```cpp
// source/RenderGraph/Nodes/SwapChainNode.h

#pragma once

#include "RenderGraph/Core/TypedNodeInstance.h"
#include "RenderGraph/Core/GraphMessages.h"

class SwapChainNode : public TypedNode<SwapChainNodeConfig> {
public:
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer cmd) override;

    // Event handlers
    void OnWindowResize(const WindowResizedMessage& msg) override;

private:
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};
```

```cpp
// source/RenderGraph/Nodes/SwapChainNode.cpp

void SwapChainNode::Setup() {
    // Subscribe to window resize events
    if (messageBus) {
        Subscribe(WindowResizedMessage::TYPE, [this](const EventBus::Message& msg) {
            OnWindowResize(static_cast<const WindowResizedMessage&>(msg));
            return true;  // Handled
        });
    }
}

void SwapChainNode::OnWindowResize(const WindowResizedMessage& msg) {
    LOG_INFO("SwapChainNode received resize: " + std::to_string(msg.newWidth) +
             "x" + std::to_string(msg.newHeight));

    // Mark self for recompilation
    MarkForRecompile();

    // Publish cascade invalidation
    if (messageBus) {
        auto invalidMsg = std::make_unique<SwapChainInvalidatedMessage>(
            GetHandle(),
            GetInstanceName(),
            "Window resized"
        );

        PublishMessage(std::move(invalidMsg));
    }
}

void SwapChainNode::Compile() {
    // Get device from input
    device = In<0, VkDevice>();

    // Recreate swapchain with new dimensions
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.imageExtent = {width, height};
    // ... other setup ...

    vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

    // Register cleanup
    RegisterCleanup("SwapChain", [this]() {
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    });

    // Output swapchain handle
    Out<0>(swapchain);

    ClearRecompileFlag();
}
```

### 6.3 Pattern 3: Multi-Event Subscriber (FramebufferNode)

**Use Case**: React to multiple invalidation sources

```cpp
// source/RenderGraph/Nodes/FramebufferNode.cpp

void FramebufferNode::Setup() {
    if (messageBus) {
        // Subscribe to swapchain invalidation
        Subscribe(SwapChainInvalidatedMessage::TYPE, [this](const EventBus::Message& msg) {
            OnSwapChainInvalidated(static_cast<const SwapChainInvalidatedMessage&>(msg));
            return true;
        });

        // Subscribe to window resize (direct)
        Subscribe(WindowResizedMessage::TYPE, [this](const EventBus::Message& msg) {
            OnWindowResize(static_cast<const WindowResizedMessage&>(msg));
            return true;
        });
    }
}

void FramebufferNode::OnSwapChainInvalidated(const SwapChainInvalidatedMessage& msg) {
    LOG_INFO("Framebuffer invalidated by: " + msg.reason);
    MarkForRecompile();
}

void FramebufferNode::OnWindowResize(const WindowResizedMessage& msg) {
    // Could handle resize directly OR rely on swapchain cascade
    // Choose based on whether framebuffer depends on window size directly
}
```

### 6.4 Pattern 4: Worker Thread Integration (PipelineNode)

**Use Case**: Async shader compilation with zero-stutter hot-reload

```cpp
// source/RenderGraph/Nodes/PipelineNode.h

#pragma once

#include "RenderGraph/Core/TypedNodeInstance.h"
#include "EventBus/WorkerThreadBridge.h"
#include "ShaderManagement/ShaderEvents.h"
#include "ResourceManagement/RM.h"

struct PipelineCompilationResult : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = 250;

    VkPipeline pipeline;
    bool success;
    std::string error;
};

class PipelineNode : public TypedNode<PipelineNodeConfig> {
public:
    PipelineNode(const std::string& name);
    ~PipelineNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer cmd) override;

    void OnShaderReload(const ShaderManagement::ShaderHotReloadReadyMessage& msg);

private:
    RM<VkPipeline> pipeline;
    std::unique_ptr<EventBus::WorkerThreadBridge<PipelineCompilationResult>> compilationBridge;

    void HandleCompilationResult(const PipelineCompilationResult& result);
};
```

```cpp
// source/RenderGraph/Nodes/PipelineNode.cpp

PipelineNode::PipelineNode(const std::string& name)
    : TypedNode(name) {}

PipelineNode::~PipelineNode() {
    // WorkerThreadBridge destructor waits for worker thread
}

void PipelineNode::Setup() {
    if (messageBus) {
        // Create worker thread bridge
        compilationBridge = std::make_unique<EventBus::WorkerThreadBridge<PipelineCompilationResult>>(
            messageBus
        );

        // Subscribe to shader hot-reload
        Subscribe(ShaderManagement::ShaderHotReloadReadyMessage::TYPE,
            [this](const EventBus::Message& msg) {
                OnShaderReload(static_cast<const ShaderManagement::ShaderHotReloadReadyMessage&>(msg));
                return true;
            }
        );

        // Subscribe to compilation results
        Subscribe(PipelineCompilationResult::TYPE,
            [this](const EventBus::Message& msg) {
                HandleCompilationResult(static_cast<const PipelineCompilationResult&>(msg));
                return true;
            }
        );
    }
}

void PipelineNode::Compile() {
    VkDevice device = In<0, VkDevice>();

    // Initial compilation (synchronous)
    VkPipeline vkPipeline = CreatePipelineSync(device);
    pipeline.Set(vkPipeline);
    pipeline.MarkReady();

    RegisterCleanup("Pipeline", [this, device]() {
        if (pipeline.Ready()) {
            vkDestroyPipeline(device, pipeline.Value(), nullptr);
            pipeline.Reset();
        }
    });

    Out<0>(pipeline.Value());
}

void PipelineNode::OnShaderReload(const ShaderManagement::ShaderHotReloadReadyMessage& msg) {
    LOG_INFO("Shader hot-reload detected: " + msg.uuid);

    if (msg.interfaceChanged) {
        LOG_WARNING("Interface changed - C++ recompilation required");
        return;  // Cannot hot-reload
    }

    // Mark pipeline outdated
    pipeline.MarkOutdated();
    pipeline.IncrementGeneration();

    // Submit async compilation (non-blocking)
    VkDevice device = In<0, VkDevice>();
    auto shaderBundle = msg.newBundle;  // Copy

    compilationBridge->SubmitWork(GetHandle(), [device, shaderBundle]() {
        PipelineCompilationResult result;
        result.sender = 0;

        try {
            result.pipeline = CompilePipelineFromBundle(device, shaderBundle);
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.error = e.what();
        }

        return result;
    });

    LOG_INFO("Async pipeline compilation started");
}

void PipelineNode::HandleCompilationResult(const PipelineCompilationResult& result) {
    if (!result.success) {
        LOG_ERROR("Pipeline compilation failed: " + result.error);
        pipeline.AddState(ResourceManagement::ResourceState::Failed);
        return;
    }

    LOG_INFO("Pipeline compilation complete");

    VkDevice device = In<0, VkDevice>();

    // Wait for GPU idle (ensure old pipeline not in use)
    vkDeviceWaitIdle(device);

    // Destroy old pipeline
    if (pipeline.Ready()) {
        vkDestroyPipeline(device, pipeline.Value(), nullptr);
    }

    // Set new pipeline
    pipeline.Set(result.pipeline);
    pipeline.MarkReady();
    pipeline.RemoveState(ResourceManagement::ResourceState::Outdated);

    // Output updated handle
    Out<0>(pipeline.Value());

    // Publish invalidation for downstream nodes
    if (messageBus) {
        auto invalidMsg = std::make_unique<PipelineInvalidatedMessage>(
            GetHandle(),
            GetInstanceName(),
            "Shader hot-reload",
            false  // Interface unchanged
        );
        PublishMessage(std::move(invalidMsg));
    }

    LOG_INFO("Pipeline hot-swapped successfully (generation: " +
             std::to_string(pipeline.GetGeneration()) + ")");
}

void PipelineNode::Execute(VkCommandBuffer cmd) {
    if (!pipeline.Ready()) {
        LOG_WARNING("Pipeline not ready, skipping execution");
        return;
    }

    if (pipeline.IsLocked()) {
        // Pipeline in-flight, use cached handle
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Value());
}
```

---

## 7. Main Loop Integration

### 7.1 Application Main Loop

```cpp
// main.cpp

#include "EventBus/MessageBus.h"
#include "RenderGraph/Core/RenderGraph.h"
#include "RenderGraph/Core/NodeTypeRegistry.h"
#include <chrono>

int main() {
    // 1. Create MessageBus (single instance shared across systems)
    EventBus::MessageBus messageBus;

    #ifdef _DEBUG
    messageBus.SetLoggingEnabled(true);
    #endif

    // 2. Create RenderGraph with MessageBus injection
    auto nodeRegistry = std::make_unique<NodeTypeRegistry>();
    RegisterAllNodeTypes(*nodeRegistry);  // DeviceNode, WindowNode, etc.

    auto renderGraph = std::make_unique<RenderGraph>(
        nodeRegistry.get(),
        &messageBus,  // Inject message bus
        logger.get()
    );

    // 3. Build graph
    BuildRenderGraph(*renderGraph);  // AddNode, ConnectNodes
    renderGraph->Compile();

    // 4. Main loop
    bool running = true;
    uint64_t frameNumber = 0;
    auto frameStartTime = std::chrono::steady_clock::now();

    while (running) {
        auto loopStart = std::chrono::steady_clock::now();

        // PHASE 1: Event Processing (safe point - between frames)
        messageBus.ProcessMessages();

        // PHASE 2: Recompile dirty nodes (invalidated by events)
        renderGraph->RecompileDirtyNodes();

        // PHASE 3: Wait for GPU (previous frame)
        // (Handled inside RenderFrame or by SynchronizationManager)

        // PHASE 4: Render frame
        renderGraph->RenderFrame();

        // PHASE 5: Publish frame complete (for profiling)
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - loopStart
        );

        auto frameCompleteMsg = std::make_unique<FrameCompleteMessage>(
            0,
            frameNumber,
            frameDuration,
            0  // drawCallCount (track separately)
        );
        messageBus.Publish(std::move(frameCompleteMsg));

        frameNumber++;

        // Check exit conditions
        // ...
    }

    // 5. Cleanup
    renderGraph->ExecuteCleanup();

    return 0;
}
```

### 7.2 RenderGraph::ProcessEvents() Implementation

```cpp
// RenderGraph/Core/RenderGraph.cpp

void RenderGraph::ProcessEvents() {
    // This is a no-op - actual processing happens in messageBus.ProcessMessages()
    // RenderGraph just needs to check dirty flags AFTER events are processed
    // (done in RecompileDirtyNodes)
}

void RenderGraph::RecompileDirtyNodes() {
    // Collect all nodes marked for recompilation
    std::vector<NodeInstance*> dirtyNodes;

    for (auto& [handle, instance] : nodeInstances) {
        if (instance->NeedsRecompile()) {
            dirtyNodes.push_back(instance.get());
        }
    }

    if (dirtyNodes.empty()) {
        return;  // Nothing to recompile
    }

    LOG_INFO("Recompiling " + std::to_string(dirtyNodes.size()) + " dirty nodes");

    // Sort by execution order (respect dependencies)
    std::sort(dirtyNodes.begin(), dirtyNodes.end(),
        [](NodeInstance* a, NodeInstance* b) {
            return a->GetExecutionOrder() < b->GetExecutionOrder();
        }
    );

    // Recompile each node
    auto compileStart = std::chrono::steady_clock::now();

    for (NodeInstance* node : dirtyNodes) {
        LOG_INFO("Recompiling: " + node->GetInstanceName());

        // Cleanup old resources
        node->CleanupImpl();

        // Recompile with new state
        node->Compile();

        // Clear dirty flag
        node->ClearRecompileFlag();
    }

    auto compileDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - compileStart
    );

    LOG_INFO("Recompilation complete (" + std::to_string(compileDuration.count()) + "ms)");

    // Publish recompilation complete event
    if (messageBus) {
        auto recompileMsg = std::make_unique<GraphRecompiledMessage>(
            0,
            dirtyNodes.size(),
            compileDuration,
            "Event-driven recompilation"
        );
        messageBus->Publish(std::move(recompileMsg));
    }
}
```

### 7.3 RenderGraph Event Subscriptions

```cpp
// RenderGraph/Core/RenderGraph.cpp

RenderGraph::RenderGraph(
    NodeTypeRegistry* registry,
    EventBus::MessageBus* bus,
    Logger* log
)
    : nodeTypeRegistry(registry)
    , messageBus(bus)
    , logger(log)
{
    topology = std::make_unique<GraphTopology>();
    cleanupStack = std::make_unique<CleanupStack>();

    if (messageBus) {
        SetupEventSubscriptions();
    }
}

void RenderGraph::SetupEventSubscriptions() {
    // Subscribe to cleanup requests
    auto cleanupSub = messageBus->Subscribe(
        CleanupRequestedMessage::TYPE,
        [this](const EventBus::Message& msg) {
            HandleCleanupRequest(static_cast<const CleanupRequestedMessage&>(msg));
            return true;
        }
    );
    graphSubscriptions.push_back(cleanupSub);

    // Subscribe to window resize (for logging/debugging)
    auto resizeSub = messageBus->Subscribe(
        WindowResizedMessage::TYPE,
        [this](const EventBus::Message& msg) {
            HandleWindowResize(static_cast<const WindowResizedMessage&>(msg));
            return true;
        }
    );
    graphSubscriptions.push_back(resizeSub);
}

void RenderGraph::HandleCleanupRequest(const CleanupRequestedMessage& msg) {
    LOG_INFO("Cleanup requested: " + msg.reason);

    std::vector<std::string> cleanedNodes;

    switch (msg.scope) {
        case CleanupRequestedMessage::Scope::Specific:
            if (msg.targetNodeName.has_value()) {
                CleanupSubgraph(msg.targetNodeName.value());
                cleanedNodes.push_back(msg.targetNodeName.value());
            }
            break;

        case CleanupRequestedMessage::Scope::ByTag:
            if (msg.tag.has_value()) {
                cleanedNodes = CleanupByTagImpl(msg.tag.value());
            }
            break;

        case CleanupRequestedMessage::Scope::ByType:
            if (msg.typeName.has_value()) {
                cleanedNodes = CleanupByTypeImpl(msg.typeName.value());
            }
            break;

        case CleanupRequestedMessage::Scope::Full:
            ExecuteCleanup();
            for (auto& [handle, instance] : nodeInstances) {
                cleanedNodes.push_back(instance->GetInstanceName());
            }
            break;
    }

    // Publish completion
    if (messageBus) {
        auto completionMsg = std::make_unique<CleanupCompletedMessage>(0, cleanedNodes);
        messageBus->Publish(std::move(completionMsg));
    }

    LOG_INFO("Cleanup complete: " + std::to_string(cleanedNodes.size()) + " nodes");
}

void RenderGraph::HandleWindowResize(const WindowResizedMessage& msg) {
    LOG_INFO("Graph received window resize: " +
             std::to_string(msg.newWidth) + "x" + std::to_string(msg.newHeight));

    // RenderGraph doesn't handle resize directly - nodes do via subscriptions
    // This is just for centralized logging/debugging
}

RenderGraph::~RenderGraph() {
    // Unsubscribe all graph-level subscriptions
    if (messageBus) {
        for (auto subID : graphSubscriptions) {
            messageBus->Unsubscribe(subID);
        }
    }
}
```

---

## 8. Hot-Reload Implementation

### 8.1 Shader Hot-Reload Flow

```
[1] File watcher detects shader file change
        ↓
[2] ShaderCompilationQueue::SubmitWork() (async compilation)
        ↓
[3] Worker thread compiles shader
        ↓
[4] Worker publishes ShaderCompilationCompletedMessage
        ↓
[5] ShaderManager receives completion, generates SDI
        ↓
[6] ShaderManager compares interface hash:
        - If unchanged: Publishes ShaderHotReloadReadyMessage
        - If changed: Publishes ShaderCompilationCompletedMessage (C++ rebuild needed)
        ↓
[7] PipelineNode receives ShaderHotReloadReadyMessage
        - Marks pipeline: pipeline.MarkOutdated()
        - Submits async pipeline compilation via WorkerThreadBridge
        ↓
[8] Worker thread compiles new pipeline
        ↓
[9] Worker publishes PipelineCompilationResult
        ↓
[10] PipelineNode receives result (main thread)
        - Waits for GPU idle (vkDeviceWaitIdle)
        - Destroys old pipeline
        - Sets new pipeline: pipeline.Set(newPipeline)
        - Publishes PipelineInvalidatedMessage
        ↓
[11] GeometryNode receives PipelineInvalidatedMessage
        - Rebinds pipeline in next Execute()
        ↓
[12] Next frame renders with hot-reloaded shader
```

**Zero-Stutter Guarantee**:
- Shader compilation happens on worker thread
- Pipeline compilation happens on worker thread
- Old pipeline remains bound during compilation
- Swap happens at safe point (between frames)
- No frame drops or stuttering

### 8.2 Interface Change Detection

```cpp
// ShaderManagement/ShaderManager.cpp

void ShaderManager::OnShaderCompilationComplete(const ShaderCompilationCompletedMessage& msg) {
    // Check if this is a hot-reload (UUID already exists)
    auto it = activeShaders.find(msg.bundle.uuid);

    if (it == activeShaders.end()) {
        // New shader - store and publish
        activeShaders[msg.bundle.uuid] = msg.bundle;
        return;
    }

    // Hot-reload - compare interface hash
    const auto& oldBundle = it->second;
    std::string oldHash = ComputeInterfaceHash(oldBundle);
    std::string newHash = ComputeInterfaceHash(msg.bundle);

    bool interfaceChanged = (oldHash != newHash);

    if (interfaceChanged) {
        LOG_WARNING("Shader interface changed - C++ recompilation required");
        LOG_WARNING("Old hash: " + oldHash);
        LOG_WARNING("New hash: " + newHash);
    }

    // Publish hot-reload ready
    auto hotReloadMsg = std::make_unique<ShaderHotReloadReadyMessage>(
        0,
        msg.bundle.uuid,
        msg.bundle,
        interfaceChanged,
        oldHash,
        newHash
    );
    messageBus->Publish(std::move(hotReloadMsg));

    // Update active shader
    activeShaders[msg.bundle.uuid] = msg.bundle;
}

std::string ShaderManager::ComputeInterfaceHash(const ShaderDataBundle& bundle) {
    // Hash descriptor set layouts, push constant ranges, vertex input attributes
    std::stringstream ss;

    // Descriptor sets
    for (const auto& setLayout : bundle.descriptorSetLayouts) {
        for (const auto& binding : setLayout.bindings) {
            ss << binding.binding << ":" << binding.descriptorType << ":" << binding.descriptorCount << ";";
        }
    }

    // Push constants
    for (const auto& range : bundle.pushConstantRanges) {
        ss << range.offset << ":" << range.size << ":" << range.stageFlags << ";";
    }

    // Vertex input (for vertex shaders)
    for (const auto& attrib : bundle.vertexInputAttributes) {
        ss << attrib.location << ":" << attrib.format << ":" << attrib.offset << ";";
    }

    // Compute simple hash (or use std::hash, SHA256, etc.)
    return std::to_string(std::hash<std::string>{}(ss.str()));
}
```

### 8.3 GPU Idle Wait Strategy

**Problem**: Cannot destroy old pipeline while GPU is using it

**Solutions**:

1. **Immediate Wait** (simplest, but may stutter):
```cpp
vkDeviceWaitIdle(device);  // Block until GPU idle
vkDestroyPipeline(device, oldPipeline, nullptr);
```

2. **Deferred Destruction** (zero-stutter):
```cpp
struct PendingDestruction {
    VkPipeline pipeline;
    uint64_t frameNumber;
};

std::vector<PendingDestruction> pendingDestructions;

// On hot-reload
pendingDestructions.push_back({oldPipeline, currentFrameNumber});

// Each frame
for (auto it = pendingDestructions.begin(); it != pendingDestructions.end();) {
    if (currentFrameNumber - it->frameNumber >= MAX_FRAMES_IN_FLIGHT) {
        // Safe to destroy (N frames have passed)
        vkDestroyPipeline(device, it->pipeline, nullptr);
        it = pendingDestructions.erase(it);
    } else {
        ++it;
    }
}
```

3. **Fence-Based** (optimal):
```cpp
// Record fence when pipeline last used
VkFence pipelineUsageFence = GetLastFrameFence();

// On hot-reload
VkResult result = vkWaitForFences(device, 1, &pipelineUsageFence, VK_TRUE, TIMEOUT);
if (result == VK_SUCCESS) {
    vkDestroyPipeline(device, oldPipeline, nullptr);
}
```

**Recommendation**: Use deferred destruction (solution 2) for true zero-stutter.

---

## 9. Migration Guide

### 9.1 Existing Code Migration

**Step 1: Add MessageBus to Application**

```cpp
// Before
int main() {
    auto renderGraph = std::make_unique<RenderGraph>(registry.get(), logger.get());
    // ...
}

// After
int main() {
    EventBus::MessageBus messageBus;
    auto renderGraph = std::make_unique<RenderGraph>(registry.get(), &messageBus, logger.get());
    // ...
}
```

**Step 2: Update Main Loop**

```cpp
// Before
while (running) {
    renderGraph->RenderFrame();
}

// After
while (running) {
    messageBus.ProcessMessages();        // NEW: Process events
    renderGraph->RecompileDirtyNodes();  // NEW: Recompile invalidated nodes
    renderGraph->RenderFrame();
}
```

**Step 3: Migrate Direct Cleanup Calls**

```cpp
// Before
void OnWindowResize(uint32_t width, uint32_t height) {
    renderGraph->CleanupSubgraph("SwapChain");
    renderGraph->CleanupSubgraph("Framebuffer");
    renderGraph->Compile();
}

// After
void OnWindowResize(uint32_t width, uint32_t height) {
    auto msg = CleanupRequestedMessage::ByTag(0, "swapchain-dependent",
                                               "Window resized");
    messageBus.Publish(std::move(msg));
    // Cleanup happens automatically in main loop
}
```

**Step 4: Update Nodes to Publish Events**

```cpp
// Before (WindowNode.cpp)
void WindowNode::Execute(VkCommandBuffer cmd) {
    // Handle resize manually in calling code
}

// After (WindowNode.cpp)
void WindowNode::Execute(VkCommandBuffer cmd) {
    if (resizeDetected) {
        auto msg = std::make_unique<WindowResizedMessage>(
            GetHandle(), newWidth, newHeight, oldWidth, oldHeight
        );
        PublishMessage(std::move(msg));
    }
}
```

**Step 5: Add Node Subscriptions**

```cpp
// Before (SwapChainNode.cpp)
void SwapChainNode::Setup() {
    // No event handling
}

// After (SwapChainNode.cpp)
void SwapChainNode::Setup() {
    if (messageBus) {
        Subscribe(WindowResizedMessage::TYPE, [this](const EventBus::Message& msg) {
            OnWindowResize(static_cast<const WindowResizedMessage&>(msg));
            return true;
        });
    }
}

void SwapChainNode::OnWindowResize(const WindowResizedMessage& msg) {
    MarkForRecompile();

    auto invalidMsg = std::make_unique<SwapChainInvalidatedMessage>(
        GetHandle(), GetInstanceName(), "Window resized"
    );
    PublishMessage(std::move(invalidMsg));
}
```

### 9.2 Gradual Adoption Strategy

**Phase 1: Passive Observability** (no behavioral changes)
- Inject MessageBus into RenderGraph
- Nodes publish events but don't subscribe
- Log events for debugging
- Verify event flow correctness

**Phase 2: Single Feature Migration** (window resize only)
- WindowNode publishes WindowResizedMessage
- SwapChainNode subscribes + marks dirty
- FramebufferNode subscribes + marks dirty
- Remove manual CleanupSubgraph calls
- Test window resize extensively

**Phase 3: Full Invalidation System** (all resource events)
- Add shader hot-reload events
- Add texture reload events
- Add pipeline invalidation cascade
- Migrate all manual cleanup to event-driven

**Phase 4: Worker Thread Integration** (async compilation)
- Add WorkerThreadBridge for shader compilation
- Add WorkerThreadBridge for pipeline compilation
- Implement deferred destruction
- Zero-stutter validation

**Phase 5: Advanced Features** (camera, lighting, profiling)
- Add camera update events
- Add lighting change events
- Add profiling events
- Dynamic node add/remove events

### 9.3 Backward Compatibility Checklist

- [ ] MessageBus parameter defaults to `nullptr` in RenderGraph constructor
- [ ] All `if (messageBus)` checks before publishing/subscribing
- [ ] Direct CleanupSubgraph() calls still functional
- [ ] Nodes work standalone without MessageBus
- [ ] No breaking changes to NodeType templates
- [ ] No breaking changes to Resource/ResourceVariant API
- [ ] Existing unit tests pass without modification
- [ ] Can disable events via config flag

---

## 10. Testing Strategy

### 10.1 Unit Tests

**EventBus Core Tests**:
```cpp
TEST(MessageBus, SubscribeAndPublish) {
    MessageBus bus;
    bool received = false;

    auto subID = bus.Subscribe(100, [&](const Message& msg) {
        received = true;
        return true;
    });

    auto msg = std::make_unique<Message>(0, 100);
    bus.Publish(std::move(msg));
    bus.ProcessMessages();

    EXPECT_TRUE(received);
}

TEST(MessageBus, Unsubscribe) {
    MessageBus bus;
    int callCount = 0;

    auto subID = bus.Subscribe(100, [&](const Message& msg) {
        callCount++;
        return true;
    });

    auto msg1 = std::make_unique<Message>(0, 100);
    bus.Publish(std::move(msg1));
    bus.ProcessMessages();
    EXPECT_EQ(callCount, 1);

    bus.Unsubscribe(subID);

    auto msg2 = std::make_unique<Message>(0, 100);
    bus.Publish(std::move(msg2));
    bus.ProcessMessages();
    EXPECT_EQ(callCount, 1);  // Not incremented
}

TEST(MessageBus, TypeFiltering) {
    MessageBus bus;
    int type100Count = 0;
    int type200Count = 0;

    bus.Subscribe(100, [&](const Message& msg) { type100Count++; return true; });
    bus.Subscribe(200, [&](const Message& msg) { type200Count++; return true; });

    bus.Publish(std::make_unique<Message>(0, 100));
    bus.Publish(std::make_unique<Message>(0, 200));
    bus.Publish(std::make_unique<Message>(0, 100));

    bus.ProcessMessages();

    EXPECT_EQ(type100Count, 2);
    EXPECT_EQ(type200Count, 1);
}
```

**RenderGraph Event Tests**:
```cpp
TEST(RenderGraph, EventInjection) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, &bus, nullptr);

    EXPECT_NE(graph.GetMessageBus(), nullptr);
}

TEST(RenderGraph, DirtyNodeRecompilation) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, &bus, nullptr);

    auto nodeHandle = graph.AddNode("TestNode", "Node1");
    auto* node = graph.GetInstanceByName("Node1");

    node->MarkForRecompile();
    EXPECT_TRUE(node->NeedsRecompile());

    graph.RecompileDirtyNodes();
    EXPECT_FALSE(node->NeedsRecompile());
}

TEST(RenderGraph, CleanupRequestHandling) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, &bus, nullptr);

    auto node1 = graph.AddNode("TestNode", "Node1");
    auto node2 = graph.AddNode("TestNode", "Node2");

    graph.GetInstanceByName("Node1")->AddTag("test-tag");
    graph.GetInstanceByName("Node2")->AddTag("test-tag");

    auto msg = CleanupRequestedMessage::ByTag(0, "test-tag");
    bus.Publish(std::move(msg));

    bool cleanupComplete = false;
    bus.Subscribe(CleanupCompletedMessage::TYPE, [&](const Message& m) {
        auto& completion = static_cast<const CleanupCompletedMessage&>(m);
        EXPECT_EQ(completion.cleanedCount, 2);
        cleanupComplete = true;
        return true;
    });

    bus.ProcessMessages();
    EXPECT_TRUE(cleanupComplete);
}
```

**Node Event Tests**:
```cpp
TEST(NodeInstance, EventBusInjection) {
    MessageBus bus;
    TestNode node("TestNode");

    node.InjectEventBus(&bus);
    EXPECT_NE(node.GetMessageBus(), nullptr);
}

TEST(NodeInstance, EventSubscription) {
    MessageBus bus;
    TestNode node("TestNode");
    node.InjectEventBus(&bus);

    bool received = false;
    node.Subscribe(100, [&](const Message& msg) {
        received = true;
        return true;
    });

    bus.Publish(std::make_unique<Message>(0, 100));
    bus.ProcessMessages();

    EXPECT_TRUE(received);
}

TEST(NodeInstance, AutoUnsubscribeOnDestruction) {
    MessageBus bus;
    int callCount = 0;

    {
        TestNode node("TestNode");
        node.InjectEventBus(&bus);
        node.Subscribe(100, [&](const Message& msg) {
            callCount++;
            return true;
        });

        bus.Publish(std::make_unique<Message>(0, 100));
        bus.ProcessMessages();
        EXPECT_EQ(callCount, 1);
    }  // node destroyed, auto-unsubscribe

    bus.Publish(std::make_unique<Message>(0, 100));
    bus.ProcessMessages();
    EXPECT_EQ(callCount, 1);  // Not incremented
}
```

### 10.2 Integration Tests

**Window Resize Test**:
```cpp
TEST(Integration, WindowResizeCascade) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RegisterAllNodeTypes(registry);

    RenderGraph graph(&registry, &bus, nullptr);

    auto windowNode = graph.AddNode("Window", "MainWindow");
    auto swapChainNode = graph.AddNode("SwapChain", "MainSwapChain");
    auto framebufferNode = graph.AddNode("Framebuffer", "MainFramebuffer");

    graph.ConnectNodes(windowNode, 0, swapChainNode, 0);  // Surface
    graph.ConnectNodes(swapChainNode, 0, framebufferNode, 0);  // SwapChain

    graph.Compile();

    // Track recompilations
    auto* swapChain = graph.GetInstanceByName("MainSwapChain");
    auto* framebuffer = graph.GetInstanceByName("MainFramebuffer");

    EXPECT_FALSE(swapChain->NeedsRecompile());
    EXPECT_FALSE(framebuffer->NeedsRecompile());

    // Publish resize event
    auto msg = std::make_unique<WindowResizedMessage>(0, 1920, 1080, 1280, 720);
    bus.Publish(std::move(msg));
    bus.ProcessMessages();

    // Both nodes should be dirty
    EXPECT_TRUE(swapChain->NeedsRecompile());
    EXPECT_TRUE(framebuffer->NeedsRecompile());

    // Recompile
    graph.RecompileDirtyNodes();

    EXPECT_FALSE(swapChain->NeedsRecompile());
    EXPECT_FALSE(framebuffer->NeedsRecompile());
}
```

**Shader Hot-Reload Test**:
```cpp
TEST(Integration, ShaderHotReload) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, &bus, nullptr);

    auto deviceNode = graph.AddNode("Device", "MainDevice");
    auto pipelineNode = graph.AddNode("Pipeline", "MainPipeline");

    graph.ConnectNodes(deviceNode, 0, pipelineNode, 0);
    graph.Compile();

    auto* pipeline = graph.GetInstanceByName("MainPipeline");
    EXPECT_FALSE(pipeline->NeedsRecompile());

    // Simulate shader hot-reload
    ShaderDataBundle newBundle;
    // ... populate bundle ...

    auto msg = std::make_unique<ShaderHotReloadReadyMessage>(
        0, "shader-uuid", newBundle, false, "hash1", "hash1"
    );
    bus.Publish(std::move(msg));
    bus.ProcessMessages();

    // Pipeline should trigger async recompilation
    // (In real test, wait for PipelineCompilationResult)
}
```

**Tag-Based Cleanup Test**:
```cpp
TEST(Integration, TagBasedCleanup) {
    MessageBus bus;
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, &bus, nullptr);

    auto shadow1 = graph.AddNode("ShadowMap", "Shadow_Light0");
    auto shadow2 = graph.AddNode("ShadowMap", "Shadow_Light1");
    auto shadow3 = graph.AddNode("ShadowMap", "Shadow_Light2");

    graph.GetInstanceByName("Shadow_Light0")->AddTag("shadow-maps");
    graph.GetInstanceByName("Shadow_Light1")->AddTag("shadow-maps");
    graph.GetInstanceByName("Shadow_Light2")->AddTag("shadow-maps");

    graph.Compile();

    // Cleanup all shadow maps
    auto msg = CleanupRequestedMessage::ByTag(0, "shadow-maps");
    bus.Publish(std::move(msg));

    bool cleanupComplete = false;
    bus.Subscribe(CleanupCompletedMessage::TYPE, [&](const Message& m) {
        auto& completion = static_cast<const CleanupCompletedMessage&>(m);
        EXPECT_EQ(completion.cleanedCount, 3);
        cleanupComplete = true;
        return true;
    });

    bus.ProcessMessages();
    EXPECT_TRUE(cleanupComplete);
}
```

### 10.3 Manual Testing Scenarios

**Scenario 1: Window Resize**
1. Launch application
2. Resize window multiple times
3. **Verify**: No crashes, swapchain recreates, framebuffers update
4. **Log Output**: Should show WindowResizedMessage, SwapChainInvalidatedMessage, recompilation

**Scenario 2: Shader Hot-Reload**
1. Launch application with shader file watcher
2. Edit shader file (change color, add calculation)
3. Save file
4. **Verify**: Shader recompiles on background thread, pipeline hot-swaps, visual update within 1-2 frames
5. **Log Output**: ShaderCompilationStarted → Progress → Completed → HotReloadReady → PipelineInvalidated

**Scenario 3: Interface Change Detection**
1. Launch application
2. Edit shader to add new uniform buffer binding
3. Save file
4. **Verify**: ShaderHotReloadReadyMessage has `interfaceChanged = true`, pipeline NOT hot-reloaded
5. **Log Output**: Warning about C++ recompilation needed

**Scenario 4: Bulk Cleanup**
1. Create graph with 10 shadow map nodes, all tagged "shadow-maps"
2. Publish `CleanupRequestedMessage::ByTag("shadow-maps")`
3. **Verify**: All 10 nodes cleaned, CleanupCompletedMessage shows count = 10

**Scenario 5: Zero-Stutter Validation**
1. Launch application with VSync enabled
2. Edit shader file during rendering
3. **Verify**: No frame drops, no stuttering, FPS remains stable
4. **Tools**: RenderDoc capture, GPU profiling

---

## 11. Future Enhancements

### 11.1 Async Compilation Queue

**Current**: Single WorkerThreadBridge per node
**Enhancement**: Centralized compilation queue with priority scheduling

```cpp
class CompilationQueue {
public:
    enum class Priority { Low, Normal, High, Critical };

    struct CompilationTask {
        Priority priority;
        std::function<void()> task;
        std::function<void(bool success)> onComplete;
    };

    void SubmitTask(CompilationTask task);
    void SetMaxWorkerThreads(uint32_t count);
    void PauseQueue();
    void ResumeQueue();
};
```

**Benefits**:
- Prioritize user-facing pipelines over background shaders
- Limit concurrent compilations (avoid CPU thrashing)
- Pause/resume during critical sections

### 11.2 Event Replay and Debugging

**Current**: Events processed and discarded
**Enhancement**: Event recording for debugging and testing

```cpp
class EventRecorder {
public:
    void EnableRecording(const std::string& outputPath);
    void DisableRecording();
    void ReplayEvents(const std::string& inputPath);
    void DumpEventLog(std::ostream& out);
};

// Usage
#ifdef _DEBUG
EventRecorder recorder;
recorder.EnableRecording("event_log.json");
#endif

// Later: Replay exact sequence for debugging
recorder.ReplayEvents("event_log.json");
```

**Benefits**:
- Reproduce bugs deterministically
- Test event-driven logic without full application
- Performance profiling (event overhead analysis)

### 11.3 Event Filtering and Throttling

**Problem**: High-frequency events (camera updates) can flood queue

**Enhancement**: Rate limiting and coalescing

```cpp
class EventThrottle {
public:
    void SetThrottleRate(MessageType type, std::chrono::milliseconds interval);
    void SetCoalescing(MessageType type, bool enabled);
};

// Example
throttle.SetThrottleRate(CameraUpdatedMessage::TYPE, 16ms);  // Max 60 Hz
throttle.SetCoalescing(WindowResizedMessage::TYPE, true);   // Coalesce rapid resizes
```

**Benefits**:
- Prevent event spam
- Reduce recompilation frequency
- Improve performance for high-frequency events

### 11.4 Conditional Event Propagation

**Current**: All subscribers receive all events
**Enhancement**: Predicate-based filtering

```cpp
bus.Subscribe(WindowResizedMessage::TYPE,
    [](const Message& msg) {
        auto& resize = static_cast<const WindowResizedMessage&>(msg);
        return resize.newWidth >= 1920;  // Only subscribe to large windows
    },
    [this](const Message& msg) {
        OnWindowResize(static_cast<const WindowResizedMessage&>(msg));
        return true;
    }
);
```

**Benefits**:
- Reduce unnecessary event handling
- Enable conditional recompilation
- More granular control

### 11.5 Multi-Threaded Graph Execution

**Current**: Single-threaded execution (topological order)
**Enhancement**: Parallel node execution with event synchronization

```cpp
// Execute independent nodes in parallel
class ParallelExecutor {
public:
    void Execute(const std::vector<NodeInstance*>& nodes);

private:
    ThreadPool workerPool;
    DependencyGraph depGraph;
};

// Synchronization via events
struct NodeExecutionCompleteMessage : public Message {
    NodeHandle nodeHandle;
    VkSemaphore completionSemaphore;
};
```

**Benefits**:
- Utilize multi-core CPUs
- Reduce frame time
- Enable parallel render pass recording

### 11.6 Event-Driven Profiling

**Current**: Manual profiling with external tools
**Enhancement**: Built-in event-based profiling

```cpp
class EventProfiler {
public:
    void Enable();
    void Disable();
    void DumpReport(std::ostream& out);

    struct EventStats {
        MessageType type;
        uint64_t count;
        std::chrono::microseconds totalTime;
        std::chrono::microseconds avgTime;
        std::chrono::microseconds maxTime;
    };

    std::vector<EventStats> GetStats() const;
};

// Usage
profiler.Enable();
// ... run application ...
profiler.DumpReport(std::cout);

// Output:
// Event Type | Count | Total Time | Avg Time | Max Time
// ----------------------------------------------------------------
// WindowResize       | 15    | 1.2ms      | 80us     | 200us
// ShaderReload       | 5     | 350ms      | 70ms     | 120ms
// PipelineInvalidate | 5     | 5ms        | 1ms      | 2ms
```

**Benefits**:
- Identify slow event handlers
- Optimize event-driven code
- Track recompilation overhead

---

## 12. Conclusion

### 12.1 Summary

This integration approach enables:

✅ **Event-driven resource invalidation** - Automatic cascade recompilation on resize, shader reload, etc.
✅ **Decoupled node communication** - Nodes publish/subscribe without direct references
✅ **Zero-stutter hot-reload** - Background worker threads for shader/pipeline compilation
✅ **Tag-based bulk operations** - Clean/invalidate multiple nodes with single event
✅ **Backward compatibility** - Existing code works without modification
✅ **Testable architecture** - Unit/integration tests for event flow
✅ **Future-proof design** - Extensible for multi-threading, profiling, advanced features

### 12.2 Key Architectural Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Window Resize** | Manual CleanupSubgraph("SwapChain") + Compile() | Automatic cascade via WindowResizedMessage |
| **Shader Hot-Reload** | Restart application | Zero-stutter background compilation + hot-swap |
| **Node Coupling** | Direct references (tight coupling) | Pub/sub (loose coupling) |
| **Bulk Cleanup** | Iterate all nodes manually | Single CleanupRequestedMessage::ByTag() |
| **Testing** | Full integration tests only | Unit test events, nodes independently |
| **Debugging** | printf/logging | Event logs, statistics, replay |

### 12.3 Implementation Estimate

| Phase | LOC (New) | LOC (Modified) | Effort (Days) |
|-------|-----------|----------------|---------------|
| Phase 1: Foundation | 200 | 100 | 1-2 |
| Phase 2: Basic Event Flow | 300 | 150 | 2-3 |
| Phase 3: Cleanup Integration | 150 | 50 | 1-2 |
| Phase 4: Hot-Reload | 400 | 100 | 3-5 |
| Phase 5: Advanced Features | 300 | 100 | 2-3 |
| **Total** | **1350** | **500** | **9-15** |

### 12.4 Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|-----------|
| Breaking existing code | Low | MessageBus optional, backward compatible |
| Event loop overhead | Low | ProcessMessages() at safe points, minimal locking |
| Debugging complexity | Medium | Event logging, statistics, replay tools |
| Thread safety issues | Medium | Careful mutex usage, deferred destruction |
| Over-engineering | Low | Phased rollout, only implement needed features |

### 12.5 Next Steps

1. **Review** this document with team
2. **Create** temp testing branch
3. **Implement** Phase 1 (Foundation)
4. **Test** basic event flow with unit tests
5. **Implement** Phase 2 (Window Resize)
6. **Validate** manually with window resizing
7. **Iterate** through remaining phases
8. **Document** event catalog in memory bank
9. **Cleanup** temp folder after integration complete
10. **Update** memory bank with new patterns

---

**Document Status**: Draft for Review
**Author**: Claude Code
**Date**: 2025-10-27
**Version**: 1.0
**Target Audience**: Development Team
**Temporary Location**: `temp/` (delete after integration)
