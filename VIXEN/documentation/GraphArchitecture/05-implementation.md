# Implementation Guide

## Overview

This guide provides concrete implementation details for building the Render Graph system, including directory structure, key classes, migration strategy, and performance considerations.

---

## Directory Structure

```
include/
  RenderGraph/
    RenderGraph.h           // Main graph class
    RenderGraphNode.h       // Node base class
    NodeType.h              // Node type definition
    NodeInstance.h          // Node instance class
    NodeTypeRegistry.h      // Type registry
    Resource.h              // Resource types
    NodeFactory.h           // Node creation
    CacheManager.h          // Multi-level caching system
    GraphCompiler.h         // Compilation logic
    ResourceAllocator.h     // Resource management
    GraphTopology.h         // Dependency graph analysis

  RenderGraph/Nodes/
    GeometryPassNode.h      // Geometry rendering
    ShadowMapNode.h         // Shadow map generation
    PostProcessNode.h       // Post-processing effects
    ComputeNode.h           // Compute shader passes
    DeviceTransferNode.h    // Cross-device transfer
    // ... more node types

source/
  RenderGraph/
    RenderGraph.cpp
    RenderGraphNode.cpp
    NodeType.cpp
    NodeInstance.cpp
    NodeTypeRegistry.cpp
    Resource.cpp
    NodeFactory.cpp
    CacheManager.cpp
    GraphCompiler.cpp
    ResourceAllocator.cpp
    GraphTopology.cpp

  RenderGraph/Nodes/
    GeometryPassNode.cpp
    ShadowMapNode.cpp
    PostProcessNode.cpp
    ComputeNode.cpp
    DeviceTransferNode.cpp
    // ... implementation for each node type
```

---

## Key Classes and Responsibilities

### RenderGraph

**Responsibility:** Main container and orchestrator

```cpp
class RenderGraph {
public:
    // Construction
    explicit RenderGraph(VulkanDevice* primaryDevice, NodeTypeRegistry* registry);

    // Graph building
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName);
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName, VulkanDevice* device);
    void ConnectNodes(NodeHandle from, uint32_t outputIdx, NodeHandle to, uint32_t inputIdx);

    // Compilation
    void Compile();

    // Execution
    void Execute(VkCommandBuffer cmd);

    // Query
    NodeInstance* GetInstance(NodeHandle handle);
    uint32_t GetInstanceCount(NodeTypeId typeId) const;

private:
    NodeTypeRegistry* typeRegistry;
    VulkanDevice* primaryDevice;
    std::vector<VulkanDevice*> usedDevices;

    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;

    GraphTopology topology;
    ResourceAllocator resourceAllocator;
    CacheManager cacheManager;
    GraphCompiler compiler;

    std::map<NodeTypeId, PipelineInstanceGroup> pipelineGroups;
    std::vector<NodeInstance*> executionOrder;
};
```

### NodeType

**Responsibility:** Define process templates

```cpp
class NodeType {
public:
    NodeTypeId GetTypeId() const { return typeId; }
    const std::string& GetTypeName() const { return typeName; }

    const std::vector<ResourceDescriptor>& GetInputSchema() const { return inputSchema; }
    const std::vector<ResourceDescriptor>& GetOutputSchema() const { return outputSchema; }

    bool SupportsInstancing() const { return supportsInstancing; }
    uint32_t GetMaxInstances() const { return maxInstances; }

    NodeInstance* CreateInstance(const std::string& instanceName, VulkanDevice* device);

protected:
    NodeTypeId typeId;
    std::string typeName;
    std::vector<ResourceDescriptor> inputSchema;
    std::vector<ResourceDescriptor> outputSchema;
    DeviceCapabilityFlags requiredCapabilities;
    PipelineType pipelineType;
    bool supportsInstancing;
    uint32_t maxInstances;

    std::function<NodeInstance*()> createInstanceFunc;
};
```

### NodeInstance

**Responsibility:** Concrete graph node

