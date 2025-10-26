# Graph Editor Planning Document

## Vision

Transform VIXEN from a Vulkan learning project into a **self-developing visual programming environment** by adding a node-based graph editor. This editor will leverage the existing Vulkan rendering infrastructure to create a tool that can visually represent, edit, and execute computational graphs - eventually becoming a tool to develop itself through visual programming.

## Goals

### Primary Goals
1. **Build a visual node graph editor** with pan, zoom, selection, and connection editing
2. **Leverage existing Vulkan renderer** for high-performance graph rendering
3. **Create a flexible graph system** that can represent various data (rendering pipelines, logic flows, data processing)
4. **Enable meta-development** - use the graph editor to visually design and modify graph node types
5. **Maintain code quality** following established C++23 patterns and RAII principles

### Meta-Development Vision
The editor should eventually be capable of:
- Visually designing new node types and their behavior
- Creating graph-based shaders and rendering pipelines
- Building UI layouts through node composition
- Defining data transformation pipelines
- Self-modifying: editing the graph editor itself through graphs

## Architecture Overview

### System Layers

```
┌─────────────────────────────────────────────────────────────┐
│                   VulkanApplication                         │
│                  (Existing Singleton)                       │
└────────────────┬────────────────────────────────────────────┘
                 │
        ┌────────┴─────────┐
        │                  │
        ▼                  ▼
┌──────────────┐    ┌─────────────────┐
│   Existing   │    │   NEW: Graph    │
│   Vulkan     │◄───┤     Editor      │
│  Renderer    │    │     System      │
└──────────────┘    └─────────────────┘
        │                  │
        │           ┌──────┴──────┬──────────────┬─────────────┐
        │           │             │              │             │
        │           ▼             ▼              ▼             ▼
        │    ┌──────────┐  ┌───────────┐ ┌───────────┐ ┌──────────┐
        │    │  Graph   │  │   Node    │ │   Input   │ │   UI     │
        │    │  Model   │  │  Editor   │ │  Handler  │ │  System  │
        │    └──────────┘  └───────────┘ └───────────┘ └──────────┘
        │           │             │              │             │
        └───────────┴─────────────┴──────────────┴─────────────┘
                                  │
                          ┌───────┴──────┐
                          ▼              ▼
                   ┌─────────────┐ ┌─────────────┐
                   │   Graph     │ │   Graph     │
                   │  Renderer   │ │ Execution   │
                   └─────────────┘ └─────────────┘
```

## Core Components

### 1. Graph Model (Data Layer)

The graph data structures form the foundation of the system.

#### 1.1 Core Classes

```cpp
// Forward declarations
class GraphNode;
class GraphConnection;
class GraphPin;
class Graph;

// Pin types for connections
enum class PinType {
    Flow,       // Execution flow (for procedural graphs)
    Data,       // Data connections
    Event       // Event/signal connections
};

enum class PinDirection {
    Input,
    Output
};

// Graph pin - connection point on a node
class GraphPin {
    std::string id;                      // Unique pin identifier
    std::string name;                    // Display name
    PinType type;                        // Pin type
    PinDirection direction;              // Input or output
    GraphNode* ownerNode;                // Parent node
    std::vector<GraphConnection*> connections; // Connected edges

    // Type information for data pins
    std::string dataType;                // "float", "vec3", "texture", etc.
    std::any defaultValue;               // Default value when unconnected

public:
    GraphPin(std::string id, std::string name, PinType type,
             PinDirection dir, GraphNode* owner);

    bool CanConnectTo(const GraphPin* other) const;
    void AddConnection(GraphConnection* connection);
    void RemoveConnection(GraphConnection* connection);

    // Value access for data flow
    template<typename T>
    std::expected<T, std::string> GetValue() const;

    template<typename T>
    void SetValue(const T& value);
};

// Connection between two pins
class GraphConnection {
    std::string id;                      // Unique connection identifier
    GraphPin* sourcePin;                 // Output pin
    GraphPin* targetPin;                 // Input pin
    Graph* ownerGraph;                   // Parent graph

    // Visual properties
    glm::vec4 color;                     // Connection color
    float thickness;                     // Line thickness

public:
    GraphConnection(std::string id, GraphPin* source,
                   GraphPin* target, Graph* owner);

    bool IsValid() const;
    void Disconnect();

    GraphPin* GetSource() const { return sourcePin; }
    GraphPin* GetTarget() const { return targetPin; }
};

// Base node class
class GraphNode {
protected:
    std::string id;                      // Unique node identifier
    std::string name;                    // Display name
    std::string typeId;                  // Node type identifier
    Graph* ownerGraph;                   // Parent graph

    // Pin management
    std::vector<std::unique_ptr<GraphPin>> inputPins;
    std::vector<std::unique_ptr<GraphPin>> outputPins;

    // Visual properties
    glm::vec2 position;                  // Position in graph space
    glm::vec2 size;                      // Node size
    glm::vec4 color;                     // Node color

    // State
    bool selected;
    bool enabled;

public:
    GraphNode(std::string id, std::string typeId, Graph* owner);
    virtual ~GraphNode() = default;

    // Pin management
    GraphPin* AddInputPin(std::string name, PinType type,
                         std::string dataType = "");
    GraphPin* AddOutputPin(std::string name, PinType type,
                          std::string dataType = "");
    GraphPin* FindPin(const std::string& pinId);

    // Execution (for executable graphs)
    virtual void Execute() {}

    // Validation
    virtual bool Validate() const { return true; }

    // Serialization
    virtual nlohmann::json Serialize() const;
    virtual void Deserialize(const nlohmann::json& data);

    // Getters/Setters
    const std::string& GetId() const { return id; }
    const std::string& GetName() const { return name; }
    const std::string& GetTypeId() const { return typeId; }
    glm::vec2 GetPosition() const { return position; }
    void SetPosition(glm::vec2 pos) { position = pos; }
    bool IsSelected() const { return selected; }
    void SetSelected(bool sel) { selected = sel; }
};

// Graph container
class Graph {
    std::string id;                      // Unique graph identifier
    std::string name;                    // Graph name

    // Node and connection storage
    std::unordered_map<std::string, std::unique_ptr<GraphNode>> nodes;
    std::unordered_map<std::string, std::unique_ptr<GraphConnection>> connections;

    // Node type registry
    static std::unordered_map<std::string,
        std::function<std::unique_ptr<GraphNode>(std::string, Graph*)>> nodeFactory;

public:
    Graph(std::string id, std::string name);

    // Node management
    GraphNode* CreateNode(const std::string& typeId, glm::vec2 position);
    void RemoveNode(const std::string& nodeId);
    GraphNode* FindNode(const std::string& nodeId);
    const auto& GetNodes() const { return nodes; }

    // Connection management
    GraphConnection* CreateConnection(const std::string& sourceNodeId,
                                     const std::string& sourcePinId,
                                     const std::string& targetNodeId,
                                     const std::string& targetPinId);
    void RemoveConnection(const std::string& connectionId);
    const auto& GetConnections() const { return connections; }

    // Node type registration
    static void RegisterNodeType(const std::string& typeId,
        std::function<std::unique_ptr<GraphNode>(std::string, Graph*)> factory);

    // Execution
    void Execute();

    // Validation
    bool Validate() const;
    bool HasCycles() const;

    // Serialization
    nlohmann::json Serialize() const;
    static std::unique_ptr<Graph> Deserialize(const nlohmann::json& data);

    // Clear
    void Clear();
};
```

