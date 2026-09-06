# Graph Editor as a RenderGraph Application

## Core Insight

The MCP (Model-View-Controller) pattern is **only needed for the graph editor application**, not for the core RenderGraph framework itself. More importantly, the graph editor application should be **built using RenderGraph nodes**, creating a meta-circular design where the framework is used to build tools for editing itself.

---

## Architecture Philosophy

```
┌─────────────────────────────────────────────────────────────┐
│         RenderGraph Framework (Core - Unchanged)            │
│  • NodeInstance, RenderGraph, TypedNode, Connections        │
│  • Resource management, Compilation, Execution              │
│  • All existing 45+ node types                              │
└─────────────────────────────────────────────────────────────┘
                          ↑ Uses
┌─────────────────────────────────────────────────────────────┐
│      Graph Editor Application (Built with RenderGraph)      │
│                                                              │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐│
│  │  Model Nodes   │  │  View Nodes    │  │ Control Nodes  ││
│  │  (Data)        │  │  (Rendering)   │  │ (Processing)   ││
│  ├────────────────┤  ├────────────────┤  ├────────────────┤│
│  │ GraphStateNode │→ │ NodeRenderer   │← │ InputHandler   ││
│  │ NodeRegistryN. │→ │ ConnectionRend.│← │ ValidationNode ││
│  │ SelectionNode  │→ │ UIRenderer     │← │ CommandNode    ││
│  └────────────────┘  └────────────────┘  └────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

**Key Principles:**

1. **Core RenderGraph** - Pure data flow framework (no MCP, no GUI dependencies)
2. **Editor Application** - Composed from RenderGraph nodes
3. **Meta-Circular** - Use the framework to build tools for itself
4. **Separation** - MCP concerns only in editor nodes, not in core

---

## Editor Application Node Types

### Category 1: Model Nodes (Data Storage)

These nodes store and manage the state of the graph being edited.

#### GraphStateNode

**Purpose:** Stores the current graph being edited

```cpp
CONSTEXPR_NODE_CONFIG(GraphStateNodeConfig, 2, 5) {
    // Inputs
    INPUT_SLOT(COMMAND_STREAM, CommandBuffer, 0, Optional, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(SERIALIZED_DATA, std::string, 1, Optional, Dependency, ReadOnly, NodeLevel);

    // Outputs
    OUTPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 0, Required, ReadOnly);
    OUTPUT_SLOT(CONNECTION_LIST, std::vector<EditorConnectionData>, 1, Required, ReadOnly);
    OUTPUT_SLOT(SELECTION_STATE, SelectionState, 2, Required, ReadOnly);
    OUTPUT_SLOT(GRAPH_DIRTY, bool, 3, Required, ReadOnly);
    OUTPUT_SLOT(VALIDATION_RESULT, ValidationResult, 4, Required, ReadOnly);
};

struct EditorNodeData {
    std::string instanceName;
    std::string nodeTypeName;
    Vec2 position;
    Color color;
    std::map<std::string, std::any> parameters;
};

struct EditorConnectionData {
    std::string sourceNode;
    uint32_t sourceSlot;
    std::string targetNode;
    uint32_t targetSlot;
};

class GraphStateNode : public TypedNode<GraphStateNodeType, GraphStateNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        // Process incoming commands (add node, delete node, etc.)
        if (auto* commandBuffer = In<COMMAND_STREAM>(ctx)) {
            ProcessCommands(*commandBuffer);
        }

        // Update outputs
        Out<NODE_LIST>(ctx) = currentNodes_;
        Out<CONNECTION_LIST>(ctx) = currentConnections_;
        Out<SELECTION_STATE>(ctx) = selectionState_;
        Out<GRAPH_DIRTY>(ctx) = isDirty_;
    }

private:
    std::vector<EditorNodeData> currentNodes_;
    std::vector<EditorConnectionData> currentConnections_;
    SelectionState selectionState_;
    bool isDirty_ = false;

    void ProcessCommands(const CommandBuffer& commands);
};
```

---

#### NodeRegistryNode

**Purpose:** Stores metadata about available node types (the "palette")

```cpp
CONSTEXPR_NODE_CONFIG(NodeRegistryNodeConfig, 0, 2) {
    // No inputs (static data)

    // Outputs
    OUTPUT_SLOT(NODE_TYPE_DESCRIPTORS, std::vector<NodeTypeDescriptor>, 0, Required, ReadOnly);
    OUTPUT_SLOT(CATEGORY_MAP, std::map<std::string, std::vector<std::string>>, 1, Required, ReadOnly);
};

