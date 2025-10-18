# Graph Compilation and Execution

## Overview

The compilation process transforms a high-level render graph into optimized, executable Vulkan commands. It consists of five phases: device affinity propagation, dependency analysis, resource allocation, pipeline generation, and command buffer recording.

---

## Graph Construction API

### RenderGraph Class

```cpp
class RenderGraph {
public:
    // Construction - Graph must be bound to a device and type registry
    explicit RenderGraph(VulkanDevice* primaryDevice, NodeTypeRegistry* registry);

    // Build graph - Create instances
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName);
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName,
                      VulkanDevice* specificDevice);

    // Connect instances
    void ConnectNodes(NodeHandle from, uint32_t outputIdx,
                     NodeHandle to, uint32_t inputIdx);

    // Query instances
    uint32_t GetInstanceCount(NodeTypeId typeId) const;
    std::vector<NodeInstance*> GetInstancesOfType(NodeTypeId typeId);
    NodeInstance* GetInstance(NodeHandle handle);

    // Compile & execute
    void Compile();
    void Execute(VkCommandBuffer cmd);

    // Device management
    VulkanDevice* GetPrimaryDevice() const { return primaryDevice; }
    const std::vector<VulkanDevice*>& GetUsedDevices() const { return usedDevices; }

private:
    // Registry
    NodeTypeRegistry* typeRegistry;           // Registry of available node types

    // Devices
    VulkanDevice* primaryDevice;              // Primary device for this graph
    std::vector<VulkanDevice*> usedDevices;   // All devices used by instances

    // Instances
    std::vector<std::unique_ptr<NodeInstance>> instances;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;

    // Graph structure
    GraphTopology topology;                    // Dependency graph
    ResourceAllocator resourceAllocator;

    // Compiled pipeline groups
    std::map<NodeTypeId, PipelineInstanceGroup> pipelineGroups;
};
```

---

## Compilation Phases

### Phase 1: Device Affinity Propagation

Assigns devices to nodes based on input dependencies and explicit assignments.

**Steps:**
1. Identify explicitly assigned device nodes
2. Propagate device affinity through connections
3. Detect device boundaries (cross-device transfers needed)
4. Insert transfer nodes automatically at boundaries
5. Validate device capabilities for each node type

**Rules:**
- Explicitly assigned devices take precedence
- Nodes inherit device from input nodes
- Multiple inputs from different devices require explicit assignment or transfer node insertion

**Example:**
```cpp
RenderGraph graph(device0);  // Primary device

auto geometryPass = graph.AddNode(NodeType::GeometryPass, "Scene");
// geometryPass->device = device0 (inherited from graph)

auto shadowMap = graph.AddNode(NodeType::ShadowMap, "Shadow");
graph.ConnectNodes(geometryPass, 0, shadowMap, 0);
// shadowMap->device = device0 (inherited from geometryPass input)

auto postProcess = graph.AddNode(NodeType::PostProcess, "Bloom", device1);
// postProcess->device = device1 (explicitly set)
graph.ConnectNodes(shadowMap, 0, postProcess, 0);
// Transfer node automatically inserted between shadowMap and postProcess
```

### Phase 2: Dependency Analysis

Analyzes the graph structure to determine execution order.

**Steps:**
1. Build directed acyclic graph (DAG) from node connections
2. Perform topological sort for execution order
3. Detect cycles (compilation error if found)
4. Group nodes by device for parallel execution
5. Identify cross-device synchronization points

**Output:**
- Execution order (topologically sorted node list)
- Device-grouped execution lists
- Synchronization requirements

### Phase 3: Resource Allocation

Analyzes resource lifetimes and allocates physical Vulkan resources.

**Steps:**
1. Analyze resource lifetimes per device
2. Allocate physical resources on appropriate devices
3. Alias transient resources where possible (memory optimization)
4. Handle cross-device resource copies
5. Generate resource creation commands

**Transient Resource Aliasing:**
Resources that don't overlap in lifetime can share the same physical memory:

```cpp
// Example: Two intermediate render targets that never exist simultaneously
auto intermediateA = CreateTransientImage(...);  // Lives during passes 1-3
auto intermediateB = CreateTransientImage(...);  // Lives during passes 4-6
// Both can alias the same VkDeviceMemory, reducing memory by 50%
```

