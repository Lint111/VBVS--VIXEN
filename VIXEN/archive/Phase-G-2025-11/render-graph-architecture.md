# Render Graph Architecture - Design Document

## Project Overview

**Branch Purpose:** Transition from static resource management to a dynamic graph-based node system for Vulkan pipeline management.

**Current State:** The renderer requires manual updates across multiple areas to change rendering behavior. Resources are statically allocated and managed.

**Target State:** A flexible, graph-based system where rendering pipelines are dynamically compiled from reusable nodes with automatic resource management and caching.

---

## 1. System Architecture

### 1.1 Core Objectives

1. **Dynamic Pipeline Compilation**: Replace static resource allocation with runtime graph compilation
2. **Reusable Components**: Enable pipeline element reuse through intelligent caching
3. **Resource Optimization**: Reduce computation and memory load via automatic resource sharing
4. **Flexible Topology**: Support arbitrary rendering pipeline configurations
5. **Multi-Device Support**: First-class support for multi-GPU systems with automatic cross-device transfer and synchronization

### 1.2 High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Render Graph                         │
│  ┌───────────┐    ┌───────────┐    ┌───────────┐      │
│  │   Node    │───▶│   Node    │───▶│   Node    │      │
│  │ (Source)  │    │ (Process) │    │  (Sink)   │      │
│  └───────────┘    └───────────┘    └───────────┘      │
│                                                          │
│  ┌────────────────────────────────────────────┐        │
│  │         Resource Cache Manager              │        │
│  │  - Pipeline cache                           │        │
│  │  - Descriptor set cache                     │        │
│  │  - Shader module cache                      │        │
│  └────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Node System Design

### 2.1 Node Type vs Node Instance

The system distinguishes between **Node Types** (reusable process definitions) and **Node Instances** (specific instantiations in the graph).

#### 2.1.1 Node Type (Process Definition)

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

    // Factory
    std::function<NodeInstance*()> CreateInstance;
};
```

**Examples of Node Types:**
- `ShadowMapPass` - Can be instanced once per light
- `GeometryPass` - Typically single instance
- `BlurPass` - Can be instanced for multi-pass effects
- `ComputeShaderPass` - Can be instanced for parallel work

#### 2.1.2 Node Instance (Graph Node)

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

    // Caching
    uint64_t cacheKey;                    // Hash for this instance's output
    bool cacheable;                       // Whether output can be cached
};
```

#### 2.1.3 Pipeline Instancing During Compilation

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

#### 2.1.4 Node Type Categories

**Process Node Types:**
- Geometry rendering
- Post-processing effects
- Compute operations
- Texture generation

**Resource Node Types:**
- Image/Texture resources
- Buffer resources
- Constant values
- Render targets

**Device Communication Node Types:**
- Cross-device transfer
- Device synchronization
- Multi-GPU composition

### 2.2 Resource Types

#### 2.2.1 Resource Classification

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

### 2.3 Node Type Registration & Instancing

#### 2.3.1 Node Type Registry

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

#### 2.3.2 Creating Node Instances

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

#### 2.3.3 Type-Instance Relationship

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

## 3. Graph Compilation & Execution

### 3.1 Graph Construction

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

### 3.2 Compilation Phases

**Phase 1: Device Affinity Propagation**
- Assign devices to nodes based on input dependencies
- Detect device boundaries (where cross-device transfer is needed)
- Insert transfer nodes automatically at device boundaries
- Validate device capabilities for each node type

**Phase 2: Dependency Analysis**
- Build directed acyclic graph (DAG)
- Topological sort for execution order
- Detect cycles (error)
- Group nodes by device for parallel execution

**Phase 3: Resource Allocation**
- Analyze resource lifetimes per device
- Allocate physical resources on appropriate devices
- Alias transient resources where possible
- Handle cross-device resource copies

**Phase 4: Pipeline Generation**
- Generate Vulkan pipelines for each node on its target device
- Check cache for existing pipelines
- Create descriptor sets per device