class NodeRegistryNode : public TypedNode<NodeRegistryNodeType, NodeRegistryNodeConfig> {
    void Compile(CompileContext& ctx) override {
        // Load all registered node types
        LoadNodeTypeDescriptors();

        Out<NODE_TYPE_DESCRIPTORS>(ctx) = descriptors_;
        Out<CATEGORY_MAP>(ctx) = categoryMap_;
    }

private:
    std::vector<NodeTypeDescriptor> descriptors_;
    std::map<std::string, std::vector<std::string>> categoryMap_;

    void LoadNodeTypeDescriptors();
};
```

---

#### SelectionNode

**Purpose:** Manages node/connection selection state

```cpp
CONSTEXPR_NODE_CONFIG(SelectionNodeConfig, 2, 3) {
    // Inputs
    INPUT_SLOT(MOUSE_EVENTS, MouseEventStream, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(NODE_BOUNDS, std::vector<Rect>, 1, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(SELECTED_NODES, std::vector<uint32_t>, 0, Required, ReadOnly);
    OUTPUT_SLOT(SELECTED_CONNECTIONS, std::vector<uint32_t>, 1, Required, ReadOnly);
    OUTPUT_SLOT(SELECTION_BOX, std::optional<Rect>, 2, Required, ReadOnly);
};

class SelectionNode : public TypedNode<SelectionNodeType, SelectionNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& mouseEvents = In<MOUSE_EVENTS>(ctx);
        auto& nodeBounds = In<NODE_BOUNDS>(ctx);

        // Process mouse events for selection
        for (const auto& event : mouseEvents) {
            if (event.type == MouseEventType::Click) {
                HandleClickSelection(event.position, nodeBounds);
            } else if (event.type == MouseEventType::DragStart) {
                BeginBoxSelection(event.position);
            } else if (event.type == MouseEventType::Drag) {
                UpdateBoxSelection(event.position);
            }
        }

        Out<SELECTED_NODES>(ctx) = selectedNodes_;
        Out<SELECTED_CONNECTIONS>(ctx) = selectedConnections_;
        Out<SELECTION_BOX>(ctx) = currentSelectionBox_;
    }

private:
    std::vector<uint32_t> selectedNodes_;
    std::vector<uint32_t> selectedConnections_;
    std::optional<Rect> currentSelectionBox_;
};
```

---

### Category 2: View Nodes (Rendering)

These nodes handle visual representation of the graph editor.

#### NodeRendererNode

**Purpose:** Renders nodes visually on the canvas

```cpp
CONSTEXPR_NODE_CONFIG(NodeRendererNodeConfig, 5, 1) {
    // Inputs
    INPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(SELECTED_NODES, std::vector<uint32_t>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CAMERA_TRANSFORM, Mat3, 2, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(RENDER_TARGET, VkImage, 3, Required, Execute, ReadWrite, TaskLevel);
    INPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 4, Required, Execute, ReadWrite, TaskLevel);

    // Outputs
    OUTPUT_SLOT(NODE_BOUNDS, std::vector<Rect>, 0, Required, ReadOnly);
};

class NodeRendererNode : public TypedNode<NodeRendererNodeType, NodeRendererNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& nodeList = In<NODE_LIST>(ctx);
        auto& selectedNodes = In<SELECTED_NODES>(ctx);
        auto& cameraTransform = In<CAMERA_TRANSFORM>(ctx);
        auto* commandBuffer = In<COMMAND_BUFFER>(ctx);

        std::vector<Rect> bounds;

        for (size_t i = 0; i < nodeList.size(); ++i) {
            const auto& nodeData = nodeList[i];

            // Transform to screen space
            Vec2 screenPos = TransformPoint(cameraTransform, nodeData.position);

            // Determine visual state
            bool isSelected = std::find(selectedNodes.begin(), selectedNodes.end(), i)
                              != selectedNodes.end();

            // Render node
            RenderNodeVisual(commandBuffer, nodeData, screenPos, isSelected);

            // Store bounds for hit testing
            bounds.push_back(Rect(screenPos, screenPos + Vec2(200, 150)));
        }

        Out<NODE_BOUNDS>(ctx) = bounds;
    }