```cpp
class NodeInstance {
public:
    // Identity
    UUID GetInstanceId() const { return instanceId; }
    const std::string& GetInstanceName() const { return instanceName; }
    NodeType* GetNodeType() const { return nodeType; }

    // Device
    VulkanDevice* GetDevice() const { return device; }
    void SetDevice(VulkanDevice* dev) { device = dev; }

    // Resources
    void SetInput(uint32_t index, const Resource& resource);
    void SetOutput(uint32_t index, const Resource& resource);
    const Resource& GetInput(uint32_t index) const;
    const Resource& GetOutput(uint32_t index) const;

    // Parameters
    void SetParameter(const std::string& name, const Variant& value);
    Variant GetParameter(const std::string& name) const;

    // Execution
    virtual void Execute(VkCommandBuffer cmd) = 0;
    virtual void RecordCommands(VkCommandBuffer cmd) = 0;

    // Caching
    uint64_t GenerateCacheKey() const;
    bool IsCacheable() const;

protected:
    UUID instanceId;
    std::string instanceName;
    NodeType* nodeType;
    VulkanDevice* device;

    std::vector<Resource> inputs;
    std::vector<Resource> outputs;
    std::map<std::string, Variant> parameters;

    NodeState state;
    std::vector<NodeInstance*> dependencies;
    std::vector<NodeInstance*> dependents;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet descriptorSet;
    std::vector<VkCommandBuffer> commandBuffers;

    uint64_t cacheKey;
    bool cacheable;
};
```

### GraphCompiler

**Responsibility:** Analyze and compile graph

```cpp
class GraphCompiler {
public:
    explicit GraphCompiler(RenderGraph* graph);

    void Compile();

private:
    void PropagateDeviceAffinity();
    void AnalyzeDependencies();
    void AllocateResources();
    void GeneratePipelines();
    void RecordCommandBuffers();

    void InsertTransferNodes();
    void ValidateDeviceCapabilities();
    std::vector<NodeInstance*> TopologicalSort();

    RenderGraph* graph;
    GraphTopology* topology;
    ResourceAllocator* resourceAllocator;
    CacheManager* cacheManager;
};
```

### ResourceAllocator

**Responsibility:** Manage Vulkan resource allocation

```cpp
class ResourceAllocator {
public:
    explicit ResourceAllocator(VulkanDevice* device);

    Resource AllocateImage(const ImageDescription& desc);
    Resource AllocateBuffer(const BufferDescription& desc);

    void AnalyzeLifetimes(const std::vector<NodeInstance*>& executionOrder);
    void AliasTransientResources();

    void Free(const Resource& resource);
    void Reset();

private:
    struct ResourceLifetime {
        NodeInstance* firstUse;
        NodeInstance* lastUse;
        uint32_t firstPassIndex;
        uint32_t lastPassIndex;

        bool OverlapsWith(const ResourceLifetime& other) const;
    };

    VulkanDevice* device;
    std::vector<Resource> allocatedResources;
    std::map<Resource*, ResourceLifetime> lifetimes;

    Resource* FindAliasCandidate(const Resource& resource);
};
```

### CacheManager

**Responsibility:** Multi-level caching

```cpp
class CacheManager {
public:
    explicit CacheManager(VulkanDevice* device);

    // Pipeline cache
    VkPipeline GetOrCreatePipeline(const PipelineDescription& desc);
    void SavePipelineCache(const std::string& path);
    void LoadPipelineCache(const std::string& path);

    // Descriptor cache
    VkDescriptorSet GetOrCreateDescriptorSet(const DescriptorSetLayout& layout,
                                             const std::vector<ResourceBinding>& bindings);
    void ResetDescriptorCache();

    // Resource cache
    std::optional<Resource> GetCachedResource(uint64_t cacheKey);
    void CacheResource(uint64_t cacheKey, const Resource& resource, size_t memorySize);
    void InvalidateResource(uint64_t cacheKey);

    // Statistics
    CacheStatistics GetStatistics() const;

private:
    VulkanDevice* device;
    PipelineCache pipelineCache;
    DescriptorSetCache descriptorCache;
    ResourceCache resourceCache;
};
```

---

## Migration Strategy (10-Week Plan)

### Phase 1: Foundation (Weeks 1-2)

**Goal:** Core infrastructure

**Tasks:**
- [ ] Implement `NodeType` class
- [ ] Implement `NodeInstance` base class
- [ ] Create `Resource` abstraction
- [ ] Build `RenderGraph` container
- [ ] Implement `NodeTypeRegistry`
- [ ] Create basic graph traversal (`GraphTopology`)
- [ ] Write unit tests for core classes

**Deliverable:** Basic graph construction and traversal working

### Phase 2: Node Types (Weeks 3-4)

**Goal:** Port existing rendering operations to nodes

