# Node System Design

## Overview

The node system is the foundation of the Render Graph architecture, distinguishing between **Node Types** (reusable process definitions) and **Node Instances** (specific instantiations in the graph).

---

## Node Type vs Node Instance

### Core Distinction

| Concept | Role | Count | Example |
|---------|------|-------|---------|
| **Node Type** | Template/Definition | 1 per process | `ShadowMapPass` |
| **Node Instance** | Concrete usage | N per scene | `ShadowMap_Light0`, `ShadowMap_Light1` |

### Node Type (Process Definition)

Node Types define the template for a rendering process. Each type describes:
- Input/output resource schemas
- Required device capabilities
- Instancing support
- Factory method for creating instances

```cpp
class NodeType {
    // Type Identification
    NodeTypeId typeId;                    // Unique type identifier
    std::string typeName;                 // Type name (e.g., "ShadowMapPass")

    // Schema Definition
    std::vector<ResourceDescriptor> inputSchema;   // Expected inputs
    std::vector<ResourceDescriptor> outputSchema;  // Produced outputs

    // Requirements
    DeviceCapabilityFlags requiredCapabilities;    // GPU features needed
    PipelineType pipelineType;                     // Graphics/Compute/RayTracing

    // Instancing
    bool supportsInstancing;              // Can be instanced in pipeline
    uint32_t maxInstances;                // Max instances per graph (0 = unlimited)

    // Unit of Work Metrics (for scheduling)
    WorkloadMetrics workloadMetrics;      // Space and time cost estimates

    // Factory
    std::function<NodeInstance*()> CreateInstance;
};
```

**Examples of Node Types:**
- `ShadowMapPass` - Can be instanced once per light
- `GeometryPass` - Typically single instance
- `BlurPass` - Can be instanced for multi-pass effects
- `ComputeShaderPass` - Can be instanced for parallel work

### Node Instance (Graph Node)

Node Instances are concrete instantiations of a Node Type within a specific render graph. Each instance has:
- Unique identity and name
- Device affinity
- Actual input/output resources
- Instance-specific parameters
- Execution state

```cpp
class NodeInstance {
    // Instance Identification
    UUID instanceId;                      // Unique instance ID
    std::string instanceName;             // Instance name (e.g., "ShadowMap_Light0")
    NodeType* nodeType;                   // Reference to type definition

    // Device Affinity
    VulkanDevice* device;                 // Device this instance executes on
    uint32_t deviceIndex;                 // Device index for multi-GPU

    // Instance-Specific Data
    std::vector<Resource> inputs;         // Actual input resources for this instance
    std::vector<Resource> outputs;        // Actual output resources for this instance
    std::map<std::string, Variant> parameters;  // Instance-specific parameters

    // Execution State
    NodeState state;                      // Ready, Compiled, Executing, Complete
    std::vector<NodeInstance*> dependencies;

    // Pipeline Resources (shared across instances of same type)
    VkPipeline pipeline;                  // May be shared with other instances
    VkPipelineLayout pipelineLayout;      // Shared layout

    // Instance-Specific Resources
    VkDescriptorSet descriptorSet;        // Unique per instance
    std::vector<VkCommandBuffer> commandBuffers;  // Per-instance commands

    // Workload Metrics (for this instance)
    size_t inputMemoryFootprint;          // Total memory of inputs on device
    PerformanceStats performanceStats;    // Execution time tracking

    // Caching
    uint64_t cacheKey;                    // Hash for this instance's output
    bool cacheable;                       // Whether output can be cached
};
```

---

## Unit of Work Tracking

### Workload Metrics (Space + Time)

Each node type tracks expected resource usage (space) and execution time (time) to enable intelligent scheduling and load balancing decisions.

#### Space: Memory Footprint

```cpp
struct WorkloadMetrics {
    // Space (Memory) - Type-level estimates
    size_t estimatedInputMemory;       // Expected input size per instance (bytes)
    size_t estimatedOutputMemory;      // Expected output size per instance (bytes)
    size_t estimatedInternalMemory;    // Scratch/temporary memory (bytes)

    // Time (Execution) - Type-level estimates
    std::chrono::microseconds estimatedExecutionTime;  // Expected time per instance

    // Confidence
    float memoryEstimateConfidence;    // 0.0-1.0 (how accurate is the estimate)
    float timeEstimateConfidence;      // 0.0-1.0 (how accurate is the estimate)
};

class NodeInstance {
    // Calculate actual memory footprint for this instance
    size_t CalculateInputMemoryFootprint() const {
        size_t total = 0;
        for (const auto& input : inputs) {
            total += GetResourceSize(input);
        }
        return total;
    }

    size_t GetTotalMemoryFootprint() const {
        return inputMemoryFootprint +
               GetOutputMemorySize() +
               nodeType->workloadMetrics.estimatedInternalMemory;
    }
};
```

