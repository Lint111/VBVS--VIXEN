# MCP Architecture for VIXEN RenderGraph GUI Editor

## Executive Summary

This document outlines the transformation of the VIXEN RenderGraph system into a Model-View-Controller (MVC) architecture suitable for a GUI graph editor application. The design separates concerns into three layers while preserving the existing compile-time type safety and runtime efficiency.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         VIEW LAYER                          │
│  (GUI Graph Editor - User Interaction & Visualization)      │
├─────────────────────────────────────────────────────────────┤
│  • GraphEditorView      - Canvas, panning, zooming          │
│  • NodeView             - Visual node representation        │
│  • ConnectionView       - Visual edge/wire representation   │
│  • SlotView             - Input/output port visualization   │
│  • InspectorView        - Node property panel              │
│  • ToolbarView          - Node palette, actions            │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                      CONTROLLER LAYER                        │
│     (Orchestration - User Actions → Model Updates)          │
├─────────────────────────────────────────────────────────────┤
│  • GraphController           - Graph CRUD operations        │
│  • NodeController            - Node lifecycle management    │
│  • ConnectionController      - Connection validation/create │
│  • ResourceController        - Resource allocation/tracking │
│  • CompilationController     - Graph compilation workflow   │
│  • ExecutionController       - Graph execution management   │
│  • SerializationController   - Save/load graph state       │
│  • ValidationController      - Real-time type checking      │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                        MODEL LAYER                           │
│    (Core Data - Graph Structure & Resources)                │
├─────────────────────────────────────────────────────────────┤
│  • GraphModel            - RenderGraph state wrapper        │
│  • NodeModel             - NodeInstance + metadata wrapper  │
│  • ConnectionModel       - Edge data + validation state     │
│  • ResourceModel         - Resource ownership & lifetime    │
│  • ConfigModel           - Node configuration schemas       │
│  • TopologyModel         - Graph structure & analysis       │
│  • ExecutionModel        - Compiled execution plan          │
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                      EXISTING CORE                           │
│          (VIXEN RenderGraph - No Changes)                   │
├─────────────────────────────────────────────────────────────┤
│  • RenderGraph, NodeInstance, NodeType, GraphTopology       │
│  • Resource, ResourceSlot, ConnectionBatch                  │
│  • All Node Implementations (45+ types)                     │
└─────────────────────────────────────────────────────────────┘
```

---

## Layer 1: MODEL LAYER (Data & Business Logic)

### Purpose
- **Single Source of Truth** for graph state
- Wraps existing RenderGraph core with observable properties
- Handles resource ownership and lifetime management
- Provides type-safe interfaces to underlying graph

### Key Components

#### 1.1 GraphModel
```cpp
class GraphModel {
    // Core graph reference
    std::unique_ptr<RenderGraph> renderGraph;

    // Observable state
    Observable<GraphState> state;  // Building, Compiling, Executing, Failed
    Observable<std::vector<NodeModel*>> nodes;
    Observable<std::vector<ConnectionModel*>> connections;
    Observable<CompilationResult> lastCompilationResult;

    // Metadata
    std::string graphName;
    UUID graphId;
    GraphMetadata metadata;

    // Lifecycle management
    GraphState GetState() const;
    bool IsDirty() const;
    void MarkDirty();

    // Query interface
    NodeModel* FindNode(const std::string& name) const;
    std::vector<ConnectionModel*> GetConnectionsForNode(NodeHandle) const;
    TopologyModel GetTopology() const;

    // Validation
    ValidationResult Validate() const;
    bool CanCompile() const;
};
```

**Key Responsibilities:**
- Owns the RenderGraph instance
- Maintains node and connection registries
- Tracks graph state transitions
- Provides query interface for topology analysis
- Emits change notifications for View updates

---

#### 1.2 NodeModel
```cpp
class NodeModel {
    // Core node reference
    NodeInstance* nodeInstance;  // Non-owning (RenderGraph owns)

    // Identity
    NodeHandle handle;
    std::string instanceName;
    std::string nodeTypeName;

    // Configuration
    const NodeConfig* config;  // Compile-time config schema
    std::map<std::string, RuntimeParameter> parameters;

    // Slot metadata
    std::vector<SlotModel> inputSlots;
    std::vector<SlotModel> outputSlots;

    // Visual metadata (for GUI)
    Vec2 position;
    Vec2 size;
    Color color;
    bool collapsed;
    std::string category;  // "Resource", "Pipeline", "Rendering", etc.

    // State
    Observable<NodeState> state;  // Unconnected, Ready, Compiled, Error
    Observable<std::vector<std::string>> errors;
    Observable<std::vector<std::string>> warnings;

