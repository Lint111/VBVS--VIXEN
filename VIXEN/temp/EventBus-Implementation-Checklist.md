# EventBus Integration Implementation Checklist

## Overview

This checklist provides a step-by-step implementation guide for integrating the EventBus system into the RenderGraph architecture with bit flag optimizations.

**Related Documents**:
- `EventBus-RenderGraph-Integration-Approach.md` - Full integration architecture and design
- `EventBus-Performance-Optimizations.md` - Bit flag filtering system details
- `documentation/EventBusArchitecture.md` - Original event bus design
- `documentation/EventBus-ResourceManagement-Integration.md` - Worker thread integration

**Estimated Total Time**: 9-15 days (see Phase estimates below)

---

## Phase 1: EventBus Core Enhancements (Foundation)

**Goal**: Add bit flag filtering to EventBus library
**Estimated Time**: 1-2 days
**Reference**: `EventBus-Performance-Optimizations.md` Section 4 & 5

### 1.1 Update EventBus/Message.h

- [x] Add `EventCategory` enum with 64-bit flags
  - [ ] System Events (bit 0-7)
  - [ ] Resource Invalidation (bit 8-15)
  - [ ] Application State (bit 16-23)
  - [ ] Graph Management (bit 24-31)
  - [ ] Shader Events (bit 32-39)
  - [ ] Debug/Profiling (bit 40-47)
  - [ ] User-Defined (bit 48-55)
  - [ ] Reserved (bit 56-63)
  - **Reference**: Section 3.1 "Bit Flag Hierarchy"

- [x] Add bit flag operators
  - [ ] `operator|(EventCategory, EventCategory)`
  - [ ] `operator&(EventCategory, EventCategory)`
  - [ ] `operator~(EventCategory)`
  - [ ] `operator|=(EventCategory&, EventCategory)`
  - [ ] `operator&=(EventCategory&, EventCategory)`
  - **Reference**: Section 4.1 "Updated Message.h" lines 60-82

- [x] Add helper functions
  - [ ] `HasCategory(EventCategory flags, EventCategory category)`
  - [ ] `HasAnyCategory(EventCategory flags, EventCategory categories)`
  - [ ] `HasAllCategories(EventCategory flags, EventCategory categories)`
  - **Reference**: Section 4.1 lines 84-97

- [x] Create `BaseEventMessage` struct
  - [ ] Add `EventCategory categoryFlags` member
  - [ ] Add `MessageType type` member
  - [ ] Add `SenderID sender` member
  - [ ] Add `uint64_t timestamp` member
  - [ ] Add constructor `BaseEventMessage(flags, type, sender)`
  - [ ] Add virtual destructor
  - [ ] Add `HasCategory()` helper methods
  - **Reference**: Section 4.1 lines 99-135

- [x] Update legacy `Message` struct for backward compatibility
  - [ ] Inherit from `BaseEventMessage`
  - [ ] Default `categoryFlags` to `EventCategory::System`
  - **Reference**: Section 4.1 lines 137-144

- [x] Update `TextMessage` and `WorkerResultMessage`
  - [ ] Inherit from `BaseEventMessage`
  - [ ] Add `static constexpr FLAGS` constant
  - **Reference**: Section 4.1 lines 146-171

### 1.2 Update EventBus/MessageBus.h

- [x] Update `MessageHandler` typedef
  - [x] Change signature to `std::function<bool(const BaseEventMessage&)>`
  - **Reference**: Section 5.1 line 18

- [x] Add `FilterMode` enum to private section
  - [ ] `All` - No filtering
  - [ ] `Type` - Exact type match
  - [ ] `Category` - Bit flag match
  - **Reference**: Section 5.1 lines 106-110

- [x] Update `Subscription` struct
  - [ ] Add `FilterMode mode` member
  - [ ] Add `EventCategory categoryFilter` member
  - [ ] Keep `MessageType typeFilter` member
  - **Reference**: Section 5.1 lines 112-118

- [x] Add new subscription methods (public)
  - [ ] `SubscriptionID SubscribeCategory(EventCategory category, MessageHandler handler)`
  - [ ] `SubscriptionID SubscribeCategories(EventCategory flags, MessageHandler handler)`
  - [ ] `SubscriptionID SubscribeAll(MessageHandler handler)`
  - **Reference**: Section 5.1 lines 48-74

### Subscription keying note: subscribe/unsubscribe by event message type (emT)

- Design: when publishers/subscribers register they use the correlating event message type (emT) as the primary key. All events still derive from `BaseEventMessage` and include `categoryFlags` (bit flags) and optional tags.
- Dispatch flow (fast path):
  1. Lookup subscribers by `MessageType emT` using an O(1) map (e.g. `unordered_map<MessageType, vector<Subscription*>>`).
 2. For each candidate subscription: perform a bit-flag check against `message.categoryFlags` (very cheap: single integer AND). If it passes, invoke handler.
 3. If the subscription uses tag-based filters, perform the tag check as a slower, secondary filter only for the remaining candidates.

- Rationale: using `emT` as the key keeps the dispatch set small (only subscribers registered for that type), then bit flags afford cheap broad-category filtering (e.g., "resource invalidation"), while tag checks remain available as a more expressive—but more expensive—filtering layer.

- API implications:
  - Keep `Subscribe(MessageType type, MessageHandler handler)` but also provide `SubscribeCategory(...)` and `SubscribeAll(...)` as earlier listed.
  - Provide `Unsubscribe(subscriptionID)` which will remove entries from both the `type` lookup and any `category` lookup tables.
  - Internally maintain both `typeSubscriptions` (by emT) and `categorySubscriptions` (by flag bit), and keep them in sync on subscribe/unsubscribe.

- Testing additions:
  - Unit test that `Subscribe(emT, handler)` only receives messages with matching emT and that category flags are respected.
  - Benchmark the dispatch path: emT lookup + bitflag filter vs category-only iteration.

- [x] Update `Publish()` signature
  - [x] Change parameter to `std::unique_ptr<BaseEventMessage>`
  - **Reference**: Section 5.1 line 82

- [x] Update `PublishImmediate()` signature
  - [x] Change parameter to `const BaseEventMessage&`
  - **Reference**: Section 5.1 line 87

- [ ] Add category lookup optimization (private)
  - [ ] `std::unordered_map<uint64_t, std::vector<Subscription*>> categoryLookup`
  - **Reference**: Section 5.1 line 127

- [ ] Update `Stats` struct
  - [ ] Add `uint64_t categoryFilterHits` member
  - [ ] Add `uint64_t typeFilterHits` member
  - [ ] Add `std::unordered_map<uint64_t, uint64_t> publishedByCategory`
  - **Reference**: Section 5.1 lines 97-103

- [ ] Add private helper method
  - [ ] `bool MatchesFilter(const Subscription& sub, const BaseEventMessage& message)`
  - **Reference**: Section 5.1 line 121

### 1.3 Update EventBus/MessageBus.cpp