#### 1.2 Example Node Types

```cpp
// Math node - performs arithmetic operations
class MathNode : public GraphNode {
    enum class Operation { Add, Subtract, Multiply, Divide, Power };
    Operation operation;

public:
    MathNode(std::string id, Graph* owner);
    void Execute() override;
};

// Value node - outputs a constant value
class ValueNode : public GraphNode {
    std::any value;
    std::string valueType;

public:
    ValueNode(std::string id, Graph* owner);
    void SetValue(const std::any& val, const std::string& type);
};

// Shader node - represents a shader stage
class ShaderNode : public GraphNode {
    std::string shaderCode;
    VkShaderStageFlagBits stage;

public:
    ShaderNode(std::string id, Graph* owner);
    void Execute() override;
};

// Texture node - loads and outputs texture data
class TextureNode : public GraphNode {
    std::string texturePath;
    TextureData* textureData;

public:
    TextureNode(std::string id, Graph* owner);
    void Execute() override;
};

// Print node - debug output
class PrintNode : public GraphNode {
public:
    PrintNode(std::string id, Graph* owner);
    void Execute() override;
};
```

### 2. Node Editor (Interaction Layer)

Handles user interaction with the graph - selection, dragging, connection creation.

#### 2.1 GraphEditor Class

```cpp
enum class EditorMode {
    Select,          // Default mode - select and move nodes
    Pan,             // Pan the canvas
    Connect,         // Creating a connection
    BoxSelect        // Box selection mode
};

class GraphEditor {
    Graph* activeGraph;                  // Current graph being edited

    // Camera/View
    glm::vec2 viewOffset;                // Pan offset
    float viewZoom;                      // Zoom level (1.0 = 100%)
    glm::vec2 viewSize;                  // Viewport size

    // Interaction state
    EditorMode mode;
    std::vector<GraphNode*> selectedNodes;
    GraphConnection* selectedConnection;

    // Connection creation state
    GraphPin* connectionStartPin;
    glm::vec2 connectionEndPoint;

    // Dragging state
    bool isDragging;
    glm::vec2 dragStart;
    glm::vec2 dragCurrent;
    std::unordered_map<GraphNode*, glm::vec2> dragOriginalPositions;

    // History for undo/redo
    std::vector<std::unique_ptr<EditorCommand>> undoStack;
    std::vector<std::unique_ptr<EditorCommand>> redoStack;

public:
    GraphEditor();

    // Graph management
    void SetActiveGraph(Graph* graph);
    Graph* GetActiveGraph() const { return activeGraph; }

    // View control
    void SetViewOffset(glm::vec2 offset) { viewOffset = offset; }
    void SetViewZoom(float zoom);
    glm::vec2 ScreenToGraph(glm::vec2 screenPos) const;
    glm::vec2 GraphToScreen(glm::vec2 graphPos) const;

    // Selection
    void SelectNode(GraphNode* node, bool addToSelection = false);
    void DeselectNode(GraphNode* node);
    void ClearSelection();
    void SelectNodesInRect(glm::vec2 min, glm::vec2 max);
    const std::vector<GraphNode*>& GetSelectedNodes() const { return selectedNodes; }

    // Node operations
    GraphNode* CreateNode(const std::string& typeId, glm::vec2 position);
    void DeleteSelectedNodes();
    void DuplicateSelectedNodes();

    // Connection operations
    void StartConnection(GraphPin* pin);
    void UpdateConnection(glm::vec2 mousePos);
    void CompleteConnection(GraphPin* pin);
    void CancelConnection();
    void DeleteConnection(GraphConnection* connection);

    // Input handling
    void OnMouseDown(int button, glm::vec2 position);
    void OnMouseUp(int button, glm::vec2 position);
    void OnMouseMove(glm::vec2 position);
    void OnMouseScroll(float delta);
    void OnKeyPress(int key, bool ctrl, bool shift, bool alt);

    // Hit testing
    GraphNode* NodeAtPosition(glm::vec2 graphPos);
    GraphPin* PinAtPosition(glm::vec2 graphPos);
    GraphConnection* ConnectionAtPosition(glm::vec2 graphPos, float tolerance = 5.0f);

    // Undo/Redo
    void ExecuteCommand(std::unique_ptr<EditorCommand> command);
    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    // Update (called per frame)
    void Update(float deltaTime);
};
```

