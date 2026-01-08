# Graph-as-Data: Transforming RenderGraph from Orchestrator to Data Structure

## The Key Architectural Insight

To build a graph editor **as a RenderGraph application**, we need a fundamental shift in perspective:

**Current Architecture:**
- `RenderGraph` is an **orchestrator** (you call methods on it)
- Nodes are **inside** the graph
- Operations are **methods** on RenderGraph: `AddNode()`, `ConnectNodes()`, `Compile()`

**New Architecture (Two-Level System):**
- **Level 1 (Runtime)**: Editor application graph - uses RenderGraph as orchestrator
- **Level 2 (Data)**: Graph being edited - represented as **data structures** flowing through Level 1
- Graph operations become **nodes** that transform graph data

---

## The Fundamental Transformation

### Before: RenderGraph as Orchestrator

```cpp
// RenderGraph is the runtime, you operate ON it
RenderGraph renderGraph;

// Methods modify the orchestrator
auto nodeHandle = renderGraph.AddNode<TextureLoaderNodeType, TextureLoaderNodeConfig>("tex_diffuse");
renderGraph.ConnectNodes(node1, slot1, node2, slot2);
renderGraph.Compile();
renderGraph.Execute();
```

**Problem:** Can't represent "add a node" as a node itself - it's a method on the orchestrator.

---

### After: Graph as Data Structure

```cpp
// GraphData is a data structure (not an orchestrator)
struct GraphData {
    std::vector<NodeDescriptor> nodes;
    std::vector<ConnectionDescriptor> connections;
    std::map<std::string, std::any> metadata;
};

struct NodeDescriptor {
    std::string typeName;
    std::string instanceName;
    std::map<std::string, std::any> parameters;
};

struct ConnectionDescriptor {
    std::string sourceNode;
    uint32_t sourceSlot;
    std::string targetNode;
    uint32_t targetSlot;
};

// Now "add node" can be a NODE that operates on graph data
class AddNodeToGraphNode : public TypedNode<...> {
    void Execute(ExecuteContext& ctx) override {
        GraphData graphData = In<INPUT_GRAPH>(ctx);
        NodeDescriptor newNode = In<INPUT_NODE_DESCRIPTOR>(ctx);

        // Pure data transformation
        graphData.nodes.push_back(newNode);

        Out<OUTPUT_GRAPH>(ctx) = graphData;
    }
};
```

**Solution:** Graph structure is data, operations on graphs are nodes.

---

## Two-Level Architecture

### Level 1: Runtime Graph (The Editor Application)

This is a **running RenderGraph** that implements the editor:

```cpp
RenderGraph editorGraph;  // This is the orchestrator

// Add editor nodes (these ARE executing)
auto graphStateNode = editorGraph.AddNode<GraphStateNodeType, ...>("graph_state");
auto addNodeNode = editorGraph.AddNode<AddNodeToGraphNodeType, ...>("add_node_operation");
auto nodeRendererNode = editorGraph.AddNode<NodeRendererNodeType, ...>("node_renderer");

// Connect them
editorGraph.ConnectNodes(addNodeNode, OUTPUT_GRAPH, graphStateNode, INPUT_GRAPH);

// Compile and run the editor
editorGraph.Compile();
editorGraph.Execute();  // Editor is running!
```

**This level uses RenderGraph as orchestrator** - normal usage, no changes needed.

---

### Level 2: Data Graph (Being Edited)

This is **graph data** flowing through the Level 1 nodes:

```cpp
// Inside GraphStateNode (a Level 1 node)
class GraphStateNode : public TypedNode<...> {
    void Execute(ExecuteContext& ctx) override {
        // currentGraphData_ is the graph being EDITED (Level 2)
        // It's just data, not a running graph
        GraphData currentGraphData_;

        // Process commands that modify the data
        if (auto* addNodeCmd = GetCommandOfType<AddNodeCommand>()) {
            currentGraphData_.nodes.push_back({
                .typeName = addNodeCmd->nodeTypeName,
                .instanceName = addNodeCmd->instanceName,
                .parameters = addNodeCmd->parameters
            });
        }

        // Output the graph data (for other nodes to consume)
        Out<OUTPUT_GRAPH_DATA>(ctx) = currentGraphData_;
    }

private:
    GraphData currentGraphData_;  // The graph being edited (data!)
};
```