- [ ] Implement `SubscribeCategory()`
  - [ ] Create subscription with `FilterMode::Category`
  - [ ] Add to `categoryLookup` hash map for fast dispatch
  - [ ] Return subscription ID
  - **Reference**: Section 5.2 lines 3-23

- [ ] Implement `SubscribeCategories()`
  - [ ] Create subscription with multiple category flags
  - [ ] Add to all matching category lookups (iterate bits)
  - [ ] Return subscription ID
  - **Reference**: Section 5.2 lines 25-47

- [ ] Implement `SubscribeAll()`
  - [ ] Create subscription with `FilterMode::All`
  - [ ] Set `categoryFilter = EventCategory::None`
  - [ ] Set `typeFilter = 0`
  - **Reference**: Section 5.1 line 73

- [ ] Update `DispatchMessage()`
  - [ ] Use `MatchesFilter()` for all subscriptions
  - [ ] Track statistics (categoryFilterHits, typeFilterHits)
  - [ ] Keep existing handler invocation logic
  - **Reference**: Section 5.2 lines 49-73

- [ ] Implement `MatchesFilter()`
  - [ ] Switch on `FilterMode`
  - [ ] `All`: return true
  - [ ] `Type`: check `typeFilter == message.type`
  - [ ] `Category`: use `HasAnyCategory(message.categoryFlags, sub.categoryFilter)`
  - [ ] Track statistics
  - **Reference**: Section 5.2 lines 75-101

- [ ] Update `Unsubscribe()`
  - [ ] Remove from `categoryLookup` if category subscription
  - [ ] Remove from `subscriptions` vector
  - **Reference**: Integration doc Section 4.2.3

### 1.4 Testing

- [ ] Unit test: Bit flag operators
  - [ ] Test `operator|`, `operator&`, `operator~`
  - [ ] Test `HasCategory()`, `HasAnyCategory()`, `HasAllCategories()`
  - **Reference**: Section 10.1 in Performance Optimizations doc

- [ ] Unit test: `SubscribeCategory()`
  - [ ] Publish message with `EventCategory::ResourceInvalidation`
  - [ ] Verify subscriber receives message
  - [ ] Verify statistics updated

- [ ] Unit test: `SubscribeCategories()`
  - [ ] Subscribe to `ResourceInvalidation | ShaderEvents`
  - [ ] Publish messages with each category
  - [ ] Verify both received

- [ ] Unit test: `SubscribeAll()`
  - [ ] Subscribe to all messages
  - [ ] Publish various message types
  - [ ] Verify all received

- [ ] Unit test: Filter mode isolation
  - [ ] Type subscription only receives exact type
  - [ ] Category subscription receives all matching categories
  - [ ] All subscription receives everything

- [ ] Benchmark: Dispatch performance
  - [ ] Measure 1000 messages with 100 subscriptions
  - [ ] Compare to baseline (type-only filtering)
  - [ ] Verify ~7x speedup
  - **Reference**: Section 8.1 "Benchmark Results"

---

## Phase 2: RenderGraph Message Updates

**Goal**: Update RenderGraph messages to use `BaseEventMessage` with bit flags
**Estimated Time**: 1 day
**Reference**: `EventBus-Performance-Optimizations.md` Section 7.1

### 2.1 Create RenderGraph/Core/GraphMessages.h

- [ ] Add includes
  - [ ] `#include "EventBus/Message.h"`
  - [ ] Standard library includes (string, vector, optional, chrono)

- [ ] Define message type enum (optional, for readability)
  - [ ] `enum class GraphMessageType : uint32_t`
  - [ ] Resource Invalidation (100-119)
  - [ ] Application State (120-139)
  - [ ] Graph Management (140-159)
  - [ ] Debug/Profiling (160-179)
  - **Reference**: Integration doc Section 4.2.3 lines 13-37

### 2.2 Implement Resource Invalidation Messages

- [ ] `WindowResizedMessage`
  - [ ] Inherit from `BaseEventMessage`
  - [ ] `static constexpr MessageType TYPE = 100`
  - [ ] `static constexpr EventCategory FLAGS = ResourceInvalidation | WindowResize`
  - [ ] Members: `newWidth`, `newHeight`, `oldWidth`, `oldHeight`
  - [ ] Constructor passing flags to base
  - **Reference**: Section 7.1 lines 20-32

- [ ] `SwapChainInvalidatedMessage`
  - [ ] TYPE = 101
  - [ ] FLAGS = `ResourceInvalidation | SwapChainInvalid`
  - [ ] Members: `swapChainNodeName`, `reason`
  - **Reference**: Section 7.1 lines 34-43

- [ ] `PipelineInvalidatedMessage`
  - [ ] TYPE = 103
  - [ ] FLAGS = `ResourceInvalidation | PipelineInvalid`
  - [ ] Members: `pipelineNodeName`, `reason`, `interfaceChanged`
  - **Reference**: Section 7.1 lines 45-57

- [ ] `FramebufferInvalidatedMessage`
  - [ ] TYPE = 102
  - [ ] FLAGS = `ResourceInvalidation | FramebufferInvalid`
  - [ ] Members: `framebufferNodeName`, `reason`

- [ ] `DescriptorInvalidatedMessage`
  - [ ] TYPE = 104
  - [ ] FLAGS = `ResourceInvalidation | DescriptorInvalid`
  - [ ] Members: `descriptorNodeName`, `reason`

### 2.3 Implement Application State Messages

- [ ] `CameraUpdatedMessage`
  - [ ] TYPE = 120
  - [ ] FLAGS = `ApplicationState | CameraUpdate`
  - [ ] Members: `cameraName`, `projectionChanged`, `transformChanged`
  - **Reference**: Section 7.1 lines 63-78

- [ ] `LightingChangedMessage`
  - [ ] TYPE = 121
  - [ ] FLAGS = `ApplicationState | LightingChange`
  - [ ] Nested `enum class ChangeType`
  - [ ] Members: `changeType`, `lightName`, `lightIndex`
  - **Reference**: Section 7.1 lines 80-100

- [ ] `SceneChangedMessage`
  - [ ] TYPE = 122
  - [ ] FLAGS = `ApplicationState | SceneChange`
  - [ ] Members: `sceneNodeName`, `changeType`

- [ ] `MaterialChangedMessage`
  - [ ] TYPE = 123
  - [ ] FLAGS = `ApplicationState | MaterialChange`
  - [ ] Members: `materialName`, `propertyName`, `valueChanged`

### 2.4 Implement Graph Management Messages

- [ ] `CleanupRequestedMessage`
  - [ ] TYPE = 140
  - [ ] FLAGS = `GraphManagement | CleanupRequest`
  - [ ] Nested `enum class Scope`
  - [ ] Members: `scope`, `targetNodeName`, `tag`, `typeName`, `reason`
  - [ ] Factory methods: `ByTag()`, `ByType()`, `Full()`
  - **Reference**: Section 7.1 lines 106-142

