# Usage Examples

## Overview

This document provides practical examples of using the Render Graph system, from simple forward rendering to complex multi-GPU scenarios.

---

## Example 1: Simple Forward Rendering

### Goal
Basic scene rendering with a single camera and geometry pass.

### Code

```cpp
#include "RenderGraph/RenderGraph.h"

// Setup registry (once per application)
NodeTypeRegistry registry;
RegisterBuiltInNodeTypes(registry);

// Create graph
RenderGraph graph(device, &registry);

// Add nodes
auto sceneGeometry = graph.AddNode("GeometryPass", "SceneGeometry");
auto mainCamera = graph.AddNode("Camera", "MainCamera");
auto colorTarget = graph.AddNode("RenderTarget", "ColorBuffer");

// Configure connections
graph.ConnectNodes(mainCamera, 0, sceneGeometry, 0);      // Camera -> Geometry
graph.ConnectNodes(colorTarget, 0, sceneGeometry, 1);     // RenderTarget -> Geometry

// Compile and execute
graph.Compile();
graph.Execute(commandBuffer);
```

### Graph Visualization

```
┌────────────┐
│ MainCamera │
└─────┬──────┘
      │
      ▼
┌──────────────────┐     ┌──────────────┐
│ SceneGeometry    │────▶│ ColorBuffer  │
└──────────────────┘     └──────────────┘
```

---

## Example 2: Shadow Mapping with Multiple Lights

### Goal
Render scene with dynamic shadows from multiple light sources.

### Code

```cpp
RenderGraph graph(device, &registry);

// Scene data
auto sceneGeometry = graph.AddNode("GeometryPass", "Scene");

// Create shadow map for each light
std::vector<NodeHandle> shadowMaps;
const int lightCount = 4;

for (int i = 0; i < lightCount; i++) {
    // Create light node
    auto light = graph.AddNode("PointLight", "Light_" + std::to_string(i));

    // Create shadow map node
    auto shadowMap = graph.AddNode("ShadowMapPass", "Shadow_" + std::to_string(i));

    // Connect geometry and light to shadow map
    graph.ConnectNodes(sceneGeometry, 0, shadowMap, 0);   // Geometry data
    graph.ConnectNodes(light, 0, shadowMap, 1);           // Light transform

    shadowMaps.push_back(shadowMap);
}

// Final composition
auto compositor = graph.AddNode("Compositor", "FinalImage");
graph.ConnectNodes(sceneGeometry, 1, compositor, 0);      // Color output

// Connect all shadow maps to compositor
for (size_t i = 0; i < shadowMaps.size(); i++) {
    graph.ConnectNodes(shadowMaps[i], 0, compositor, i + 1);
}

// Compile and execute
graph.Compile();

// At runtime
graph.Execute(commandBuffer);
```

### Graph Visualization

```
                    ┌────────────┐
                    │   Light_0  │
                    └─────┬──────┘
                          │
┌────────────┐      ┌─────▼──────┐
│   Scene    │─────▶│  Shadow_0  │
└─────┬──────┘      └─────┬──────┘
      │                   │
      │             ┌─────▼──────┐
      ├────────────▶│  Shadow_1  │
      │             └─────┬──────┘
      │                   │
      │             ┌─────▼──────┐
      ├────────────▶│  Shadow_2  │
      │             └─────┬──────┘
      │                   │
      │             ┌─────▼──────┐
      ├────────────▶│  Shadow_3  │
      │             └─────┬──────┘
      │                   │
      │                   │
      ▼                   ▼
┌─────────────────────────────────┐
│        Compositor               │
│  (Combines color + 4 shadows)   │
└─────────────────────────────────┘
```

**Benefits:**
- 4 `ShadowMapPass` instances share a single pipeline (if compatible)
- Automatic resource allocation for all shadow maps
- Parallel execution where dependencies allow

---

## Example 3: Post-Processing Chain

### Goal
Apply multiple post-processing effects: bloom, depth of field, tone mapping.

### Code

