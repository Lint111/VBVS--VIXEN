# Meta-Development: Self-Hosting Graph Editor

## Vision

Build the graph editor application **itself as a RenderGraph**, where UI components, input handling, and rendering are all represented as nodes. This creates a self-hosting system where:

1. **The editor is a graph** - UI panels, canvas, input handlers are all nodes
2. **The editor edits graphs** - Including the graph that defines itself
3. **Bootstrap from code** - Initial hardcoded graph gets the editor running
4. **Self-modify** - Save the editor graph to JSON, load it next time
5. **True meta-development** - Tool that builds and modifies itself

## Architecture: Editor as a Graph

### Conceptual Flow

```
┌────────────────────────────────────────────────────────────────┐
│                  EDITOR APPLICATION GRAPH                      │
│                                                                │
│  ┌──────────┐    ┌──────────┐    ┌──────────────┐            │
│  │ Window   │───►│SwapChain │───►│ EditorState  │            │
│  │  Node    │    │  Node    │    │    Node      │            │
│  └──────────┘    └──────────┘    └──────┬───────┘            │
│                                          │                     │
│  ┌──────────┐    ┌──────────┐    ┌──────▼───────┐            │
│  │  Input   │───►│  Event   │───►│   Command    │            │
│  │ Handler  │    │Dispatcher│    │   Processor  │            │
│  │  Node    │    │   Node   │    │     Node     │            │
│  └──────────┘    └──────────┘    └──────┬───────┘            │
│                                          │                     │
│  ┌──────────┐    ┌──────────┐    ┌──────▼───────┐            │
│  │  Node    │    │  Graph   │◄───│  Properties  │            │
│  │ Palette  │    │  Canvas  │    │    Panel     │            │
│  │  Node    │    │   Node   │    │     Node     │            │
│  └──────────┘    └──────────┘    └──────────────┘            │
│       │               │                    │                  │
│       └───────────────┴────────────────────┘                  │
│                       │                                        │
│                  ┌────▼─────┐                                 │
│                  │ Framebuf │                                 │
│                  │   Node   │                                 │
│                  └────┬─────┘                                 │
│                       │                                        │
│                  ┌────▼─────┐                                 │
│                  │ Present  │                                 │
│                  │   Node   │                                 │
│                  └──────────┘                                 │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Key Insight

The **EditorStateNode** holds the "graph being edited" as data. This graph could be:
- A rendering pipeline graph
- A data processing graph
- **The editor graph itself** (self-modification!)

## Node Type Catalog: Editor Components

### 1. Core UI Node Types

#### EditorStateNode
```cpp
/**
 * @brief Central state for the graph editor
 *
 * Type ID: 200
 *
 * Inputs:
 * - commands: EditorCommand queue (from CommandProcessorNode)
 *
 * Outputs:
 * - editorState: EditorState struct (current graph, selection, camera)
 * - graphData: Graph being edited (serialized or live reference)
 */
class EditorStateNode : public TypedNode<EditorStateNodeConfig> {
    // Holds the graph currently being edited
    std::unique_ptr<EditorGraph> currentGraph;

    // Selection
    std::vector<std::string> selectedNodeIds;

    // Camera state
    glm::vec2 cameraPosition;
    float cameraZoom;

    // Undo/redo stacks
    std::vector<std::unique_ptr<EditorCommand>> undoStack;
    std::vector<std::unique_ptr<EditorCommand>> redoStack;

public:
    void Setup() override;
    void Execute(VkCommandBuffer cmd) override;

    // Apply commands from input
    void ProcessCommands(const std::vector<EditorCommand*>& commands);

    // Serialization
    nlohmann::json SerializeCurrentGraph() const;
    void LoadGraph(const nlohmann::json& data);
};
```

#### InputHandlerNode
```cpp
/**
 * @brief Processes raw input events
 *
 * Type ID: 201
 *
 * Inputs:
 * - window: VkSurfaceKHR (for capturing input)
 *
 * Outputs:
 * - inputEvents: Queue of InputEvent structs
 * - mouseState: MouseState (position, buttons)
 * - keyboardState: KeyboardState (modifiers, pressed keys)
 */