#### 2.2 Command Pattern for Undo/Redo

```cpp
// Base command class
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual std::string GetDescription() const = 0;
};

// Create node command
class CreateNodeCommand : public EditorCommand {
    Graph* graph;
    std::string nodeId;
    std::string typeId;
    glm::vec2 position;
    std::unique_ptr<GraphNode> nodeData; // For undo

public:
    CreateNodeCommand(Graph* graph, std::string typeId, glm::vec2 pos);
    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Create Node"; }
};

// Delete node command
class DeleteNodeCommand : public EditorCommand {
    Graph* graph;
    std::string nodeId;
    std::unique_ptr<GraphNode> nodeData;
    std::vector<std::unique_ptr<GraphConnection>> affectedConnections;

public:
    DeleteNodeCommand(Graph* graph, std::string nodeId);
    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Delete Node"; }
};

// Move nodes command
class MoveNodesCommand : public EditorCommand {
    std::unordered_map<GraphNode*, glm::vec2> oldPositions;
    std::unordered_map<GraphNode*, glm::vec2> newPositions;

public:
    MoveNodesCommand(const std::vector<GraphNode*>& nodes, glm::vec2 delta);
    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Move Nodes"; }
};

// Create connection command
class CreateConnectionCommand : public EditorCommand {
    Graph* graph;
    std::string connectionId;
    std::string sourceNodeId;
    std::string sourcePinId;
    std::string targetNodeId;
    std::string targetPinId;

public:
    CreateConnectionCommand(Graph* graph, GraphPin* source, GraphPin* target);
    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Create Connection"; }
};
```

### 3. Graph Renderer (Visualization Layer)

Renders the graph using existing Vulkan infrastructure.

#### 3.1 GraphRenderer Class

```cpp
class GraphRenderer {
    VulkanRenderer* vulkanRenderer;      // Reference to existing renderer
    GraphEditor* editor;                 // Reference to editor for view transform

    // Rendering resources
    std::unique_ptr<VulkanDrawable> nodeDrawable;
    std::unique_ptr<VulkanDrawable> connectionDrawable;
    std::unique_ptr<VulkanDrawable> pinDrawable;
    std::unique_ptr<VulkanDrawable> selectionDrawable;

    // Shaders
    VulkanShader* nodeShader;
    VulkanShader* connectionShader;
    VulkanShader* textShader;

    // Textures/Icons
    std::unordered_map<std::string, TextureData*> nodeIcons;

    // Text rendering
    std::unique_ptr<TextRenderer> textRenderer;

    // Render settings
    struct RenderSettings {
        glm::vec4 backgroundColor = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
        glm::vec4 gridColor = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
        glm::vec4 nodeColor = glm::vec4(0.3f, 0.3f, 0.35f, 1.0f);
        glm::vec4 nodeSelectedColor = glm::vec4(0.8f, 0.5f, 0.2f, 1.0f);
        glm::vec4 connectionColor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
        float nodeCornerRadius = 5.0f;
        float pinRadius = 6.0f;
        float connectionThickness = 3.0f;
        float gridSpacing = 20.0f;
        bool showGrid = true;
    } settings;

public:
    GraphRenderer(VulkanRenderer* renderer, GraphEditor* editor);

    // Initialization
    void Initialize();
    void LoadResources();

    // Rendering
    void Render(VkCommandBuffer commandBuffer);
    void RenderGrid(VkCommandBuffer commandBuffer);
    void RenderConnections(VkCommandBuffer commandBuffer, const Graph* graph);
    void RenderNodes(VkCommandBuffer commandBuffer, const Graph* graph);
    void RenderPins(VkCommandBuffer commandBuffer, const GraphNode* node);
    void RenderSelection(VkCommandBuffer commandBuffer);
    void RenderConnectionInProgress(VkCommandBuffer commandBuffer);

    // Helpers
    void RenderNode(VkCommandBuffer commandBuffer, const GraphNode* node);
    void RenderConnection(VkCommandBuffer commandBuffer, const GraphConnection* connection);
    void RenderBezierCurve(VkCommandBuffer commandBuffer,
                          glm::vec2 start, glm::vec2 end,
                          glm::vec4 color, float thickness);

    // Settings
    RenderSettings& GetSettings() { return settings; }
};
```

#### 3.2 Connection Rendering Strategy

Connections will use cubic Bezier curves for smooth, natural-looking connections:

```cpp
// Generate Bezier curve points
std::vector<glm::vec2> GenerateBezierCurve(glm::vec2 p0, glm::vec2 p3,
                                          int segments = 32) {
    std::vector<glm::vec2> points;

    // Control points offset based on horizontal distance
    float controlPointOffset = std::abs(p3.x - p0.x) * 0.5f;
    glm::vec2 p1 = p0 + glm::vec2(controlPointOffset, 0.0f);
    glm::vec2 p2 = p3 - glm::vec2(controlPointOffset, 0.0f);

    // Generate curve segments
    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        glm::vec2 point = uuu * p0 + 3.0f * uu * t * p1 +
                         3.0f * u * tt * p2 + ttt * p3;
        points.push_back(point);
    }

    return points;
}
```

### 4. Input Handler (Control Layer)

Handles platform-specific input and translates to editor actions.

#### 4.1 InputHandler Class

