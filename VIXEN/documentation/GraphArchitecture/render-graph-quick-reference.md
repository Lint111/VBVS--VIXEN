# Render Graph - Quick Reference

## Core Concepts

### Node Type vs Node Instance

| Concept | Description | Count | Example |
|---------|-------------|-------|---------|
| **Node Type** | Template/Definition of a process | 1 per process type | `ShadowMapPass` type |
| **Node Instance** | Concrete usage in the graph | N per scene | `ShadowMap_Light0`, `ShadowMap_Light1`, etc. |

### Key Classes

```cpp
// 1. Node Type (Definition)
class NodeType {
    NodeTypeId typeId;           // e.g., NodeTypeId::ShadowMap
    std::string typeName;        // e.g., "ShadowMapPass"
    bool supportsInstancing;     // Can create multiple instances?
    // ... schema, requirements, factory
};

// 2. Node Instance (Usage)
class NodeInstance {
    UUID instanceId;             // Unique ID
    std::string instanceName;    // e.g., "ShadowMap_Light0"
    NodeType* nodeType;          // Points to ShadowMapPass type
    VulkanDevice* device;        // Which GPU to use
    // ... inputs, outputs, state
};

// 3. Registry (Type Storage)
class NodeTypeRegistry {
    void RegisterNodeType(std::unique_ptr<NodeType> type);
    NodeType* GetNodeType(NodeTypeId id);
};

// 4. Graph (Instance Container)
class RenderGraph {
    RenderGraph(VulkanDevice* device, NodeTypeRegistry* registry);
    NodeHandle AddNode(NodeTypeId type, std::string name);  // Creates instance
    void ConnectNodes(NodeHandle from, NodeHandle to);
    void Compile();  // Analyzes instances, creates pipelines
    void Execute();
};
```

## Workflow

### 1. Setup (Once)
```cpp
// Register available node types
NodeTypeRegistry registry;
RegisterBuiltInNodeTypes(registry);  // ShadowMapPass, GeometryPass, etc.
```

### 2. Build Graph (Per Scene)
```cpp
RenderGraph graph(device, &registry);

// Create instances from types
auto scene = graph.AddNode(NodeTypeId::GeometryPass, "MainScene");
auto shadow0 = graph.AddNode(NodeTypeId::ShadowMap, "ShadowMap_Light0");
auto shadow1 = graph.AddNode(NodeTypeId::ShadowMap, "ShadowMap_Light1");

// Connect instances
graph.ConnectNodes(scene, 0, shadow0, 0);
graph.ConnectNodes(scene, 0, shadow1, 0);
```

### 3. Compile (Analyzes Instances)
```cpp
graph.Compile();
// Compiler discovers:
// - 1 instance of GeometryPass
// - 2 instances of ShadowMapPass
// - Both ShadowMap instances can share a pipeline
// - Creates 1 pipeline for GeometryPass, 1 shared pipeline for 2 ShadowMaps
```

### 4. Execute
```cpp
graph.Execute(commandBuffer);
```

## Pipeline Instancing

The compiler groups instances by type and determines pipeline sharing:

```
Input: 10 shadow map instances (same ShadowMapPass type)

Scenario A - All Identical:
→ 1 shared VkPipeline for all 10 instances
→ Each instance has unique descriptor set and command buffer

Scenario B - 2 Variants (8 @ 1024x1024, 2 @ 2048x2048):
→ 2 VkPipelines (one per resolution variant)
→ 8 instances share pipeline #1
→ 2 instances share pipeline #2

Scenario C - All Different:
→ 10 separate VkPipelines
```

## Device Affinity

```cpp
// Graph bound to primary device
RenderGraph graph(gpu0, &registry);

// Instance inherits device from graph
auto scene = graph.AddNode(NodeTypeId::GeometryPass, "Scene");
// scene->device = gpu0

// Instance inherits device from input
auto shadow = graph.AddNode(NodeTypeId::ShadowMap, "Shadow");
graph.ConnectNodes(scene, 0, shadow, 0);
// shadow->device = gpu0 (inherited from scene)

// Explicit device assignment
auto postFx = graph.AddNode(NodeTypeId::PostProcess, "Bloom", gpu1);
// postFx->device = gpu1 (explicit)

graph.Compile();
// Compiler inserts transfer node: scene (gpu0) → postFx (gpu1)
```

## Resource Types

| Type | Description | Lifetime |
|------|-------------|----------|
| **Variable** | Constant input (uniforms, push constants) | Per-frame or static |
| **Product** | Output from another node | Managed by graph |
| **Transient** | Temporary within frame | Freed after last use |
| **Persistent** | Lives across frames | Managed explicitly |

## Compilation Phases

1. **Device Affinity Propagation** - Assign devices to instances
2. **Dependency Analysis** - Build DAG, topological sort
3. **Resource Allocation** - Allocate per-device, alias transients
4. **Pipeline Generation** - Group instances by type, create shared pipelines
5. **Command Buffer Recording** - Generate commands per device

## Caching

- **Pipeline Cache**: Shared pipelines across instances of same type
- **Descriptor Set Cache**: Reuse sets with identical bindings
- **Resource Cache**: Reuse rendered outputs from previous frames

## Example: Shadow Maps

```cpp
// Type definition (once)
auto shadowType = std::make_unique<NodeType>();
shadowType->typeId = NodeTypeId::ShadowMap;
shadowType->supportsInstancing = true;
registry.RegisterNodeType(std::move(shadowType));

// Instance creation (per light)
for (int i = 0; i < numLights; i++) {
    auto shadow = graph.AddNode(NodeTypeId::ShadowMap, "Shadow_" + std::to_string(i));
    // Each instance references the same ShadowMapPass type
}

// Compilation result:
// - All instances share 1 pipeline (if compatible)
// - Each has unique descriptor set for its light transform
// - Efficient GPU instancing achieved
```

## Key Benefits

1. **Type Safety** - Schema validation at compile time
2. **Pipeline Reuse** - Automatic sharing across compatible instances
3. **Memory Efficiency** - Single pipeline for many instances
4. **Flexibility** - Unlimited instances per type (unless maxInstances set)
5. **Multi-GPU** - Automatic cross-device transfer and load balancing

---

**See:** `render-graph-architecture.md` for full details