class InputHandlerNode : public TypedNode<InputHandlerNodeConfig> {
    // Raw input state
    glm::vec2 mousePosition;
    std::array<bool, 3> mouseButtons;
    std::unordered_set<int> pressedKeys;

    // Event queue
    std::queue<InputEvent> eventQueue;

public:
    void Setup() override;
    void Execute(VkCommandBuffer cmd) override;

    // Platform-specific polling
    void PollEvents();

    // Output events
    std::queue<InputEvent> GetEvents();
};
```

#### EventDispatcherNode
```cpp
/**
 * @brief Translates input events to editor commands
 *
 * Type ID: 202
 *
 * Inputs:
 * - inputEvents: Queue of InputEvent (from InputHandlerNode)
 * - editorState: EditorState (current state context)
 *
 * Outputs:
 * - commands: Queue of EditorCommand (for EditorStateNode)
 */
class EventDispatcherNode : public TypedNode<EventDispatcherNodeConfig> {
public:
    void Execute(VkCommandBuffer cmd) override;

    // Translate raw events to commands
    std::vector<EditorCommand*> ProcessEvents(
        const std::queue<InputEvent>& events,
        const EditorState& state
    );

    // Examples:
    // MouseDown on node + Drag → MoveNodeCommand
    // MouseDown on pin + Drag to pin → CreateConnectionCommand
    // KeyPress Delete → DeleteSelectedNodesCommand
};
```

#### GraphCanvasNode
```cpp
/**
 * @brief Renders the graph canvas (nodes, connections)
 *
 * Type ID: 203
 *
 * Inputs:
 * - editorState: EditorState (graph to render)
 * - renderPass: VkRenderPass
 * - framebuffer: VkFramebuffer
 *
 * Outputs:
 * - renderCommands: VkCommandBuffer with draw calls
 */
class GraphCanvasNode : public TypedNode<GraphCanvasNodeConfig> {
    // Rendering resources
    VulkanShader* nodeShader;
    VulkanShader* bezierShader;
    VulkanDrawable* nodeQuads;
    VulkanDrawable* connectionLines;

public:
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer cmd) override;

    // Rendering methods
    void RenderGrid(VkCommandBuffer cmd, const EditorState& state);
    void RenderConnections(VkCommandBuffer cmd, const EditorGraph& graph);
    void RenderNodes(VkCommandBuffer cmd, const EditorGraph& graph);
    void RenderSelection(VkCommandBuffer cmd, const EditorState& state);
};
```

#### NodePaletteNode
```cpp
/**
 * @brief Renders the node palette panel (ImGui)
 *
 * Type ID: 204
 *
 * Inputs:
 * - nodeTypeRegistry: NodeTypeRegistry* (available node types)
 *
 * Outputs:
 * - createNodeCommands: Queue of CreateNodeCommand
 */
class NodePaletteNode : public TypedNode<NodePaletteNodeConfig> {
    NodeTypeRegistry* registry;
    std::string searchFilter;

    struct Category {
        std::string name;
        std::vector<NodeTypeId> nodeTypes;
    };
    std::vector<Category> categories;

public:
    void Setup() override;
    void Execute(VkCommandBuffer cmd) override;

    // ImGui rendering
    void RenderPalette();

    // When user clicks a node type, emit CreateNodeCommand
    void OnNodeTypeSelected(const std::string& typeName);
};
```

#### PropertiesPanelNode
```cpp
/**
 * @brief Renders the properties panel for selected nodes
 *
 * Type ID: 205
 *
 * Inputs:
 * - editorState: EditorState (selected nodes)
 *
 * Outputs:
 * - parameterCommands: Queue of SetParameterCommand
 */