**Expected Benefit:** 30-50% memory reduction for typical scenes

### Phase 4: Pipeline Generation

Creates Vulkan pipelines for each node, leveraging pipeline sharing across instances.

**Steps:**
1. Group instances by NodeType
2. Determine pipeline sharing compatibility
3. Generate shared pipelines or variants
4. Check pipeline cache for existing pipelines
5. Create descriptor sets per instance (per device)
6. Store compiled pipelines in cache

**Pipeline Sharing Logic:**
```cpp
for (auto& [typeId, group] : pipelineGroups) {
    if (CanSharePipeline(group.instances)) {
        // Create single pipeline for all instances
        group.sharedPipeline = CreatePipeline(group.instances[0]);
        for (auto* instance : group.instances) {
            instance->pipeline = group.sharedPipeline;
        }
    } else {
        // Create variants based on instance differences
        for (auto* instance : group.instances) {
            uint64_t variantKey = GenerateVariantKey(instance);
            if (!group.variantPipelines.contains(variantKey)) {
                group.variantPipelines[variantKey] = CreatePipeline(instance);
            }
            instance->pipeline = group.variantPipelines[variantKey];
        }
    }
}
```

### Phase 5: Command Buffer Recording

Generates optimized command buffer structure per device with cache-aware batching.

**Steps:**
1. For each device, create command buffer(s)
2. Group nodes into cache-friendly batches
3. Record commands in cache-optimized order
4. Insert pipeline barriers automatically
5. Insert cross-device synchronization (semaphores)
6. Coordinate multi-device synchronization

**Cache-Aware Batching:**
```cpp
struct NodeBatch {
    std::vector<NodeInstance*> nodes;
    size_t totalWorkingSet;           // Combined memory footprint
    bool fitsInCache;                 // Does batch fit in L2?

    bool CanAddNode(NodeInstance* node, size_t cacheSize) const {
        size_t newTotal = totalWorkingSet + node->GetTotalMemoryFootprint();
        return newTotal <= cacheSize;
    }
};

class GraphCompiler {
    std::vector<NodeBatch> CreateCacheAwareBatches(
        const std::vector<NodeInstance*>& executionOrder,
        VulkanDevice* device) {

        std::vector<NodeBatch> batches;
        const size_t effectiveCache = device->GetCacheInfo().effectiveWorkingSet;

        NodeBatch currentBatch;
        currentBatch.totalWorkingSet = 0;
        currentBatch.fitsInCache = true;

        for (auto* node : executionOrder) {
            size_t nodeFootprint = node->GetTotalMemoryFootprint();

            // Check if node fits in current batch
            if (currentBatch.CanAddNode(node, effectiveCache)) {
                // Add to current batch
                currentBatch.nodes.push_back(node);
                currentBatch.totalWorkingSet += nodeFootprint;
            } else {
                // Current batch is full, start new one
                if (!currentBatch.nodes.empty()) {
                    batches.push_back(std::move(currentBatch));
                }

                // Start new batch with this node
                currentBatch = NodeBatch();
                currentBatch.nodes.push_back(node);
                currentBatch.totalWorkingSet = nodeFootprint;
                currentBatch.fitsInCache = (nodeFootprint <= effectiveCache);
            }
        }

        // Add final batch
        if (!currentBatch.nodes.empty()) {
            batches.push_back(std::move(currentBatch));
        }

        return batches;
    }

    void RecordCommandBuffer(VkCommandBuffer cmd,
                            const std::vector<NodeBatch>& batches) {
        for (const auto& batch : batches) {
            // Optional: Insert cache flush/invalidate between batches
            if (batch.totalWorkingSet > batch.nodes[0]->device->GetCacheInfo().l2CacheSize) {
                // Large batch - may cause cache thrashing
                LOG_WARN("Batch size ({} MB) exceeds L2 cache ({} MB)",
                         batch.totalWorkingSet / (1024.0 * 1024.0),
                         batch.nodes[0]->device->GetCacheInfo().l2CacheSize / (1024.0 * 1024.0));
            }

            // Record all nodes in this batch
            for (auto* node : batch.nodes) {
                // Insert resource barriers (automatic)
                InsertResourceBarriers(cmd, node);

                // Bind pipeline (may be shared)
                vkCmdBindPipeline(cmd, ..., node->pipeline);

                // Bind descriptor set (unique per instance)
                vkCmdBindDescriptorSets(cmd, ..., node->descriptorSet);

                // Execute node-specific commands
                node->RecordCommands(cmd);
            }
        }
    }
};
```