```cpp
RenderGraph graph(device, &registry);

// Main scene rendering
auto scene = graph.AddNode("GeometryPass", "MainScene");

// Post-processing chain
auto bloom = graph.AddNode("BloomPass", "Bloom");
graph.ConnectNodes(scene, 0, bloom, 0);

auto dof = graph.AddNode("DepthOfFieldPass", "DOF");
graph.ConnectNodes(bloom, 0, dof, 0);
graph.ConnectNodes(scene, 1, dof, 1);  // Depth buffer

auto toneMap = graph.AddNode("ToneMappingPass", "ToneMap");
graph.ConnectNodes(dof, 0, toneMap, 0);

auto finalOutput = graph.AddNode("OutputPass", "Final");
graph.ConnectNodes(toneMap, 0, finalOutput, 0);

// Compile and execute
graph.Compile();
graph.Execute(commandBuffer);
```

### Graph Visualization

```
┌────────────┐
│ MainScene  │
└──┬───────┬─┘
   │       │
   │       │(depth)
   ▼       │
┌──────┐   │
│ Bloom│   │
└───┬──┘   │
    │      │
    ▼      │
┌───────┐  │
│  DOF  │◀─┘
└───┬───┘
    │
    ▼
┌──────────┐
│ ToneMap  │
└────┬─────┘
     │
     ▼
┌──────────┐
│  Final   │
└──────────┘
```

---

## Example 4: Multi-GPU Rendering

### Goal
Distribute rendering work across multiple GPUs for maximum performance.

### Code

```cpp
// Setup: Two GPUs available
VulkanDevice* gpu0 = deviceManager.GetDevice(0);  // High-end GPU
VulkanDevice* gpu1 = deviceManager.GetDevice(1);  // Secondary GPU

// Create graph on primary GPU
RenderGraph graph(gpu0, &registry);

// GPU 0: Render main scene geometry
auto mainCamera = graph.AddNode("Camera", "MainCamera");
auto sceneGeometry = graph.AddNode("GeometryPass", "MainScene");
// sceneGeometry implicitly uses gpu0 (inherited from graph)

graph.ConnectNodes(mainCamera, 0, sceneGeometry, 0);

// GPU 1: Render expensive post-processing effects on secondary GPU
auto postProcess = graph.AddNode("PostProcess", "DOF", gpu1);
graph.ConnectNodes(sceneGeometry, 0, postProcess, 0);
// Transfer node automatically inserted: gpu0 -> gpu1

// GPU 0: Final composition back on main GPU
auto compositor = graph.AddNode("Compositor", "FinalImage", gpu0);
graph.ConnectNodes(postProcess, 0, compositor, 0);
// Transfer node automatically inserted: gpu1 -> gpu0

// Compile detects cross-device transfers and inserts sync
graph.Compile();

// Execute will run sceneGeometry on gpu0 in parallel with
// any gpu1-assigned nodes that have satisfied dependencies
graph.Execute(commandBuffer);

// Result: Main rendering on gpu0, heavy post-processing offloaded to gpu1
```

### Graph Visualization

```
GPU 0:  ┌────────┐     ┌────────────┐
        │ Camera │────▶│   Scene    │
        └────────┘     └─────┬──────┘
                             │
                       ┌─────▼──────────┐
                       │  Transfer      │  (auto-inserted)
                       │  (gpu0→gpu1)   │
                       └─────┬──────────┘
                             │
GPU 1:                 ┌─────▼──────┐
                       │    DOF     │
                       └─────┬──────┘
                             │
                       ┌─────▼──────────┐
                       │  Transfer      │  (auto-inserted)
                       │  (gpu1→gpu0)   │
                       └─────┬──────────┘
                             │
GPU 0:                 ┌─────▼──────┐
                       │ Compositor │
                       └────────────┘
```

**Execution Timeline:**
```
GPU 0: [Scene]─────[Signal]──────────────────[Wait]───[Compositor]
                       │                        ▲
                       └──────────────┬─────────┘
                                      │
GPU 1: ─────────────[Wait]───[DOF]───[Signal]────────────────────
```

---

## Example 5: Explicit Device Assignment for Load Balancing

### Goal
Distribute shadow map rendering across 4 GPUs for parallel execution.

### Code

