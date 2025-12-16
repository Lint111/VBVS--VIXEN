# Extracted Operations: Avoiding Logic Duplication

## The Core Question

**Q:** If we create nodes that operate on GraphData (AddNodeToGraphNode, ConnectNodesNode, etc.), and RenderGraph already has methods that do the same things (AddNode(), ConnectNodes()), won't we duplicate all the logic?

**A:** No - we **extract** the core operations into shared implementations that both RenderGraph methods AND node wrappers can use.

---

## The Architecture: Three Layers

```
┌─────────────────────────────────────────────────────────┐
│  INTERFACE LAYER 1: RenderGraph (Orchestrator API)     │
│  • graph.AddNode<T>(name)                              │
│  • graph.ConnectNodes(src, slot, dst, slot)            │
│  • graph.Compile()                                      │
│  (Convenience API for direct programmatic use)          │
└────────────────────┬────────────────────────────────────┘
                     │ calls ↓
┌─────────────────────────────────────────────────────────┐
│  CORE OPERATIONS LAYER (Shared Implementation)          │
│  namespace GraphOperations {                            │
│    void AddNode(GraphData&, NodeDescriptor);           │
│    void ConnectNodes(GraphData&, ConnectionDescriptor);│
│    ValidationResult Validate(const GraphData&);        │
│    void Compile(GraphData&, RenderGraph*);             │
│  }                                                      │
│  (Pure logic, no interface dependencies)               │
└────────────────────┬────────────────────────────────────┘
                     │ called by ↓
┌─────────────────────────────────────────────────────────┐
│  INTERFACE LAYER 2: Operation Nodes (Data Flow API)    │
│  • AddNodeToGraphNode (transforms GraphData)           │
│  • ConnectNodesInGraphNode (transforms GraphData)      │
│  • ValidateGraphNode (validates GraphData)             │
│  (Node-based API for graph-as-data workflows)          │
└─────────────────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────┐
│  DATA LAYER: GraphData (Pure Data Structures)          │
│  • std::vector<NodeDescriptor> nodes                   │
│  • std::vector<ConnectionDescriptor> connections       │
│  • metadata, version, etc.                             │
└─────────────────────────────────────────────────────────┘
```

---

## Detailed Design

### Layer 1: Pure Data Structures

```cpp
// VIXEN/libraries/RenderGraph/include/Data/GraphData.h
namespace VIXEN {

// Pure data - no methods, no logic
struct NodeDescriptor {
    std::string typeName;
    std::string instanceName;
    std::map<std::string, std::any> parameters;

    // Editor metadata (ignored during compilation)
    Vec2 position{0, 0};
    Color color{255, 255, 255};
};

struct ConnectionDescriptor {
    std::string sourceNode;
    uint32_t sourceSlot;
    std::string targetNode;
    uint32_t targetSlot;
    ConnectionType type = ConnectionType::Direct;
};

struct GraphData {
    std::vector<NodeDescriptor> nodes;
    std::vector<ConnectionDescriptor> connections;
    std::string name;
    std::map<std::string, std::any> metadata;
};

} // namespace VIXEN
```

---

### Layer 2: Core Operations (Shared Logic)