    // Query interface
    const SlotModel& GetInputSlot(uint32_t index) const;
    const SlotModel& GetOutputSlot(uint32_t index) const;
    bool IsSlotConnected(uint32_t index, bool isInput) const;
    std::vector<ConnectionModel*> GetConnections(uint32_t slotIndex, bool isInput) const;

    // Validation
    bool CanConnect(uint32_t outputSlot, NodeModel* target, uint32_t inputSlot) const;
};
```

**Key Responsibilities:**
- Wraps NodeInstance with GUI-specific metadata
- Stores visual properties (position, size, color)
- Maintains slot connection state
- Provides type-safe connection validation
- Tracks compilation errors per-node

---

#### 1.3 SlotModel
```cpp
struct SlotModel {
    uint32_t index;
    std::string name;
    std::string typeName;  // "VkImage", "VkBuffer", etc.

    // From ResourceSlot
    SlotNullability nullability;
    SlotRole role;
    SlotMutability mutability;
    SlotScope scope;

    // Connection state
    bool isConnected;
    ConnectionModel* connection;  // For inputs (single connection)
    std::vector<ConnectionModel*> connections;  // For outputs (multi-connection)

    // Visual hints
    Color typeColor;  // Color-code by type
    std::string tooltip;

    // Validation
    bool IsCompatibleWith(const SlotModel& other) const;
};
```

**Key Responsibilities:**
- Encapsulates slot metadata from compile-time config
- Tracks connection state per-slot
- Provides type compatibility checking
- Stores visual hints for rendering

---

#### 1.4 ConnectionModel
```cpp
class ConnectionModel {
    UUID connectionId;

    // Source
    NodeModel* sourceNode;
    uint32_t sourceSlotIndex;

    // Target
    NodeModel* targetNode;
    uint32_t targetSlotIndex;

    // Connection type
    ConnectionType type;  // Direct, Array, Constant, Variadic, FieldExtraction

    // Array connection metadata
    std::optional<uint32_t> targetArrayIndex;

    // Field extraction metadata
    std::optional<std::string> fieldPath;  // e.g., "transform.position.x"

    // Constant connection metadata
    std::optional<std::any> constantValue;

    // Validation state
    Observable<bool> isValid;
    Observable<std::string> errorMessage;

    // Visual state
    Color wireColor;
    float wireThickness;
    bool isSelected;

    // Lifecycle
    bool IsRegistered() const;
    void Validate();
};
```

**Key Responsibilities:**
- Represents a single edge in the graph
- Stores source and target node/slot references
- Tracks connection type and metadata
- Validates connection legality
- Stores visual properties for rendering

---

#### 1.5 ResourceModel
```cpp
class ResourceModel {
    Resource* resource;  // Non-owning (RenderGraph owns)

    // Identity
    UUID resourceId;
    std::string resourceName;
    ResourceType type;

    // Ownership
    NodeModel* owner;  // Node that created this resource
    std::vector<NodeModel*> consumers;  // Nodes that read this resource

    // Lifetime
    ResourceLifetime lifetime;  // Transient or Persistent
    bool isAllocated;

    // Usage tracking
    std::set<ResourceUsage> usageFlags;

    // Dependencies
    std::vector<ResourceModel*> dependsOn;
    std::vector<ResourceModel*> requiredBy;

    // Visual representation
    std::string displayName;
    std::string description;

    // Query interface
    size_t GetMemoryUsage() const;
    bool IsShared() const;
    std::vector<NodeModel*> GetAllDependents() const;
};
```

**Key Responsibilities:**
- Wraps Resource with ownership tracking
- Maintains producer/consumer relationships
- Tracks resource dependencies for cleanup ordering
- Provides memory usage metrics

---

#### 1.6 ConfigModel (Registry)
```cpp
class ConfigModelRegistry {
    // Registered node types
    std::map<std::string, NodeTypeDescriptor> nodeTypes;

    struct NodeTypeDescriptor {
        std::string typeName;
        std::string category;
        std::string description;
        std::vector<SlotDescriptor> inputs;
        std::vector<SlotDescriptor> outputs;
        std::function<NodeInstance*()> factoryFunction;

        // Visual hints
        Color defaultColor;
        std::string iconPath;
    };

    struct SlotDescriptor {
        std::string name;
        std::string typeName;
        SlotNullability nullability;
        SlotRole role;
        SlotMutability mutability;
        SlotScope scope;
    };

    // Registration
    void RegisterNodeType(const NodeTypeDescriptor& descriptor);

    // Query
    const NodeTypeDescriptor* GetNodeType(const std::string& typeName) const;
    std::vector<std::string> GetAllNodeTypes() const;
    std::vector<std::string> GetNodeTypesByCategory(const std::string& category) const;

