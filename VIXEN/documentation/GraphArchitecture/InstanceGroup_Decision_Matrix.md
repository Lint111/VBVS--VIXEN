/**
 * @file InstanceGroup_Decision_Matrix.md
 * @brief Quick reference: When to use manual instances vs InstanceGroups
 */

# Instance Creation Decision Matrix

## When to Use Manual Multi-Instance

**Pattern:**
```cpp
NodeHandle wood = graph->AddNode("TextureLoader", "wood_diffuse");
NodeHandle metal = graph->AddNode("TextureLoader", "metal_normal");
wood->SetParameter("file_path", "wood.png");
metal->SetParameter("file_path", "metal.png");
```

**Use When:**
- ✅ Each instance represents **DIFFERENT semantic entity**
- ✅ Different configurations per instance (different paths, settings, purposes)
- ✅ Different graph connections per instance (wood→material1, metal→material2)
- ✅ Instance count known at compile time
- ✅ Instances have different lifetimes (some loaded/unloaded independently)

**Examples:**
- Different material textures (wood diffuse, wood normal, metal diffuse, metal normal)
- Multiple cameras (main camera, shadow camera, reflection probe)
- Different light types (directional light, point light, spot light)
- UI elements (button texture, icon texture, background texture)
- Specific meshes (character mesh, weapon mesh, environment mesh)

**Current Status:** ✅ Fully implemented and tested


---

## When to Use InstanceGroup (Future)

**Pattern:**
```cpp
InstanceGroupConfig config;
config.groupName = "StreamingLoaders";
config.scalingPolicy = InstanceScalingPolicy::WorkloadBatching;
config.preferredBatchSize = 50;

auto group = graph->CreateInstanceGroup(config);
// Spawns N instances based on runtime workload
```

**Use When:**
- ✅ Instances represent **SAME semantic entity** (parallel workers)
- ✅ All instances share same configuration (except workload data)
- ✅ Same graph connections for all instances
- ✅ Instance count determined at runtime (based on workload, device, budget)
- ✅ Workload can be distributed/partitioned across instances
- ✅ All instances have same lifetime (spawn/destroy together)

**Examples:**
- Texture streaming (200 textures → 6 parallel loaders)
- Mesh processing (1000 meshes → 8 parallel processors)
- Shadow maps for many lights (100 point lights → 16 parallel shadow renderers)
- Particle systems (10000 particles → 4 parallel update passes)
- Async compute jobs (culling, skinning, physics)

**Current Status:** ⚠️ Design complete, implementation pending


---

## Comparison Table

| Aspect                    | Manual Multi-Instance          | InstanceGroup                  |
|---------------------------|--------------------------------|--------------------------------|
| **Semantic Meaning**      | Different entities             | Parallel workers               |
| **Creation**              | Explicit `AddNode()` per inst  | Single `CreateInstanceGroup()` |
| **Instance Count**        | User-controlled (static)       | Graph-calculated (dynamic)     |
| **Configuration**         | Different per instance         | Shared across group            |
| **Workload**              | Each has unique task           | Shared task queue/batching     |
| **Connections**           | Manually wired per instance    | Template applied to all        |
| **Recompilation Behavior**| Instances persist              | May respawn with different N   |
| **Memory**                | Independent resource ownership | Coordinated resource pooling   |
| **Use Case**              | "3 specific textures"          | "Process N textures in parallel"|

---

## Decision Flowchart

```
Do instances represent DIFFERENT semantic entities?
│
├─ YES → Use Manual Multi-Instance
│         Examples: wood_diffuse, metal_normal, stone_roughness
│         Each has unique meaning/purpose
│
└─ NO → Instances are PARALLEL WORKERS for same task?
          │
          ├─ YES → Use InstanceGroup
          │         Examples: 6 texture loaders processing 200 textures
          │         All do same thing, just on different data
          │
          └─ NO → You probably need just ONE instance
                    Example: Single shader library, single device node
```

---

## Hybrid Scenarios

Some scenarios need BOTH:

### Example: Multi-Material with Streaming

```cpp
// Manual instances: Different material types
NodeHandle woodMaterial = graph->AddNode("Material", "wood_material");
NodeHandle metalMaterial = graph->AddNode("Material", "metal_material");

// InstanceGroup: Stream textures for wood material
InstanceGroupConfig woodTextureConfig;
woodTextureConfig.groupName = "WoodTextureLoaders";
auto woodLoaders = graph->CreateInstanceGroup(woodTextureConfig);
// Spawns N loaders for wood textures (diffuse, normal, roughness, AO, etc.)

// InstanceGroup: Stream textures for metal material  
InstanceGroupConfig metalTextureConfig;
metalTextureConfig.groupName = "MetalTextureLoaders";
auto metalLoaders = graph->CreateInstanceGroup(metalTextureConfig);
// Spawns M loaders for metal textures

// Result:
// - 2 material instances (manual - different semantic entities)
// - N+M texture loader instances (groups - parallel workers)
```

---

## Migration Path

### Phase 1 (Current):
All manual instances:
```cpp
wood1, wood2, wood3, metal1, metal2, stone1, ...  // 10+ explicit nodes
```

### Phase 2 (Near Future - Fixed Groups):
Manual instances for semantic entities + Fixed groups for known sets:
```cpp
// Semantic entities (manual)
woodMaterial, metalMaterial

// Known texture sets (fixed groups)
woodTextureGroup (3 textures: diffuse, normal, roughness)
metalTextureGroup (4 textures: diffuse, normal, metallic, roughness)
```

### Phase 3 (Future - Dynamic Groups):
Manual instances + Dynamic groups with slot-based data:
```cpp
// Semantic entities (manual)
woodMaterial, metalMaterial

// Dynamic streaming (workload-driven groups)
streamingTextureGroup (N instances based on pending load count)
streamingMeshGroup (M instances based on visibility culling results)
```

---

## Current Recommendation (Chapter Focus)

**Use Manual Multi-Instance ONLY**

Why:
- InstanceGroups are future optimization
- Manual instances fully implemented and tested
- Simpler to understand and debug
- Sufficient for current rendering pipeline

Example:
```cpp
// Create specific nodes for each semantic entity
NodeHandle windowNode = graph->AddNode("Window", "main_window");
NodeHandle deviceNode = graph->AddNode("Device", "main_device");
NodeHandle swapChainNode = graph->AddNode("SwapChain", "main_swapchain");
NodeHandle depthBufferNode = graph->AddNode("DepthBuffer", "depth_buffer");
NodeHandle vertexBufferNode = graph->AddNode("VertexBuffer", "triangle_vb");
NodeHandle renderPassNode = graph->AddNode("RenderPass", "main_pass");
NodeHandle framebufferNode = graph->AddNode("Framebuffer", "main_fb");
NodeHandle shaderLibNode = graph->AddNode("ShaderLibrary", "shader_lib");
NodeHandle descriptorSetNode = graph->AddNode("DescriptorSet", "main_descriptors");
NodeHandle pipelineNode = graph->AddNode("GraphicsPipeline", "triangle_pipeline");
NodeHandle commandPoolNode = graph->AddNode("CommandPool", "main_cmd_pool");
NodeHandle geometryRenderNode = graph->AddNode("GeometryRender", "triangle_render");
NodeHandle presentNode = graph->AddNode("Present", "present");

// Wire with TypedConnection
ConnectionBatch batch(graph);
batch.Connect(windowNode, WindowNodeConfig::SURFACE, swapChainNode, SwapChainNodeConfig::SURFACE)
     .Connect(deviceNode, DeviceNodeConfig::DEVICE, swapChainNode, SwapChainNodeConfig::DEVICE)
     // ... etc
     .RegisterAll();
```

InstanceGroups become relevant when optimizing for:
- Large-scale streaming (100s of textures)
- Dynamic workloads (variable object counts)
- Multi-threaded execution
- Advanced scheduling