```cpp
RenderGraph graph(primaryGPU, &registry);

// Scene geometry (primary GPU)
auto scene = graph.AddNode("GeometryPass", "MainScene");

// Distribute shadow maps across multiple GPUs
std::vector<VulkanDevice*> gpus = {gpu0, gpu1, gpu2, gpu3};
std::vector<NodeHandle> shadowMaps;

for (int i = 0; i < 16; i++) {
    // Round-robin assignment across GPUs
    VulkanDevice* targetGPU = gpus[i % gpus.size()];

    auto light = graph.AddNode("PointLight", "Light_" + std::to_string(i));
    auto shadowMap = graph.AddNode("ShadowMapPass", "Shadow_" + std::to_string(i), targetGPU);

    graph.ConnectNodes(scene, 0, shadowMap, 0);
    graph.ConnectNodes(light, 0, shadowMap, 1);

    shadowMaps.push_back(shadowMap);
}

// Compositor (primary GPU) gathers results
auto compositor = graph.AddNode("Compositor", "Final");
graph.ConnectNodes(scene, 1, compositor, 0);  // Color

for (size_t i = 0; i < shadowMaps.size(); i++) {
    graph.ConnectNodes(shadowMaps[i], 0, compositor, i + 1);
}

// All shadow maps rendered in parallel across 4 GPUs
graph.Compile();
graph.Execute(commandBuffer);
```

### Distribution

```
GPU 0: Shadow_0, Shadow_4, Shadow_8,  Shadow_12  (4 shadows)
GPU 1: Shadow_1, Shadow_5, Shadow_9,  Shadow_13  (4 shadows)
GPU 2: Shadow_2, Shadow_6, Shadow_10, Shadow_14  (4 shadows)
GPU 3: Shadow_3, Shadow_7, Shadow_11, Shadow_15  (4 shadows)

All execute in parallel, synchronized at compositor
```

**Performance Gain:** ~4x speedup for shadow rendering (ideal case)

---

## Example 6: Dynamic Graph Modification

### Goal
Add/remove nodes at runtime based on scene changes.

### Code

```cpp
class DynamicShadowSystem {
public:
    DynamicShadowSystem(RenderGraph* graph) : graph(graph) {}

    void AddLight(const Light& light) {
        std::string lightName = "Light_" + std::to_string(nextLightId);
        std::string shadowName = "Shadow_" + std::to_string(nextLightId);

        auto lightNode = graph->AddNode("PointLight", lightName);
        auto shadowNode = graph->AddNode("ShadowMapPass", shadowName);

        graph->ConnectNodes(sceneNode, 0, shadowNode, 0);
        graph->ConnectNodes(lightNode, 0, shadowNode, 1);
        graph->ConnectNodes(shadowNode, 0, compositorNode, nextLightId + 1);

        shadowNodes[nextLightId] = shadowNode;
        nextLightId++;

        // Mark graph as dirty - needs recompilation
        graph->MarkDirty();
    }

    void RemoveLight(uint32_t lightId) {
        if (shadowNodes.contains(lightId)) {
            graph->RemoveNode(shadowNodes[lightId]);
            shadowNodes.erase(lightId);
            graph->MarkDirty();
        }
    }

    void Update() {
        // Recompile only if graph changed
        if (graph->IsDirty()) {
            graph->Compile();
        }
    }

private:
    RenderGraph* graph;
    NodeHandle sceneNode;
    NodeHandle compositorNode;
    std::map<uint32_t, NodeHandle> shadowNodes;
    uint32_t nextLightId = 0;
};
```

---

## Example 7: Compute Shader Integration

### Goal
Use compute shaders for particle simulation, integrated with graphics pipeline.

### Code

```cpp
RenderGraph graph(device, &registry);

// Particle buffers
auto particleBuffer = graph.AddNode("BufferResource", "ParticleBuffer");

// Compute pass for particle update
auto particleUpdate = graph.AddNode("ComputePass", "ParticleUpdate");
graph.ConnectNodes(particleBuffer, 0, particleUpdate, 0);  // Read
particleUpdate.SetOutput(0, particleBuffer);                // Write (in-place)

// Render particles
auto particleRender = graph.AddNode("ParticleRenderPass", "RenderParticles");
graph.ConnectNodes(particleUpdate, 0, particleRender, 0);

// Compose with scene
auto scene = graph.AddNode("GeometryPass", "Scene");
auto compositor = graph.AddNode("Compositor", "Final");
graph.ConnectNodes(scene, 0, compositor, 0);
graph.ConnectNodes(particleRender, 0, compositor, 1);

graph.Compile();
graph.Execute(commandBuffer);
```