private:
    void RenderNodeVisual(VkCommandBuffer* cmd, const EditorNodeData& node,
                          Vec2 position, bool selected);
};
```

---

#### ConnectionRendererNode

**Purpose:** Renders connection wires

```cpp
CONSTEXPR_NODE_CONFIG(ConnectionRendererNodeConfig, 5, 0) {
    // Inputs
    INPUT_SLOT(CONNECTION_LIST, std::vector<EditorConnectionData>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(SELECTED_CONNECTIONS, std::vector<uint32_t>, 2, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CAMERA_TRANSFORM, Mat3, 3, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 4, Required, Execute, ReadWrite, TaskLevel);

    // No outputs (pure rendering)
};

class ConnectionRendererNode : public TypedNode<ConnectionRendererNodeType, ConnectionRendererNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& connections = In<CONNECTION_LIST>(ctx);
        auto& nodes = In<NODE_LIST>(ctx);
        auto& selectedConnections = In<SELECTED_CONNECTIONS>(ctx);
        auto& cameraTransform = In<CAMERA_TRANSFORM>(ctx);
        auto* commandBuffer = In<COMMAND_BUFFER>(ctx);

        for (size_t i = 0; i < connections.size(); ++i) {
            const auto& conn = connections[i];

            // Find source and target node positions
            Vec2 sourcePos = FindNodePosition(nodes, conn.sourceNode);
            Vec2 targetPos = FindNodePosition(nodes, conn.targetNode);

            // Transform to screen space
            sourcePos = TransformPoint(cameraTransform, sourcePos);
            targetPos = TransformPoint(cameraTransform, targetPos);

            // Determine visual state
            bool isSelected = std::find(selectedConnections.begin(),
                                       selectedConnections.end(), i)
                              != selectedConnections.end();

            // Render Bezier curve
            RenderBezierWire(commandBuffer, sourcePos, targetPos, isSelected);
        }
    }

private:
    void RenderBezierWire(VkCommandBuffer* cmd, Vec2 start, Vec2 end, bool selected);
};
```

---

#### UIRendererNode

**Purpose:** Renders UI panels (toolbar, inspector, node palette)

```cpp
CONSTEXPR_NODE_CONFIG(UIRendererNodeConfig, 4, 0) {
    // Inputs
    INPUT_SLOT(SELECTED_NODES, std::vector<uint32_t>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(NODE_REGISTRY, std::vector<NodeTypeDescriptor>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(RENDER_TARGET, VkImage, 2, Required, Execute, ReadWrite, TaskLevel);
    INPUT_SLOT(COMMAND_BUFFER, VkCommandBuffer, 3, Required, Execute, ReadWrite, TaskLevel);
};

class UIRendererNode : public TypedNode<UIRendererNodeType, UIRendererNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& selectedNodes = In<SELECTED_NODES>(ctx);
        auto& nodeRegistry = In<NODE_REGISTRY>(ctx);
        auto* commandBuffer = In<COMMAND_BUFFER>(ctx);

        // Render toolbar
        RenderToolbar(commandBuffer);

        // Render node palette
        RenderNodePalette(commandBuffer, nodeRegistry);

        // Render inspector (if node selected)
        if (!selectedNodes.empty()) {
            RenderInspector(commandBuffer, selectedNodes[0]);
        }

        // Render status bar
        RenderStatusBar(commandBuffer);
    }

private:
    void RenderToolbar(VkCommandBuffer* cmd);
    void RenderNodePalette(VkCommandBuffer* cmd, const std::vector<NodeTypeDescriptor>& registry);
    void RenderInspector(VkCommandBuffer* cmd, uint32_t selectedNodeIndex);
    void RenderStatusBar(VkCommandBuffer* cmd);
};
```

---

### Category 3: Controller Nodes (Input & Processing)

These nodes handle user input and orchestrate operations.

#### InputHandlerNode

**Purpose:** Processes mouse and keyboard input

```cpp
CONSTEXPR_NODE_CONFIG(InputHandlerNodeConfig, 2, 3) {
    // Inputs
    INPUT_SLOT(WINDOW_HANDLE, void*, 0, Required, Execute, ReadOnly, NodeLevel);
    INPUT_SLOT(CAMERA_TRANSFORM, Mat3, 1, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(MOUSE_EVENTS, MouseEventStream, 0, Required, ReadOnly);
    OUTPUT_SLOT(KEYBOARD_EVENTS, KeyboardEventStream, 1, Required, ReadOnly);
    OUTPUT_SLOT(CAMERA_DELTA, CameraDelta, 2, Required, ReadOnly);
};

struct MouseEventStream {
    std::vector<MouseEvent> events;
};

struct KeyboardEventStream {
    std::vector<KeyEvent> events;
};

struct CameraDelta {
    Vec2 panDelta;
    float zoomDelta;
};

class InputHandlerNode : public TypedNode<InputHandlerNodeType, InputHandlerNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        // Poll input from window
        MouseEventStream mouseEvents;
        KeyboardEventStream keyboardEvents;
        CameraDelta cameraDelta;

        PollInputEvents(In<WINDOW_HANDLE>(ctx), mouseEvents, keyboardEvents);

        // Process camera controls
        ProcessCameraInput(keyboardEvents, mouseEvents, cameraDelta);

        Out<MOUSE_EVENTS>(ctx) = mouseEvents;
        Out<KEYBOARD_EVENTS>(ctx) = keyboardEvents;
        Out<CAMERA_DELTA>(ctx) = cameraDelta;
    }