```cpp
// VIXEN/libraries/RenderGraph/include/Operations/GraphOperations.h
namespace VIXEN::GraphOperations {

// ============================================================================
// Core operations (single source of truth for graph manipulation logic)
// ============================================================================

struct OperationResult {
    bool success;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

// Add node to graph data
OperationResult AddNode(
    GraphData& graph,
    const NodeDescriptor& node,
    const NodeTypeRegistry* registry = nullptr  // For validation
) {
    // Validation
    if (node.instanceName.empty()) {
        return OperationResult{false, "Node instance name cannot be empty"};
    }

    // Check for duplicate names
    for (const auto& existing : graph.nodes) {
        if (existing.instanceName == node.instanceName) {
            return OperationResult{false, "Node '" + node.instanceName + "' already exists"};
        }
    }

    // Validate node type exists (if registry provided)
    if (registry && !registry->IsTypeRegistered(node.typeName)) {
        return OperationResult{false, "Unknown node type: " + node.typeName};
    }

    // Add node
    graph.nodes.push_back(node);

    return OperationResult{true, ""};
}

// Remove node from graph data
OperationResult RemoveNode(
    GraphData& graph,
    const std::string& instanceName
) {
    // Find node
    auto nodeIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const NodeDescriptor& n) { return n.instanceName == instanceName; });

    if (nodeIt == graph.nodes.end()) {
        return OperationResult{false, "Node '" + instanceName + "' not found"};
    }

    // Remove node
    graph.nodes.erase(nodeIt);

    // Remove all connections involving this node
    auto& connections = graph.connections;
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&](const ConnectionDescriptor& c) {
                return c.sourceNode == instanceName || c.targetNode == instanceName;
            }),
        connections.end()
    );

    return OperationResult{true, ""};
}

// Connect nodes in graph data
OperationResult ConnectNodes(
    GraphData& graph,
    const ConnectionDescriptor& connection,
    const NodeTypeRegistry* registry = nullptr  // For type validation
) {
    // Validate source node exists
    auto sourceIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const NodeDescriptor& n) { return n.instanceName == connection.sourceNode; });
    if (sourceIt == graph.nodes.end()) {
        return OperationResult{false, "Source node '" + connection.sourceNode + "' not found"};
    }

    // Validate target node exists
    auto targetIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const NodeDescriptor& n) { return n.instanceName == connection.targetNode; });
    if (targetIt == graph.nodes.end()) {
        return OperationResult{false, "Target node '" + connection.targetNode + "' not found"};
    }

    // Type validation (if registry provided)
    if (registry) {
        auto validationResult = ValidateConnection(connection, *sourceIt, *targetIt, registry);
        if (!validationResult.success) {
            return validationResult;
        }
    }

    // Check for duplicate connections
    for (const auto& existing : graph.connections) {
        if (existing.sourceNode == connection.sourceNode &&
            existing.sourceSlot == connection.sourceSlot &&
            existing.targetNode == connection.targetNode &&
            existing.targetSlot == connection.targetSlot) {
            return OperationResult{false, "Connection already exists"};
        }
    }

    // Add connection
    graph.connections.push_back(connection);

    return OperationResult{true, ""};
}

// Disconnect nodes in graph data
OperationResult DisconnectNodes(
    GraphData& graph,
    const std::string& sourceNode,
    uint32_t sourceSlot,
    const std::string& targetNode,
    uint32_t targetSlot
) {
    auto& connections = graph.connections;
    auto it = std::find_if(connections.begin(), connections.end(),
        [&](const ConnectionDescriptor& c) {
            return c.sourceNode == sourceNode && c.sourceSlot == sourceSlot &&
                   c.targetNode == targetNode && c.targetSlot == targetSlot;
        });

    if (it == connections.end()) {
        return OperationResult{false, "Connection not found"};
    }

    connections.erase(it);
    return OperationResult{true, ""};
}

// Validate graph structure
struct ValidationResult {
    bool isValid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

ValidationResult ValidateGraph(
    const GraphData& graph,
    const NodeTypeRegistry* registry
) {
    ValidationResult result{true, {}, {}};

    // Check for cycles
    if (HasCycles(graph)) {
        result.isValid = false;
        result.errors.push_back("Graph contains cycles");
    }

    // Validate all connections
    for (const auto& conn : graph.connections) {
        auto sourceIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [&](const NodeDescriptor& n) { return n.instanceName == conn.sourceNode; });
        auto targetIt = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [&](const NodeDescriptor& n) { return n.instanceName == conn.targetNode; });

        if (registry) {
            auto connValidation = ValidateConnection(conn, *sourceIt, *targetIt, registry);
            if (!connValidation.success) {
                result.isValid = false;
                result.errors.push_back(connValidation.errorMessage);
            }
        }
    }

    // Check required slots are connected
    if (registry) {
        for (const auto& node : graph.nodes) {
            auto requiredSlots = registry->GetRequiredInputSlots(node.typeName);
            for (uint32_t slotIndex : requiredSlots) {
                bool connected = std::any_of(graph.connections.begin(), graph.connections.end(),
                    [&](const ConnectionDescriptor& c) {
                        return c.targetNode == node.instanceName && c.targetSlot == slotIndex;
                    });

                if (!connected) {
                    result.warnings.push_back(
                        "Node '" + node.instanceName + "' has unconnected required slot " +
                        std::to_string(slotIndex)
                    );
                }
            }
        }
    }

    return result;
}

// Helper: Check for cycles
bool HasCycles(const GraphData& graph) {
    // Build adjacency list
    std::map<std::string, std::vector<std::string>> adjList;
    for (const auto& node : graph.nodes) {
        adjList[node.instanceName] = {};
    }
    for (const auto& conn : graph.connections) {
        adjList[conn.sourceNode].push_back(conn.targetNode);
    }

    // DFS cycle detection
    std::set<std::string> visited;
    std::set<std::string> recStack;

    std::function<bool(const std::string&)> hasCycleDFS = [&](const std::string& node) -> bool {
        visited.insert(node);
        recStack.insert(node);

        for (const auto& neighbor : adjList[node]) {
            if (recStack.count(neighbor)) {
                return true;  // Cycle detected
            }
            if (!visited.count(neighbor) && hasCycleDFS(neighbor)) {
                return true;
            }
        }

        recStack.erase(node);
        return false;
    };

    for (const auto& [nodeName, _] : adjList) {
        if (!visited.count(nodeName)) {
            if (hasCycleDFS(nodeName)) {
                return true;
            }
        }
    }

    return false;
}

// Compile GraphData into a RenderGraph instance
OperationResult CompileToRenderGraph(
    const GraphData& graphData,
    RenderGraph& targetGraph,
    const NodeTypeRegistry* registry
) {
    // Validate first
    auto validation = ValidateGraph(graphData, registry);
    if (!validation.isValid) {
        return OperationResult{false, "Graph validation failed: " + validation.errors[0]};
    }

    // Create node instances
    std::map<std::string, NodeHandle> nodeHandles;
    for (const auto& nodeDesc : graphData.nodes) {
        if (!registry) {
            return OperationResult{false, "NodeTypeRegistry required for compilation"};
        }

        // Create node instance using registry factory
        NodeInstance* instance = registry->CreateNodeInstance(nodeDesc.typeName);
        if (!instance) {
            return OperationResult{false, "Failed to create node of type: " + nodeDesc.typeName};
        }

        // Apply parameters
        ApplyParameters(instance, nodeDesc.parameters);

        // Add to graph
        NodeHandle handle = targetGraph.AddNode(nodeDesc.instanceName, instance);
        nodeHandles[nodeDesc.instanceName] = handle;
    }

    // Create connections
    for (const auto& connDesc : graphData.connections) {
        NodeHandle srcHandle = nodeHandles[connDesc.sourceNode];
        NodeHandle dstHandle = nodeHandles[connDesc.targetNode];

        targetGraph.ConnectNodes(srcHandle, connDesc.sourceSlot, dstHandle, connDesc.targetSlot);
    }

    // Compile the graph
    try {
        targetGraph.Compile();
    } catch (const std::exception& e) {
        return OperationResult{false, std::string("Compilation failed: ") + e.what()};
    }

    return OperationResult{true, ""};
}

} // namespace VIXEN::GraphOperations
```