class PropertiesPanelNode : public TypedNode<PropertiesPanelNodeConfig> {
public:
    void Execute(VkCommandBuffer cmd) override;

    // ImGui rendering
    void RenderProperties(const std::vector<EditorNode*>& selectedNodes);

    // Emit commands when properties change
    void OnParameterChanged(const std::string& nodeId,
                           const std::string& paramName,
                           const std::any& value);
};
```

#### MenuBarNode
```cpp
/**
 * @brief Renders the menu bar (File, Edit, View, Graph, Help)
 *
 * Type ID: 206
 *
 * Inputs:
 * - editorState: EditorState (for undo/redo availability)
 *
 * Outputs:
 * - menuCommands: Queue of commands (New, Open, Save, Undo, etc.)
 */
class MenuBarNode : public TypedNode<MenuBarNodeConfig> {
public:
    void Execute(VkCommandBuffer cmd) override;

    // ImGui menu bar
    void RenderMenuBar();

    // Menu actions
    void OnFileNew();
    void OnFileOpen();
    void OnFileSave();
    void OnEditUndo();
    void OnEditRedo();
    void OnGraphCompile();
    void OnGraphExecute();
};
```

### 2. Data Structure: EditorState

```cpp
/**
 * @brief Complete editor state that flows through the graph
 */
struct EditorState {
    // The graph being edited
    std::unique_ptr<EditorGraph> currentGraph;

    // Selection
    std::vector<std::string> selectedNodeIds;
    std::vector<std::string> selectedConnectionIds;

    // Camera
    glm::vec2 cameraPosition;
    float cameraZoom;
    glm::vec2 viewSize;

    // Interaction state
    enum class Tool { Select, Pan, Connect };
    Tool currentTool;

    bool isDragging;
    glm::vec2 dragStart;

    // Connection creation
    std::string connectionSourceNodeId;
    uint32_t connectionSourcePin;
    glm::vec2 connectionEndPoint;

    // Undo/redo
    size_t undoStackSize;
    size_t redoStackSize;

    // Compiled state
    bool graphCompiled;
    bool graphExecuting;

    // File state
    std::string currentFilePath;
    bool hasUnsavedChanges;
};
```

### 3. Data Structure: InputEvent

```cpp
/**
 * @brief Input events flowing through the graph
 */
struct InputEvent {
    enum class Type {
        MouseDown, MouseUp, MouseMove, MouseScroll,
        KeyDown, KeyUp,
        WindowResize
    };

    Type type;

    // Mouse data
    glm::vec2 mousePosition;
    int mouseButton;              // 0=Left, 1=Right, 2=Middle
    float scrollDelta;

    // Keyboard data
    int keyCode;
    bool ctrlPressed;
    bool shiftPressed;
    bool altPressed;

    // Window data
    uint32_t newWidth;
    uint32_t newHeight;

    uint64_t timestamp;
};
```

## Bootstrap Strategy

### Phase 1: Hardcoded Bootstrap

Initially, the editor graph is defined in C++ code:

```cpp
/**
 * @brief Create the initial editor graph (hardcoded)
 */