    // Validation
    bool IsTypeRegistered(const std::string& typeName) const;
};
```

**Key Responsibilities:**
- Central registry of all node types
- Provides factory functions for node creation
- Stores compile-time configuration as runtime metadata
- Enables dynamic node palette generation

---

#### 1.7 TopologyModel
```cpp
class TopologyModel {
    GraphTopology* topology;  // Non-owning

    // Cached analysis results
    std::vector<NodeModel*> rootNodes;
    std::vector<NodeModel*> executionOrder;
    std::map<NodeModel*, std::vector<NodeModel*>> dependencyMap;

    // Validation
    bool hasCycles;
    std::vector<std::vector<NodeModel*>> cycles;
    std::vector<NodeModel*> unreachableNodes;

    // Query interface
    bool IsReachable(NodeModel* from, NodeModel* to) const;
    std::vector<NodeModel*> GetDependencies(NodeModel* node) const;
    std::vector<NodeModel*> GetDependents(NodeModel* node) const;
    int GetDepth(NodeModel* node) const;  // Distance from root

    // Analysis
    void Analyze();
    std::vector<NodeModel*> FindCriticalPath() const;
    std::map<std::string, size_t> GetStatistics() const;
};
```

**Key Responsibilities:**
- Analyzes graph structure
- Detects cycles and connectivity issues
- Provides execution order preview
- Enables graph visualization algorithms (layering, routing)

---

#### 1.8 ExecutionModel
```cpp
class ExecutionModel {
    // Compilation state
    Observable<CompilationState> state;
    Observable<std::vector<CompilationError>> errors;
    Observable<std::vector<CompilationWarning>> warnings;

    struct CompilationError {
        NodeModel* node;
        std::string message;
        ErrorSeverity severity;
    };

    // Execution state
    Observable<ExecutionState> executionState;
    Observable<uint64_t> frameCount;
    Observable<double> lastFrameTime;

    // Performance tracking
    std::map<NodeModel*, PerformanceMetrics> nodeMetrics;

    struct PerformanceMetrics {
        double avgExecutionTime;
        double maxExecutionTime;
        uint64_t executionCount;
    };

    // Control
    void StartExecution();
    void PauseExecution();
    void StopExecution();
    void StepFrame();

    // Query
    const std::vector<CompilationError>& GetErrors() const;
    bool CanExecute() const;
};
```

**Key Responsibilities:**
- Tracks compilation and execution state
- Collects performance metrics per-node
- Provides execution control (play/pause/step)
- Stores validation results

---

## Layer 2: CONTROLLER LAYER (Orchestration)

### Purpose
- Translates user actions into model updates
- Enforces business rules and constraints
- Coordinates multi-step operations
- Maintains graph invariants

### Key Components

#### 2.1 GraphController
```cpp
class GraphController {
    GraphModel* model;

    // Commands (undoable)
    CommandResult CreateGraph(const std::string& name);
    CommandResult LoadGraph(const std::filesystem::path& path);
    CommandResult SaveGraph(const std::filesystem::path& path);

    // Lifecycle
    void Compile();
    void Execute();
    void Cleanup();
    void Reset();

    // State management
    void BeginBatch();  // Suspend change notifications
    void EndBatch();    // Emit batched changes
    void MarkDirty();

    // Undo/Redo
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    // Events
    Event<GraphState> onStateChanged;
    Event<CompilationResult> onCompilationFinished;
    Event<ValidationResult> onValidationFinished;
};
```

---

#### 2.2 NodeController
```cpp
class NodeController {
    GraphModel* graphModel;
    ConfigModelRegistry* registry;

    // Node operations (undoable commands)
    CommandResult CreateNode(const std::string& nodeTypeName, const std::string& instanceName);
    CommandResult DeleteNode(NodeHandle handle);
    CommandResult DuplicateNode(NodeHandle handle);
    CommandResult RenameNode(NodeHandle handle, const std::string& newName);

    // Node properties
    CommandResult SetNodePosition(NodeHandle handle, Vec2 position);
    CommandResult SetNodeParameter(NodeHandle handle, const std::string& paramName, const std::any& value);

    // Bulk operations
    CommandResult DeleteNodes(const std::vector<NodeHandle>& handles);
    CommandResult MoveNodes(const std::vector<NodeHandle>& handles, Vec2 delta);

    // Validation
    ValidationResult ValidateNodeName(const std::string& name) const;
    bool CanDeleteNode(NodeHandle handle) const;

    // Events
    Event<NodeModel*> onNodeCreated;
    Event<NodeModel*> onNodeDeleted;
    Event<NodeModel*> onNodeModified;
};
```

---

#### 2.3 ConnectionController
```cpp
class ConnectionController {
    GraphModel* graphModel;
    ValidationController* validator;