**Tasks:**
- [ ] Create `GeometryPassNode` (port existing geometry rendering)
- [ ] Create `ShadowMapNode` (port shadow map generation)
- [ ] Create `TextureSamplingNode` (port texture operations)
- [ ] Create `PostProcessNode` base (port post-processing foundation)
- [ ] Implement `NodeFactory` for instancing
- [ ] Register built-in node types
- [ ] Write integration tests for each node type

**Deliverable:** All existing rendering features available as nodes

### Phase 3: Resource Management (Weeks 5-6)

**Goal:** Automatic resource allocation and optimization

**Tasks:**
- [ ] Implement `ResourceAllocator`
- [ ] Add lifetime analysis algorithm
- [ ] Implement transient resource aliasing
- [ ] Build automatic barrier insertion
- [ ] Add resource validation
- [ ] Profile memory usage (target 30-50% reduction)
- [ ] Write tests for resource aliasing correctness

**Deliverable:** Automatic resource management with memory optimization

### Phase 4: Caching (Week 7)

**Goal:** Multi-level caching for performance

**Tasks:**
- [ ] Implement `PipelineCache` with VkPipelineCache integration
- [ ] Implement `DescriptorSetCache` with pooling
- [ ] Implement `ResourceCache` with LRU eviction
- [ ] Add cache key generation
- [ ] Implement cache serialization (pipeline cache to disk)
- [ ] Add cache statistics tracking
- [ ] Write tests for cache invalidation

**Deliverable:** Working multi-level cache with >80% hit rate

### Phase 5: Optimization (Week 8)

**Goal:** Performance tuning and multi-threading

**Tasks:**
- [ ] Add multi-threading to compilation phase
- [ ] Optimize command buffer generation
- [ ] Implement memory pooling for allocations
- [ ] Add performance profiling hooks
- [ ] Optimize cache key hashing
- [ ] Minimize virtual calls in hot paths
- [ ] Profile and benchmark (target <5% overhead)

**Deliverable:** Optimized system meeting performance targets

### Phase 6: Integration (Weeks 9-10)

**Goal:** Replace current renderer

**Tasks:**
- [ ] Create migration tool for existing scenes
- [ ] Replace `VulkanRenderer` with `RenderGraph`
- [ ] Update `VulkanDrawable` to work with nodes
- [ ] Port all existing rendering code to use graph
- [ ] Add comprehensive validation
- [ ] Update documentation
- [ ] Create usage examples
- [ ] Final testing and bug fixes

**Deliverable:** Fully migrated codebase using Render Graph

---

## Performance Considerations

### Memory Management

**Strategies:**
1. **Pool allocators** for nodes and resources
   ```cpp
   class NodeInstancePool {
       std::vector<std::unique_ptr<NodeInstance>> pool;
       std::stack<NodeInstance*> freeList;
   };
   ```

2. **Transient resource aliasing** (30-50% memory reduction expected)
   - Analyze resource lifetimes
   - Reuse memory for non-overlapping resources

3. **Descriptor set pooling**
   - Pre-allocate descriptor pools
   - Reuse descriptor sets when possible

### Compilation Cost

**Optimization:**
1. **Cache compiled graphs** across frames
   - Store execution order
   - Reuse pipeline groups
   - Only recompile on graph changes

2. **Incremental recompilation**
   - Detect which nodes changed
   - Only recompile affected subgraphs

3. **Parallel compilation**
   - Compile independent subgraphs in parallel
   - Use multiple threads for pipeline creation

### Runtime Overhead

**Target:** < 5% vs hand-written Vulkan code

**Optimization:**
1. **Minimize virtual calls**
   - Use templates where possible
   - Inline hot paths
   - Profile and optimize bottlenecks

2. **Batch API calls**
   - Group descriptor updates
   - Combine barriers
   - Reduce state changes

3. **Command buffer inheritance**
   - Reuse secondary command buffers
   - Pre-record static commands

---

## Testing Strategy

### Unit Tests

**Coverage:**
- Node creation and connection
- Resource lifetime analysis
- Cache key generation
- Dependency resolution
- Topological sort
- Resource aliasing algorithm

**Framework:** Google Test or Catch2