private:
    void PollInputEvents(void* windowHandle, MouseEventStream& mouse, KeyboardEventStream& keyboard);
    void ProcessCameraInput(const KeyboardEventStream& keys, const MouseEventStream& mouse,
                           CameraDelta& delta);
};
```

---

#### ValidationNode

**Purpose:** Validates graph structure and connections

```cpp
CONSTEXPR_NODE_CONFIG(ValidationNodeConfig, 3, 1) {
    // Inputs
    INPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CONNECTION_LIST, std::vector<EditorConnectionData>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(NODE_REGISTRY, std::vector<NodeTypeDescriptor>, 2, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(VALIDATION_RESULT, ValidationResult, 0, Required, ReadOnly);
};

struct ValidationResult {
    bool isValid;
    std::vector<ValidationError> errors;
    std::vector<ValidationWarning> warnings;
};

class ValidationNode : public TypedNode<ValidationNodeType, ValidationNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& nodes = In<NODE_LIST>(ctx);
        auto& connections = In<CONNECTION_LIST>(ctx);
        auto& registry = In<NODE_REGISTRY>(ctx);

        ValidationResult result;

        // Check for cycles
        if (HasCycles(nodes, connections)) {
            result.errors.push_back({"Graph contains cycles"});
        }

        // Validate connection types
        for (const auto& conn : connections) {
            if (!AreTypesCompatible(conn, nodes, registry)) {
                result.errors.push_back({
                    "Type mismatch: " + conn.sourceNode + " -> " + conn.targetNode
                });
            }
        }

        // Check required slots
        for (const auto& node : nodes) {
            if (!AllRequiredSlotsConnected(node, connections, registry)) {
                result.warnings.push_back({
                    "Node '" + node.instanceName + "' has unconnected required slots"
                });
            }
        }

        result.isValid = result.errors.empty();
        Out<VALIDATION_RESULT>(ctx) = result;
    }