**This level is pure data** - no execution, no orchestration, just structures.

---

## Required Refactoring: Meta Operations → Data Nodes

### 1. Graph Manipulation Operations

**Current (methods on RenderGraph):**
```cpp
NodeHandle AddNode(const std::string& name, NodeInstance* instance);
void ConnectNodes(NodeHandle src, uint32_t srcSlot, NodeHandle dst, uint32_t dstSlot);
void RemoveNode(NodeHandle handle);
void Compile();
void Execute();
```

**New (data structures + transformation nodes):**

```cpp
// Data structures
struct GraphData {
    std::vector<NodeDescriptor> nodes;
    std::vector<ConnectionDescriptor> connections;
    GraphMetadata metadata;
};

// Transformation nodes
class AddNodeToGraphNode {
    CONSTEXPR_NODE_CONFIG(AddNodeToGraphNodeConfig, 2, 1) {
        INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
        INPUT_SLOT(NODE_TO_ADD, NodeDescriptor, 1, Required, Execute, ReadOnly, TaskLevel);
        OUTPUT_SLOT(OUTPUT_GRAPH, GraphData, 0, Required, WriteOnly);
    };

    void Execute(ExecuteContext& ctx) override {
        GraphData graph = In<INPUT_GRAPH>(ctx);
        NodeDescriptor node = In<NODE_TO_ADD>(ctx);

        graph.nodes.push_back(node);

        Out<OUTPUT_GRAPH>(ctx) = graph;
    }
};

class ConnectNodesInGraphNode {
    CONSTEXPR_NODE_CONFIG(ConnectNodesInGraphNodeConfig, 2, 1) {
        INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
        INPUT_SLOT(CONNECTION, ConnectionDescriptor, 1, Required, Execute, ReadOnly, TaskLevel);
        OUTPUT_SLOT(OUTPUT_GRAPH, GraphData, 0, Required, WriteOnly);
    };

    void Execute(ExecuteContext& ctx) override {
        GraphData graph = In<INPUT_GRAPH>(ctx);
        ConnectionDescriptor conn = In<CONNECTION>(ctx);

        graph.connections.push_back(conn);

        Out<OUTPUT_GRAPH>(ctx) = graph;
    }
};

class RemoveNodeFromGraphNode {
    CONSTEXPR_NODE_CONFIG(RemoveNodeFromGraphNodeConfig, 2, 1) {
        INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
        INPUT_SLOT(NODE_NAME, std::string, 1, Required, Execute, ReadOnly, TaskLevel);
        OUTPUT_SLOT(OUTPUT_GRAPH, GraphData, 0, Required, WriteOnly);
    };

    void Execute(ExecuteContext& ctx) override {
        GraphData graph = In<INPUT_GRAPH>(ctx);
        std::string nodeName = In<NODE_NAME>(ctx);

        // Remove node and all its connections
        auto& nodes = graph.nodes;
        nodes.erase(
            std::remove_if(nodes.begin(), nodes.end(),
                [&](const NodeDescriptor& n) { return n.instanceName == nodeName; }),
            nodes.end()
        );

        // Remove connections involving this node
        auto& connections = graph.connections;
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                [&](const ConnectionDescriptor& c) {
                    return c.sourceNode == nodeName || c.targetNode == nodeName;
                }),
            connections.end()
        );

        Out<OUTPUT_GRAPH>(ctx) = graph;
    }
};
```

---

### 2. Graph Compilation: Data → Runtime

The bridge between Level 2 (data) and actual execution is a **compiler node**:

```cpp
class GraphCompilerNode {
    CONSTEXPR_NODE_CONFIG(GraphCompilerNodeConfig, 2, 2) {
        INPUT_SLOT(GRAPH_DATA, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
        INPUT_SLOT(COMPILE_TRIGGER, bool, 1, Required, Execute, ReadOnly, TaskLevel);
        OUTPUT_SLOT(COMPILED_GRAPH, std::shared_ptr<RenderGraph>, 0, Optional, WriteOnly);
        OUTPUT_SLOT(COMPILATION_RESULT, CompilationResult, 1, Required, WriteOnly);
    };

    void Execute(ExecuteContext& ctx) override {
        if (!In<COMPILE_TRIGGER>(ctx)) return;

        GraphData graphData = In<GRAPH_DATA>(ctx);

        // Create a NEW RenderGraph instance (Level 1 orchestrator)
        auto compiledGraph = std::make_shared<RenderGraph>();

        try {
            // Transform data → runtime
            std::map<std::string, NodeHandle> nodeHandles;

            // Add nodes
            for (const auto& nodeDesc : graphData.nodes) {
                NodeInstance* instance = CreateNodeInstance(nodeDesc.typeName);
                ApplyParameters(instance, nodeDesc.parameters);

                NodeHandle handle = compiledGraph->AddNode(nodeDesc.instanceName, instance);
                nodeHandles[nodeDesc.instanceName] = handle;
            }

            // Add connections
            for (const auto& connDesc : graphData.connections) {
                compiledGraph->ConnectNodes(
                    nodeHandles[connDesc.sourceNode], connDesc.sourceSlot,
                    nodeHandles[connDesc.targetNode], connDesc.targetSlot
                );
            }

            // Compile the actual graph
            compiledGraph->Compile();

            Out<COMPILED_GRAPH>(ctx) = compiledGraph;
            Out<COMPILATION_RESULT>(ctx) = CompilationResult{.success = true};

        } catch (const std::exception& e) {
            Out<COMPILATION_RESULT>(ctx) = CompilationResult{
                .success = false,
                .errorMessage = e.what()
            };
        }
    }

private:
    NodeInstance* CreateNodeInstance(const std::string& typeName);
    void ApplyParameters(NodeInstance* instance, const std::map<std::string, std::any>& params);
};
```

**Key insight:** GraphCompilerNode **creates a RenderGraph instance** from data. It transforms Level 2 (data) into a Level 1 (runtime) graph.

---

### 3. Graph State Management

Instead of RenderGraph being a singleton orchestrator, graph state becomes **data flowing through nodes**:

```cpp
class GraphStateNode {
    CONSTEXPR_NODE_CONFIG(GraphStateNodeConfig, 1, 1) {
        INPUT_SLOT(COMMAND_STREAM, CommandBuffer, 0, Required, Execute, ReadOnly, TaskLevel);
        OUTPUT_SLOT(GRAPH_DATA, GraphData, 0, Required, WriteOnly);
    };

    void Execute(ExecuteContext& ctx) override {
        auto& commands = In<COMMAND_STREAM>(ctx);

        // Process each command (pure data transformations)
        for (auto& cmd : commands.commands) {
            if (auto* addCmd = dynamic_cast<AddNodeCommand*>(cmd.get())) {
                currentGraphData_.nodes.push_back({
                    .typeName = addCmd->nodeTypeName,
                    .instanceName = addCmd->instanceName,
                    .parameters = {}
                });
            }
            else if (auto* connectCmd = dynamic_cast<ConnectNodesCommand*>(cmd.get())) {
                currentGraphData_.connections.push_back({
                    .sourceNode = connectCmd->sourceNode,
                    .sourceSlot = connectCmd->sourceSlot,
                    .targetNode = connectCmd->targetNode,
                    .targetSlot = connectCmd->targetSlot
                });
            }
            else if (auto* deleteCmd = dynamic_cast<DeleteNodeCommand*>(cmd.get())) {
                // Remove from data structure
                auto& nodes = currentGraphData_.nodes;
                nodes.erase(
                    std::remove_if(nodes.begin(), nodes.end(),
                        [&](const NodeDescriptor& n) {
                            return n.instanceName == deleteCmd->nodeName;
                        }),
                    nodes.end()
                );
            }
        }

        Out<GRAPH_DATA>(ctx) = currentGraphData_;
    }

private:
    GraphData currentGraphData_;  // Persistent state (the graph being edited)
};
```

---

## Migration Strategy: What Needs to Change

### Core RenderGraph (NO CHANGES)

The existing RenderGraph orchestrator **remains unchanged**:
- Still used to run applications (including the editor)
- Still manages node lifecycle
- Still compiles and executes
- **Zero changes to existing codebase**

---

### New Additions: Graph Data Structures

Add lightweight data structures to represent graphs:

```cpp
// VIXEN/libraries/RenderGraph/include/Data/GraphData.h
#pragma once

namespace VIXEN {

struct NodeDescriptor {
    std::string typeName;        // "WindowNode", "TextureLoaderNode", etc.
    std::string instanceName;    // "main_window", "tex_diffuse", etc.
    std::map<std::string, std::any> parameters;

    // Editor metadata (not part of compiled graph)
    Vec2 position;
    Color color;
    std::map<std::string, std::string> editorMetadata;
};

struct ConnectionDescriptor {
    std::string sourceNode;
    uint32_t sourceSlot;
    std::string targetNode;
    uint32_t targetSlot;

    // Connection type
    enum class Type {
        Direct,
        Array,
        Constant,
        Variadic,
        FieldExtraction
    } type = Type::Direct;

    // Type-specific data
    std::optional<uint32_t> arrayIndex;
    std::optional<std::string> fieldPath;
    std::optional<std::any> constantValue;
};

struct GraphData {
    std::vector<NodeDescriptor> nodes;
    std::vector<ConnectionDescriptor> connections;

    // Graph metadata
    std::string name;
    std::string description;
    uint32_t version = 1;
    std::map<std::string, std::any> metadata;

    // Serialization
    std::string ToJSON() const;
    static GraphData FromJSON(const std::string& json);
};

} // namespace VIXEN
```

---

### New Additions: Graph Operation Nodes

Create nodes that operate on `GraphData`:

```cpp
// VIXEN/libraries/RenderGraphEditor/include/Nodes/GraphOperationNodes.h

namespace VIXEN::Editor {

// Add node operation
class AddNodeToGraphNode;
CONSTEXPR_NODE_CONFIG(AddNodeToGraphNodeConfig, 2, 1) { ... };

// Connect nodes operation
class ConnectNodesInGraphNode;
CONSTEXPR_NODE_CONFIG(ConnectNodesInGraphNodeConfig, 2, 1) { ... };

// Remove node operation
class RemoveNodeFromGraphNode;
CONSTEXPR_NODE_CONFIG(RemoveNodeFromGraphNodeConfig, 2, 1) { ... };

// Validate graph operation
class ValidateGraphNode;
CONSTEXPR_NODE_CONFIG(ValidateGraphNodeConfig, 1, 1) { ... };

// Compile graph operation
class GraphCompilerNode;
CONSTEXPR_NODE_CONFIG(GraphCompilerNodeConfig, 2, 2) { ... };

// Serialize graph operation
class SerializeGraphNode;
CONSTEXPR_NODE_CONFIG(SerializeGraphNodeConfig, 1, 1) { ... };

// Deserialize graph operation
class DeserializeGraphNode;
CONSTEXPR_NODE_CONFIG(DeserializeGraphNodeConfig, 1, 1) { ... };

} // namespace VIXEN::Editor
```

---

## Complete Example: Editor Graph Operating on Graph Data

```cpp
// The editor application (Level 1: Runtime)
RenderGraph editorGraph;

// ==== Model Nodes (manage graph data) ====
auto graphStateNode = editorGraph.AddNode<GraphStateNodeType, GraphStateNodeConfig>("graph_state");

// ==== Controller Nodes (process user input) ====
auto inputNode = editorGraph.AddNode<InputHandlerNodeType, InputHandlerNodeConfig>("input");
auto commandNode = editorGraph.AddNode<CommandProcessorNodeType, CommandProcessorNodeConfig>("commands");

// ==== View Nodes (render graph data) ====
auto nodeRendererNode = editorGraph.AddNode<NodeRendererNodeType, NodeRendererNodeConfig>("node_renderer");

// ==== Utility Nodes (compile graph data) ====
auto compilerNode = editorGraph.AddNode<GraphCompilerNodeType, GraphCompilerNodeConfig>("compiler");

// Connections
ConnectionBatch batch(editorGraph);

// Input → Commands → GraphState
batch.Connect(inputNode, InputHandlerNodeConfig::OUTPUT_EVENTS,
             commandNode, CommandProcessorNodeConfig::INPUT_EVENTS);
batch.Connect(commandNode, CommandProcessorNodeConfig::OUTPUT_COMMAND_STREAM,
             graphStateNode, GraphStateNodeConfig::INPUT_COMMAND_STREAM);

// GraphState → Renderer (renders the graph being edited)
batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_GRAPH_DATA,
             nodeRendererNode, NodeRendererNodeConfig::INPUT_GRAPH_DATA);

// GraphState → Compiler (compiles graph data to executable graph)
batch.Connect(graphStateNode, GraphStateNodeConfig::OUTPUT_GRAPH_DATA,
             compilerNode, GraphCompilerNodeConfig::INPUT_GRAPH_DATA);

batch.RegisterAll();

// Run the editor
editorGraph.Compile();

while (running) {
    editorGraph.Execute();

    // The GRAPH_DATA flowing through graphStateNode represents
    // the graph being edited (Level 2)

    // When user clicks "Run", compilerNode transforms GRAPH_DATA
    // into an actual RenderGraph instance that can execute
}
```