### Execution Flow

```
┌──────────────┐
│ParticleBuffer│
└──────┬───────┘
       │
       ▼
┌──────────────┐ (Compute)
│ParticleUpdate│
└──────┬───────┘
       │
       ▼
┌──────────────┐ (Graphics)
│RenderParticle│
└──────┬───────┘
       │         ┌───────┐
       │         │ Scene │
       │         └───┬───┘
       │             │
       ▼             ▼
     ┌─────────────────┐
     │   Compositor    │
     └─────────────────┘
```

**Note:** Automatic compute-to-graphics barrier insertion

---

## Example 8: Conditional Execution

### Goal
Enable/disable nodes based on runtime conditions (e.g., quality settings).

### Code

```cpp
class QualitySettings {
public:
    void ApplyToGraph(RenderGraph* graph) {
        if (enableSSAO) {
            auto ssao = graph->AddNode("SSAOPass", "SSAO");
            graph->ConnectNodes(gbuffer, 0, ssao, 0);
            graph->ConnectNodes(ssao, 0, lighting, 1);
        }

        if (enableBloom) {
            auto bloom = graph->AddNode("BloomPass", "Bloom");
            graph->ConnectNodes(lighting, 0, bloom, 0);
            graph->ConnectNodes(bloom, 0, tonemap, 0);
        } else {
            graph->ConnectNodes(lighting, 0, tonemap, 0);
        }

        if (shadowQuality == ShadowQuality::High) {
            // 2048x2048 shadows
            shadowMapSize = 2048;
        } else {
            // 1024x1024 shadows
            shadowMapSize = 1024;
        }

        // Recompile with new configuration
        graph->Compile();
    }

private:
    bool enableSSAO = true;
    bool enableBloom = true;
    ShadowQuality shadowQuality = ShadowQuality::High;
    uint32_t shadowMapSize = 2048;
};
```

---

## Common Patterns

### Pattern 1: Resource Reuse

Share intermediate resources across passes:

```cpp
auto intermediateRT = graph.AddNode("RenderTarget", "Intermediate");

auto pass1 = graph.AddNode("BlurPass", "BlurH");
graph.ConnectNodes(input, 0, pass1, 0);
pass1.SetOutput(0, intermediateRT);  // Write to intermediate

auto pass2 = graph.AddNode("BlurPass", "BlurV");
graph.ConnectNodes(intermediateRT, 0, pass2, 0);  // Read from intermediate
```

### Pattern 2: Multi-Pass Effects

Chain multiple passes of the same type:

```cpp
std::vector<NodeHandle> blurPasses;
NodeHandle input = sceneColor;

for (int i = 0; i < numBlurPasses; i++) {
    auto blur = graph.AddNode("BlurPass", "Blur_" + std::to_string(i));
    graph.ConnectNodes(input, 0, blur, 0);
    input = blur;  // Output becomes next input
    blurPasses.push_back(blur);
}
```

**Optimization:** All blur passes may share the same pipeline

### Pattern 3: Split-Screen Rendering

Render different views to different render targets:

```cpp
// Player 1 view
auto camera1 = graph.AddNode("Camera", "Player1Camera");
auto view1 = graph.AddNode("GeometryPass", "Player1View");
auto rt1 = graph.AddNode("RenderTarget", "RT1");
graph.ConnectNodes(camera1, 0, view1, 0);
graph.ConnectNodes(rt1, 0, view1, 1);

// Player 2 view
auto camera2 = graph.AddNode("Camera", "Player2Camera");
auto view2 = graph.AddNode("GeometryPass", "Player2View");
auto rt2 = graph.AddNode("RenderTarget", "RT2");
graph.ConnectNodes(camera2, 0, view2, 0);
graph.ConnectNodes(rt2, 0, view2, 1);

// Combine views
auto splitScreen = graph.AddNode("SplitScreenComposite", "Final");
graph.ConnectNodes(rt1, 0, splitScreen, 0);
graph.ConnectNodes(rt2, 0, splitScreen, 1);
```

---

**See also:**
- [Node System](01-node-system.md) - Understanding node types and instances
- [Graph Compilation](02-graph-compilation.md) - How graphs are compiled
- [Multi-Device Support](03-multi-device.md) - Multi-GPU details