**Batch Optimization Strategies:**

1. **Locality-Aware Grouping**
   ```cpp
   // Group nodes that share resources
   std::sort(executionOrder.begin(), executionOrder.end(),
       [](NodeInstance* a, NodeInstance* b) {
           return CountSharedResources(a, b) > 0;
       });
   ```

2. **Pipeline Change Minimization**
   ```cpp
   // Within a batch, group by pipeline to reduce state changes
   std::stable_sort(batch.nodes.begin(), batch.nodes.end(),
       [](NodeInstance* a, NodeInstance* b) {
           return a->pipeline < b->pipeline;
       });
   ```

3. **Working Set Calculation**
   ```cpp
   size_t CalculateBatchWorkingSet(const NodeBatch& batch) {
       std::set<const Resource*> uniqueResources;

       for (auto* node : batch.nodes) {
           // Count unique resources (avoid double-counting shared resources)
           for (const auto& input : node->inputs) {
               uniqueResources.insert(&input);
           }
           for (const auto& output : node->outputs) {
               uniqueResources.insert(&output);
           }
       }

       size_t total = 0;
       for (const auto* resource : uniqueResources) {
           total += GetResourceSize(*resource);
       }

       return total;
   }
   ```

---

## Execution Model

### Single-Device Execution

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (auto& node : executionOrder) {
        // Check cache
        if (node->cacheable && cache.Has(node->cacheKey)) {
            UseCachedResource(node, cache);
            continue;
        }

        // Execute node
        node->Execute(cmd);

        // Update cache if applicable
        if (node->cacheable) {
            cache.Store(node->cacheKey, node->GetOutput());
        }
    }
}
```

### Multi-Device Execution

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    // Group nodes by device
    std::map<VulkanDevice*, std::vector<RenderGraphNode*>> deviceGroups;
    for (auto& node : executionOrder) {
        deviceGroups[node->device].push_back(node.get());
    }

    // Execute in parallel on each device
    std::vector<std::thread> deviceThreads;
    for (auto& [device, nodes] : deviceGroups) {
        deviceThreads.emplace_back([device, nodes]() {
            VkCommandBuffer deviceCmd = AllocateCommandBuffer(device);

            for (auto* node : nodes) {
                // Wait for cross-device dependencies
                if (node->HasCrossDeviceDependencies()) {
                    WaitForCrossDeviceSemaphores(node);
                }

                node->Execute(deviceCmd);

                // Signal if other devices depend on this node
                if (node->HasCrossDeviceDependents()) {
                    SignalCrossDeviceSemaphore(node);
                }
            }

            SubmitCommandBuffer(device, deviceCmd);
        });
    }

    // Wait for all devices to complete
    for (auto& thread : deviceThreads) {
        thread.join();
    }
}
```

---

## Dependency Analysis Details

### Directed Acyclic Graph (DAG)

The dependency graph must be acyclic to ensure a valid execution order:

```cpp
class GraphTopology {
public:
    void AddEdge(NodeInstance* from, NodeInstance* to);
    bool HasCycle() const;
    std::vector<NodeInstance*> TopologicalSort();

private:
    std::map<NodeInstance*, std::vector<NodeInstance*>> adjacencyList;

    bool HasCycleUtil(NodeInstance* node,
                      std::set<NodeInstance*>& visited,
                      std::set<NodeInstance*>& recursionStack) const;
};
```

**Cycle Detection:**
If a cycle is detected during compilation, the graph is invalid:
```
A → B → C → A  // ERROR: Cycle detected
```

### Topological Sort

Determines valid execution order respecting dependencies:

```cpp
std::vector<NodeInstance*> GraphTopology::TopologicalSort() {
    std::vector<NodeInstance*> result;
    std::set<NodeInstance*> visited;
    std::stack<NodeInstance*> stack;

    // DFS-based topological sort
    for (auto& [node, _] : adjacencyList) {
        if (!visited.contains(node)) {
            TopologicalSortUtil(node, visited, stack);
        }
    }

    // Stack contains nodes in reverse topological order
    while (!stack.empty()) {
        result.push_back(stack.top());
        stack.pop();
    }

    return result;
}
```