**Phase 5: Command Buffer Recording**
- Generate optimal command buffer structure per device
- Insert barriers automatically (including device-to-device)
- Batch similar operations
- Coordinate multi-device synchronization

### 3.3 Execution Model

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (auto& node : executionOrder) {
        // Check cache
        if (node->cacheable && cache.Has(node->cacheKey)) {
            UseCache dResource(node, cache);
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

---

## 4. Multi-Device & Cross-Device Communication

### 4.1 Device Affinity Rules

**Automatic Device Assignment:**
```cpp
// Rule 1: Explicit device assignment takes precedence
auto node = graph.AddNode(NodeType::GeometryPass, "Scene", specificDevice);

// Rule 2: Inherit device from input nodes
// If nodeB reads from nodeA's output, nodeB inherits device from nodeA
graph.ConnectNodes(nodeA, 0, nodeB, 0);  // nodeB -> device = nodeA->device

// Rule 3: Multiple inputs - conflict resolution
// If inputs come from different devices, node must explicitly specify device
// OR a transfer node is automatically inserted
```

**Device Inheritance Example:**
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

### 5.2 Cross-Device Transfer Nodes

When data flows from a node on Device A to a node on Device B, the compiler automatically inserts a transfer node:

```cpp
class DeviceTransferNode : public RenderGraphNode {
public:
    DeviceTransferNode(VulkanDevice* srcDevice, VulkanDevice* dstDevice)
        : sourceDevice(srcDevice)
        , destinationDevice(dstDevice) {}

    void Execute(VkCommandBuffer cmd) override {
        // 1. Create staging buffer/image on source device
        // 2. Copy resource to staging
        // 3. Transfer ownership to destination device
        // 4. Copy from staging to destination resource
        // 5. Signal semaphore for cross-device synchronization
    }

private:
    VulkanDevice* sourceDevice;
    VulkanDevice* destinationDevice;
    VkSemaphore transferCompleteSemaphore;  // Cross-device sync
};
```

### 5.3 Multi-Device Execution

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

### 4.4 Device Capabilities Validation

```cpp
struct DeviceCapabilities {
    bool supportsGeometry;
    bool supportsCompute;
    bool supportsRayTracing;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    // ... more capabilities
};

class GraphCompiler {
    void ValidateNodeDeviceCompatibility(RenderGraphNode* node) {
        auto& caps = node->device->GetCapabilities();

        switch (node->processTypeId) {
            case NodeTypeId::RayTracingPass:
                if (!caps.supportsRayTracing) {
                    throw CompilationError(
                        "Node '" + node->name + "' requires ray tracing, "
                        "but assigned device doesn't support it"
                    );
                }
                break;
            // ... validate other node types
        }
    }
};
```

### 4.5 Cross-Device Synchronization

**Semaphore-Based Sync:**
```cpp
// Device 0 renders shadow map
VkSemaphore shadowComplete;
vkCreateSemaphore(device0, &semaphoreInfo, nullptr, &shadowComplete);

// Queue on device 0
vkQueueSubmit(device0Queue, &submitInfo, VK_NULL_HANDLE);
vkQueueSubmit(device0Queue, ..., shadowComplete);  // Signal

// Queue on device 1 waits
VkSubmitInfo waitSubmitInfo = {};
waitSubmitInfo.waitSemaphoreCount = 1;
waitSubmitInfo.pWaitSemaphores = &shadowComplete;  // Wait
vkQueueSubmit(device1Queue, &waitSubmitInfo, VK_NULL_HANDLE);
```

**Timeline Semaphores (Vulkan 1.2+):**
```cpp
// More efficient for complex dependencies
VkSemaphoreTypeCreateInfo timelineInfo = {};
timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
timelineInfo.initialValue = 0;

// Device 0 signals value 1
vkQueueSubmit(device0Queue, ..., timelineSemaphore, signalValue=1);

// Device 1 waits for value 1
vkQueueSubmit(device1Queue, ..., timelineSemaphore, waitValue=1);
```

### 4.6 Resource Ownership Transfer

For optimal performance, resources can transfer ownership between devices:

```cpp
struct ResourceOwnership {
    VulkanDevice* currentOwner;
    VkAccessFlags currentAccessMask;
    VkPipelineStageFlags currentStageMask;

    void TransferTo(VulkanDevice* newOwner) {
        // Release barrier on current owner
        VkImageMemoryBarrier releaseBarrier = {};
        releaseBarrier.srcQueueFamilyIndex = currentOwner->queueFamilyIndex;
        releaseBarrier.dstQueueFamilyIndex = newOwner->queueFamilyIndex;
        // ... configure barrier
        vkCmdPipelineBarrier(currentOwnerCmd, ..., &releaseBarrier);

        // Acquire barrier on new owner
        VkImageMemoryBarrier acquireBarrier = {};
        acquireBarrier.srcQueueFamilyIndex = currentOwner->queueFamilyIndex;
        acquireBarrier.dstQueueFamilyIndex = newOwner->queueFamilyIndex;
        // ... configure barrier
        vkCmdPipelineBarrier(newOwnerCmd, ..., &acquireBarrier);

        currentOwner = newOwner;
    }
};
```

---

## 5. Caching System

### 5.1 Cache Strategy

The cache system operates at multiple levels to maximize reuse:

#### 5.1.1 Pipeline Cache (Vulkan VkPipelineCache)
- Shader modules
- Pipeline state objects
- Persistent across application runs

#### 5.1.2 Descriptor Set Cache
- Reuse descriptor sets with identical layouts
- Track resource bindings
- Invalidate on resource changes

#### 5.1.3 Resource Cache
- Rendered outputs from previous frames
- Static geometry buffers
- Pre-processed textures

### 5.2 Cache Key Generation

```cpp
class CacheKeyGenerator {
    uint64_t GenerateKey(const RenderGraphNode* node) {
        uint64_t key = HashNodeType(node->processTypeId);

        // Hash inputs
        for (const auto& input : node->inputs) {
            key = CombineHash(key, HashResource(input));
        }

        // Hash parameters
        key = CombineHash(key, HashParameters(node->parameters));

        return key;
    }
};
```

### 5.3 Cache Invalidation

**Triggers:**
- Input resource modification
- Node parameter change
- Explicit invalidation request
- LRU eviction (memory pressure)

---

## 5. Migration Strategy

### 5.1 Phase 1: Foundation (Weeks 1-2)
- [ ] Implement base `RenderGraphNode` class
- [ ] Create `Resource` abstraction
- [ ] Build `RenderGraph` container
- [ ] Implement basic graph traversal

### 5.2 Phase 2: Node Types (Weeks 3-4)
- [ ] Port existing rendering operations to nodes:
  - [ ] Geometry pass node
  - [ ] Shadow map node
  - [ ] Texture sampling node
  - [ ] Post-process nodes
- [ ] Implement `NodeFactory` for instancing

### 5.3 Phase 3: Resource Management (Weeks 5-6)
- [ ] Implement `ResourceAllocator`
- [ ] Add transient resource aliasing
- [ ] Build lifetime analysis
- [ ] Automatic barrier insertion

### 5.4 Phase 4: Caching (Week 7)
- [ ] Pipeline cache integration
- [ ] Descriptor set caching
- [ ] Resource output caching
- [ ] Cache key generation

### 5.5 Phase 5: Optimization (Week 8)
- [ ] Multi-threading support
- [ ] Command buffer optimization
- [ ] Memory pooling
- [ ] Performance profiling

### 5.6 Phase 6: Integration (Week 9-10)
- [ ] Replace current renderer implementation
- [ ] Migration tools for existing scenes
- [ ] Validation and testing
- [ ] Documentation

---

## 6. Example Usage

### 6.1 Simple Forward Rendering

```cpp
// Create graph
RenderGraph graph;

// Add nodes
auto sceneGeometry = graph.AddNode(NodeType::GeometryPass, "SceneGeometry");
auto mainCamera = graph.AddNode(NodeType::Camera, "MainCamera");
auto colorTarget = graph.AddNode(NodeType::RenderTarget, "ColorBuffer");

// Configure nodes
sceneGeometry.SetInput("camera", mainCamera.GetOutput("viewProjection"));
sceneGeometry.SetInput("target", colorTarget.GetOutput("image"));

// Compile and execute
graph.Compile();
graph.Execute(commandBuffer);
```

### 6.2 Shadow Mapping with Multiple Lights

```cpp
RenderGraph graph;

// Scene data
auto sceneGeometry = graph.AddNode(NodeType::GeometryPass, "Scene");

// Create shadow map for each light
std::vector<NodeHandle> shadowMaps;
for (int i = 0; i < lightCount; i++) {
    auto light = graph.AddNode(NodeType::PointLight, "Light_" + std::to_string(i));
    auto shadowMap = graph.AddNode(NodeType::ShadowMap, "Shadow_" + std::to_string(i));

    // Connect
    shadowMap.SetInput("geometry", sceneGeometry.GetOutput("vertices"));
    shadowMap.SetInput("lightTransform", light.GetOutput("transform"));

    shadowMaps.push_back(shadowMap);
}

// Final composition
auto compositor = graph.AddNode(NodeType::Compositor, "FinalImage");
compositor.SetInput("geometry", sceneGeometry.GetOutput("color"));
for (size_t i = 0; i < shadowMaps.size(); i++) {
    compositor.SetInput("shadow_" + std::to_string(i), shadowMaps[i].GetOutput("depthMap"));
}

graph.Compile();
graph.Execute(commandBuffer);
```

### 6.3 Multi-GPU Rendering

```cpp
// Setup: Two GPUs available
VulkanDevice* gpu0 = deviceManager.GetDevice(0);  // High-end GPU
VulkanDevice* gpu1 = deviceManager.GetDevice(1);  // Secondary GPU

// Create graph on primary GPU
RenderGraph graph(gpu0);

// GPU 0: Render main scene geometry
auto mainCamera = graph.AddNode(NodeType::Camera, "MainCamera");
auto sceneGeometry = graph.AddNode(NodeType::GeometryPass, "MainScene");
// sceneGeometry implicitly uses gpu0 (inherited from graph)

// GPU 1: Render expensive post-processing effects on secondary GPU
auto postProcess = graph.AddNode(NodeType::PostProcess, "DOF", gpu1);
graph.ConnectNodes(sceneGeometry, 0, postProcess, 0);
// Transfer node automatically inserted: gpu0 -> gpu1

// GPU 0: Final composition back on main GPU
auto compositor = graph.AddNode(NodeType::Compositor, "FinalImage", gpu0);
graph.ConnectNodes(postProcess, 0, compositor, 0);
// Transfer node automatically inserted: gpu1 -> gpu0

// Compile detects cross-device transfers and inserts sync
graph.Compile();

// Execute will run sceneGeometry on gpu0 in parallel with
// any gpu1-assigned nodes that have satisfied dependencies
graph.Execute(commandBuffer);

// Result: Main rendering on gpu0, heavy post-processing offloaded to gpu1
```

### 6.4 Explicit Device Assignment for Load Balancing

```cpp
RenderGraph graph(primaryGPU);

// Distribute shadow maps across multiple GPUs
std::vector<VulkanDevice*> gpus = {gpu0, gpu1, gpu2};

for (int i = 0; i < numLights; i++) {
    // Round-robin assignment across GPUs
    VulkanDevice* targetGPU = gpus[i % gpus.size()];

    auto light = graph.AddNode(NodeType::PointLight, "Light_" + std::to_string(i));
    auto shadowMap = graph.AddNode(NodeType::ShadowMap, "Shadow_" + std::to_string(i), targetGPU);

    shadowMap.SetInput("lightTransform", light.GetOutput("transform"));
}

// All shadow maps rendered in parallel across 3 GPUs
graph.Compile();
graph.Execute(commandBuffer);
```

---

## 7. Implementation Details

### 7.1 Directory Structure

```
include/
  RenderGraph/
    RenderGraph.h           // Main graph class
    RenderGraphNode.h       // Node base class
    Resource.h              // Resource types
    NodeFactory.h           // Node creation
    CacheManager.h          // Caching system
    GraphCompiler.h         // Compilation logic
    ResourceAllocator.h     // Resource management

  RenderGraph/Nodes/
    GeometryPassNode.h
    ShadowMapNode.h
    PostProcessNode.h
    ComputeNode.h
    // ... more node types

source/
  RenderGraph/
    RenderGraph.cpp
    RenderGraphNode.cpp
    Resource.cpp
    NodeFactory.cpp
    CacheManager.cpp
    GraphCompiler.cpp
    ResourceAllocator.cpp

  RenderGraph/Nodes/
    // Implementation for each node type
```

### 7.2 Key Classes

#### 7.2.1 RenderGraph
- Container for all nodes
- Orchestrates compilation and execution
- Manages global resources

#### 7.2.2 GraphCompiler
- Analyzes dependencies
- Generates execution order
- Optimizes resource allocation
- Inserts synchronization barriers

#### 7.2.3 ResourceAllocator
- Tracks resource lifetimes
- Allocates Vulkan resources
- Implements aliasing for transient resources
- Manages memory pools

#### 7.2.4 CacheManager
- Multi-level caching strategy
- Cache key generation
- LRU eviction policy
- Persistent cache serialization

---

## 8. Performance Considerations

### 8.1 Memory Management
- Pool allocators for nodes and resources
- Transient resource aliasing (30-50% memory reduction expected)
- Descriptor set pooling

### 8.2 Compilation Cost
- Cache compiled graphs across frames
- Incremental recompilation on changes
- Parallel compilation for independent subgraphs

### 8.3 Runtime Overhead
- Minimize virtual calls in hot paths
- Batch API calls
- Use command buffer inheritance

---

## 9. Testing Strategy

### 9.1 Unit Tests
- Node creation and connection
- Resource lifetime analysis
- Cache key generation
- Dependency resolution

### 9.2 Integration Tests
- Port existing renderer tests
- Shadow mapping correctness
- Post-processing accuracy
- Resource leak detection

### 9.3 Performance Tests
- Frame time comparison (target: <5% overhead)
- Memory usage profiling
- Cache hit rate monitoring

---

## 10. Future Extensions

### 10.1 Short Term
- GPU-driven rendering nodes
- Async compute integration
- Multi-viewport support

### 10.2 Long Term
- Visual graph editor
- Hot-reload for graph definitions
- Machine learning-based optimization
- Ray tracing node types

---

## 11. References & Resources

### 11.1 Academic Papers
- "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017)
- "Render Graphs and Vulkan — a deep dive" (Khronos)

### 11.2 Existing Implementations
- Frostbite FrameGraph
- Unity Scriptable Render Pipeline
- Unreal Engine 5 Render Dependency Graph

### 11.3 Vulkan Specifications
- VkPipelineCache documentation
- Synchronization and barriers
- Resource aliasing

---

## Appendix A: Data Structures

### Node Type Registry

```cpp
enum class NodeTypeId : uint32_t {
    GeometryPass = 0x0001,
    ShadowMap    = 0x0002,
    PostProcess  = 0x0003,
    Compute      = 0x0004,
    // ... more types
};

struct NodeTypeInfo {
    NodeTypeId id;
    std::string name;
    std::vector<ResourceDescriptor> inputSchema;
    std::vector<ResourceDescriptor> outputSchema;
    std::function<RenderGraphNode*()> factory;
};
```

### Cache Entry

```cpp
struct CacheEntry {
    uint64_t key;
    std::chrono::steady_clock::time_point lastAccess;
    size_t memorySize;
    std::variant<VkPipeline, VkDescriptorSet, VkImage> resource;
    uint32_t accessCount;
};
```

---

**Document Version:** 1.0
**Date:** 2025-10-18
**Status:** Draft - Ready for Review