RenderGraph* CreateBootstrapEditorGraph(
    NodeTypeRegistry* registry,
    VulkanDevice* device
) {
    auto graph = new RenderGraph(registry);

    // Setup nodes
    auto window = graph->AddNode("Window", "EditorWindow");
    auto swapchain = graph->AddNode("SwapChain", "EditorSwapChain");
    auto depthBuffer = graph->AddNode("DepthBuffer", "EditorDepth");

    // Editor state management
    auto editorState = graph->AddNode("EditorState", "MainEditorState");
    auto inputHandler = graph->AddNode("InputHandler", "MainInput");
    auto eventDispatcher = graph->AddNode("EventDispatcher", "MainDispatcher");

    // UI components
    auto canvas = graph->AddNode("GraphCanvas", "MainCanvas");
    auto palette = graph->AddNode("NodePalette", "MainPalette");
    auto properties = graph->AddNode("PropertiesPanel", "MainProperties");
    auto menuBar = graph->AddNode("MenuBar", "MainMenu");

    // Rendering pipeline
    auto renderPass = graph->AddNode("RenderPass", "EditorRenderPass");
    auto framebuffer = graph->AddNode("Framebuffer", "EditorFramebuffer");
    auto present = graph->AddNode("Present", "EditorPresent");

    // Connect the graph
    // Window → SwapChain
    graph->ConnectNodes(window, 0, swapchain, 0);  // surface

    // Input flow
    graph->ConnectNodes(inputHandler, 0, eventDispatcher, 0);  // events
    graph->ConnectNodes(editorState, 0, eventDispatcher, 1);   // state context
    graph->ConnectNodes(eventDispatcher, 0, editorState, 0);   // commands

    // Canvas rendering
    graph->ConnectNodes(editorState, 0, canvas, 0);            // state
    graph->ConnectNodes(renderPass, 0, canvas, 1);             // render pass
    graph->ConnectNodes(framebuffer, 0, canvas, 2);            // framebuffer

    // UI panels
    graph->ConnectNodes(editorState, 0, palette, 0);           // state
    graph->ConnectNodes(editorState, 0, properties, 0);        // state
    graph->ConnectNodes(editorState, 0, menuBar, 0);           // state

    // Panel commands back to state
    graph->ConnectNodes(palette, 0, editorState, 1);           // create commands
    graph->ConnectNodes(properties, 0, editorState, 2);        // param commands
    graph->ConnectNodes(menuBar, 0, editorState, 3);           // menu commands

    // Presentation
    graph->ConnectNodes(swapchain, 0, present, 0);             // swapchain
    graph->ConnectNodes(canvas, 0, present, 1);                // rendered image

    return graph;
}
```

### Phase 2: Serialize Bootstrap

After the editor is running, save its own graph definition:

```cpp
// In the editor application
void OnSaveEditorGraph() {
    // Get the currently running editor graph
    RenderGraph* editorGraph = GetCurrentRenderGraph();

    // Serialize it (including node positions for visual editing)
    nlohmann::json serialized = SerializeGraphWithVisualState(editorGraph);

    // Save to special file
    std::ofstream file("editor_graph.json");
    file << serialized.dump(2);
}
```

### Phase 3: Load from JSON

Next time the application starts, load the editor from the saved graph:

```cpp
int main() {
    NodeTypeRegistry registry;
    RegisterAllNodeTypes(&registry);  // Including UI node types

    RenderGraph* editorGraph = nullptr;

    // Try to load saved editor graph
    if (std::filesystem::exists("editor_graph.json")) {
        std::ifstream file("editor_graph.json");
        nlohmann::json data = nlohmann::json::parse(file);
        editorGraph = DeserializeGraph(data, &registry);
    } else {
        // Fall back to bootstrap
        editorGraph = CreateBootstrapEditorGraph(&registry, device);
    }

    // Compile and run
    editorGraph->Compile();

    // Main loop
    while (running) {
        editorGraph->Execute(commandBuffer);
    }
}
```

### Phase 4: Self-Modification

The editor can now modify itself:

1. **Open editor_graph.json** in the editor itself
2. **Edit the graph visually** - Add/remove UI panels, change layout, modify behavior
3. **Save changes** - Overwrites editor_graph.json
4. **Restart application** - Editor now has the new behavior!

Example self-modifications:
- Add a new "History Panel" node
- Rearrange UI panel layout
- Change input handling behavior
- Add custom shortcuts
- Modify rendering style

## Data Flow: Editing a Graph While Being a Graph

### The Editor Graph (running)
```
InputHandlerNode → EventDispatcherNode → EditorStateNode
                                              ↓
                                         [Holds Graph A]
                                              ↓
                                         GraphCanvasNode
                                              ↓
                                         (Renders Graph A)