private:
    bool HasCycles(const std::vector<EditorNodeData>& nodes,
                   const std::vector<EditorConnectionData>& connections);
    bool AreTypesCompatible(const EditorConnectionData& conn,
                           const std::vector<EditorNodeData>& nodes,
                           const std::vector<NodeTypeDescriptor>& registry);
};
```

---

#### CommandProcessorNode

**Purpose:** Processes user commands (create node, delete node, connect, etc.)

```cpp
CONSTEXPR_NODE_CONFIG(CommandProcessorNodeConfig, 3, 1) {
    // Inputs
    INPUT_SLOT(MOUSE_EVENTS, MouseEventStream, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(KEYBOARD_EVENTS, KeyboardEventStream, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(UI_INTERACTIONS, UIInteractionStream, 2, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(COMMAND_STREAM, CommandBuffer, 0, Required, ReadOnly);
};

struct CommandBuffer {
    std::vector<std::unique_ptr<EditorCommand>> commands;
};

class CommandProcessorNode : public TypedNode<CommandProcessorNodeType, CommandProcessorNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& mouseEvents = In<MOUSE_EVENTS>(ctx);
        auto& keyboardEvents = In<KEYBOARD_EVENTS>(ctx);
        auto& uiInteractions = In<UI_INTERACTIONS>(ctx);

        CommandBuffer commandBuffer;

        // Process drag-and-drop (create node)
        for (const auto& interaction : uiInteractions.events) {
            if (interaction.type == UIInteractionType::NodePaletteDrag) {
                commandBuffer.commands.push_back(
                    std::make_unique<CreateNodeCommand>(
                        interaction.nodeTypeName,
                        interaction.dropPosition
                    )
                );
            }
        }

        // Process delete key (delete selected)
        for (const auto& keyEvent : keyboardEvents.events) {
            if (keyEvent.key == Key::Delete && keyEvent.action == KeyAction::Press) {
                commandBuffer.commands.push_back(
                    std::make_unique<DeleteSelectionCommand>()
                );
            }
        }

        // Process connection drag
        ProcessConnectionDrag(mouseEvents, commandBuffer);

        Out<COMMAND_STREAM>(ctx) = commandBuffer;
    }

private:
    void ProcessConnectionDrag(const MouseEventStream& mouse, CommandBuffer& buffer);
};
```

---

#### UndoRedoNode

**Purpose:** Manages command history for undo/redo

```cpp
CONSTEXPR_NODE_CONFIG(UndoRedoNodeConfig, 2, 1) {
    // Inputs
    INPUT_SLOT(COMMAND_STREAM, CommandBuffer, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(KEYBOARD_EVENTS, KeyboardEventStream, 1, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(PROCESSED_COMMANDS, CommandBuffer, 0, Required, ReadOnly);
};

class UndoRedoNode : public TypedNode<UndoRedoNodeType, UndoRedoNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& incomingCommands = In<COMMAND_STREAM>(ctx);
        auto& keyboardEvents = In<KEYBOARD_EVENTS>(ctx);

        CommandBuffer outputCommands;

        // Check for undo/redo hotkeys
        for (const auto& keyEvent : keyboardEvents.events) {
            if (keyEvent.key == Key::Z && keyEvent.modifiers & Modifier::Ctrl) {
                if (keyEvent.modifiers & Modifier::Shift) {
                    Redo(outputCommands);
                } else {
                    Undo(outputCommands);
                }
            }
        }

        // Process incoming commands
        for (auto& command : incomingCommands.commands) {
            commandHistory_.push_back(std::move(command));
            historyIndex_ = commandHistory_.size();
        }

        // Forward commands (or undo/redo results)
        Out<PROCESSED_COMMANDS>(ctx) = std::move(outputCommands.commands.empty()
            ? incomingCommands
            : outputCommands);
    }

private:
    std::vector<std::unique_ptr<EditorCommand>> commandHistory_;
    size_t historyIndex_ = 0;

    void Undo(CommandBuffer& output);
    void Redo(CommandBuffer& output);
};
```

---

### Category 4: Compilation & Execution Nodes

#### GraphCompilerNode

**Purpose:** Compiles the edited graph into an actual RenderGraph

```cpp
CONSTEXPR_NODE_CONFIG(GraphCompilerNodeConfig, 3, 2) {
    // Inputs
    INPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CONNECTION_LIST, std::vector<EditorConnectionData>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(COMPILE_TRIGGER, bool, 2, Required, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(COMPILED_GRAPH, RenderGraph*, 0, Optional, WriteOnly);
    OUTPUT_SLOT(COMPILATION_RESULT, CompilationResult, 1, Required, ReadOnly);
};

class GraphCompilerNode : public TypedNode<GraphCompilerNodeType, GraphCompilerNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        if (!In<COMPILE_TRIGGER>(ctx)) {
            return;  // No compilation requested
        }

        auto& nodes = In<NODE_LIST>(ctx);
        auto& connections = In<CONNECTION_LIST>(ctx);

        // Create new RenderGraph
        auto graph = std::make_unique<RenderGraph>();

        CompilationResult result;

        try {
            // Add nodes to graph
            for (const auto& nodeData : nodes) {
                AddNodeToGraph(*graph, nodeData);
            }

            // Add connections
            for (const auto& connData : connections) {
                AddConnectionToGraph(*graph, connData);
            }

            // Compile the graph
            graph->Compile();

            result.success = true;
            Out<COMPILED_GRAPH>(ctx) = graph.release();

        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = e.what();
        }

        Out<COMPILATION_RESULT>(ctx) = result;
    }

private:
    void AddNodeToGraph(RenderGraph& graph, const EditorNodeData& nodeData);
    void AddConnectionToGraph(RenderGraph& graph, const EditorConnectionData& connData);
};
```

---

### Category 5: Serialization Nodes

#### GraphSerializerNode

**Purpose:** Saves/loads graphs to/from files

```cpp
CONSTEXPR_NODE_CONFIG(GraphSerializerNodeConfig, 3, 1) {
    // Inputs
    INPUT_SLOT(NODE_LIST, std::vector<EditorNodeData>, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CONNECTION_LIST, std::vector<EditorConnectionData>, 1, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(SAVE_PATH, std::filesystem::path, 2, Optional, Execute, ReadOnly, TaskLevel);

    // Outputs
    OUTPUT_SLOT(SERIALIZED_DATA, std::string, 0, Required, ReadOnly);
};

class GraphSerializerNode : public TypedNode<GraphSerializerNodeType, GraphSerializerNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        auto& nodes = In<NODE_LIST>(ctx);
        auto& connections = In<CONNECTION_LIST>(ctx);

        // Serialize to JSON
        nlohmann::json j;
        j["version"] = 1;
        j["nodes"] = SerializeNodes(nodes);
        j["connections"] = SerializeConnections(connections);

        std::string serialized = j.dump(2);

        // Save to file if path provided
        if (auto* savePath = In<SAVE_PATH>(ctx)) {
            std::ofstream file(*savePath);
            file << serialized;
        }

        Out<SERIALIZED_DATA>(ctx) = serialized;
    }