---

## Resource Allocation Strategy

### Lifetime Analysis

```cpp
struct ResourceLifetime {
    NodeInstance* firstUse;   // First node to read/write this resource
    NodeInstance* lastUse;    // Last node to read/write this resource
    uint32_t firstPassIndex;  // Execution order index of first use
    uint32_t lastPassIndex;   // Execution order index of last use
};

class ResourceAllocator {
    void AnalyzeLifetimes() {
        for (auto& resource : allResources) {
            resource.lifetime = DetermineLifetime(resource);
        }
    }

    void AllocateResources() {
        // Sort resources by first use
        std::sort(resources.begin(), resources.end(),
                  [](auto& a, auto& b) { return a.lifetime.firstPassIndex < b.lifetime.firstPassIndex; });

        // Alias transient resources with non-overlapping lifetimes
        for (auto& resource : transientResources) {
            if (auto* aliasTarget = FindAliasCandidate(resource)) {
                resource.physicalMemory = aliasTarget->physicalMemory;
            } else {
                resource.physicalMemory = AllocateNewMemory(resource);
            }
        }
    }

    Resource* FindAliasCandidate(const Resource& resource) {
        // Find resource with compatible format that doesn't overlap lifetime
        for (auto& candidate : allocatedResources) {
            if (candidate.CanAlias(resource) && !candidate.lifetime.OverlapsWith(resource.lifetime)) {
                return &candidate;
            }
        }
        return nullptr;
    }
};
```

### Automatic Barrier Insertion

Barriers are automatically inserted based on resource usage:

```cpp
void InsertResourceBarriers(VkCommandBuffer cmd, NodeInstance* node) {
    for (auto& input : node->inputs) {
        if (input.needsBarrier) {
            VkImageMemoryBarrier barrier = {};
            barrier.srcAccessMask = input.previousAccess;
            barrier.dstAccessMask = input.requiredAccess;
            barrier.oldLayout = input.previousLayout;
            barrier.newLayout = input.requiredLayout;
            barrier.image = input.vulkan.image;

            vkCmdPipelineBarrier(cmd,
                                input.previousStage,
                                input.requiredStage,
                                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }
}
```

---

## Pipeline Cache Integration

```cpp
class PipelineCache {
public:
    VkPipeline GetOrCreate(const PipelineDescription& desc, VulkanDevice* device) {
        uint64_t key = HashPipelineDescription(desc);

        if (auto it = cache.find(key); it != cache.end()) {
            cacheHits++;
            return it->second;
        }

        // Create new pipeline using VkPipelineCache
        VkPipeline pipeline = CreatePipeline(desc, device, vkPipelineCache);
        cache[key] = pipeline;
        cacheMisses++;

        return pipeline;
    }

    void SaveToFile(const std::string& path) {
        size_t dataSize;
        vkGetPipelineCacheData(device, vkPipelineCache, &dataSize, nullptr);

        std::vector<uint8_t> data(dataSize);
        vkGetPipelineCacheData(device, vkPipelineCache, &dataSize, data.data());

        WriteToFile(path, data);
    }

private:
    VkPipelineCache vkPipelineCache;
    std::map<uint64_t, VkPipeline> cache;
    uint32_t cacheHits = 0;
    uint32_t cacheMisses = 0;
};
```

---

## Performance Metrics

### Compilation Time Targets

| Graph Complexity | Target Time |
|-----------------|-------------|
| Simple (< 10 nodes) | < 10ms |
| Medium (10-50 nodes) | < 50ms |
| Complex (> 50 nodes) | < 100ms |

### Runtime Overhead

Target: < 5% overhead compared to hand-written Vulkan code

**Optimization Strategies:**
- Minimize virtual calls in hot paths
- Batch API calls where possible
- Use command buffer inheritance
- Cache compiled graphs across frames

---

**See also:**
- [Node System](01-node-system.md) - Understanding node types and instances
- [Multi-Device Support](03-multi-device.md) - Cross-device compilation details
- [Caching System](04-caching.md) - Multi-level caching strategies