---

### Layer 3: RenderGraph Orchestrator (Uses Core Operations)

```cpp
// VIXEN/libraries/RenderGraph/include/Core/RenderGraph.h
namespace VIXEN {

class RenderGraph {
public:
    RenderGraph() = default;

    // ========================================================================
    // EXISTING API (unchanged externally, refactored internally)
    // ========================================================================

    template<typename NodeType, typename ConfigType>
    NodeHandle AddNode(const std::string& instanceName) {
        // Create node descriptor
        NodeDescriptor nodeDesc;
        nodeDesc.typeName = NodeType::GetTypeName();
        nodeDesc.instanceName = instanceName;

        // Use core operation
        auto result = GraphOperations::AddNode(graphData_, nodeDesc, registry_);
        if (!result.success) {
            throw std::runtime_error("AddNode failed: " + result.errorMessage);
        }

        // Create actual node instance (runtime state)
        auto nodeInstance = std::make_unique<TypedNode<NodeType, ConfigType>>();
        NodeHandle handle = static_cast<NodeHandle>(nodeInstances_.size());
        nodeInstances_.push_back(std::move(nodeInstance));

        // Map name to handle
        nameToHandle_[instanceName] = handle;

        return handle;
    }

    void ConnectNodes(
        NodeHandle sourceHandle, uint32_t sourceSlot,
        NodeHandle targetHandle, uint32_t targetSlot
    ) {
        // Get node names from handles
        std::string sourceName = GetNodeName(sourceHandle);
        std::string targetName = GetNodeName(targetHandle);

        // Create connection descriptor
        ConnectionDescriptor connDesc{sourceName, sourceSlot, targetName, targetSlot};

        // Use core operation
        auto result = GraphOperations::ConnectNodes(graphData_, connDesc, registry_);
        if (!result.success) {
            throw std::runtime_error("ConnectNodes failed: " + result.errorMessage);
        }

        // Update runtime state (connections tracked separately for execution)
        RuntimeConnection runtimeConn{sourceHandle, sourceSlot, targetHandle, targetSlot};
        runtimeConnections_.push_back(runtimeConn);
    }

    void Compile() {
        // Validate using core operation
        auto validation = GraphOperations::ValidateGraph(graphData_, registry_);
        if (!validation.isValid) {
            throw std::runtime_error("Graph validation failed: " + validation.errors[0]);
        }

        // Perform compilation (existing logic)
        AnalyzeDependencies();
        AllocateResources();
        GeneratePipelines();
        BuildExecutionOrder();
    }

    void Execute() {
        // Execute nodes in compiled order (existing logic)
        for (NodeHandle handle : executionOrder_) {
            nodeInstances_[handle]->Execute();
        }
    }

    // ========================================================================
    // NEW API: Direct GraphData access
    // ========================================================================

    const GraphData& GetGraphData() const { return graphData_; }

    void LoadFromGraphData(const GraphData& data) {
        // Clear existing state
        nodeInstances_.clear();
        runtimeConnections_.clear();
        nameToHandle_.clear();

        // Use core compilation operation
        auto result = GraphOperations::CompileToRenderGraph(data, *this, registry_);
        if (!result.success) {
            throw std::runtime_error("Failed to load graph: " + result.errorMessage);
        }

        graphData_ = data;
    }

private:
    // Data representation (NEW - extracted from implementation)
    GraphData graphData_;

    // Runtime state (EXISTING - unchanged)
    std::vector<std::unique_ptr<NodeInstance>> nodeInstances_;
    std::vector<RuntimeConnection> runtimeConnections_;
    std::map<std::string, NodeHandle> nameToHandle_;
    std::vector<NodeHandle> executionOrder_;
    NodeTypeRegistry* registry_ = nullptr;

    // Existing private methods (unchanged)
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void BuildExecutionOrder();

    std::string GetNodeName(NodeHandle handle) const {
        for (const auto& [name, h] : nameToHandle_) {
            if (h == handle) return name;
        }
        return "";
    }
};

} // namespace VIXEN
```