- [ ] `CleanupCompletedMessage`
  - [ ] TYPE = 141
  - [ ] FLAGS = `GraphManagement`
  - [ ] Members: `cleanedNodes`, `cleanedCount`
  - **Reference**: Section 7.1 lines 144-154

- [ ] `NodeAddedMessage`
  - [ ] TYPE = 142
  - [ ] FLAGS = `GraphManagement | NodeAddRemove`
  - [ ] Members: `nodeName`, `nodeType`

- [ ] `NodeRemovedMessage`
  - [ ] TYPE = 143
  - [ ] FLAGS = `GraphManagement | NodeAddRemove`
  - [ ] Members: `nodeName`, `nodeType`

- [ ] `GraphRecompiledMessage`
  - [ ] TYPE = 144
  - [ ] FLAGS = `GraphManagement | GraphRecompile`
  - [ ] Members: `nodeCount`, `compilationTime`, `reason`

### 2.5 Implement Debug/Profiling Messages (Optional)

- [ ] `FrameCompleteMessage`
  - [ ] TYPE = 160
  - [ ] FLAGS = `Debug | Profiling | FrameTiming`
  - [ ] Members: `frameNumber`, `frameTime`, `drawCallCount`
  - **Reference**: Integration doc Section 5.2

- [ ] `NodeExecutionStartMessage`
  - [ ] TYPE = 161
  - [ ] FLAGS = `Debug | Profiling | NodeProfiling`
  - [ ] Members: `nodeName`, `timestamp`

- [ ] `NodeExecutionEndMessage`
  - [ ] TYPE = 162
  - [ ] FLAGS = `Debug | Profiling | NodeProfiling`
  - [ ] Members: `nodeName`, `timestamp`, `duration`

### 2.6 Testing

- [ ] Unit test: Message category flags
  - [ ] Verify `WindowResizedMessage::FLAGS` has `ResourceInvalidation` bit set
  - [ ] Verify `WindowResizedMessage::FLAGS` has `WindowResize` bit set
  - [ ] Test `HasCategory()` method on message instances

- [ ] Unit test: Message construction
  - [ ] Create each message type
  - [ ] Verify `type`, `categoryFlags`, `sender`, `timestamp` set correctly
  - [ ] Verify polymorphic base pointer works

- [ ] Unit test: Factory methods
  - [ ] `CleanupRequestedMessage::ByTag()`
  - [ ] `CleanupRequestedMessage::ByType()`
  - [ ] `CleanupRequestedMessage::Full()`
  - [ ] Verify `scope` and optional fields set correctly

---

## Phase 3: RenderGraph Core Integration

**Goal**: Integrate MessageBus into RenderGraph and NodeInstance
**Estimated Time**: 2-3 days
**Reference**: `EventBus-RenderGraph-Integration-Approach.md` Section 4.2

### 3.1 Update RenderGraph/Core/RenderGraph.h

- [ ] Add includes
  - [ ] `#include "EventBus/MessageBus.h"`
  - [ ] `#include "GraphMessages.h"`
  - [ ] `#include <set>`

- [ ] Update constructor signature
  - [ ] Add parameter: `EventBus::MessageBus* messageBus = nullptr`
  - [ ] Make parameter optional (default nullptr for backward compat)
  - **Reference**: Integration doc Section 4.2.1 lines 14-18

- [ ] Add public methods
  - [ ] `void ProcessEvents()`
  - [ ] `void RecompileDirtyNodes()`
  - [ ] `EventBus::MessageBus* GetMessageBus()`
  - **Reference**: Integration doc Section 4.2.1 lines 21-23

- [ ] Add private members
  - [ ] `EventBus::MessageBus* messageBus` (injected, not owned)
  - [ ] `std::set<NodeHandle> dirtyNodes`
  - [ ] `std::vector<EventBus::SubscriptionID> graphSubscriptions`
  - **Reference**: Integration doc Section 4.2.1 lines 64-73

- [ ] Add private helper methods
  - [ ] `void SetupEventSubscriptions()`
  - [ ] `void HandleCleanupRequest(const CleanupRequestedMessage& msg)`
  - [ ] `void HandleWindowResize(const WindowResizedMessage& msg)`
  - [ ] `void MarkNodeDirty(NodeHandle handle)`
  - [ ] `void CollectDirtyNodes()`
  - **Reference**: Integration doc Section 4.2.1 lines 52-57

### 3.2 Update RenderGraph/Core/RenderGraph.cpp

- [ ] Update constructor implementation
  - [ ] Store `messageBus` pointer
  - [ ] Call `SetupEventSubscriptions()` if `messageBus != nullptr`
  - **Reference**: Integration doc Section 7.3 lines 3-18

- [ ] Implement `SetupEventSubscriptions()`
  - [ ] Subscribe to `CleanupRequestedMessage::TYPE`
  - [ ] Subscribe to `WindowResizedMessage::TYPE` (for logging)
  - [ ] Store subscription IDs in `graphSubscriptions`
  - [ ] Use category subscription: `SubscribeCategory(EventCategory::GraphManagement, handler)`
  - **Reference**: Integration doc Section 7.3 lines 20-40

- [ ] Implement `HandleCleanupRequest()`
  - [ ] Switch on `msg.scope`
  - [ ] `Specific`: Call `CleanupSubgraph(targetNodeName)`
  - [ ] `ByTag`: Call `CleanupByTagImpl(tag)`
  - [ ] `ByType`: Call `CleanupByTypeImpl(typeName)`
  - [ ] `Full`: Call `ExecuteCleanup()`
  - [ ] Publish `CleanupCompletedMessage` with cleaned node list
  - **Reference**: Integration doc Section 7.3 lines 42-78

- [ ] Implement `HandleWindowResize()`
  - [ ] Log resize event (centralized logging)
  - [ ] Optional: Track window dimensions for debugging
  - **Reference**: Integration doc Section 7.3 lines 80-85

- [ ] Implement `ProcessEvents()`
  - [ ] No-op (actual processing in `messageBus.ProcessMessages()`)
  - [ ] Or call `CollectDirtyNodes()` to scan all nodes
  - **Reference**: Integration doc Section 7.2 lines 3-6

- [ ] Implement `RecompileDirtyNodes()`
  - [ ] Collect all nodes with `needsRecompile = true`
  - [ ] Sort by execution order (topological)
  - [ ] For each dirty node:
    - [ ] Log recompilation
    - [ ] Call `node->CleanupImpl()`
    - [ ] Call `node->Compile()`
    - [ ] Call `node->ClearRecompileFlag()`
  - [ ] Publish `GraphRecompiledMessage` with statistics
  - **Reference**: Integration doc Section 7.2 lines 8-43

- [ ] Update `AddNode()` implementation
  - [ ] After creating instance, call `instance->InjectEventBus(messageBus)`
  - [ ] This allows nodes to subscribe in `Setup()`
  - **Reference**: Integration doc Section 3.1 lines 259-276