```cpp
enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2
};

enum class KeyCode {
    Delete,
    Escape,
    A, C, V, X, Z, Y,  // Common shortcuts
    Space,
    Shift, Ctrl, Alt
};

class InputHandler {
    GraphEditor* editor;
    HWND windowHandle;                   // Platform-specific window handle

    // Mouse state
    glm::vec2 mousePosition;
    glm::vec2 lastMousePosition;
    bool mouseButtonStates[3];

    // Keyboard state
    std::unordered_map<KeyCode, bool> keyStates;
    bool ctrlPressed;
    bool shiftPressed;
    bool altPressed;

    // Event queue
    struct InputEvent {
        enum class Type { MouseDown, MouseUp, MouseMove, MouseScroll, KeyDown, KeyUp };
        Type type;
        int button;
        KeyCode key;
        glm::vec2 position;
        float scrollDelta;
    };
    std::queue<InputEvent> eventQueue;

public:
    InputHandler(GraphEditor* editor, HWND window);

    // Platform integration
    void RegisterWindowCallbacks();
    static LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam);

    // Event processing
    void ProcessEvents();
    void HandleMouseDown(MouseButton button, glm::vec2 position);
    void HandleMouseUp(MouseButton button, glm::vec2 position);
    void HandleMouseMove(glm::vec2 position);
    void HandleMouseScroll(float delta);
    void HandleKeyDown(KeyCode key);
    void HandleKeyUp(KeyCode key);

    // State queries
    bool IsMouseButtonDown(MouseButton button) const;
    bool IsKeyDown(KeyCode key) const;
    glm::vec2 GetMousePosition() const { return mousePosition; }
    glm::vec2 GetMouseDelta() const { return mousePosition - lastMousePosition; }

    // Shortcuts
    void RegisterShortcut(KeyCode key, bool ctrl, bool shift, bool alt,
                         std::function<void()> callback);
};
```

#### 4.2 Common Shortcuts

```
Ctrl+C          - Copy selected nodes
Ctrl+V          - Paste nodes
Ctrl+X          - Cut selected nodes
Ctrl+D          - Duplicate selected nodes
Delete          - Delete selected nodes/connections
Ctrl+Z          - Undo
Ctrl+Y          - Redo
Ctrl+A          - Select all nodes
Space+Drag      - Pan view
Mouse Wheel     - Zoom
Middle Mouse    - Pan view
Right Click     - Context menu
```

### 5. UI System (Interface Layer)

Provides UI panels, menus, and widgets. Consider using Dear ImGui for rapid development.

#### 5.1 ImGui Integration

```cpp
class GraphEditorUI {
    GraphEditor* editor;
    Graph* graph;

    // UI state
    bool showNodePalette;
    bool showProperties;
    bool showHistory;
    std::string searchFilter;

public:
    GraphEditorUI(GraphEditor* editor);

    // Initialization
    void Initialize(VkInstance instance, VkDevice device,
                   VkRenderPass renderPass, VkQueue queue);

    // Rendering
    void BeginFrame();
    void Render();
    void EndFrame();

    // UI Panels
    void RenderMenuBar();
    void RenderNodePalette();      // Node creation palette
    void RenderPropertiesPanel();  // Selected node properties
    void RenderHistoryPanel();     // Undo/redo history
    void RenderContextMenu();      // Right-click menu

    // Dialogs
    void ShowNewGraphDialog();
    void ShowOpenGraphDialog();
    void ShowSaveGraphDialog();
    void ShowAboutDialog();
};
```

#### 5.2 Node Palette

```cpp
// Node palette - categorized list of available node types
struct NodeCategory {
    std::string name;
    std::vector<std::string> nodeTypes;
};

class NodePalette {
    std::vector<NodeCategory> categories;
    std::string searchFilter;

public:
    NodePalette() {
        // Initialize categories
        categories.push_back({"Math", {"Add", "Subtract", "Multiply", "Divide"}});
        categories.push_back({"Values", {"Float", "Vector2", "Vector3", "Color"}});
        categories.push_back({"Graphics", {"Shader", "Texture", "RenderPass"}});
        categories.push_back({"Logic", {"Branch", "Switch", "Compare"}});
        categories.push_back({"Debug", {"Print", "Visualize"}});
    }

    void Render();
    void OnNodeSelected(const std::string& typeId);
};
```

### 6. Serialization (Persistence Layer)

Save and load graphs to/from files.

#### 6.1 Serialization Format (JSON)

```json
{
  "version": "1.0",
  "graph": {
    "id": "main_graph",
    "name": "My Graph",
    "nodes": [
      {
        "id": "node_001",
        "type": "MathNode",
        "name": "Add",
        "position": [100.0, 200.0],
        "properties": {
          "operation": "Add"
        },
        "inputs": [
          {"id": "input_a", "name": "A", "type": "Data", "dataType": "float"},
          {"id": "input_b", "name": "B", "type": "Data", "dataType": "float"}
        ],
        "outputs": [
          {"id": "output", "name": "Result", "type": "Data", "dataType": "float"}
        ]
      },
      {
        "id": "node_002",
        "type": "ValueNode",
        "name": "Value",
        "position": [0.0, 180.0],
        "properties": {
          "value": 5.0,
          "valueType": "float"
        },
        "outputs": [
          {"id": "output", "name": "Value", "type": "Data", "dataType": "float"}
        ]
      }
    ],
    "connections": [
      {
        "id": "conn_001",
        "source": {"node": "node_002", "pin": "output"},
        "target": {"node": "node_001", "pin": "input_a"}
      }
    ]
  }
}
```

#### 6.2 Serialization Manager

```cpp
class GraphSerializer {
public:
    // Save graph to file
    static bool SaveToFile(const Graph* graph, const std::string& filepath);

    // Load graph from file
    static std::unique_ptr<Graph> LoadFromFile(const std::string& filepath);

    // Clipboard operations (for copy/paste)
    static std::string SerializeNodes(const std::vector<GraphNode*>& nodes);
    static std::vector<std::unique_ptr<GraphNode>>
        DeserializeNodes(const std::string& data, Graph* targetGraph);
};
```

### 7. Graph Execution (Runtime Layer)

Execute graphs for various purposes (data processing, rendering pipeline generation, etc.)

#### 7.1 Graph Executor