**Space Calculation:**
- **Input Memory**: Sum of all input resource sizes (images, buffers, uniforms)
- **Output Memory**: Size of all output resources
- **Internal Memory**: Temporary allocations needed during execution

**Example:**
```cpp
// Shadow map node
ShadowMapPass:
  Input Memory  = GeometryBuffer (10MB) + Transform (256 bytes) = ~10MB
  Output Memory = DepthMap 1024x1024 (4MB)
  Internal      = 0 (no scratch memory)
  Total         = ~14MB per instance
```

#### Time: Performance Profiling

```cpp
struct PerformanceStats {
    // Timing measurements (debug builds only)
    std::chrono::microseconds lastExecutionTime;
    std::chrono::microseconds averageExecutionTime;
    std::chrono::microseconds maxExecutionTime;
    std::chrono::microseconds minExecutionTime;

    // Sample tracking
    uint32_t executionCount;           // Number of times executed
    uint32_t samplesCollected;         // Number of timing samples

    // Moving average window
    static constexpr uint32_t SAMPLE_WINDOW = 120;  // ~2 seconds at 60fps
    std::deque<std::chrono::microseconds> recentSamples;

    void RecordExecution(std::chrono::microseconds duration) {
        lastExecutionTime = duration;
        executionCount++;

        // Update moving average
        recentSamples.push_back(duration);
        if (recentSamples.size() > SAMPLE_WINDOW) {
            recentSamples.pop_front();
        }

        // Calculate statistics
        samplesCollected = recentSamples.size();
        averageExecutionTime = CalculateAverage(recentSamples);
        maxExecutionTime = std::max(maxExecutionTime, duration);
        minExecutionTime = (minExecutionTime.count() == 0) ? duration : std::min(minExecutionTime, duration);
    }

private:
    std::chrono::microseconds CalculateAverage(const std::deque<std::chrono::microseconds>& samples) {
        auto sum = std::accumulate(samples.begin(), samples.end(), std::chrono::microseconds(0));
        return sum / samples.size();
    }
};
```

**Time Tracking (Debug Mode):**
```cpp
class NodeInstance {
    void Execute(VkCommandBuffer cmd) {
#ifdef RENDER_GRAPH_PROFILE
        auto startTime = std::chrono::high_resolution_clock::now();
#endif

        // Execute node work
        RecordCommands(cmd);

#ifdef RENDER_GRAPH_PROFILE
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        performanceStats.RecordExecution(duration);

        // Update type-level estimate with actual measurements
        UpdateTypeEstimate(duration);
#endif
    }

    void UpdateTypeEstimate(std::chrono::microseconds actualTime) {
        // Feed actual measurements back to type for better estimates
        nodeType->workloadMetrics.estimatedExecutionTime =
            (nodeType->workloadMetrics.estimatedExecutionTime + actualTime) / 2;

        // Increase confidence as we collect more samples
        nodeType->workloadMetrics.timeEstimateConfidence =
            std::min(1.0f, nodeType->workloadMetrics.timeEstimateConfidence + 0.01f);
    }
};
```

### Using Metrics for Scheduling

#### Device Assignment Based on Workload