---

## Key Transformations Summary

| Aspect | Before (Orchestrator) | After (Data + Nodes) |
|--------|----------------------|---------------------|
| **Graph Representation** | `RenderGraph` instance | `GraphData` struct |
| **Add Node** | `graph.AddNode<T>(name)` | `AddNodeToGraphNode` processes `GraphData` |
| **Connect** | `graph.ConnectNodes(...)` | `ConnectNodesInGraphNode` processes `GraphData` |
| **Compile** | `graph.Compile()` | `GraphCompilerNode` transforms `GraphData` → `RenderGraph*` |
| **Validation** | Method on graph | `ValidateGraphNode` processes `GraphData` |
| **Serialization** | External serializer | `SerializeGraphNode` processes `GraphData` |
| **State** | Managed by `RenderGraph` | Flows through nodes as data |

---

## Benefits of This Refactoring

### 1. **Pure Data Transformations**
- Graph operations become functional (input graph → output graph)
- No side effects on global state
- Easier to reason about, test, and debug

### 2. **Composable Operations**
- Chain graph operations as node graphs
- Example: `AddNode → ValidateGraph → SerializeGraph`

### 3. **Undo/Redo for Free**
- Every operation produces a new `GraphData`
- Store old `GraphData` for undo
- No need to reverse operations

### 4. **Multiple Graphs**
- Editor can work on multiple graphs simultaneously
- Each `GraphData` is independent
- No singleton orchestrator limiting you to one graph

### 5. **Self-Hosting**
- Editor graph (Level 1) can load and modify itself
- Load editor's own graph data, modify it, compile it, run it
- True meta-circular design

### 6. **Clear Separation**
- **RenderGraph**: Orchestrator for running graphs (unchanged)
- **GraphData**: Data structures representing graphs (new)
- **Editor Nodes**: Operations on GraphData (new)

---

## Migration Checklist

- [ ] Define `GraphData`, `NodeDescriptor`, `ConnectionDescriptor` structs
- [ ] Implement serialization: `GraphData::ToJSON()`, `GraphData::FromJSON()`
- [ ] Create graph operation nodes:
  - [ ] `AddNodeToGraphNode`
  - [ ] `RemoveNodeFromGraphNode`
  - [ ] `ConnectNodesInGraphNode`
  - [ ] `DisconnectNodesInGraphNode`
  - [ ] `ValidateGraphNode`
- [ ] Create compilation node:
  - [ ] `GraphCompilerNode` (GraphData → RenderGraph*)
- [ ] Create state management node:
  - [ ] `GraphStateNode` (stores current GraphData)
- [ ] Create serialization nodes:
  - [ ] `SerializeGraphNode` (GraphData → JSON)
  - [ ] `DeserializeGraphNode` (JSON → GraphData)
- [ ] Build editor application using these nodes
- [ ] Test meta-circular editing (editor edits itself)

---

## Conclusion

**The key insight:** To build a graph editor as a graph, we need to treat **graphs as data** that flows through nodes, rather than as orchestrators that contain nodes.

This requires:
1. **GraphData** - Lightweight data structures representing graphs
2. **Operation Nodes** - Nodes that transform GraphData (add, remove, connect, etc.)
3. **Compiler Node** - Bridge from GraphData to RenderGraph instance
4. **Two-Level Architecture** - Runtime graph (editor) operates on data graph (being edited)

**RenderGraph itself doesn't change** - it remains the orchestrator for running applications. We just add the ability to represent graphs as data and operate on them with nodes.

This is the refactoring needed to make the graph editor meta-circular and self-hosting.