---

### Layer 4: Operation Nodes (Also Use Core Operations)

```cpp
// VIXEN/libraries/RenderGraphEditor/include/Nodes/GraphOperationNodes.h
namespace VIXEN::Editor {

// ============================================================================
// AddNodeToGraphNode - Wraps GraphOperations::AddNode
// ============================================================================

CONSTEXPR_NODE_CONFIG(AddNodeToGraphNodeConfig, 2, 2) {
    INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(NODE_DESCRIPTOR, NodeDescriptor, 1, Required, Execute, ReadOnly, TaskLevel);
    OUTPUT_SLOT(OUTPUT_GRAPH, GraphData, 0, Required, WriteOnly);
    OUTPUT_SLOT(OPERATION_RESULT, GraphOperations::OperationResult, 1, Required, WriteOnly);
};

class AddNodeToGraphNode : public TypedNode<AddNodeToGraphNodeType, AddNodeToGraphNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        // Get inputs
        GraphData graph = In<INPUT_GRAPH>(ctx);
        NodeDescriptor node = In<NODE_DESCRIPTOR>(ctx);

        // Use SAME core operation as RenderGraph::AddNode()
        auto result = GraphOperations::AddNode(graph, node, registry_);

        // Output results
        Out<OUTPUT_GRAPH>(ctx) = graph;
        Out<OPERATION_RESULT>(ctx) = result;
    }

private:
    NodeTypeRegistry* registry_ = nullptr;  // Injected
};

// ============================================================================
// ConnectNodesInGraphNode - Wraps GraphOperations::ConnectNodes
// ============================================================================

CONSTEXPR_NODE_CONFIG(ConnectNodesInGraphNodeConfig, 2, 2) {
    INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
    INPUT_SLOT(CONNECTION, ConnectionDescriptor, 1, Required, Execute, ReadOnly, TaskLevel);
    OUTPUT_SLOT(OUTPUT_GRAPH, GraphData, 0, Required, WriteOnly);
    OUTPUT_SLOT(OPERATION_RESULT, GraphOperations::OperationResult, 1, Required, WriteOnly);
};

class ConnectNodesInGraphNode : public TypedNode<ConnectNodesInGraphNodeType, ConnectNodesInGraphNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        GraphData graph = In<INPUT_GRAPH>(ctx);
        ConnectionDescriptor conn = In<CONNECTION>(ctx);

        // Use SAME core operation as RenderGraph::ConnectNodes()
        auto result = GraphOperations::ConnectNodes(graph, conn, registry_);

        Out<OUTPUT_GRAPH>(ctx) = graph;
        Out<OPERATION_RESULT>(ctx) = result;
    }

private:
    NodeTypeRegistry* registry_ = nullptr;
};

// ============================================================================
// ValidateGraphNode - Wraps GraphOperations::ValidateGraph
// ============================================================================

CONSTEXPR_NODE_CONFIG(ValidateGraphNodeConfig, 1, 1) {
    INPUT_SLOT(INPUT_GRAPH, GraphData, 0, Required, Execute, ReadOnly, TaskLevel);
    OUTPUT_SLOT(VALIDATION_RESULT, GraphOperations::ValidationResult, 0, Required, WriteOnly);
};

class ValidateGraphNode : public TypedNode<ValidateGraphNodeType, ValidateGraphNodeConfig> {
    void Execute(ExecuteContext& ctx) override {
        const GraphData& graph = In<INPUT_GRAPH>(ctx);

        // Use SAME core operation
        auto result = GraphOperations::ValidateGraph(graph, registry_);

        Out<VALIDATION_RESULT>(ctx) = result;
    }

private:
    NodeTypeRegistry* registry_ = nullptr;
};

} // namespace VIXEN::Editor
```