```cpp
class GraphCompiler {
    VulkanDevice* SelectOptimalDevice(NodeInstance* node,
                                      const std::vector<VulkanDevice*>& availableDevices) {
        struct DeviceScore {
            VulkanDevice* device;
            float score;  // Higher is better
        };

        std::vector<DeviceScore> scores;

        for (auto* device : availableDevices) {
            float score = 0.0f;

            // Factor 1: Current device memory usage
            size_t availableMemory = device->GetAvailableMemory();
            size_t requiredMemory = node->GetTotalMemoryFootprint();
            float memoryScore = (availableMemory > requiredMemory) ?
                (float)availableMemory / requiredMemory : 0.0f;

            // Factor 2: Current device load (estimated time)
            auto deviceLoad = CalculateDeviceLoad(device);
            float loadScore = 1.0f - (deviceLoad.count() / 16666.0f);  // Normalized to 60fps

            // Factor 3: Data locality (inputs already on this device)
            float localityScore = CalculateDataLocality(node, device);

            // Weighted combination
            score = (memoryScore * 0.3f) + (loadScore * 0.4f) + (localityScore * 0.3f);

            scores.push_back({device, score});
        }

        // Select device with highest score
        auto best = std::max_element(scores.begin(), scores.end(),
            [](const DeviceScore& a, const DeviceScore& b) { return a.score < b.score; });

        return best->device;
    }

    std::chrono::microseconds CalculateDeviceLoad(VulkanDevice* device) {
        std::chrono::microseconds totalLoad(0);

        for (auto* node : GetNodesOnDevice(device)) {
            totalLoad += node->performanceStats.averageExecutionTime;
        }

        return totalLoad;
    }
};
```

#### Load Balancing Example

```cpp
// Scenario: 16 shadow maps, 4 GPUs
RenderGraph graph(primaryGPU, &registry);

for (int i = 0; i < 16; i++) {
    auto shadow = graph.AddNode("ShadowMapPass", "Shadow_" + std::to_string(i));

    // Compiler uses workload metrics to distribute:
    // - GPU 0: 4 shadows (least loaded)
    // - GPU 1: 4 shadows
    // - GPU 2: 4 shadows
    // - GPU 3: 4 shadows (evenly distributed)
}

graph.Compile();  // Automatically balances based on metrics
```

### Metrics Reporting

```cpp
struct WorkloadReport {
    NodeTypeId typeId;
    std::string typeName;

    // Space metrics
    size_t totalMemoryFootprint;
    size_t averageInstanceMemory;
    uint32_t instanceCount;

    // Time metrics
    std::chrono::microseconds totalExecutionTime;
    std::chrono::microseconds averageInstanceTime;
    std::chrono::microseconds maxInstanceTime;

    // Load percentage
    float cpuTimePercentage;      // % of frame time
    float gpuTimePercentage;      // % of GPU time
};

class RenderGraph {
    std::vector<WorkloadReport> GenerateWorkloadReport() const {
        std::vector<WorkloadReport> reports;

        for (const auto& [typeId, instances] : instancesByType) {
            WorkloadReport report;
            report.typeId = typeId;
            report.typeName = instances[0]->nodeType->typeName;
            report.instanceCount = instances.size();

            // Aggregate metrics
            for (const auto* instance : instances) {
                report.totalMemoryFootprint += instance->GetTotalMemoryFootprint();
                report.totalExecutionTime += instance->performanceStats.averageExecutionTime;
                report.maxInstanceTime = std::max(report.maxInstanceTime,
                                                 instance->performanceStats.maxExecutionTime);
            }

            report.averageInstanceMemory = report.totalMemoryFootprint / report.instanceCount;
            report.averageInstanceTime = report.totalExecutionTime / report.instanceCount;

            reports.push_back(report);
        }

        return reports;
    }

    void PrintWorkloadReport() const {
        auto reports = GenerateWorkloadReport();

        LOG_INFO("=== Render Graph Workload Report ===");
        LOG_INFO("{:<20} {:>10} {:>15} {:>15}", "Node Type", "Instances", "Memory (MB)", "Time (ms)");
        LOG_INFO("{:-<65}", "");

        for (const auto& report : reports) {
            LOG_INFO("{:<20} {:>10} {:>15.2f} {:>15.3f}",
                     report.typeName,
                     report.instanceCount,
                     report.totalMemoryFootprint / (1024.0 * 1024.0),
                     report.totalExecutionTime.count() / 1000.0);
        }
    }
};
```

**Example Output:**
```
=== Render Graph Workload Report ===
Node Type              Instances      Memory (MB)        Time (ms)
-----------------------------------------------------------------
ShadowMapPass                 16           224.00            8.320
GeometryPass                   1            45.50            2.150
BloomPass                      3            18.75            1.680
ToneMappingPass                1             8.00            0.240
```

---

## Pipeline Instancing During Compilation

During graph compilation, the system analyzes instances of the same type to optimize pipeline creation:

```cpp
struct PipelineInstanceGroup {
    NodeType* nodeType;                           // The type being instanced
    std::vector<NodeInstance*> instances;         // All instances of this type
    VkPipeline sharedPipeline;                    // Shared pipeline (if compatible)
    std::map<uint64_t, VkPipeline> variantPipelines;  // Variants for different configs
};

class GraphCompiler {
    void CompilePipelines() {
        // Group instances by type
        std::map<NodeTypeId, PipelineInstanceGroup> instanceGroups;

        for (auto& instance : allInstances) {
            instanceGroups[instance->nodeType->typeId].instances.push_back(instance);
        }

        // For each type, determine if instances can share a pipeline
        for (auto& [typeId, group] : instanceGroups) {
            if (CanSharePipeline(group.instances)) {
                // Create single pipeline for all instances
                group.sharedPipeline = CreatePipeline(group.instances[0]);

                // All instances use the same pipeline
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
    }

    bool CanSharePipeline(const std::vector<NodeInstance*>& instances) {
        // Check if all instances have compatible state
        // - Same shader variants
        // - Same render pass format
        // - Same vertex input layout
        // - Same dynamic state requirements
        return AllInstancesHaveCompatibleState(instances);
    }
};
```

**Example:** 10 shadow map instances:
- **Scenario A (Identical)**: All shadows use same resolution, format → 1 shared pipeline
- **Scenario B (Variants)**: 8 use 1024x1024, 2 use 2048x2048 → 2 pipeline variants
- **Scenario C (All Different)**: Each uses different config → 10 separate pipelines

---

## Node Type Categories

### Process Node Types
- Geometry rendering
- Post-processing effects
- Compute operations
- Texture generation

### Resource Node Types
- Image/Texture resources
- Buffer resources
- Constant values
- Render targets

### Device Communication Node Types
- Cross-device transfer
- Device synchronization
- Multi-GPU composition

---

## Resource Types

### Resource Classification

```cpp
enum class ResourceType {
    Variable,    // Constant input (uniforms, push constants)
    Product,     // Output from another node
    Transient,   // Temporary resource within frame
    Persistent   // Lives across multiple frames
};

struct Resource {
    std::string name;
    ResourceType type;

    union {
        // Variable (constant)
        struct {
            void* data;
            size_t size;
        } constant;

        // Product (from node)
        struct {
            RenderGraphNode* sourceNode;
            uint32_t outputIndex;
        } product;

        // Vulkan resource handles
        struct {
            VkImage image;
            VkBuffer buffer;
            VkDeviceMemory memory;
            VkImageView imageView;
        } vulkan;
    };
};
```

---

## Node Type Registry and Instancing

### Node Type Registry

The registry is a centralized database of all available node types:

```cpp
class NodeTypeRegistry {
public:
    // Register a new node type
    void RegisterNodeType(std::unique_ptr<NodeType> nodeType);

    // Query registered types
    NodeType* GetNodeType(NodeTypeId typeId);
    NodeType* GetNodeType(const std::string& typeName);

    // Get all registered types
    const std::vector<NodeType*>& GetAllTypes() const;

private:
    std::map<NodeTypeId, std::unique_ptr<NodeType>> typeRegistry;
    std::map<std::string, NodeTypeId> nameToIdMap;
};

// Example registration
void RegisterBuiltInNodeTypes(NodeTypeRegistry& registry) {
    // Shadow Map Type
    auto shadowMapType = std::make_unique<NodeType>();
    shadowMapType->typeId = NodeTypeId::ShadowMap;
    shadowMapType->typeName = "ShadowMapPass";
    shadowMapType->supportsInstancing = true;
    shadowMapType->maxInstances = 0;  // Unlimited
    shadowMapType->inputSchema = {
        {"geometry", ResourceType::GeometryBuffer},
        {"lightTransform", ResourceType::Matrix4x4}
    };
    shadowMapType->outputSchema = {
        {"depthMap", ResourceType::Texture2D}
    };
    registry.RegisterNodeType(std::move(shadowMapType));

    // Geometry Pass Type (single instance)
    auto geometryType = std::make_unique<NodeType>();
    geometryType->typeId = NodeTypeId::GeometryPass;
    geometryType->typeName = "GeometryPass";
    geometryType->supportsInstancing = false;
    geometryType->maxInstances = 1;
    // ... configure schema
    registry.RegisterNodeType(std::move(geometryType));
}
```