```cpp
enum class ExecutionResult {
    Success,
    Error,
    Incomplete
};

class GraphExecutor {
    Graph* graph;

    // Execution state
    std::unordered_map<GraphNode*, bool> executedNodes;
    std::unordered_map<GraphPin*, std::any> pinValues;

public:
    GraphExecutor(Graph* graph);

    // Execution
    ExecutionResult Execute();
    ExecutionResult ExecuteNode(GraphNode* node);

    // Topological sort for execution order
    std::vector<GraphNode*> GetExecutionOrder();

    // Value management
    void SetPinValue(GraphPin* pin, const std::any& value);
    std::any GetPinValue(GraphPin* pin) const;

    // Reset
    void Reset();
};
```

## Meta-Development: Using Graphs to Develop Itself

### Vision

The ultimate goal is to create a **self-developing system** where the graph editor can be extended and modified through visual programming.

### Meta-Development Features

#### 1. Visual Node Type Definition

Create new node types visually using a "Node Blueprint" graph:

```
[Input Pins Graph] → [Node Logic Graph] → [Output Pins Graph] → [New Node Type]
```

Example: Creating a custom "Lerp" node visually:
- Define inputs: A (float), B (float), T (float)
- Define logic: (1-T) * A + T * B
- Define output: Result (float)
- Register as new node type

#### 2. Graph-Based Shader Creation

Design shaders visually and compile to SPIR-V:

```
[Vertex Inputs] → [Transform Operations] → [Fragment Processing] → [Output] → [SPIR-V Shader]
```

Benefits:
- Visual debugging of shader logic
- Reusable shader node libraries
- Live shader preview
- Automatic SPIR-V generation

#### 3. Visual Pipeline Editor

Design Vulkan rendering pipelines as graphs:

```
[Vertex Buffer] → [Vertex Shader] → [Rasterizer] → [Fragment Shader] → [Framebuffer]
```

This allows:
- Visual understanding of rendering pipeline
- Easy experimentation with different configurations
- Pipeline presets and templates

#### 4. UI Layout Graphs

Design UI layouts using graphs:

```
[Container Node] → [Layout Node] → [Widget Nodes] → [UI Layout]
```

Eventually, the graph editor's own UI could be defined and modified through graphs.

#### 5. Data Processing Pipelines

Create data transformation pipelines:

```
[Load File] → [Parse] → [Transform] → [Filter] → [Export]
```

Use cases:
- Texture processing
- Mesh generation
- Asset pipeline automation

### Implementation Strategy for Meta-Development

#### Phase 1: Foundation (Weeks 1-4)
- Core graph data structures
- Basic node types (math, values, debug)
- Simple graph execution

#### Phase 2: Editor (Weeks 5-8)
- Visual editor with pan/zoom
- Node creation and connection
- Selection and manipulation
- Basic UI

#### Phase 3: Advanced Features (Weeks 9-12)
- Undo/redo system
- Copy/paste
- Save/load
- Node library

#### Phase 4: Meta-Development (Weeks 13-16)
- Node blueprint system
- Custom node type registration
- Graph-based shader generation
- Pipeline visualization

#### Phase 5: Self-Development (Weeks 17+)
- UI graph system
- Editor customization through graphs
- Plugin system
- Community node libraries

## Integration with Existing VIXEN Architecture

### Integration Points

#### 1. VulkanApplication Integration

```cpp
class VulkanApplication {
    // Existing members...

    // NEW: Graph editor integration
    std::unique_ptr<GraphEditor> graphEditor;
    std::unique_ptr<GraphRenderer> graphRenderer;
    std::unique_ptr<GraphEditorUI> graphEditorUI;
    std::unique_ptr<InputHandler> inputHandler;

    // NEW: Editor mode flag
    bool editorMode;

public:
    // Modified lifecycle methods
    VkResult Initialize();         // Initialize graph editor
    VkResult Prepare();           // Prepare graph rendering resources
    void Update();                // Update editor state
    void render();                // Render graph or application
    void DeInitialize();          // Cleanup graph resources

    // NEW: Editor control
    void SetEditorMode(bool enabled);
    bool IsEditorMode() const { return editorMode; }
};
```

#### 2. Dual-Mode Operation

The application will support two modes:

**Editor Mode** (default):
- Render graph editor
- Handle graph editing input
- Show UI panels
- Execute graphs on demand

**Runtime Mode**:
- Execute graph continuously
- Render graph output
- Hide editor UI

Toggle with F1 key or UI button.

#### 3. Rendering Integration

```cpp
void VulkanApplication::render() {
    if (editorMode) {
        // Render graph editor
        graphRenderer->Render(commandBuffer);
        graphEditorUI->Render();
    } else {
        // Execute and render graph output
        graphEditor->GetActiveGraph()->Execute();
        // Render resulting graphics
        vulkanRenderer->Render();
    }
}
```

#### 4. Resource Sharing

The graph editor will reuse existing Vulkan resources:
- VulkanRenderer for rendering
- VulkanShader for custom shaders
- VulkanDrawable for UI elements
- TextureLoader for icons
- Logger for debugging

### File Structure Integration

```
VIXEN/
├── source/
│   ├── Graph/                          # NEW: Graph system
│   │   ├── Graph.cpp
│   │   ├── GraphNode.cpp
│   │   ├── GraphConnection.cpp
│   │   ├── GraphPin.cpp
│   │   └── Nodes/                      # Node implementations
│   │       ├── MathNodes.cpp
│   │       ├── ValueNodes.cpp
│   │       ├── ShaderNodes.cpp
│   │       └── DebugNodes.cpp
│   ├── GraphEditor/                    # NEW: Editor system
│   │   ├── GraphEditor.cpp
│   │   ├── GraphRenderer.cpp
│   │   ├── GraphEditorUI.cpp
│   │   ├── InputHandler.cpp
│   │   ├── GraphSerializer.cpp
│   │   ├── GraphExecutor.cpp
│   │   └── Commands/                   # Undo/redo commands
│   │       ├── EditorCommand.cpp
│   │       ├── CreateNodeCommand.cpp
│   │       └── DeleteNodeCommand.cpp
│   ├── VulkanApplication.cpp           # MODIFIED: Add editor integration
│   └── main.cpp                        # MODIFIED: Initialize editor
├── include/
│   ├── Graph/                          # NEW: Graph headers
│   └── GraphEditor/                    # NEW: Editor headers
├── memory-bank/
│   └── graph-editor-plan.md           # THIS DOCUMENT
└── graphs/                             # NEW: Saved graphs
    ├── examples/
    └── templates/
```