private:
    nlohmann::json SerializeNodes(const std::vector<EditorNodeData>& nodes);
    nlohmann::json SerializeConnections(const std::vector<EditorConnectionData>& connections);
};
```

---

## Complete Graph Editor Application Graph

Now let's compose these nodes into the actual editor application:

```cpp
// GraphEditorApplication.cpp
class GraphEditorApplication {
public:
    void InitializeEditorGraph() {
        RenderGraph editorGraph;

        // ====================================================================
        // RESOURCE NODES
        // ====================================================================
        auto windowNode = editorGraph.AddNode<WindowNodeType, WindowNodeConfig>("editor_window");
        auto deviceNode = editorGraph.AddNode<DeviceNodeType, DeviceNodeConfig>("editor_device");
        auto swapChainNode = editorGraph.AddNode<SwapChainNodeType, SwapChainNodeConfig>("editor_swapchain");

        // ====================================================================
        // MODEL NODES (Data Storage)
        // ====================================================================
        auto graphStateNode = editorGraph.AddNode<GraphStateNodeType, GraphStateNodeConfig>("graph_state");
        auto nodeRegistryNode = editorGraph.AddNode<NodeRegistryNodeType, NodeRegistryNodeConfig>("node_registry");
        auto selectionNode = editorGraph.AddNode<SelectionNodeType, SelectionNodeConfig>("selection");
        auto cameraNode = editorGraph.AddNode<CameraNodeType, CameraNodeConfig>("editor_camera");

        // ====================================================================
        // CONTROLLER NODES (Input & Processing)
        // ====================================================================
        auto inputHandlerNode = editorGraph.AddNode<InputHandlerNodeType, InputHandlerNodeConfig>("input_handler");
        auto commandProcessorNode = editorGraph.AddNode<CommandProcessorNodeType, CommandProcessorNodeConfig>("command_processor");
        auto undoRedoNode = editorGraph.AddNode<UndoRedoNodeType, UndoRedoNodeConfig>("undo_redo");
        auto validationNode = editorGraph.AddNode<ValidationNodeType, ValidationNodeConfig>("validation");

        // ====================================================================
        // VIEW NODES (Rendering)
        // ====================================================================
        auto nodeRendererNode = editorGraph.AddNode<NodeRendererNodeType, NodeRendererNodeConfig>("node_renderer");
        auto connectionRendererNode = editorGraph.AddNode<ConnectionRendererNodeType, ConnectionRendererNodeConfig>("connection_renderer");
        auto uiRendererNode = editorGraph.AddNode<UIRendererNodeType, UIRendererNodeConfig>("ui_renderer");

        // ====================================================================
        // SERIALIZATION NODES
        // ====================================================================
        auto serializerNode = editorGraph.AddNode<GraphSerializerNodeType, GraphSerializerNodeConfig>("serializer");

        // ====================================================================
        // COMPILATION NODE
        // ====================================================================
        auto compilerNode = editorGraph.AddNode<GraphCompilerNodeType, GraphCompilerNodeConfig>("graph_compiler");

        // ====================================================================
        // CONNECTIONS
        // ====================================================================
        ConnectionBatch batch(editorGraph);

        // Window → SwapChain
        batch.Connect(windowNode, WindowNodeConfig::OUTPUT_SURFACE,
                     swapChainNode, SwapChainNodeConfig::INPUT_SURFACE);

        // InputHandler → Camera updates
        batch.Connect(inputHandlerNode, InputHandlerNodeConfig::OUTPUT_CAMERA_DELTA,
                     cameraNode, CameraNodeConfig::INPUT_DELTA);

        // InputHandler → Selection
        batch.Connect(inputHandlerNode, InputHandlerNodeConfig::OUTPUT_MOUSE_EVENTS,
                     selectionNode, SelectionNodeConfig::INPUT_MOUSE_EVENTS);

        // InputHandler → CommandProcessor
        batch.Connect(inputHandlerNode, InputHandlerNodeConfig::OUTPUT_MOUSE_EVENTS,
                     commandProcessorNode, CommandProcessorNodeConfig::INPUT_MOUSE_EVENTS);
        batch.Connect(inputHandlerNode, InputHandlerNodeConfig::OUTPUT_KEYBOARD_EVENTS,
                     commandProcessorNode, CommandProcessorNodeConfig::INPUT_KEYBOARD_EVENTS);

        // CommandProcessor → UndoRedo → GraphState
        batch.Connect(commandProcessorNode, CommandProcessorNodeConfig::OUTPUT_COMMAND_STREAM,
                     undoRedoNode, UndoRedoNodeConfig::INPUT_COMMAND_STREAM);
        batch.Connect(undoRedoNode, UndoRedoNodeConfig::OUTPUT_PROCESSED_COMMANDS,
                     graphStateNode, GraphStateNodeConfig::INPUT_COMMAND_STREAM);

        // GraphState → Validation
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_NODE_LIST,
                     validationNode, ValidationNodeConfig::INPUT_NODE_LIST);
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_CONNECTION_LIST,
                     validationNode, ValidationNodeConfig::INPUT_CONNECTION_LIST);

        // GraphState + Camera → NodeRenderer
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_NODE_LIST,
                     nodeRendererNode, NodeRendererNodeConfig::INPUT_NODE_LIST);
        batch.Connect(cameraNode, CameraNodeConfig::OUTPUT_VIEW_MATRIX,
                     nodeRendererNode, NodeRendererNodeConfig::INPUT_CAMERA_TRANSFORM);

        // GraphState + Camera → ConnectionRenderer
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_CONNECTION_LIST,
                     connectionRendererNode, ConnectionRendererNodeConfig::INPUT_CONNECTION_LIST);
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_NODE_LIST,
                     connectionRendererNode, ConnectionRendererNodeConfig::INPUT_NODE_LIST);
        batch.Connect(cameraNode, CameraNodeConfig::OUTPUT_VIEW_MATRIX,
                     connectionRendererNode, ConnectionRendererNodeConfig::INPUT_CAMERA_TRANSFORM);

        // Selection → Renderers
        batch.Connect(selectionNode, SelectionNodeConfig::OUTPUT_SELECTED_NODES,
                     nodeRendererNode, NodeRendererNodeConfig::INPUT_SELECTED_NODES);
        batch.Connect(selectionNode, SelectionNodeConfig::OUTPUT_SELECTED_CONNECTIONS,
                     connectionRendererNode, ConnectionRendererNodeConfig::INPUT_SELECTED_CONNECTIONS);

        // NodeRegistry → UIRenderer
        batch.Connect(nodeRegistryNode, NodeRegistryNodeConfig::OUTPUT_NODE_TYPE_DESCRIPTORS,
                     uiRendererNode, UIRendererNodeConfig::INPUT_NODE_REGISTRY);

        // GraphState → Serializer
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_NODE_LIST,
                     serializerNode, GraphSerializerNodeConfig::INPUT_NODE_LIST);
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_CONNECTION_LIST,
                     serializerNode, GraphSerializerNodeConfig::INPUT_CONNECTION_LIST);

        // GraphState → Compiler
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_NODE_LIST,
                     compilerNode, GraphCompilerNodeConfig::INPUT_NODE_LIST);
        batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_CONNECTION_LIST,
                     compilerNode, GraphCompilerNodeConfig::INPUT_CONNECTION_LIST);

        batch.RegisterAll();

        // ====================================================================
        // COMPILE & RUN
        // ====================================================================
        editorGraph.Compile();

        // Main loop
        while (!ShouldClose(windowNode)) {
            editorGraph.Execute();
        }
    }
};
```

---

## Data Flow Example: User Creates a Node

```
1. User drags "TextureLoaderNode" from palette to canvas
   ↓