    // Connection operations (undoable)
    CommandResult CreateConnection(
        NodeHandle source, uint32_t sourceSlot,
        NodeHandle target, uint32_t targetSlot
    );
    CommandResult CreateArrayConnection(
        NodeHandle source, uint32_t sourceSlot,
        NodeHandle target, uint32_t targetSlot, uint32_t arrayIndex
    );
    CommandResult CreateConstantConnection(
        NodeHandle target, uint32_t targetSlot,
        const std::any& constantValue
    );
    CommandResult DeleteConnection(UUID connectionId);

    // Batch operations
    CommandResult CreateConnectionBatch(const std::vector<ConnectionDescriptor>& connections);
    CommandResult DeleteConnectionsForNode(NodeHandle handle);

    // Validation (real-time)
    ConnectionValidation ValidateConnection(
        NodeHandle source, uint32_t sourceSlot,
        NodeHandle target, uint32_t targetSlot
    ) const;

    struct ConnectionValidation {
        bool isValid;
        std::string errorMessage;
        CompatibilityReason reason;
    };

    // Wire routing
    std::vector<Vec2> ComputeWirePath(ConnectionModel* connection) const;

    // Events
    Event<ConnectionModel*> onConnectionCreated;
    Event<ConnectionModel*> onConnectionDeleted;
    Event<ConnectionModel*> onConnectionValidated;
};
```

---

#### 2.4 ResourceController
```cpp
class ResourceController {
    GraphModel* graphModel;

    // Resource tracking
    void OnCompilationStarted();
    void OnCompilationFinished();
    void OnResourceAllocated(Resource* resource, NodeModel* owner);
    void OnResourceDeallocated(Resource* resource);

    // Query
    std::vector<ResourceModel*> GetAllResources() const;
    std::vector<ResourceModel*> GetResourcesOwnedBy(NodeHandle handle) const;
    std::vector<ResourceModel*> GetResourcesConsumedBy(NodeHandle handle) const;
    size_t GetTotalMemoryUsage() const;

    // Dependency analysis
    std::vector<ResourceModel*> GetCleanupOrder() const;
    std::vector<NodeModel*> GetResourceDependents(ResourceModel* resource) const;

    // Visualization
    ResourceDependencyGraph BuildDependencyGraph() const;
};
```

---

#### 2.5 CompilationController
```cpp
class CompilationController {
    GraphModel* graphModel;
    ValidationController* validator;

    // Compilation workflow
    void StartCompilation();
    void CancelCompilation();

    // Steps
    void ValidateGraph();
    void AnalyzeTopology();
    void AllocateResources();
    void CompileNodes();
    void BuildExecutionOrder();

    // Incremental compilation
    void RecompileDirtyNodes(const std::vector<NodeHandle>& dirtyNodes);
    void PartialCleanup(const std::vector<NodeHandle>& nodesToRemove);

    // Progress tracking
    Observable<CompilationProgress> progress;

    struct CompilationProgress {
        CompilationPhase phase;
        uint32_t currentNode;
        uint32_t totalNodes;
        std::string statusMessage;
    };

    // Events
    Event<CompilationPhase> onPhaseChanged;
    Event<CompilationResult> onCompilationFinished;
    Event<CompilationError> onErrorOccurred;
};
```

---

#### 2.6 ExecutionController
```cpp
class ExecutionController {
    GraphModel* graphModel;
    ExecutionModel* executionModel;

    // Execution control
    void Play();
    void Pause();
    void Stop();
    void Step();  // Execute one frame

    // Performance tracking
    void BeginNodeExecution(NodeHandle handle);
    void EndNodeExecution(NodeHandle handle);
    void RecordFrameTime(double deltaTime);

    // Profiling
    void EnableProfiling(bool enabled);
    PerformanceReport GeneratePerformanceReport() const;

    struct PerformanceReport {
        std::map<NodeModel*, PerformanceMetrics> nodeMetrics;
        double avgFrameTime;
        double minFrameTime;
        double maxFrameTime;
        uint64_t totalFrames;
    };

    // Events
    Event<ExecutionState> onExecutionStateChanged;
    Event<uint64_t> onFrameCompleted;
};
```

---

#### 2.7 SerializationController
```cpp
class SerializationController {
    GraphModel* graphModel;

    // Serialization formats
    enum class Format {
        JSON,
        Binary,
        YAML
    };

    // Save/Load
    Result SaveGraph(const std::filesystem::path& path, Format format = Format::JSON);
    Result LoadGraph(const std::filesystem::path& path);

    // Export/Import
    Result ExportToJSON(std::ostream& stream);
    Result ImportFromJSON(std::istream& stream);

    // Clipboard operations
    std::string SerializeSelection(const std::vector<NodeHandle>& nodes);
    Result DeserializeAndPaste(const std::string& serialized, Vec2 offset);