## Dependencies

### Required New Dependencies

1. **Dear ImGui** (MIT License)
   - Purpose: UI framework for editor interface
   - Version: Latest (1.90+)
   - Integration: FetchContent in CMake

2. **nlohmann/json** (MIT License)
   - Purpose: JSON serialization for graph files
   - Version: 3.11.3+
   - Integration: Single-header include

3. **Optional: ImNodes** (MIT License)
   - Purpose: Node editor widgets for ImGui
   - Version: Latest
   - Note: May build custom solution instead

### CMakeLists.txt Updates

```cmake
# Add graph editor components
add_library(GraphSystem STATIC
    source/Graph/Graph.cpp
    source/Graph/GraphNode.cpp
    source/Graph/GraphConnection.cpp
    source/Graph/GraphPin.cpp
    source/Graph/Nodes/MathNodes.cpp
    source/Graph/Nodes/ValueNodes.cpp
    source/Graph/Nodes/ShaderNodes.cpp
    source/Graph/Nodes/DebugNodes.cpp
)

add_library(GraphEditor STATIC
    source/GraphEditor/GraphEditor.cpp
    source/GraphEditor/GraphRenderer.cpp
    source/GraphEditor/GraphEditorUI.cpp
    source/GraphEditor/InputHandler.cpp
    source/GraphEditor/GraphSerializer.cpp
    source/GraphEditor/GraphExecutor.cpp
    source/GraphEditor/Commands/EditorCommand.cpp
    source/GraphEditor/Commands/CreateNodeCommand.cpp
    source/GraphEditor/Commands/DeleteNodeCommand.cpp
)

# Fetch Dear ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)
FetchContent_MakeAvailable(imgui)

# Link graph system
target_link_libraries(${PROJECT_NAME} GraphSystem GraphEditor imgui)
```

## Technical Challenges and Solutions

### Challenge 1: Text Rendering

**Problem**: Vulkan has no built-in text rendering.

**Solutions**:
1. **ImGui text rendering** (recommended for initial implementation)
   - Pros: Easy integration, handles text automatically
   - Cons: Tied to ImGui rendering

2. **FreeType + custom renderer** (future improvement)
   - Pros: More control, better performance
   - Cons: More complex implementation

3. **Signed distance field (SDF) text** (advanced)
   - Pros: Sharp at any zoom level, GPU-friendly
   - Cons: Complex setup, requires preprocessing

**Recommendation**: Start with ImGui text, migrate to SDF later for production quality.

### Challenge 2: High DPI Support

**Problem**: Different monitor DPIs require scaling.

**Solution**:
```cpp
float GetDPIScale() {
    HDC screen = GetDC(NULL);
    float dpiX = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX));
    ReleaseDC(NULL, screen);
    return dpiX / 96.0f; // 96 is standard DPI
}
```

Apply DPI scaling to all UI elements and fonts.

### Challenge 3: Performance with Large Graphs

**Problem**: Rendering thousands of nodes and connections can be slow.

**Solutions**:
1. **Frustum culling**: Only render visible nodes
2. **Level of detail**: Simplify rendering at low zoom levels
3. **Instanced rendering**: Batch similar nodes
4. **Spatial partitioning**: Quadtree for fast queries

```cpp
// Example: Frustum culling
bool GraphRenderer::IsNodeVisible(const GraphNode* node) {
    glm::vec2 nodeMin = node->GetPosition();
    glm::vec2 nodeMax = nodeMin + node->GetSize();
    glm::vec2 viewMin = editor->GetViewOffset();
    glm::vec2 viewMax = viewMin + editor->GetViewSize() / editor->GetViewZoom();

    return !(nodeMax.x < viewMin.x || nodeMin.x > viewMax.x ||
             nodeMax.y < viewMin.y || nodeMin.y > viewMax.y);
}
```

### Challenge 4: Graph Cycles

**Problem**: Cycles in the graph can cause infinite loops during execution.

**Solution**: Implement cycle detection using depth-first search:

```cpp
bool Graph::HasCycles() const {
    std::unordered_set<GraphNode*> visited;
    std::unordered_set<GraphNode*> recursionStack;

    for (const auto& [id, node] : nodes) {
        if (HasCyclesUtil(node.get(), visited, recursionStack)) {
            return true;
        }
    }
    return false;
}

bool Graph::HasCyclesUtil(GraphNode* node,
                         std::unordered_set<GraphNode*>& visited,
                         std::unordered_set<GraphNode*>& recursionStack) const {
    visited.insert(node);
    recursionStack.insert(node);

    // Check all output connections
    for (const auto& pin : node->GetOutputPins()) {
        for (const auto& connection : pin->GetConnections()) {
            GraphNode* targetNode = connection->GetTarget()->GetOwnerNode();

            if (recursionStack.find(targetNode) != recursionStack.end()) {
                return true; // Cycle detected
            }

            if (visited.find(targetNode) == visited.end()) {
                if (HasCyclesUtil(targetNode, visited, recursionStack)) {
                    return true;
                }
            }
        }
    }

    recursionStack.erase(node);
    return false;
}
```

### Challenge 5: Thread Safety

**Problem**: Graph execution might need to run on separate thread.

**Solution**: Use mutex for graph modifications, reader-writer lock for execution:

```cpp
class Graph {
    mutable std::shared_mutex graphMutex;

public:
    // Write operations (exclusive lock)
    GraphNode* CreateNode(const std::string& typeId, glm::vec2 position) {
        std::unique_lock lock(graphMutex);
        // ... create node
    }

    // Read operations (shared lock)
    void Execute() const {
        std::shared_lock lock(graphMutex);
        // ... execute graph
    }
};
```

## Testing Strategy

### Unit Tests

```cpp
// Test graph node creation
TEST(GraphTest, CreateNode) {
    Graph graph("test", "Test Graph");
    GraphNode* node = graph.CreateNode("MathNode", glm::vec2(0, 0));
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(graph.GetNodes().size(), 1);
}

// Test connection creation
TEST(GraphTest, CreateConnection) {
    Graph graph("test", "Test Graph");
    GraphNode* node1 = graph.CreateNode("ValueNode", glm::vec2(0, 0));
    GraphNode* node2 = graph.CreateNode("MathNode", glm::vec2(100, 0));

    GraphConnection* conn = graph.CreateConnection(
        node1->GetId(), node1->GetOutputPins()[0]->GetId(),
        node2->GetId(), node2->GetInputPins()[0]->GetId()
    );

    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->IsValid());
}

// Test cycle detection
TEST(GraphTest, DetectCycles) {
    Graph graph("test", "Test Graph");
    GraphNode* node1 = graph.CreateNode("MathNode", glm::vec2(0, 0));
    GraphNode* node2 = graph.CreateNode("MathNode", glm::vec2(100, 0));

    // Create cycle: node1 -> node2 -> node1
    graph.CreateConnection(node1->GetId(), "output", node2->GetId(), "input_a");
    graph.CreateConnection(node2->GetId(), "output", node1->GetId(), "input_a");

    EXPECT_TRUE(graph.HasCycles());
}

// Test serialization
TEST(GraphTest, Serialization) {
    Graph graph("test", "Test Graph");
    graph.CreateNode("ValueNode", glm::vec2(0, 0));

    nlohmann::json data = graph.Serialize();
    auto loadedGraph = Graph::Deserialize(data);

    EXPECT_EQ(loadedGraph->GetNodes().size(), 1);
}
```

### Integration Tests

1. **Editor interaction tests**: Simulate mouse/keyboard input
2. **Rendering tests**: Verify graph renders correctly
3. **Execution tests**: Verify graphs execute correctly
4. **Performance tests**: Measure frame rate with various graph sizes

### Manual Testing Checklist

- [ ] Create nodes from palette
- [ ] Connect nodes with drag and drop
- [ ] Select and move nodes
- [ ] Delete nodes and connections
- [ ] Undo/redo operations
- [ ] Copy/paste nodes
- [ ] Save and load graphs
- [ ] Pan and zoom viewport
- [ ] Execute graphs
- [ ] Node property editing
- [ ] Context menus
- [ ] Keyboard shortcuts
- [ ] Multiple selection
- [ ] Box selection

## Implementation Roadmap

### Phase 1: Foundation (4 weeks)

**Week 1: Core Data Structures**
- [ ] Implement Graph class
- [ ] Implement GraphNode base class
- [ ] Implement GraphPin class
- [ ] Implement GraphConnection class
- [ ] Unit tests for core classes

**Week 2: Basic Node Types**
- [ ] Implement MathNode (Add, Subtract, Multiply, Divide)
- [ ] Implement ValueNode (Float, Vec2, Vec3)
- [ ] Implement PrintNode (Debug output)
- [ ] Node factory system
- [ ] Node type registration

**Week 3: Graph Execution**
- [ ] Implement GraphExecutor
- [ ] Topological sort
- [ ] Cycle detection
- [ ] Data flow between nodes
- [ ] Execution tests

**Week 4: Serialization**
- [ ] JSON serialization for Graph
- [ ] JSON serialization for Nodes
- [ ] Save to file
- [ ] Load from file
- [ ] Serialization tests

### Phase 2: Editor (4 weeks)

**Week 5: GraphEditor Core**
- [ ] Implement GraphEditor class
- [ ] View transformation (pan, zoom)
- [ ] Screen-to-graph coordinate conversion
- [ ] Selection system
- [ ] Editor state management

**Week 6: Interaction**
- [ ] Implement InputHandler
- [ ] Mouse event handling
- [ ] Keyboard event handling
- [ ] Node dragging
- [ ] Connection creation

**Week 7: Commands & Undo**
- [ ] Command pattern base
- [ ] CreateNodeCommand
- [ ] DeleteNodeCommand
- [ ] MoveNodesCommand
- [ ] CreateConnectionCommand
- [ ] Undo/redo system

**Week 8: Hit Testing**
- [ ] Node hit testing
- [ ] Pin hit testing
- [ ] Connection hit testing
- [ ] Box selection
- [ ] Connection hovering

### Phase 3: Rendering (4 weeks)

**Week 9: GraphRenderer Core**
- [ ] Implement GraphRenderer class
- [ ] Integration with VulkanRenderer
- [ ] Basic node rendering (rectangles)
- [ ] Grid rendering
- [ ] View transformation in shaders

**Week 10: Connection Rendering**
- [ ] Bezier curve generation
- [ ] Connection line rendering
- [ ] Connection coloring
- [ ] Connection thickness
- [ ] Connection hovering/selection

**Week 11: Advanced Node Rendering**
- [ ] Rounded corners
- [ ] Node headers
- [ ] Pin rendering
- [ ] Selection highlighting
- [ ] Node icons

**Week 12: Text & Polish**
- [ ] Integrate Dear ImGui
- [ ] Text rendering for node names
- [ ] Text rendering for pin names
- [ ] Anti-aliasing
- [ ] Visual polish

### Phase 4: UI System (4 weeks)

**Week 13: ImGui Integration**
- [ ] Set up Dear ImGui with Vulkan
- [ ] ImGui rendering in editor mode
- [ ] Menu bar
- [ ] Basic window management