2. UIRendererNode detects drag → emits UIInteraction event
   ↓
3. CommandProcessorNode receives UIInteraction
   → Creates CreateNodeCommand("TextureLoaderNode", position)
   → Outputs to COMMAND_STREAM
   ↓
4. UndoRedoNode receives command
   → Adds to history
   → Forwards to PROCESSED_COMMANDS
   ↓
5. GraphStateNode receives command
   → Adds EditorNodeData to currentNodes_
   → Outputs updated NODE_LIST
   ↓
6. NodeRendererNode receives updated NODE_LIST
   → Renders new node on next Execute()
   ↓
7. ValidationNode receives updated NODE_LIST
   → Validates (may show warning if slots unconnected)
   ↓
User sees new node on canvas!
```

---

## Benefits of This Approach

### 1. **Meta-Circular Design**
- The editor is built using the framework it edits
- Self-hosting: Can edit the editor's own graph
- Dogfooding: Using your own framework exposes design issues

### 2. **No Framework Pollution**
- Core RenderGraph remains pure (no GUI dependencies)
- MCP concerns isolated to editor application nodes
- Separation of concerns at node granularity

### 3. **Composability**
- Editor features are nodes that can be mixed and matched
- Want a read-only viewer? Remove CommandProcessorNode
- Want a headless compiler? Remove all View nodes
- Want a validation service? Use only ValidationNode

### 4. **Testability**
- Each node is independently testable
- Mock input nodes for unit testing
- Validate output nodes for correctness

### 5. **Extensibility**
- Add new features by adding nodes
- Example: Add ProfilingNode to track performance
- Example: Add CollaborationNode for multi-user editing

### 6. **Performance**
- Leverages RenderGraph's compilation and execution optimizations
- Parallel execution of independent nodes
- Resource management handled by existing framework

### 7. **Consistency**
- Same mental model as the graphs you're editing
- Same debugging tools work on the editor
- Same profiling tools work on the editor

---

## Comparison: Traditional MCP vs. Node-Based Editor

### Traditional MCP Architecture:
```
GraphModel ← GraphController → GraphEditorView
    ↕              ↕                  ↕