```

### Two Scenarios

#### Scenario 1: Editing a Rendering Pipeline
```
EditorStateNode.currentGraph = "rendering_pipeline.json"
  → User adds TextureLoaderNode to the pipeline
  → Canvas renders the pipeline graph
  → User clicks "Compile"
  → Pipeline graph is compiled and can execute
```

#### Scenario 2: Editing the Editor Itself
```
EditorStateNode.currentGraph = "editor_graph.json" (ITSELF!)
  → User adds HistoryPanelNode to the editor
  → Canvas renders the editor graph (meta-view!)
  → User clicks "Save"
  → Editor saves its own modified definition
  → User restarts → New HistoryPanel now appears!
```

## Implementation Phases

### Phase 1: Core UI Nodes (2 weeks)
- [ ] EditorStateNode
- [ ] InputHandlerNode
- [ ] EventDispatcherNode
- [ ] GraphCanvasNode (basic rendering)
- [ ] Test: Render a simple graph

### Phase 2: UI Panels (2 weeks)
- [ ] NodePaletteNode
- [ ] PropertiesPanelNode
- [ ] MenuBarNode
- [ ] ImGui integration with RenderGraph
- [ ] Test: Create nodes from palette

### Phase 3: Command System (1 week)
- [ ] EditorCommand base class
- [ ] Command implementations (Create, Delete, Move, Connect, SetParameter)
- [ ] Undo/redo in EditorStateNode
- [ ] Test: Undo/redo operations

### Phase 4: Bootstrap (1 week)
- [ ] CreateBootstrapEditorGraph function
- [ ] Run editor as a graph
- [ ] Test: Editor functions correctly

### Phase 5: Serialization (1 week)
- [ ] Serialize editor graph with visual state
- [ ] Deserialize and reconstruct editor
- [ ] Save/load editor_graph.json
- [ ] Test: Load editor from JSON

### Phase 6: Self-Modification (1 week)
- [ ] Open editor_graph.json in the editor
- [ ] Edit and save changes
- [ ] Verify changes persist after restart
- [ ] Test: Add a new panel and see it appear

**Total: 8 weeks to self-hosting editor**

## Advantages of This Approach

### 1. Ultimate Consistency
Everything is a node. Everything is a graph. No special cases.

### 2. Visual Debugging
You can see the editor's own execution flow as a graph!

### 3. Extensibility
Want a new feature? Just add a node. The editor itself is extensible through its own interface.

### 4. Serializable
The editor's entire state and behavior can be saved and versioned.

### 5. Composable
UI panels are composable nodes - rearrange them visually.

### 6. Live Reload
Change editor_graph.json → Reload → New behavior (no recompilation!)

### 7. Community Extensions
Users can create custom editor layouts and share them as JSON files.

## Comparison to Traditional Approach

### Traditional (Original Plan)
```cpp
class GraphEditor {
    EditorGraph editorGraph;      // Hardcoded C++ class
    NodePalette palette;          // Hardcoded C++ class
    PropertiesPanel properties;   // Hardcoded C++ class
    GraphCanvas canvas;           // Hardcoded C++ class

    void Update() {
        // Hardcoded update logic
        inputHandler.Update();
        canvas.Render();
        palette.Render();
        properties.Render();
    }
};
```

To change behavior → Recompile C++

### Self-Hosting (This Approach)
```cpp
RenderGraph* editorGraph = LoadGraph("editor_graph.json");

editorGraph->Execute(commandBuffer);  // That's it!

// Editor behavior is defined by the graph structure
```

To change behavior → Edit graph JSON → Reload

## Technical Challenges

### Challenge 1: ImGui Integration with RenderGraph

**Problem**: ImGui expects immediate-mode rendering, RenderGraph is retained-mode.

**Solution**: Create an `ImGuiContextNode` that wraps ImGui:

```cpp
class ImGuiContextNode : public TypedNode<ImGuiContextNodeConfig> {
    ImGuiContext* imguiContext;

public:
    void Setup() override {
        IMGUI_CHECKVERSION();
        imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(imguiContext);
        ImGui_ImplVulkan_Init(...);
    }