- [ ] Update destructor
  - [ ] Unsubscribe all graph-level subscriptions
  - [ ] `for (auto subID : graphSubscriptions) messageBus->Unsubscribe(subID)`
  - **Reference**: Integration doc Section 7.3 lines 87-93

### 3.3 Update RenderGraph/Core/NodeInstance.h

- [ ] Add includes
  - [ ] `#include "EventBus/MessageBus.h"`
  - [ ] `#include "GraphMessages.h"`

- [ ] Add public methods
  - [ ] `void InjectEventBus(EventBus::MessageBus* bus)`
  - [ ] `EventBus::MessageBus* GetMessageBus() const`
  - [ ] `void PublishMessage(std::unique_ptr<EventBus::BaseEventMessage> msg)`
  - [ ] `void PublishImmediate(const EventBus::BaseEventMessage& msg)`
  - **Reference**: Integration doc Section 4.2.2 lines 25-28

- [ ] Add subscription helper methods (public)
  - [ ] `EventBus::SubscriptionID Subscribe(EventBus::MessageType type, EventBus::MessageHandler handler)`
  - [ ] `EventBus::SubscriptionID SubscribeCategory(EventBus::EventCategory category, EventBus::MessageHandler handler)`
  - [ ] `void Unsubscribe(EventBus::SubscriptionID id)`
  - [ ] `void UnsubscribeAll()`
  - **Reference**: Integration doc Section 4.2.2 lines 30-34

- [ ] Add virtual event handler methods (public)
  - [ ] `virtual void OnWindowResize(const WindowResizedMessage& msg) {}`
  - [ ] `virtual void OnShaderReload(const ShaderManagement::ShaderHotReloadReadyMessage& msg) {}`
  - [ ] `virtual void OnSwapChainInvalidated(const SwapChainInvalidatedMessage& msg) {}`
  - [ ] `virtual void OnCameraUpdate(const CameraUpdatedMessage& msg) {}`
  - **Reference**: Integration doc Section 4.2.2 lines 36-40

- [ ] Add dirty flag management (public)
  - [ ] `bool NeedsRecompile() const`
  - [ ] `void MarkForRecompile()`
  - [ ] `void ClearRecompileFlag()`
  - **Reference**: Integration doc Section 4.2.2 lines 42-45

- [ ] Add protected members
  - [ ] `EventBus::MessageBus* messageBus = nullptr`
  - [ ] `std::vector<EventBus::SubscriptionID> subscriptions`
  - [ ] `bool needsRecompile = false`
  - **Reference**: Integration doc Section 4.2.2 lines 67-70

### 3.4 Update RenderGraph/Core/NodeInstance.cpp

- [ ] Implement `InjectEventBus()`
  - [ ] Store `messageBus` pointer
  - [ ] Do NOT subscribe here (wait for `Setup()`)
  - **Reference**: Integration doc Section 6.2 lines 3-5

- [ ] Implement `PublishMessage()`
  - [ ] Check `if (messageBus) messageBus->Publish(std::move(msg))`
  - **Reference**: Integration doc Section 6.1 lines 30-37

- [ ] Implement `PublishImmediate()`
  - [ ] Check `if (messageBus) messageBus->PublishImmediate(msg)`

- [ ] Implement `Subscribe(type, handler)`
  - [ ] Check `if (!messageBus) return 0`
  - [ ] Call `messageBus->Subscribe(type, handler)`
  - [ ] Store returned ID in `subscriptions` vector
  - [ ] Return ID
  - **Reference**: Integration doc Section 6.2 lines 7-12

- [ ] Implement `SubscribeCategory(category, handler)`
  - [ ] Check `if (!messageBus) return 0`
  - [ ] Call `messageBus->SubscribeCategory(category, handler)`
  - [ ] Store returned ID in `subscriptions` vector
  - [ ] Return ID
  - **Reference**: Performance doc Section 7.2

- [ ] Implement `Unsubscribe(id)`
  - [ ] Check `if (!messageBus) return`
  - [ ] Call `messageBus->Unsubscribe(id)`
  - [ ] Remove ID from `subscriptions` vector

- [ ] Implement `UnsubscribeAll()`
  - [ ] Check `if (!messageBus) return`
  - [ ] For each ID in `subscriptions`: `messageBus->Unsubscribe(id)`
  - [ ] Clear `subscriptions` vector
  - **Reference**: Integration doc Section 6.2 lines 70-77

- [ ] Update destructor
  - [ ] Call `UnsubscribeAll()` before cleanup
  - [ ] Prevents dangling callbacks
  - **Reference**: Integration doc Section 6.2 lines 79-84

- [ ] Implement dirty flag methods (inline)
  - [ ] `NeedsRecompile()`: `return needsRecompile`
  - [ ] `MarkForRecompile()`: `needsRecompile = true`
  - [ ] `ClearRecompileFlag()`: `needsRecompile = false`

### 3.5 Testing

- [ ] Unit test: RenderGraph MessageBus injection
  - [ ] Create RenderGraph with MessageBus
  - [ ] Verify `GetMessageBus() != nullptr`
  - [ ] Create RenderGraph without MessageBus
  - [ ] Verify `GetMessageBus() == nullptr`
  - **Reference**: Integration doc Section 10.1

- [ ] Unit test: NodeInstance EventBus injection
  - [ ] Create node instance
  - [ ] Call `InjectEventBus(&bus)`
  - [ ] Verify `GetMessageBus() != nullptr`

- [ ] Unit test: Node subscription lifecycle
  - [ ] Create node with event bus
  - [ ] Subscribe to message type
  - [ ] Publish message
  - [ ] Verify handler called
  - [ ] Destroy node
  - [ ] Publish message again
  - [ ] Verify handler NOT called (auto-unsubscribe)
  - **Reference**: Integration doc Section 10.1

- [ ] Unit test: Dirty flag management
  - [ ] Create node
  - [ ] Verify `NeedsRecompile() == false`
  - [ ] Call `MarkForRecompile()`
  - [ ] Verify `NeedsRecompile() == true`
  - [ ] Call `ClearRecompileFlag()`
  - [ ] Verify `NeedsRecompile() == false`

- [ ] Unit test: `RecompileDirtyNodes()`
  - [ ] Create graph with 3 nodes
  - [ ] Mark 2 nodes dirty
  - [ ] Call `RecompileDirtyNodes()`
  - [ ] Verify `Compile()` called on both dirty nodes
  - [ ] Verify dirty flags cleared
  - **Reference**: Integration doc Section 10.1

- [ ] Unit test: Cleanup request handling
  - [ ] Create graph with nodes tagged "test-tag"
  - [ ] Publish `CleanupRequestedMessage::ByTag("test-tag")`
  - [ ] Process messages
  - [ ] Verify `CleanupCompletedMessage` published
  - [ ] Verify correct node count
  - **Reference**: Integration doc Section 10.1

---

## Phase 4: Node Implementation (Window Resize)

**Goal**: Implement window resize cascade using events
**Estimated Time**: 1-2 days
**Reference**: `EventBus-RenderGraph-Integration-Approach.md` Section 6