    // Versioning
    uint32_t GetFormatVersion() const;
    bool CanLoadVersion(uint32_t version) const;

    // Schema
    struct GraphSchema {
        std::string name;
        UUID id;
        std::vector<NodeSchema> nodes;
        std::vector<ConnectionSchema> connections;
    };
};
```

---

#### 2.8 ValidationController
```cpp
class ValidationController {
    ConfigModelRegistry* registry;

    // Real-time validation
    ValidationResult ValidateGraph(const GraphModel* graph) const;
    ValidationResult ValidateNode(const NodeModel* node) const;
    ValidationResult ValidateConnection(const ConnectionModel* connection) const;

    struct ValidationResult {
        bool isValid;
        std::vector<ValidationError> errors;
        std::vector<ValidationWarning> warnings;
    };

    // Type checking
    bool AreTypesCompatible(const std::string& sourceType, const std::string& targetType) const;
    bool CanConnectSlots(const SlotModel& source, const SlotModel& target) const;

    // Constraint checking
    bool CheckNullability(const ConnectionModel* connection) const;
    bool CheckMutability(const SlotModel& slot, const ConnectionModel* connection) const;
    bool CheckScope(const SlotModel& slot) const;

    // Graph-level validation
    bool HasCycles(const GraphModel* graph) const;
    bool AllRequiredSlotsConnected(const NodeModel* node) const;
    std::vector<NodeModel*> FindUnreachableNodes(const GraphModel* graph) const;
};
```

---

## Layer 3: VIEW LAYER (GUI Presentation)

### Purpose
- Renders the graph visually
- Handles user input (mouse, keyboard)
- Provides interactive editing experience
- Observes model changes and updates UI

### Key Components

#### 3.1 GraphEditorView
```cpp
class GraphEditorView : public Widget {
    GraphController* graphController;
    GraphModel* graphModel;

    // Child views
    std::vector<NodeView*> nodeViews;
    std::vector<ConnectionView*> connectionViews;
    CanvasView* canvas;

    // Camera/Transform
    Vec2 cameraPosition;
    float zoomLevel;
    Mat3 worldToScreen;

    // Rendering
    void Render() override;
    void RenderGrid();
    void RenderNodes();
    void RenderConnections();
    void RenderSelectionBox();

    // Input handling
    void OnMouseDown(MouseEvent event) override;
    void OnMouseMove(MouseEvent event) override;
    void OnMouseUp(MouseEvent event) override;
    void OnKeyPress(KeyEvent event) override;
    void OnScroll(ScrollEvent event) override;

    // Interaction modes
    enum class Mode {
        Select,
        Pan,
        CreateConnection,
        BoxSelect
    };
    Mode currentMode;

    // Selection
    std::set<NodeModel*> selectedNodes;
    std::set<ConnectionModel*> selectedConnections;

    void SelectNode(NodeModel* node, bool additive = false);
    void DeselectAll();
    void DeleteSelection();

    // Connection creation
    SlotModel* connectionStartSlot;
    Vec2 connectionDragPosition;

    // Camera control
    void Pan(Vec2 delta);
    void Zoom(float delta, Vec2 focus);
    void FrameAll();
    void FrameSelection();

    // Model observation
    void OnNodeAdded(NodeModel* node);
    void OnNodeRemoved(NodeModel* node);
    void OnConnectionAdded(ConnectionModel* connection);
    void OnConnectionRemoved(ConnectionModel* connection);
};
```

---

#### 3.2 NodeView
```cpp
class NodeView : public Widget {
    NodeModel* model;
    NodeController* controller;

    // Visual state
    Vec2 position;  // Synced with model
    Vec2 size;
    Color backgroundColor;
    bool isSelected;
    bool isHovered;

    // Child views
    std::vector<SlotView*> inputSlots;
    std::vector<SlotView*> outputSlots;
    HeaderView* header;
    BodyView* body;

    // Rendering
    void Render() override;
    void RenderFrame();
    void RenderHeader();
    void RenderBody();
    void RenderSlots();
    void RenderErrorBadge();

    // Layout
    void UpdateLayout();
    Rect GetBounds() const;

    // Input handling
    void OnMouseDown(MouseEvent event) override;
    void OnMouseMove(MouseEvent event) override;
    void OnDragStart(DragEvent event);
    void OnDrag(DragEvent event);
    void OnDragEnd(DragEvent event);

    // State
    void SetSelected(bool selected);
    void SetHovered(bool hovered);
    void UpdateFromModel();

    // Slot lookup
    SlotView* GetSlotAtPosition(Vec2 position) const;
    SlotView* GetInputSlot(uint32_t index) const;
    SlotView* GetOutputSlot(uint32_t index) const;
};
```

---

#### 3.3 ConnectionView
```cpp
class ConnectionView : public Widget {
    ConnectionModel* model;
    ConnectionController* controller;