```cpp
TEST(RenderGraphTest, NodeConnectionCreatesEdge) {
    RenderGraph graph(device, &registry);
    auto nodeA = graph.AddNode("GeometryPass", "A");
    auto nodeB = graph.AddNode("ShadowMapPass", "B");

    graph.ConnectNodes(nodeA, 0, nodeB, 0);

    auto* instanceB = graph.GetInstance(nodeB);
    EXPECT_EQ(instanceB->GetInput(0).sourceNode, graph.GetInstance(nodeA));
}
```

### Integration Tests

**Coverage:**
- Port existing renderer tests to graph
- Shadow mapping correctness
- Post-processing accuracy
- Multi-GPU execution
- Resource leak detection (Valgrind/ASan)

```cpp
TEST(IntegrationTest, ShadowMappingProducesCorrectOutput) {
    RenderGraph graph(device, &registry);

    // Build shadow mapping graph
    auto geometry = graph.AddNode("GeometryPass", "Scene");
    auto shadow = graph.AddNode("ShadowMapPass", "Shadow");
    graph.ConnectNodes(geometry, 0, shadow, 0);

    // Compile and execute
    graph.Compile();
    graph.Execute(cmd);

    // Validate output
    auto shadowMap = shadow.GetOutput(0);
    EXPECT_TRUE(ValidateShadowMap(shadowMap));
}
```

### Performance Tests

**Metrics:**
- Frame time comparison (target: <5% overhead)
- Memory usage profiling
- Cache hit rate monitoring
- Compilation time measurement

```cpp
TEST(PerformanceTest, FrameTimeOverhead) {
    // Measure hand-written renderer
    auto baselineTime = BenchmarkExistingRenderer();

    // Measure graph-based renderer
    auto graphTime = BenchmarkGraphRenderer();

    // Verify overhead is < 5%
    float overhead = (graphTime - baselineTime) / baselineTime;
    EXPECT_LT(overhead, 0.05f);  // Less than 5% overhead
}
```

---

## Error Handling

### Compilation Errors

```cpp
class CompilationError : public std::runtime_error {
public:
    CompilationError(const std::string& message, NodeInstance* node = nullptr)
        : std::runtime_error(message), node(node) {}

    NodeInstance* GetNode() const { return node; }

private:
    NodeInstance* node;
};

// Usage
void GraphCompiler::Compile() {
    try {
        PropagateDeviceAffinity();
        AnalyzeDependencies();
        // ...
    } catch (const CompilationError& e) {
        LOG_ERROR("Compilation failed: {}", e.what());
        if (e.GetNode()) {
            LOG_ERROR("  At node: {}", e.GetNode()->GetInstanceName());
        }
        throw;
    }
}
```

### Runtime Validation

```cpp
class RenderGraph {
    void ValidateBeforeExecution() {
        // Check all nodes are compiled
        for (auto& node : instances) {
            if (node->state != NodeState::Compiled) {
                throw std::runtime_error("Graph not compiled");
            }
        }

        // Check all resources are allocated
        for (auto& node : instances) {
            for (auto& output : node->outputs) {
                if (!output.IsAllocated()) {
                    throw std::runtime_error("Resource not allocated");
                }
            }
        }
    }
};
```

---

## Debugging Tools

### Graph Visualization

Export graph to DOT format for visualization:

```cpp
void RenderGraph::ExportToDot(const std::string& path) {
    std::ofstream file(path);
    file << "digraph RenderGraph {\n";

    for (auto& node : instances) {
        file << "  " << node->GetInstanceName() << " [label=\""
             << node->GetNodeType()->GetTypeName() << "\\n"
             << node->GetInstanceName() << "\"];\n";
    }

    for (auto& node : instances) {
        for (auto* dep : node->dependencies) {
            file << "  " << dep->GetInstanceName() << " -> "
                 << node->GetInstanceName() << ";\n";
        }
    }

    file << "}\n";
}

// Usage: dot -Tpng graph.dot -o graph.png
```

### Profiling Hooks

```cpp
class ProfilingScope {
public:
    ProfilingScope(const std::string& name)
        : name(name), start(std::chrono::high_resolution_clock::now()) {}

    ~ProfilingScope() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        LOG_PROFILE("{}: {} μs", name, duration.count());
    }

private:
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
};

// Usage
void GraphCompiler::Compile() {
    ProfilingScope profile("GraphCompilation");
    // ... compilation code
}
```

---

**See also:**
- [Node System](01-node-system.md) - Core node architecture
- [Graph Compilation](02-graph-compilation.md) - Compilation implementation
- [Caching System](04-caching.md) - Cache implementation details