### 4.1 Update WindowNode

- [ ] Update `source/RenderGraph/Nodes/WindowNode.h`
  - [ ] Add includes: `#include "RenderGraph/Core/GraphMessages.h"`
  - [ ] Add private members: `currentWidth`, `currentHeight`
  - **Reference**: Integration doc Section 6.1 lines 3-17

- [ ] Update `source/RenderGraph/Nodes/WindowNode.cpp`
  - [ ] In `Execute()`, detect window resize (WM_SIZE message)
  - [ ] Compare `newWidth/newHeight` to `currentWidth/currentHeight`
  - [ ] If changed, create `WindowResizedMessage`
  - [ ] Call `PublishMessage(std::move(msg))`
  - [ ] Update `currentWidth/currentHeight`
  - [ ] Log resize event
  - **Reference**: Integration doc Section 6.1 lines 19-48

### 4.2 Update SwapChainNode

- [ ] Update `source/RenderGraph/Nodes/SwapChainNode.h`
  - [ ] Add virtual override: `void OnWindowResize(const WindowResizedMessage& msg) override`
  - [ ] Add private members: `width`, `height`
  - **Reference**: Integration doc Section 6.2 lines 3-19

- [ ] Update `source/RenderGraph/Nodes/SwapChainNode.cpp`
  - [ ] Implement `Setup()` method
    - [ ] Subscribe to `EventCategory::ResourceInvalidation` (category-based)
    - [ ] Or subscribe to `WindowResizedMessage::TYPE` (type-specific)
    - [ ] Lambda handler calls `OnWindowResize()`
    - **Reference**: Integration doc Section 6.2 lines 21-30

  - [ ] Implement `OnWindowResize()`
    - [ ] Log resize event
    - [ ] Store new dimensions: `width = msg.newWidth`, `height = msg.newHeight`
    - [ ] Call `MarkForRecompile()`
    - [ ] Create `SwapChainInvalidatedMessage`
    - [ ] Call `PublishMessage()` to cascade invalidation
    - **Reference**: Integration doc Section 6.2 lines 32-44

  - [ ] Update `Compile()` method
    - [ ] Check `if (swapchain != VK_NULL_HANDLE)` destroy old swapchain
    - [ ] Create new swapchain with `width x height` dimensions
    - [ ] Register cleanup callback
    - [ ] Set output: `Out<0>(swapchain)`
    - [ ] Call `ClearRecompileFlag()`
    - **Reference**: Integration doc Section 6.2 lines 46-75

### 4.3 Update FramebufferNode

- [ ] Update `source/RenderGraph/Nodes/FramebufferNode.h`
  - [ ] Add virtual override: `void OnSwapChainInvalidated(const SwapChainInvalidatedMessage& msg) override`
  - [ ] Add virtual override: `void OnWindowResize(const WindowResizedMessage& msg) override`

- [ ] Update `source/RenderGraph/Nodes/FramebufferNode.cpp`
  - [ ] Implement `Setup()` method
    - [ ] Subscribe to `SwapChainInvalidatedMessage::TYPE`
    - [ ] Subscribe to `WindowResizedMessage::TYPE` (optional)
    - **Reference**: Integration doc Section 6.3 lines 3-17

  - [ ] Implement `OnSwapChainInvalidated()`
    - [ ] Log invalidation event
    - [ ] Call `MarkForRecompile()`
    - **Reference**: Integration doc Section 6.3 lines 19-22

  - [ ] Implement `OnWindowResize()` (optional)
    - [ ] Alternative to swapchain cascade
    - [ ] Call `MarkForRecompile()` if framebuffer depends on window size directly

  - [ ] Update `Compile()` method
    - [ ] Destroy old framebuffers if they exist
    - [ ] Create new framebuffers with swapchain images
    - [ ] Register cleanup callback
    - [ ] Call `ClearRecompileFlag()`

### 4.4 Testing

- [ ] Integration test: Window resize cascade
  - [ ] Create graph: WindowNode → SwapChainNode → FramebufferNode
  - [ ] Connect nodes via surface and swapchain resources
  - [ ] Compile graph
  - [ ] Publish `WindowResizedMessage`
  - [ ] Process messages
  - [ ] Verify SwapChainNode marked dirty
  - [ ] Verify FramebufferNode marked dirty
  - [ ] Call `RecompileDirtyNodes()`
  - [ ] Verify both nodes recompiled
  - **Reference**: Integration doc Section 10.2 "Window Resize Test"

- [ ] Manual test: Window resize
  - [ ] Launch application
  - [ ] Resize window multiple times
  - [ ] Verify no crashes
  - [ ] Verify rendering continues correctly
  - [ ] Check logs for event flow:
    - [ ] WindowResizedMessage published
    - [ ] SwapChainNode received message
    - [ ] SwapChainInvalidatedMessage published
    - [ ] FramebufferNode received message
    - [ ] Both nodes recompiled
  - **Reference**: Integration doc Section 10.3 "Scenario 1: Window Resize"

---

## Phase 5: Shader Hot-Reload Integration

**Goal**: Implement zero-stutter shader hot-reload with worker threads
**Estimated Time**: 3-5 days
**Reference**: `EventBus-RenderGraph-Integration-Approach.md` Section 6.4 & 8

### 5.1 Update ShaderManagement Events

- [ ] Verify `ShaderManagement/ShaderEvents.h` exists
  - [ ] Check message types 200-206 defined
  - [ ] Check all inherit from `BaseEventMessage` (update if needed)
  - [ ] Add `EventCategory::ShaderEvents` flags to all messages
  - **Reference**: Performance doc Section 7.1

- [ ] Update `ShaderCompilationCompletedMessage`
  - [ ] FLAGS = `ShaderEvents | ShaderCompilation`
  - [ ] Verify contains `ShaderDataBundle`

- [ ] Update `ShaderHotReloadReadyMessage`
  - [ ] FLAGS = `ShaderEvents | ShaderHotReload | ResourceInvalidation | PipelineInvalid`
  - [ ] Verify contains `interfaceChanged` flag
  - [ ] Verify contains old/new interface hashes
  - **Reference**: Existing ShaderEvents.h

### 5.2 Implement PipelineNode with Hot-Reload

- [ ] Update `source/RenderGraph/Nodes/PipelineNode.h`
  - [ ] Add includes:
    - [ ] `#include "EventBus/WorkerThreadBridge.h"`
    - [ ] `#include "ShaderManagement/ShaderEvents.h"`
    - [ ] `#include "ResourceManagement/RM.h"`
  - [ ] Define `PipelineCompilationResult` message (TYPE = 250)
  - [ ] Add members:
    - [ ] `RM<VkPipeline> pipeline`
    - [ ] `std::unique_ptr<EventBus::WorkerThreadBridge<PipelineCompilationResult>> compilationBridge`
  - [ ] Add virtual overrides:
    - [ ] `void OnShaderReload(const ShaderManagement::ShaderHotReloadReadyMessage& msg) override`
  - [ ] Add private method:
    - [ ] `void HandleCompilationResult(const PipelineCompilationResult& result)`
  - **Reference**: Integration doc Section 6.4 lines 3-34