    // Visual state
    Color wireColor;
    float wireThickness;
    bool isSelected;
    bool isHovered;

    // Layout
    Vec2 startPosition;  // Computed from source slot
    Vec2 endPosition;    // Computed from target slot
    std::vector<Vec2> controlPoints;  // Bezier curve

    // Rendering
    void Render() override;
    void RenderWire();
    void RenderArrow();
    void RenderSelectionHighlight();

    // Wire routing
    void UpdatePath();
    std::vector<Vec2> ComputeBezierPath() const;
    std::vector<Vec2> ComputeOrthogonalPath() const;

    // Interaction
    bool HitTest(Vec2 position, float tolerance = 5.0f) const;
    void OnMouseDown(MouseEvent event) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;

    // State
    void SetSelected(bool selected);
    void UpdateFromModel();
};
```

---

#### 3.4 SlotView
```cpp
class SlotView : public Widget {
    SlotModel* model;
    NodeView* parentNode;

    // Visual state
    Vec2 position;  // Relative to parent node
    float radius;
    Color color;  // Type-based color
    bool isHovered;
    bool isConnected;

    // Rendering
    void Render() override;
    void RenderCircle();
    void RenderLabel();
    void RenderTooltip();
    void RenderTypeIndicator();

    // Layout
    Vec2 GetWorldPosition() const;
    Rect GetBounds() const;

    // Interaction
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown(MouseEvent event) override;
    void OnDragStart(DragEvent event);  // Begin connection creation

    // State
    void UpdateFromModel();
    bool CanAcceptConnection(const SlotModel& other) const;
    void ShowCompatibilityIndicator(bool compatible);
};
```

---

#### 3.5 InspectorView
```cpp
class InspectorView : public Panel {
    NodeModel* selectedNode;
    ValidationController* validator;

    // Sections
    PropertyGridView* properties;
    SlotListView* inputs;
    SlotListView* outputs;
    ErrorListView* errors;

    // Rendering
    void Render() override;
    void RenderNodeInfo();
    void RenderProperties();
    void RenderSlots();
    void RenderErrors();

    // Property editing
    void OnPropertyChanged(const std::string& propertyName, const std::any& value);
    void OnParameterChanged(const std::string& paramName, const std::any& value);

    // State
    void SetSelectedNode(NodeModel* node);
    void ClearSelection();
    void UpdateFromModel();
};
```

---

#### 3.6 ToolbarView
```cpp
class ToolbarView : public Panel {
    GraphController* graphController;
    ConfigModelRegistry* registry;

    // Sections
    NodePaletteView* nodePalette;
    ActionBarView* actionBar;

    // Node palette
    void RenderNodePalette();
    void RenderNodeCategory(const std::string& category);
    void RenderNodeButton(const NodeTypeDescriptor& descriptor);

    // Actions
    void OnCompileClicked();
    void OnExecuteClicked();
    void OnStopClicked();
    void OnSaveClicked();
    void OnLoadClicked();

    // Search
    void RenderSearchBox();
    void FilterNodes(const std::string& searchQuery);
    std::vector<NodeTypeDescriptor> filteredNodes;

    // Drag-and-drop
    void OnNodeDragStart(const NodeTypeDescriptor& descriptor);
};
```

---

## Data Flow Examples

### Example 1: User Creates a Node

```
[USER] Drags "TextureLoaderNode" from palette to canvas
   ↓
[VIEW] ToolbarView detects drag-and-drop
   ↓
[VIEW] GraphEditorView.OnDrop(nodeTypeName="TextureLoaderNode", position=Vec2(100, 200))
   ↓
[CONTROLLER] NodeController.CreateNode("TextureLoaderNode", "texture_diffuse_1")
   ↓
[CONTROLLER] Validates name uniqueness
   ↓
[MODEL] GraphModel.AddNode(nodeModel)
   ↓
[MODEL] GraphModel.nodes.Notify(NodeAdded)
   ↓
[VIEW] GraphEditorView.OnNodeAdded(nodeModel)
   ↓
[VIEW] Creates NodeView and adds to canvas
   ↓
[VIEW] Renders updated graph
```

---

### Example 2: User Connects Two Nodes

```
[USER] Drags from output slot of Node A
   ↓
[VIEW] SlotView.OnDragStart(sourceSlot)
   ↓
[VIEW] GraphEditorView enters "CreateConnection" mode
   ↓
[VIEW] Renders temporary wire following cursor
   ↓
[USER] Drops on input slot of Node B
   ↓
[VIEW] SlotView.OnDrop(targetSlot)
   ↓