---

## Key Benefits of This Architecture

### 1. **Zero Duplication**
- Core logic implemented ONCE in `GraphOperations` namespace
- RenderGraph methods call these operations
- Operation nodes call these operations
- Single source of truth

### 2. **Backward Compatibility**
- Existing RenderGraph API unchanged
- Existing code continues to work
- `graph.AddNode<T>()` still works exactly as before

### 3. **Separation of Concerns**

| Component | Responsibility |
|-----------|----------------|
| **GraphData** | Pure data structures |
| **GraphOperations** | Pure logic (validation, manipulation) |
| **RenderGraph** | Orchestrator + runtime state management |
| **Operation Nodes** | Data flow interface to operations |

### 4. **Testability**
- Test `GraphOperations` functions independently
- Pure functions: input → output
- No dependencies on RenderGraph or nodes

### 5. **Flexibility**
- Use RenderGraph directly (programmatic)
- Use operation nodes (graph-based workflows)
- Use GraphOperations directly (custom tools)

---

## Usage Examples

### Example 1: Traditional RenderGraph Usage (Unchanged)

```cpp
// Existing code continues to work
RenderGraph graph;

auto window = graph.AddNode<WindowNodeType, WindowNodeConfig>("main_window");
auto swapChain = graph.AddNode<SwapChainNodeType, SwapChainNodeConfig>("swapchain");

graph.ConnectNodes(window, 0, swapChain, 1);
graph.Compile();
graph.Execute();
```

Internally, this now uses `GraphOperations`, but externally identical.

---

### Example 2: Direct GraphData Manipulation

```cpp
// Create graph as data
GraphData graph;

// Add nodes using core operations
NodeDescriptor windowNode{
    .typeName = "WindowNode",
    .instanceName = "main_window"
};
GraphOperations::AddNode(graph, windowNode);

NodeDescriptor swapChainNode{
    .typeName = "SwapChainNode",
    .instanceName = "swapchain"
};
GraphOperations::AddNode(graph, swapChainNode);

// Add connection
ConnectionDescriptor conn{
    .sourceNode = "main_window",
    .sourceSlot = 0,
    .targetNode = "swapchain",
    .targetSlot = 1
};
GraphOperations::ConnectNodes(graph, conn);

// Validate
auto validation = GraphOperations::ValidateGraph(graph, registry);

// Compile to executable graph
RenderGraph executableGraph;
GraphOperations::CompileToRenderGraph(graph, executableGraph, registry);
executableGraph.Execute();
```

---

### Example 3: Node-Based Graph Manipulation