**Week 14: Node Palette**
- [ ] Node palette window
- [ ] Categorized node list
- [ ] Search/filter
- [ ] Drag-to-create nodes
- [ ] Node documentation

**Week 15: Properties Panel**
- [ ] Properties panel window
- [ ] Show selected node properties
- [ ] Edit node properties
- [ ] Pin value editing
- [ ] Multi-selection properties

**Week 16: Tools & Polish**
- [ ] History panel (undo/redo list)
- [ ] Context menus
- [ ] Keyboard shortcuts
- [ ] Settings dialog
- [ ] Help/about dialog

### Phase 5: Meta-Development (4+ weeks)

**Week 17: Node Blueprints**
- [ ] Design node blueprint system
- [ ] Graph-based node definition
- [ ] Custom node type registration
- [ ] Blueprint execution

**Week 18: Shader Graphs**
- [ ] Shader node types
- [ ] Graph-to-GLSL compiler
- [ ] SPIR-V generation
- [ ] Live shader preview

**Week 19: Pipeline Graphs**
- [ ] Pipeline node types
- [ ] Vulkan pipeline visualization
- [ ] Pipeline graph execution
- [ ] Pipeline presets

**Week 20+: Advanced Features**
- [ ] UI layout graphs
- [ ] Data processing graphs
- [ ] Plugin system
- [ ] Node library system
- [ ] Community features

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Create nodes visually
- [ ] Connect nodes with bezier curves
- [ ] Select and move nodes
- [ ] Delete nodes and connections
- [ ] Save and load graphs
- [ ] Execute simple math graphs
- [ ] Undo/redo
- [ ] Basic UI with node palette

### Full Feature Set
- [ ] All MVP features
- [ ] Copy/paste nodes
- [ ] Node property editing
- [ ] Multiple node types (10+)
- [ ] Graph validation
- [ ] Performance: 60 FPS with 100+ nodes
- [ ] Shader graph generation
- [ ] Node blueprints

### Meta-Development Goals
- [ ] Create new node types visually
- [ ] Design shaders through graphs
- [ ] Visualize rendering pipelines
- [ ] Build UI through graphs
- [ ] Self-documenting system

## Documentation Requirements

### User Documentation
1. **Getting Started Guide**
   - Installation
   - First graph
   - Basic operations
   - Shortcuts

2. **User Manual**
   - Interface overview
   - Node types reference
   - Graph execution
   - Saving/loading

3. **Tutorial Series**
   - Creating a math graph
   - Building a shader
   - Designing a pipeline
   - Creating custom nodes

### Developer Documentation
1. **Architecture Document** (this document)

2. **API Reference**
   - Graph API
   - Node API
   - Editor API
   - Rendering API

3. **Extension Guide**
   - Creating custom nodes
   - Adding new node types
   - Plugin development
   - Contributing

## Risk Analysis

### Technical Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Text rendering complexity | High | Medium | Use ImGui initially, defer custom solution |
| Performance with large graphs | High | High | Implement culling and LOD early |
| ImGui Vulkan integration issues | Medium | Low | Use proven imgui_impl_vulkan backend |
| Thread safety bugs | High | Medium | Careful design, extensive testing |
| Serialization version conflicts | Medium | Medium | Version field in JSON, migration system |

### Schedule Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Underestimated complexity | High | High | Phased approach, MVP first |
| Scope creep | High | High | Strict phase boundaries, feature freeze |
| Integration issues | Medium | Medium | Early integration, continuous testing |
| Learning curve (Vulkan) | Low | Low | Already have Vulkan foundation |

### Organizational Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Feature overload | High | High | Focus on MVP, defer advanced features |
| Poor UX design | Medium | Medium | User testing, iterate on feedback |
| Inadequate documentation | Medium | High | Document as you build, not after |

## Future Enhancements

### Near-term (3-6 months)
- Minimap for large graphs
- Graph thumbnail previews
- Node grouping/subgraphs
- Custom node colors
- Connection rerouting
- Alignment tools
- Grid snapping

### Mid-term (6-12 months)
- Collaborative editing
- Version control integration
- Node libraries/marketplace
- Graph debugging tools
- Performance profiling
- Animation timeline
- Scripting support (Lua/Python)

### Long-term (12+ months)
- Cloud graph storage
- AI-assisted graph generation
- Visual debugging with breakpoints
- Live collaborative editing
- Mobile/web viewer
- VR/AR graph editing
- Game engine integration

## References

### Existing Node Editors
- Unreal Engine Blueprints
- Unity Shader Graph
- Blender Geometry Nodes
- Houdini
- TouchDesigner

### Libraries & Resources
- Dear ImGui: https://github.com/ocornut/imgui
- ImNodes: https://github.com/Nelarius/imnodes
- nlohmann/json: https://github.com/nlohmann/json
- Vulkan Tutorial: https://vulkan-tutorial.com/

### Design Patterns
- Command Pattern (undo/redo)
- Factory Pattern (node creation)
- Observer Pattern (graph changes)
- Singleton Pattern (application)
- RAII (resource management)

## Conclusion

This graph editor will transform VIXEN from a Vulkan learning project into a powerful visual programming environment. By leveraging the existing rendering infrastructure and following a phased approach, we can build a system that:

1. **Serves immediate needs** - Visual programming for various tasks
2. **Enables meta-development** - Tool that can develop itself
3. **Maintains quality** - Follows established patterns and principles
4. **Scales effectively** - Architected for growth and extension

The key to success is:
- Start with MVP and iterate
- Maintain code quality throughout
- Test continuously
- Document as you build
- Keep the vision of meta-development in mind

This planning document should be treated as a living document, updated as implementation progresses and new insights are gained.

---

**Document Version**: 1.0
**Last Updated**: 2025-10-26
**Author**: Claude Code
**Status**: Planning Phase