- [ ] Update `source/RenderGraph/Nodes/PipelineNode.cpp`
  - [ ] Implement constructor
    - [ ] Call base class constructor
    - **Reference**: Integration doc Section 6.4 lines 38-39

  - [ ] Implement destructor
    - [ ] WorkerThreadBridge destructor auto-waits for worker thread
    - **Reference**: Integration doc Section 6.4 lines 41-42

  - [ ] Implement `Setup()` method
    - [ ] Create `WorkerThreadBridge` if `messageBus != nullptr`
    - [ ] Subscribe to `ShaderHotReloadReadyMessage::TYPE`
    - [ ] Subscribe to `PipelineCompilationResult::TYPE`
    - **Reference**: Integration doc Section 6.4 lines 44-63

  - [ ] Implement `Compile()` method (initial compilation)
    - [ ] Get device from input: `In<0, VkDevice>()`
    - [ ] Create pipeline synchronously
    - [ ] Store in `RM<VkPipeline>`: `pipeline.Set(vkPipeline)`
    - [ ] Mark ready: `pipeline.MarkReady()`
    - [ ] Register cleanup callback
    - [ ] Set output: `Out<0>(pipeline.Value())`
    - **Reference**: Integration doc Section 6.4 lines 65-78

  - [ ] Implement `OnShaderReload()` method
    - [ ] Log shader reload event
    - [ ] Check `msg.interfaceChanged`:
      - [ ] If true: Log warning, return (cannot hot-reload)
      - [ ] If false: Continue
    - [ ] Mark pipeline outdated: `pipeline.MarkOutdated()`
    - [ ] Increment generation: `pipeline.IncrementGeneration()`
    - [ ] Submit async compilation via `compilationBridge->SubmitWork()`
    - [ ] Lambda captures device and shader bundle
    - [ ] Lambda creates new pipeline, returns `PipelineCompilationResult`
    - [ ] Log async compilation started
    - **Reference**: Integration doc Section 6.4 lines 80-109

  - [ ] Implement `HandleCompilationResult()` method
    - [ ] Check `result.success`:
      - [ ] If false: Log error, mark `pipeline.AddState(Failed)`, return
    - [ ] Log compilation complete
    - [ ] Wait for GPU idle: `vkDeviceWaitIdle(device)` (or use deferred destruction)
    - [ ] Destroy old pipeline: `vkDestroyPipeline(device, pipeline.Value(), nullptr)`
    - [ ] Set new pipeline: `pipeline.Set(result.pipeline)`
    - [ ] Mark ready: `pipeline.MarkReady()`
    - [ ] Remove outdated state: `pipeline.RemoveState(Outdated)`
    - [ ] Set output: `Out<0>(pipeline.Value())`
    - [ ] Publish `PipelineInvalidatedMessage`
    - [ ] Log hot-swap success with generation number
    - **Reference**: Integration doc Section 6.4 lines 111-144

  - [ ] Update `Execute()` method
    - [ ] Check `if (!pipeline.Ready())` log warning, return
    - [ ] Check `if (pipeline.IsLocked())` use cached handle
    - [ ] Bind pipeline: `vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Value())`
    - **Reference**: Integration doc Section 6.4 lines 146-154

### 5.3 Implement ShaderManager Interface Hash Detection

- [ ] Update `ShaderManagement/ShaderManager.h`
  - [ ] Add private method: `std::string ComputeInterfaceHash(const ShaderDataBundle& bundle)`
  - [ ] Add private member: `std::unordered_map<std::string, ShaderDataBundle> activeShaders`

- [ ] Update `ShaderManagement/ShaderManager.cpp`
  - [ ] Implement `OnShaderCompilationComplete()` handler
    - [ ] Check if UUID already exists in `activeShaders`
    - [ ] If new shader: Store and return
    - [ ] If hot-reload: Compare interface hashes
    - [ ] Publish `ShaderHotReloadReadyMessage` with `interfaceChanged` flag
    - **Reference**: Integration doc Section 8.2 lines 3-30

  - [ ] Implement `ComputeInterfaceHash()`
    - [ ] Hash descriptor set layouts (binding, type, count)
    - [ ] Hash push constant ranges (offset, size, stage flags)
    - [ ] Hash vertex input attributes (location, format, offset)
    - [ ] Return hash string (std::hash or SHA256)
    - **Reference**: Integration doc Section 8.2 lines 32-53

### 5.4 Deferred Destruction System (Optional - Zero Stutter)

- [ ] Create `RenderGraph/Core/DeferredDestruction.h`
  - [ ] Define `struct PendingDestruction<T>`
  - [ ] Members: `T handle`, `uint64_t frameNumber`, `VkDevice device`
  - [ ] Define `class DeferredDestructionQueue`
  - [ ] Methods: `Add()`, `ProcessFrame()`, `Flush()`

- [ ] Implement deferred destruction in PipelineNode
  - [ ] Replace `vkDeviceWaitIdle()` with `deferredQueue.Add(oldPipeline, currentFrame)`
  - [ ] In main loop, call `deferredQueue.ProcessFrame(currentFrame)`
  - [ ] Destroy pipelines after N frames have passed
  - **Reference**: Integration doc Section 8.3 "Deferred Destruction"

### 5.5 Testing

- [ ] Unit test: Interface hash stability
  - [ ] Create two identical `ShaderDataBundle` objects
  - [ ] Compute hash for both
  - [ ] Verify hashes match

- [ ] Unit test: Interface hash change detection
  - [ ] Create `ShaderDataBundle` with descriptor set layout A
  - [ ] Compute hash
  - [ ] Modify descriptor set layout (add binding)
  - [ ] Compute new hash
  - [ ] Verify hashes differ

- [ ] Integration test: Shader hot-reload flow
  - [ ] Create graph with PipelineNode
  - [ ] Compile graph
  - [ ] Publish `ShaderHotReloadReadyMessage` (interfaceChanged = false)
  - [ ] Process messages
  - [ ] Verify PipelineNode triggered async compilation
  - [ ] Publish `PipelineCompilationResult` (simulated worker result)
  - [ ] Process messages
  - [ ] Verify `HandleCompilationResult()` called
  - [ ] Verify `PipelineInvalidatedMessage` published
  - **Reference**: Integration doc Section 10.2 "Shader Hot-Reload Test"

- [ ] Manual test: Shader hot-reload (visual)
  - [ ] Launch application with file watcher
  - [ ] Edit shader file (change color)
  - [ ] Save file
  - [ ] Verify shader recompiles in background
  - [ ] Verify visual update within 1-2 frames
  - [ ] Verify no frame drops or stuttering
  - [ ] Check logs for event flow:
    - [ ] ShaderCompilationCompleted
    - [ ] ShaderHotReloadReady
    - [ ] Async compilation started
    - [ ] PipelineCompilationResult
    - [ ] Pipeline hot-swapped
  - **Reference**: Integration doc Section 10.3 "Scenario 2: Shader Hot-Reload"