    void Execute(VkCommandBuffer cmd) override {
        ImGui::SetCurrentContext(imguiContext);
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        // UI nodes will call ImGui functions here

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
};
```

### Challenge 2: Circular References

**Problem**: Editor graph contains EditorStateNode which contains the graph being edited, which might be the editor graph itself!

**Solution**: Use handles/IDs instead of direct pointers:

```cpp
struct EditorState {
    std::string currentGraphId;       // "editor_graph" or "pipeline_graph"

    // Lookup in global graph registry
    RenderGraph* GetCurrentGraph() const {
        return GraphRegistry::Get(currentGraphId);
    }
};
```

### Challenge 3: Real-Time Editing

**Problem**: User is editing Graph A while the editor (Graph B) is running. How do we preview Graph A's execution?

**Solution**: Multi-graph execution:

```cpp
// Main loop
while (running) {
    // Execute the editor graph
    editorGraph->Execute(commandBuffer);

    // If user clicks "Preview", also execute the edited graph
    if (previewMode && editedGraph->IsCompiled()) {
        editedGraph->Execute(commandBuffer);
    }
}
```

## Example: Adding a History Panel to the Editor

### Step 1: User opens editor_graph.json in the editor

```json
{
  "nodes": [
    {"id": "1", "type": "EditorState", ...},
    {"id": "2", "type": "NodePalette", ...},
    {"id": "3", "type": "PropertiesPanel", ...},
    {"id": "4", "type": "GraphCanvas", ...}
  ],
  "connections": [...]
}
```

### Step 2: User drags "HistoryPanelNode" from palette

The editor (running as a graph) processes this:
```
InputHandlerNode → EventDispatcherNode → EditorStateNode
                                              ↓
                                    [Executes CreateNodeCommand]
                                              ↓
                                    Graph now has HistoryPanelNode
```

### Step 3: User connects HistoryPanelNode

```
EditorStateNode → HistoryPanelNode (shows undo/redo history)
```

### Step 4: User saves editor_graph.json

```json
{
  "nodes": [
    {"id": "1", "type": "EditorState", ...},
    {"id": "2", "type": "NodePalette", ...},
    {"id": "3", "type": "PropertiesPanel", ...},
    {"id": "4", "type": "GraphCanvas", ...},
    {"id": "5", "type": "HistoryPanel", ...}  // NEW!
  ],
  "connections": [
    ...,
    {"source": "1", "target": "5", ...}       // NEW!
  ]
}
```

### Step 5: User restarts application

```cpp
RenderGraph* editorGraph = LoadGraph("editor_graph.json");
// Now includes HistoryPanelNode!

editorGraph->Compile();
editorGraph->Execute(commandBuffer);
// HistoryPanel now appears in the UI!
```

## Conclusion

**Yes, the editor can absolutely be constructed using the RenderGraph system and represent itself!**

This creates a truly self-hosting, meta-development environment where:
- ✅ The editor is a graph
- ✅ The editor edits graphs (including itself)
- ✅ Changes persist through serialization
- ✅ No recompilation needed for editor modifications
- ✅ Community can share custom editor layouts
- ✅ Ultimate consistency: everything is a node

This is the **purest form of meta-development** - the tool literally builds and modifies itself through its own interface.

**Recommended Path**:
1. Start with traditional approach (faster to MVP)
2. Migrate to self-hosting incrementally
3. Eventually achieve full meta-development

Or, if you're ambitious, **go straight for self-hosting** - it's more complex but philosophically beautiful and practically powerful.

---

**Document Version**: 1.0
**Last Updated**: 2025-10-26
**Author**: Claude Code
**Status**: Conceptual Design - Ready for Discussion