```cpp
// Editor application graph
RenderGraph editorGraph;

auto graphStateNode = editorGraph.AddNode<GraphStateNodeType, ...>("state");
auto addNodeOpNode = editorGraph.AddNode<AddNodeToGraphNodeType, ...>("add_node_op");
auto validateNode = editorGraph.AddNode<ValidateGraphNodeType, ...>("validate");

// Graph state → Add node operation → Validation
editorGraph.ConnectNodes(
    graphStateNode, GraphStateNodeConfig::OUTPUT_GRAPH,
    addNodeOpNode, AddNodeToGraphNodeConfig::INPUT_GRAPH
);
editorGraph.ConnectNodes(
    addNodeOpNode, AddNodeToGraphNodeConfig::OUTPUT_GRAPH,
    validateNode, ValidateGraphNodeConfig::INPUT_GRAPH
);

editorGraph.Compile();
editorGraph.Execute();  // Processes graph data through operations
```

---

## What Needs to Be Extracted?

### From RenderGraph to GraphOperations:

✅ **Extract:**
- Add node validation logic
- Remove node logic
- Connection validation logic (type checking, nullability, etc.)
- Cycle detection
- Topological sort
- Required slot checking
- Graph serialization/deserialization

❌ **Keep in RenderGraph:**
- Runtime state management (NodeInstance ownership)
- Resource allocation (VkImage, VkBuffer creation)
- Compilation to Vulkan pipelines
- Execution scheduling
- Frame synchronization

### Why This Split?

**GraphOperations** handles **graph structure** (nodes, connections, validation)
**RenderGraph** handles **runtime execution** (resources, pipelines, scheduling)

Graph structure is **data** (can be serialized, edited, transformed).
Runtime execution is **imperative** (must run, allocate, synchronize).

---

## Migration Path

### Phase 1: Extract GraphData
- [x] Define `GraphData`, `NodeDescriptor`, `ConnectionDescriptor`
- [ ] Add `graphData_` member to RenderGraph
- [ ] Populate `graphData_` in existing `AddNode()` and `ConnectNodes()`

### Phase 2: Extract Core Operations
- [ ] Create `GraphOperations` namespace
- [ ] Implement `AddNode()`, `RemoveNode()`, `ConnectNodes()` as pure functions
- [ ] Implement `ValidateGraph()`, `HasCycles()`
- [ ] Implement `CompileToRenderGraph()`

### Phase 3: Refactor RenderGraph Internals
- [ ] Modify `RenderGraph::AddNode()` to call `GraphOperations::AddNode()`
- [ ] Modify `RenderGraph::ConnectNodes()` to call `GraphOperations::ConnectNodes()`
- [ ] Add `RenderGraph::LoadFromGraphData()` method
- [ ] Test backward compatibility (existing code should work unchanged)

### Phase 4: Create Operation Nodes
- [ ] Implement `AddNodeToGraphNode`
- [ ] Implement `ConnectNodesInGraphNode`
- [ ] Implement `RemoveNodeFromGraphNode`
- [ ] Implement `ValidateGraphNode`
- [ ] Implement `GraphCompilerNode`

### Phase 5: Build Editor
- [ ] Create `GraphStateNode` (stores GraphData)
- [ ] Wire operation nodes in editor graph
- [ ] Test round-trip: GraphData → RenderGraph → Execute

---

## Final Architecture Diagram

```
User Application (e.g., Editor)
    ↓
RenderGraph::AddNode()  ← (Convenience API)
    ↓
GraphOperations::AddNode()  ← (Core logic, single implementation)
    ↑
AddNodeToGraphNode::Execute()  ← (Data flow API)
    ↓
GraphData (pure data)
```

**Key Points:**
1. **GraphOperations** = Single source of truth (no duplication)
2. **RenderGraph** = Orchestrator (uses GraphOperations internally)
3. **Operation Nodes** = Data flow interface (also use GraphOperations)
4. **GraphData** = Pure data (flows through nodes)

---

## Conclusion

**Q: Do we need node wrappers for every RenderGraph operation?**
**A: Yes, but they don't duplicate logic - they share it.**

**Q: How do we avoid duplication?**
**A: Extract core operations into `GraphOperations` namespace. Both RenderGraph methods AND operation nodes call these shared functions.**

**Q: Does RenderGraph become pure data?**
**A: No - RenderGraph stays as orchestrator (manages runtime state). GraphData is the pure data representation.**

**Q: Who uses what?**
- **Normal applications**: Use RenderGraph API directly (unchanged)
- **Editor application**: Uses operation nodes that transform GraphData
- **Both**: Use shared GraphOperations underneath (zero duplication)

This architecture:
- ✅ Keeps RenderGraph backward compatible
- ✅ Avoids all logic duplication
- ✅ Enables graph-as-data workflows
- ✅ Provides flexibility (API, nodes, or direct operations)
- ✅ Maintains clear separation of concerns