### Creating Node Instances

```cpp
class RenderGraph {
public:
    // Create instance from type name
    NodeHandle AddNode(const std::string& typeName, const std::string& instanceName);

    // Create instance from type ID
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName);

    // Create instance with explicit device
    NodeHandle AddNode(NodeTypeId typeId, const std::string& instanceName,
                      VulkanDevice* device);

    // Get instance count for a type
    uint32_t GetInstanceCount(NodeTypeId typeId) const;

    // Get all instances of a type
    std::vector<NodeInstance*> GetInstancesOfType(NodeTypeId typeId);

private:
    NodeTypeRegistry* typeRegistry;
    std::map<NodeTypeId, std::vector<NodeInstance*>> instancesByType;
};
```

**Example Usage:**
```cpp
RenderGraph graph(device, &registry);

// Create multiple shadow map instances
for (int i = 0; i < numLights; i++) {
    std::string instanceName = "ShadowMap_" + std::to_string(i);
    auto shadowMap = graph.AddNode(NodeTypeId::ShadowMap, instanceName);
    // Each instance is unique, but shares the ShadowMapPass type
}

// Later, during compilation:
uint32_t shadowMapCount = graph.GetInstanceCount(NodeTypeId::ShadowMap);
// shadowMapCount = numLights

// Compiler can optimize: "I have 10 instances of ShadowMap type,
// can they share a pipeline?"
```

**Use Cases:**
- Multiple shadow maps (one per light source) - **Same Type, Multiple Instances**
- Multiple blur passes with different radii - **Same Type, Different Parameters → Variants**
- Instanced geometry rendering with different transforms - **Same Type, Different Uniforms**

---

## Type-Instance Relationship

```
┌─────────────────────────────────────────────────────────────┐
│              Node Type Registry                              │
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │ ShadowMapPass    │  │ GeometryPass     │  ...           │
│  │ - typeId         │  │ - typeId         │                │
│  │ - inputSchema    │  │ - inputSchema    │                │
│  │ - outputSchema   │  │ - outputSchema   │                │
│  │ - instancing: ✓  │  │ - instancing: ✗  │                │
│  └──────────────────┘  └──────────────────┘                │
└────────┬─────────────────────┬──────────────────────────────┘
         │                     │
         │ instantiate         │ instantiate (once)
         ▼                     ▼
┌─────────────────────────────────────────────────────────────┐
│                  Render Graph Instances                      │
│                                                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ ShadowMap_0 │  │ ShadowMap_1 │  │ ShadowMap_2 │ ...     │
│  │ instanceId  │  │ instanceId  │  │ instanceId  │         │
│  │ type: ─────────┼─── type: ───┼──── type: ShadowMapPass  │
│  │ device: gpu0│  │ device: gpu1│  │ device: gpu2│         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
│                                                               │
│  ┌─────────────┐                                            │
│  │ MainScene   │                                            │
│  │ instanceId  │                                            │
│  │ type: GeometryPass                                      │
│  │ device: gpu0│                                            │
│  └─────────────┘                                            │
└─────────────────────────────────────────────────────────────┘
         │
         │ compilation
         ▼
┌─────────────────────────────────────────────────────────────┐
│           Pipeline Instance Grouping                         │
│                                                               │
│  ShadowMapPass:                                              │
│    sharedPipeline: VkPipeline (used by all 3 instances)     │
│    instances: [ShadowMap_0, ShadowMap_1, ShadowMap_2]       │
│                                                               │
│  GeometryPass:                                               │
│    sharedPipeline: VkPipeline (used by MainScene)           │
│    instances: [MainScene]                                    │
└─────────────────────────────────────────────────────────────┘
```

**Key Points:**
1. **NodeType** = Template/Definition (1 per process type)
2. **NodeInstance** = Concrete usage (N per graph, based on scene requirements)
3. **Pipeline Sharing** = Determined at compile time based on instance compatibility
4. **Each instance** has unique descriptor sets, command buffers, and parameters
5. **Instances can share** pipelines, pipeline layouts, and shader modules

---

**See also:**
- [Graph Compilation](02-graph-compilation.md) - How instances are compiled into execution pipelines
- [Multi-Device Support](03-multi-device.md) - Device affinity for node instances
- [Usage Examples](06-examples.md) - Practical examples of node types and instances
