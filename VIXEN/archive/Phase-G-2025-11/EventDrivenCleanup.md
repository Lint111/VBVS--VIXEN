# Event-Driven Cleanup System

## Overview

The RenderGraph now supports **event-driven cleanup** via the MessageBus. Applications and nodes can trigger cleanup without knowing specific node names—the graph automatically resolves dependencies and cleans affected nodes.

## Architecture

```
Application/System → MessageBus.Publish(CleanupRequestedMessage)
                          ↓
              RenderGraph subscribes & processes
                          ↓
         Identifies affected nodes (by name/tag/type)
                          ↓
      Recursive cleanup with reference counting
                          ↓
      MessageBus.Publish(CleanupCompletedMessage)
```

## Setup

### 1. Create MessageBus

```cpp
#include "EventBus/MessageBus.h"

// Application initialization
EventBus::MessageBus messageBus;
```

### 2. Pass to RenderGraph

```cpp
#include "RenderGraph/Core/RenderGraph.h"
#include "RenderGraph/Core/GraphMessages.h"

auto graph = std::make_unique<RenderGraph>(
    nodeRegistry.get(),
    &messageBus,      // Enable event-driven cleanup
    mainLogger.get()
);
```

## Usage Patterns

### Cleanup Specific Node

```cpp
using namespace Vixen::RenderGraph;

// Cleanup single node + orphaned dependencies
auto msg = std::make_unique<CleanupRequestedMessage>(0, "MainPass");
msg->reason = "Shader hot-reload";
messageBus.Publish(std::move(msg));

// Process events (once per frame or at safe point)
messageBus.ProcessMessages();
```

### Cleanup by Tag (Bulk Operations)

```cpp
// Tag nodes during graph construction
auto shadowNode1 = graph->AddNode("ShadowMapPass", "Shadow_Light0");
graph->GetInstanceByName("Shadow_Light0")->AddTag("shadow-maps");

auto shadowNode2 = graph->AddNode("ShadowMapPass", "Shadow_Light1");
graph->GetInstanceByName("Shadow_Light1")->AddTag("shadow-maps");

// Later: Cleanup all shadow maps at once
auto msg = CleanupRequestedMessage::ByTag(0, "shadow-maps", "Light setup changed");
messageBus.Publish(std::move(msg));
```

### Cleanup by Type

```cpp
// Cleanup all nodes of specific type
auto msg = CleanupRequestedMessage::ByType(0, "GeometryPass", "Switching to compute path");
messageBus.Publish(std::move(msg));
```

### Full Graph Cleanup

```cpp
// Nuclear option: clean everything
auto msg = CleanupRequestedMessage::Full(0, "Application shutdown");
messageBus.Publish(std::move(msg));
```

## CleanupScope Options

```cpp
enum class CleanupScope {
    Specific,  // Clean specific node name
    ByTag,     // Clean all nodes with tag
    ByType,    // Clean all nodes of type
    Full       // Full graph cleanup
};
```

## Message Types

### CleanupRequestedMessage

```cpp
struct CleanupRequestedMessage {
    CleanupScope scope;
    std::optional<std::string> targetNodeName;  // For Specific
    std::optional<std::string> tag;             // For ByTag
    std::optional<std::string> typeName;        // For ByType
    std::string reason;                         // Optional debug string
};

// Factory methods
CleanupRequestedMessage::ByTag(senderID, "tag-name", "reason");
CleanupRequestedMessage::ByType(senderID, "NodeType", "reason");
CleanupRequestedMessage::Full(senderID, "reason");
```

### CleanupCompletedMessage

```cpp
struct CleanupCompletedMessage {
    std::vector<std::string> cleanedNodes;
    size_t cleanedCount;
};

// Subscribe to completion
messageBus.Subscribe(CleanupCompletedMessage::TYPE, [](const Message& msg) {
    auto& completion = static_cast<const CleanupCompletedMessage&>(msg);
    std::cout << "Cleaned " << completion.cleanedCount << " nodes" << std::endl;
    return true;
});
```

## Real-World Examples

### Window Resize (SwapChain Recreation)

```cpp
// Tag swapchain-dependent nodes
swapChainNode->AddTag("swapchain-dependent");
framebufferNode->AddTag("swapchain-dependent");

// On window resize event
void OnWindowResize(uint32_t width, uint32_t height) {
    // Publish resize event
    auto resizeMsg = std::make_unique<WindowResizedMessage>(0, width, height);
    messageBus.Publish(std::move(resizeMsg));
    
    // Cleanup swapchain-dependent nodes
    auto cleanupMsg = CleanupRequestedMessage::ByTag(
        0,
        "swapchain-dependent",
        "Window resized to " + std::to_string(width) + "x" + std::to_string(height)
    );
    messageBus.Publish(std::move(cleanupMsg));
    
    // Process events
    messageBus.ProcessMessages();
    
    // Recompile graph (nodes recreate resources)
    graph->Compile();
}
```

### Shader Hot-Reload

```cpp
// Tag pipeline-dependent nodes
pipelineNode->AddTag("graphics-pipeline");
geometryPassNode->AddTag("graphics-pipeline");

// On shader file changed
void OnShaderReload(const std::string& shaderPath) {
    // Publish shader reload event
    auto shaderMsg = std::make_unique<ShaderReloadedMessage>(0, shaderPath);
    messageBus.Publish(std::move(shaderMsg));
    
    // Cleanup affected pipelines
    auto cleanupMsg = CleanupRequestedMessage::ByTag(
        0,
        "graphics-pipeline",
        "Shader reloaded: " + shaderPath
    );
    messageBus.Publish(std::move(cleanupMsg));
    
    messageBus.ProcessMessages();
    graph->Compile();
}
```