- [ ] Manual test: Interface change detection
  - [ ] Edit shader to add new uniform buffer
  - [ ] Save file
  - [ ] Verify `interfaceChanged = true` in logs
  - [ ] Verify pipeline NOT hot-reloaded
  - [ ] Verify warning about C++ recompilation needed
  - **Reference**: Integration doc Section 10.3 "Scenario 3: Interface Change Detection"

- [ ] Performance test: Zero-stutter validation
  - [ ] Enable VSync
  - [ ] Monitor FPS with RenderDoc or profiler
  - [ ] Edit shader during rendering
  - [ ] Verify no frame drops
  - [ ] Verify FPS remains stable
  - **Reference**: Integration doc Section 10.3 "Scenario 5: Zero-Stutter Validation"

---

## Phase 6: Main Loop Integration

**Goal**: Integrate event processing into application main loop
**Estimated Time**: 1 day
**Reference**: `EventBus-RenderGraph-Integration-Approach.md` Section 7

### 6.1 Update main.cpp

- [ ] Add includes
  - [ ] `#include "EventBus/MessageBus.h"`
  - [ ] `#include "RenderGraph/Core/GraphMessages.h"`

- [ ] Create MessageBus instance
  - [ ] `EventBus::MessageBus messageBus;`
  - [ ] Enable logging in debug: `#ifdef _DEBUG messageBus.SetLoggingEnabled(true); #endif`
  - **Reference**: Integration doc Section 7.1 lines 12-16

- [ ] Inject MessageBus into RenderGraph
  - [ ] Pass `&messageBus` to RenderGraph constructor
  - [ ] **Reference**: Integration doc Section 7.1 lines 18-23

- [ ] Update main loop structure
  - [ ] **Phase 1**: `messageBus.ProcessMessages()` - Process events
  - [ ] **Phase 2**: `renderGraph->RecompileDirtyNodes()` - Recompile invalidated nodes
  - [ ] **Phase 3**: Wait for GPU (frame in flight fence)
  - [ ] **Phase 4**: `renderGraph->RenderFrame()` - Execute graph
  - [ ] **Phase 5**: Publish `FrameCompleteMessage` (optional profiling)
  - **Reference**: Integration doc Section 7.1 lines 28-56

- [ ] Optional: Publish frame complete events
  - [ ] Measure frame time with `std::chrono`
  - [ ] Create `FrameCompleteMessage`
  - [ ] Publish for profiling subscribers
  - **Reference**: Integration doc Section 7.1 lines 48-56

### 6.2 Testing

- [ ] Integration test: Full render loop
  - [ ] Create complete graph (Device → Window → SwapChain → Framebuffer → Geometry → Present)
  - [ ] Inject MessageBus
  - [ ] Run main loop for 100 frames
  - [ ] Publish `WindowResizedMessage` at frame 50
  - [ ] Verify cascade recompilation occurs
  - [ ] Verify rendering continues without crash

- [ ] Manual test: Application stability
  - [ ] Run application for 5+ minutes
  - [ ] Resize window multiple times
  - [ ] Monitor memory usage (no leaks)
  - [ ] Verify consistent FPS
  - [ ] Check event statistics via `messageBus.GetStats()`

---

## Phase 7: Advanced Features (Optional)

**Goal**: Implement camera, lighting, and profiling events
**Estimated Time**: 2-3 days
**Reference**: `EventBus-RenderGraph-Integration-Approach.md` Section 5 & 11

### 7.1 Camera Update Events

- [ ] Create CameraSystem class
  - [ ] Track active cameras (name → transform/projection)
  - [ ] Subscribe to input events (mouse, keyboard)
  - [ ] Publish `CameraUpdatedMessage` when camera moves

- [ ] Update GeometryNode to subscribe
  - [ ] Subscribe to `CameraUpdatedMessage::TYPE`
  - [ ] Update uniform buffer with view/projection matrices
  - [ ] Mark command buffer for re-recording if needed

### 7.2 Lighting Change Events

- [ ] Create LightManager class
  - [ ] Track active lights (name → position/intensity/color)
  - [ ] Publish `LightingChangedMessage` when light added/removed/moved

- [ ] Update ShadowMapNode to subscribe
  - [ ] Subscribe to `LightingChangedMessage::TYPE`
  - [ ] Add/remove shadow map nodes dynamically
  - [ ] Tag with "shadow-maps" for bulk cleanup

- [ ] Update GeometryNode to subscribe
  - [ ] Update lighting uniform buffer
  - [ ] Re-record command buffer if light count changed

### 7.3 Profiling Events

- [ ] Implement `NodeExecutionStartMessage` / `NodeExecutionEndMessage`
  - [ ] Publish in `NodeInstance::Execute()` entry/exit
  - [ ] Include timestamp and node name

- [ ] Create EventProfiler class
  - [ ] Subscribe to all profiling events
  - [ ] Track per-node execution times
  - [ ] Generate reports (avg, min, max, total)

- [ ] Implement `FrameCompleteMessage`
  - [ ] Publish at end of frame in main loop
  - [ ] Include frame number, frame time, draw call count

### 7.4 Testing

- [ ] Test camera update cascade
  - [ ] Move camera
  - [ ] Verify `CameraUpdatedMessage` published
  - [ ] Verify GeometryNode updates uniform buffer

- [ ] Test light management
  - [ ] Add light
  - [ ] Verify `LightingChangedMessage` published
  - [ ] Verify ShadowMapNode created and tagged
  - [ ] Remove light
  - [ ] Verify cleanup via tag

- [ ] Test profiling
  - [ ] Run application for 1000 frames
  - [ ] Collect profiling data
  - [ ] Generate report
  - [ ] Verify per-node times accurate

---

## Phase 8: Documentation & Cleanup

**Goal**: Update memory bank, create migration guide, delete temp files
**Estimated Time**: 1 day

### 8.1 Update Memory Bank

- [ ] Update `memory-bank/activeContext.md`
  - [ ] Document EventBus integration as current focus
  - [ ] List recently implemented nodes (WindowNode, SwapChainNode, PipelineNode)
  - [ ] Document event-driven architecture pattern

- [ ] Update `memory-bank/systemPatterns.md`
  - [ ] Add "Event-Driven Invalidation" section
  - [ ] Document bit flag category system
  - [ ] Add node subscription patterns
  - [ ] Add hot-reload pattern

- [ ] Update `memory-bank/techContext.md`
  - [ ] Add EventBus library to tech stack
  - [ ] Add WorkerThreadBridge for async compilation
  - [ ] Document RM<T> resource wrapper

- [ ] Update `memory-bank/progress.md`
  - [ ] Mark EventBus integration as complete
  - [ ] List implemented features (window resize, hot-reload)
  - [ ] Document performance improvements