NodeModel  ← NodeController →  NodeView
    ↕              ↕                  ↕
  ...            ...                ...
```
- **Pros**: Well-understood pattern, clear separation
- **Cons**: Separate framework, not self-hosting, framework pollution

### Node-Based Editor Architecture:
```
RenderGraph (Editor Application)
  ├─ GraphStateNode (Model)
  ├─ CommandProcessorNode (Controller)
  ├─ NodeRendererNode (View)
  ├─ ValidationNode (Controller)
  └─ ...

RenderGraph (Being Edited)
  ├─ WindowNode
  ├─ TextureLoaderNode
  └─ ...
```
- **Pros**: Self-hosting, no pollution, composable, same tools
- **Cons**: More initial design work, need good editor node library

---

## Implementation Strategy

### Phase 1: Core Editor Nodes
1. Implement `GraphStateNode` (model data storage)
2. Implement `NodeRegistryNode` (node type metadata)
3. Implement basic `InputHandlerNode`
4. Implement simple `CommandProcessorNode`

### Phase 2: Basic Rendering
1. Implement `NodeRendererNode` (simple box rendering)
2. Implement `ConnectionRendererNode` (straight lines)
3. Implement `CameraNode` (pan/zoom)

### Phase 3: Interaction
1. Implement `SelectionNode`
2. Extend `CommandProcessorNode` (create, delete, connect)
3. Implement `UndoRedoNode`

### Phase 4: Validation & Compilation
1. Implement `ValidationNode`
2. Implement `GraphCompilerNode`
3. Implement execution preview

### Phase 5: Persistence
1. Implement `GraphSerializerNode`
2. Implement `GraphDeserializerNode`
3. Add save/load UI

### Phase 6: Advanced Features
1. Implement `UIRendererNode` (inspector panel)
2. Add node palette
3. Add performance profiling
4. Add multi-graph support

---

## Conclusion

By building the graph editor **as a RenderGraph application**, we achieve:

✅ **Pure Framework** - RenderGraph stays clean (no MVC/GUI dependencies)
✅ **Self-Hosting** - Use the framework to build tools for itself
✅ **MVC via Nodes** - Model/View/Controller as node categories
✅ **Composability** - Mix and match editor features as nodes
✅ **Same Tools** - Debug/profile the editor like any other graph
✅ **Extensibility** - Add features by adding nodes

This is the most elegant approach: **the graph editor is itself a graph**.