[CONTROLLER] ConnectionController.CreateConnection(nodeA, slot0, nodeB, slot1)
   ↓
[CONTROLLER] ValidationController.ValidateConnection(...)
   ↓
[CONTROLLER] If valid: Create ConnectionModel
   ↓
[MODEL] GraphModel.AddConnection(connectionModel)
   ↓
[MODEL] GraphModel.MarkDirty()
   ↓
[MODEL] GraphModel.connections.Notify(ConnectionAdded)
   ↓
[VIEW] GraphEditorView.OnConnectionAdded(connectionModel)
   ↓
[VIEW] Creates ConnectionView and renders wire
```

---

### Example 3: User Compiles Graph

```
[USER] Clicks "Compile" button
   ↓
[VIEW] ToolbarView.OnCompileClicked()
   ↓
[CONTROLLER] CompilationController.StartCompilation()
   ↓
[CONTROLLER] CompilationController.ValidateGraph()
   ↓
[CONTROLLER] ValidationController returns errors (if any)
   ↓
[CONTROLLER] If valid: CompilationController.AnalyzeTopology()
   ↓
[MODEL] TopologyModel analyzes graph structure
   ↓
[CONTROLLER] CompilationController.AllocateResources()
   ↓
[MODEL] ResourceController tracks resource creation
   ↓
[CONTROLLER] CompilationController.CompileNodes()
   ↓
[MODEL] RenderGraph.Compile() called
   ↓
[MODEL] Each NodeInstance.Compile() executed
   ↓
[MODEL] ExecutionModel updated with results
   ↓
[MODEL] ExecutionModel.state.Notify(CompilationFinished)
   ↓
[VIEW] ToolbarView shows "Compiled Successfully" indicator
   ↓