### 8.2 Create Migration Guide

- [ ] Create `documentation/EventBus-Migration-Guide.md`
  - [ ] Copy migration section from integration doc
  - [ ] Add step-by-step instructions
  - [ ] Include before/after code examples
  - [ ] Add troubleshooting section

### 8.3 Cleanup Temp Files

- [ ] Review all documents in `temp/`
  - [ ] `EventBus-RenderGraph-Integration-Approach.md`
  - [ ] `EventBus-Performance-Optimizations.md`
  - [ ] `EventBus-Implementation-Checklist.md` (this file)

- [ ] Extract permanent content
  - [ ] Key patterns → memory bank
  - [ ] Migration steps → migration guide
  - [ ] Performance data → tech context

- [ ] Delete temp folder
  - [ ] `rm -rf temp/`

### 8.4 Final Testing

- [ ] Run all unit tests
  - [ ] EventBus core tests
  - [ ] RenderGraph integration tests
  - [ ] Node subscription tests

- [ ] Run all integration tests
  - [ ] Window resize cascade
  - [ ] Shader hot-reload
  - [ ] Tag-based cleanup
  - [ ] Full render loop

- [ ] Manual validation
  - [ ] Run application for extended period
  - [ ] Test all event-driven features
  - [ ] Monitor memory/performance
  - [ ] Collect statistics

- [ ] Performance benchmarking
  - [ ] Compare before/after event dispatch times
  - [ ] Verify ~7x speedup with bit flags
  - [ ] Profile main loop overhead
  - [ ] Document results

---

## Summary Checklist

### Phase Completion Tracking

- [ ] **Phase 1**: EventBus Core Enhancements (1-2 days)
  - [ ] Bit flag category system implemented
  - [ ] `BaseEventMessage` created
  - [ ] `SubscribeCategory()` methods added
  - [ ] Unit tests passing
  - [ ] Performance benchmarked

- [ ] **Phase 2**: RenderGraph Message Updates (1 day)
  - [ ] `GraphMessages.h` created with all message types
  - [ ] All messages inherit from `BaseEventMessage`
  - [ ] Category flags assigned to all messages
  - [ ] Factory methods implemented
  - [ ] Unit tests passing

- [ ] **Phase 3**: RenderGraph Core Integration (2-3 days)
  - [ ] `RenderGraph` updated with MessageBus injection
  - [ ] `NodeInstance` updated with event methods
  - [ ] `ProcessEvents()` and `RecompileDirtyNodes()` implemented
  - [ ] Cleanup request handling implemented
  - [ ] Unit tests passing

- [ ] **Phase 4**: Node Implementation - Window Resize (1-2 days)
  - [ ] `WindowNode` publishes resize events
  - [ ] `SwapChainNode` subscribes and cascades invalidation
  - [ ] `FramebufferNode` subscribes and recompiles
  - [ ] Integration tests passing
  - [ ] Manual resize testing validated

- [ ] **Phase 5**: Shader Hot-Reload Integration (3-5 days)
  - [ ] `PipelineNode` implemented with async compilation
  - [ ] `WorkerThreadBridge` integrated
  - [ ] Interface hash detection implemented
  - [ ] Deferred destruction system implemented (optional)
  - [ ] Zero-stutter validation complete

- [ ] **Phase 6**: Main Loop Integration (1 day)
  - [ ] `main.cpp` updated with event processing
  - [ ] MessageBus injected into RenderGraph
  - [ ] Main loop phases implemented correctly
  - [ ] Full application runs without crashes

- [ ] **Phase 7**: Advanced Features (2-3 days, optional)
  - [ ] Camera update events implemented
  - [ ] Lighting change events implemented
  - [ ] Profiling events implemented
  - [ ] All subscriptions working correctly

- [ ] **Phase 8**: Documentation & Cleanup (1 day)
  - [ ] Memory bank updated
  - [ ] Migration guide created
  - [ ] Temp files deleted
  - [ ] Final testing complete

### Total Progress

**Estimated Time**: 9-15 days
**Phases Complete**: _____ / 8
**Features Implemented**: _____ / _____
**Tests Passing**: _____ / _____

---

## Quick Reference

### Key Files

| File | Description | Doc Reference |
|------|-------------|---------------|
| `EventBus/Message.h` | Event categories and `BaseEventMessage` | Performance Optimizations §4.1 |
| `EventBus/MessageBus.h` | Enhanced subscription API | Performance Optimizations §5.1 |
| `RenderGraph/Core/GraphMessages.h` | RenderGraph-specific messages | Performance Optimizations §7.1 |
| `RenderGraph/Core/RenderGraph.h` | Graph with event integration | Integration Approach §4.2.1 |
| `RenderGraph/Core/NodeInstance.h` | Node base class with events | Integration Approach §4.2.2 |
| `source/RenderGraph/Nodes/*.cpp` | Node implementations | Integration Approach §6 |
| `main.cpp` | Main loop with event processing | Integration Approach §7.1 |

### Event Message Ranges

| Range | Owner | Example Messages |
|-------|-------|------------------|
| 0-99 | EventBus | TextMessage, WorkerResultMessage |
| 100-119 | RenderGraph (Invalidation) | WindowResized, SwapChainInvalidated |
| 120-139 | RenderGraph (App State) | CameraUpdated, LightingChanged |
| 140-159 | RenderGraph (Management) | CleanupRequested, GraphRecompiled |
| 160-179 | RenderGraph (Debug) | FrameComplete, NodeProfiling |
| 200-299 | ShaderManagement | ShaderCompilation*, HotReload |

### Common Patterns

**Pattern 1: Publish Event**
```cpp
auto msg = std::make_unique<WindowResizedMessage>(GetHandle(), newW, newH, oldW, oldH);
PublishMessage(std::move(msg));
```

**Pattern 2: Subscribe by Type**
```cpp
void Setup() override {
    Subscribe(WindowResizedMessage::TYPE, [this](const BaseEventMessage& msg) {
        OnWindowResize(static_cast<const WindowResizedMessage&>(msg));
        return false;
    });
}
```

**Pattern 3: Subscribe by Category**
```cpp
void Setup() override {
    SubscribeCategory(EventCategory::ResourceInvalidation, [this](const BaseEventMessage& msg) {
        // Handle all resource invalidation events
        return false;
    });
}
```

**Pattern 4: Mark Dirty & Cascade**
```cpp
void OnWindowResize(const WindowResizedMessage& msg) override {
    MarkForRecompile();

    auto invalidMsg = std::make_unique<SwapChainInvalidatedMessage>(...);
    PublishMessage(std::move(invalidMsg));
}
```

---

**Document Status**: Implementation Checklist
**Author**: Claude Code
**Date**: 2025-10-27
**Version**: 1.0
**Temporary Location**: `temp/` (delete after integration complete)
**Related Docs**:
- `EventBus-RenderGraph-Integration-Approach.md`
- `EventBus-Performance-Optimizations.md`