### Dynamic Light Management

```cpp
class LightManager {
    std::vector<std::string> activeLights;
    
    void AddLight(int lightID) {
        std::string shadowNodeName = "Shadow_Light" + std::to_string(lightID);
        
        // Add node dynamically
        auto shadowNode = graph->AddNode("ShadowMapPass", shadowNodeName);
        graph->GetInstanceByName(shadowNodeName)->AddTag("dynamic-lights");
        
        // Connect to existing graph
        graph->ConnectNodes(deviceNode, 0, shadowNode, 0);
        
        // Compile only new node
        graph->CompileNode(shadowNodeName);
        
        activeLights.push_back(shadowNodeName);
    }
    
    void RemoveLight(int lightID) {
        std::string shadowNodeName = "Shadow_Light" + std::to_string(lightID);
        
        // Event-driven cleanup
        auto msg = std::make_unique<CleanupRequestedMessage>(0, shadowNodeName);
        msg->reason = "Light " + std::to_string(lightID) + " removed";
        messageBus.Publish(std::move(msg));
        
        messageBus.ProcessMessages();
        
        // Remove from graph topology
        graph->RemoveNode(shadowNodeName);
        
        activeLights.erase(
            std::remove(activeLights.begin(), activeLights.end(), shadowNodeName),
            activeLights.end()
        );
    }
    
    void RemoveAllLights() {
        // Bulk cleanup via tag
        auto msg = CleanupRequestedMessage::ByTag(0, "dynamic-lights", "Clearing all lights");
        messageBus.Publish(std::move(msg));
        messageBus.ProcessMessages();
        
        activeLights.clear();
    }
};
```

## Node Tagging Best Practices

```cpp
// Tag during graph construction
void BuildRenderGraph() {
    // Shadow maps
    for (int i = 0; i < numLights; ++i) {
        auto node = graph->AddNode("ShadowMapPass", "Shadow_Light" + std::to_string(i));
        graph->GetInstanceByName("Shadow_Light" + std::to_string(i))->AddTag("shadow-maps");
        graph->GetInstanceByName("Shadow_Light" + std::to_string(i))->AddTag("dynamic");
    }
    
    // Post-process chain
    auto bloomNode = graph->AddNode("BloomPass", "Bloom");
    bloomNode->AddTag("post-process");
    bloomNode->AddTag("bloom-chain");
    
    auto toneMapNode = graph->AddNode("ToneMappingPass", "ToneMap");
    toneMapNode->AddTag("post-process");
    
    // SwapChain dependents
    swapChainNode->AddTag("swapchain-dependent");
    framebufferNode->AddTag("swapchain-dependent");
    presentNode->AddTag("swapchain-dependent");
}
```

### Common Tags

- `"shadow-maps"` - All shadow mapping nodes
- `"post-process"` - Post-processing effects
- `"dynamic"` - Dynamically added/removed nodes
- `"swapchain-dependent"` - Needs recreation on window resize
- `"graphics-pipeline"` - Affected by shader reload
- `"debug"` - Debug visualization nodes (can disable bulk)

## Benefits vs Direct Calls

### Old Way (Tight Coupling)
```cpp
// ❌ Application knows specific node names
graph->CleanupSubgraph("MainPass");
graph->CleanupSubgraph("ShadowPass_Light0");
graph->CleanupSubgraph("ShadowPass_Light1");
// Must know ALL affected nodes manually
```

### New Way (Event-Driven)
```cpp
// ✅ Application broadcasts intent
auto msg = CleanupRequestedMessage::ByTag(0, "shadow-maps");
messageBus.Publish(std::move(msg));
// Graph resolves which nodes affected
```

**Advantages:**
- ✅ **Decoupled:** Application doesn't know node names
- ✅ **Dynamic:** Works with runtime-created nodes
- ✅ **Scalable:** Tag-based bulk operations
- ✅ **Flexible:** Easy to add/remove nodes without code changes
- ✅ **Safe:** Reference counting prevents premature cleanup

## Processing Events

### Safe Point Processing

```cpp
// Once per frame (during non-rendering phase)
void Update() {
    // Handle input, logic updates...
    
    // Safe point: process cleanup events
    messageBus.ProcessMessages();
    
    // Recompile if needed
    if (graphDirty) {
        graph->Compile();
        graphDirty = false;
    }
}

void Render() {
    // NO event processing here (mid-frame unsafe)
    graph->RenderFrame();
}
```

### Immediate Processing (Rare)

```cpp
// Only if cleanup must happen NOW (blocking)
auto msg = CleanupRequestedMessage::Full(0, "Critical shutdown");
messageBus.PublishImmediate(std::move(msg));  // Processes immediately
```

## Summary

**Event-driven cleanup eliminates hardcoded node names** and enables:
- Dynamic graph modifications at runtime
- Bulk operations via tags (`"shadow-maps"`, `"post-process"`)
- Type-based cleanup (`"GeometryPass"`)
- Loose coupling between application and graph internals

**Key workflow:**
1. Tag nodes during graph construction
2. Publish `CleanupRequestedMessage` when needed
3. Process events at safe points
4. Graph automatically resolves dependencies
5. Subscribe to `CleanupCompletedMessage` for feedback