[VIEW] NodeViews update visual state (errors highlighted)
```

---

## Implementation Strategy

### Phase 1: Model Layer Foundation
1. Create `GraphModel` wrapping `RenderGraph`
2. Implement `NodeModel` with observable properties
3. Implement `ConnectionModel` with validation
4. Build `ConfigModelRegistry` from existing node configs
5. Create `ResourceModel` tracking system

**Deliverables:**
- `RenderGraphModel.h/cpp`
- `NodeModel.h/cpp`
- `ConnectionModel.h/cpp`
- `ConfigModelRegistry.h/cpp`
- `ResourceModel.h/cpp`
- Unit tests for each model class

---

### Phase 2: Controller Layer
1. Implement `GraphController` with command pattern
2. Implement `NodeController` with undo/redo
3. Implement `ConnectionController` with real-time validation
4. Implement `CompilationController` with progress tracking
5. Implement `ValidationController` with type checking

**Deliverables:**
- `GraphController.h/cpp`
- `NodeController.h/cpp`
- `ConnectionController.h/cpp`
- `CompilationController.h/cpp`
- `ValidationController.h/cpp`
- Command pattern infrastructure (`Command.h`)
- Integration tests

---

### Phase 3: View Layer (Basic)
1. Set up GUI framework (Dear ImGui or custom)
2. Implement `GraphEditorView` with pan/zoom
3. Implement `NodeView` with basic rendering
4. Implement `ConnectionView` with Bezier curves
5. Implement `SlotView` with hit testing

**Deliverables:**
- `GraphEditorView.h/cpp`
- `NodeView.h/cpp`
- `ConnectionView.h/cpp`
- `SlotView.h/cpp`
- Rendering utilities (`WireRouter.h`, `NodeLayoutEngine.h`)

---

### Phase 4: View Layer (Advanced)
1. Implement `InspectorView` with property editing
2. Implement `ToolbarView` with node palette
3. Add drag-and-drop support
4. Add selection and multi-select
5. Add keyboard shortcuts

**Deliverables:**
- `InspectorView.h/cpp`
- `ToolbarView.h/cpp`
- `DragDropManager.h/cpp`
- `SelectionManager.h/cpp`
- `KeyboardShortcutManager.h/cpp`

---

### Phase 5: Serialization & Persistence
1. Implement JSON serialization for `GraphModel`
2. Implement save/load functionality
3. Add clipboard support (copy/paste nodes)
4. Add template/preset system
5. Add version migration

**Deliverables:**
- `SerializationController.h/cpp`
- `GraphSerializer.h/cpp`
- `ClipboardManager.h/cpp`
- `TemplateManager.h/cpp`

---

### Phase 6: Execution & Debugging
1. Implement `ExecutionController` with play/pause
2. Add performance profiling per-node
3. Add visual execution indicator (current executing node)
4. Add breakpoint support
5. Add step-through execution

**Deliverables:**
- `ExecutionController.h/cpp`
- `PerformanceProfiler.h/cpp`
- `DebugVisualization.h/cpp`

---

## Key Design Principles

### 1. Separation of Concerns
- **Model**: Knows nothing about GUI (no rendering, no input)
- **View**: Knows nothing about business logic (only rendering and input)
- **Controller**: Mediates between Model and View

### 2. Observable Pattern
- Models emit change notifications
- Views observe models and update automatically
- No manual synchronization required

### 3. Command Pattern
- All user actions are commands
- Commands are undoable/redoable
- Commands validate before execution

### 4. Type Safety
- Compile-time type checking preserved
- Runtime type validation for connections
- No type erasure except where necessary (PassThroughStorage)

### 5. Zero Overhead Abstraction
- Models wrap existing classes (no duplication)
- No performance penalty for GUI layer
- Existing RenderGraph performance unchanged

---

## Technology Recommendations

### GUI Framework Options

#### Option 1: Dear ImGui (Recommended)
**Pros:**
- Immediate mode (simple state management)
- Mature node editor extension (imnodes)
- Low overhead
- Easy integration

**Cons:**
- Less polished visuals
- Requires custom styling for professional look

#### Option 2: Qt
**Pros:**
- Native widgets
- Mature ecosystem
- Professional appearance

**Cons:**
- Heavyweight
- Licensing considerations
- Steeper learning curve

#### Option 3: Custom (OpenGL/Vulkan)
**Pros:**
- Full control
- Perfect Vulkan integration
- Lightweight

**Cons:**
- High development cost
- Requires custom UI framework

---

### Serialization Format

**Recommended: JSON**
```json
{
  "version": 1,
  "graph": {
    "name": "MyRenderGraph",
    "id": "a1b2c3d4-...",
    "nodes": [
      {
        "handle": 0,
        "type": "WindowNodeType",
        "instance": "main_window",
        "position": [100.0, 200.0],
        "parameters": {
          "width": 1920,
          "height": 1080
        }
      }
    ],
    "connections": [
      {
        "source": {"node": 0, "slot": 2},
        "target": {"node": 1, "slot": 0},
        "type": "direct"
      }
    ]
  }
}
```

---

## File Structure

```
VIXEN/
├── libraries/
│   ├── RenderGraph/              # Existing (no changes)
│   │   ├── include/
│   │   └── src/
│   │
│   └── RenderGraphEditor/        # New MCP GUI layer
│       ├── include/
│       │   ├── Model/
│       │   │   ├── GraphModel.h
│       │   │   ├── NodeModel.h
│       │   │   ├── ConnectionModel.h
│       │   │   ├── ResourceModel.h
│       │   │   ├── ConfigModelRegistry.h
│       │   │   ├── TopologyModel.h
│       │   │   └── ExecutionModel.h
│       │   │
│       │   ├── Controller/
│       │   │   ├── GraphController.h
│       │   │   ├── NodeController.h
│       │   │   ├── ConnectionController.h
│       │   │   ├── ResourceController.h
│       │   │   ├── CompilationController.h
│       │   │   ├── ExecutionController.h
│       │   │   ├── SerializationController.h
│       │   │   └── ValidationController.h
│       │   │
│       │   └── View/
│       │       ├── GraphEditorView.h
│       │       ├── NodeView.h
│       │       ├── ConnectionView.h
│       │       ├── SlotView.h
│       │       ├── InspectorView.h
│       │       ├── ToolbarView.h
│       │       └── Utilities/
│       │           ├── WireRouter.h
│       │           ├── NodeLayoutEngine.h
│       │           └── RenderingUtils.h
│       │
│       └── src/
│           ├── Model/
│           ├── Controller/
│           └── View/
│
└── applications/
    └── GraphEditor/              # Standalone GUI application
        ├── main.cpp
        └── GraphEditorApplication.h
```

---

## Next Steps

1. **Review & Approval**: Review this architecture with team
2. **Prototype**: Build Phase 1 (Model Layer) as proof-of-concept
3. **Iterate**: Refine based on prototype learnings
4. **Implement**: Execute phases sequentially
5. **Test**: Comprehensive testing at each phase
6. **Document**: API documentation and user guide

---

## Conclusion

This MCP architecture provides:

✅ **Clean Separation**: Model (data), View (GUI), Controller (logic)
✅ **Type Safety**: Compile-time + runtime validation
✅ **Undo/Redo**: Command pattern throughout
✅ **Real-time Validation**: Immediate feedback on errors
✅ **Observable Updates**: Automatic UI synchronization
✅ **Zero Overhead**: No performance penalty for existing RenderGraph
✅ **Extensibility**: Easy to add new node types
✅ **Testability**: Each layer independently testable

The architecture leverages the existing RenderGraph system while adding a robust, maintainable GUI layer suitable for professional graph editing applications.
