# Dynamic Dependency-Aware Cleanup System

## Overview

The RenderGraph now supports **automatic cleanup dependency resolution** via the `ResourceDependencyTracker`. Nodes no longer need hardcoded cleanup dependency names—dependencies are discovered automatically from input slot connections.

## Architecture

```
┌──────────────────────────────────────────────────┐
│           RenderGraph                            │
│  ┌────────────────┐  ┌──────────────────────┐   │
│  │ CleanupStack   │  │ DependencyTracker    │   │
│  │ (execution)    │  │ (resolution)         │   │
│  └────────────────┘  └──────────────────────┘   │
│           ▲                    ▲                 │
│           │                    │                 │
│           └────────┬───────────┘                 │
│                    │                             │
│              NodeInstance                        │
│            RegisterCleanup()                     │
└──────────────────────────────────────────────────┘

Flow:
1. ConnectNodes() → Tracks Resource → NodeInstance mapping
2. node->RegisterCleanup() → Queries tracker for dependencies
3. CleanupStack.ExecuteAll() → Runs in reverse-dependency order
```

## Key Components

### ResourceDependencyTracker

**Purpose:** Tracks which NodeInstance produces each Resource.

**Core API:**
```cpp
// Called internally by RenderGraph::ConnectNodes()
void RegisterResourceProducer(
    Resource* resource,
    NodeInstance* producer,
    uint32_t outputSlotIndex
);

// Query producer of a resource
NodeInstance* GetProducer(Resource* resource) const;

// Build cleanup dependencies for a consumer node
std::vector<std::string> BuildCleanupDependencies(NodeInstance* consumer) const;
```

**How it works:**
- When `ConnectNodes(from, outIdx, to, inIdx)` called:
  - Resource created/retrieved from `from` node output
  - Tracker registers: `resource → from` mapping
  - Resource connected to `to` node input
- When `to->RegisterCleanup()` called:
  - Tracker examines all input slots of `to`
  - Finds producer nodes for each input resource
  - Returns cleanup names: `["from_Cleanup", ...]`

### NodeInstance::RegisterCleanup()

**Automatic cleanup registration:**
```cpp
void NodeInstance::RegisterCleanup() {
    // Build dependencies from input slots automatically
    auto& tracker = owningGraph->GetDependencyTracker();
    std::vector<std::string> deps = tracker.BuildCleanupDependencies(this);
    
    // Register with CleanupStack
    owningGraph->GetCleanupStack().Register(
        GetInstanceName() + "_Cleanup",
        [this]() { this->Cleanup(); },
        deps  // Automatic dependency list
    );
}
```

## Usage Pattern (Node Implementation)

### Old System (Hardcoded Dependencies)

```cpp
void DeviceNode::Compile() {
    // ... create resources ...
    
    // ❌ Hardcoded cleanup name
    if (GetOwningGraph()) {
        GetOwningGraph()->GetCleanupStack().Register(
            "DeviceNode_Cleanup",  // Hardcoded string
            [this]() { this->Cleanup(); },
            {}  // Manual dependency list
        );
    }
}

void CommandPoolNode::Compile() {
    // ... create resources ...
    
    // ❌ Hardcoded dependency reference
    if (GetOwningGraph()) {
        GetOwningGraph()->GetCleanupStack().Register(
            GetInstanceName() + "_Cleanup",
            [this]() { this->Cleanup(); },
            { "DeviceNode_Cleanup" }  // Must know exact producer name
        );
    }
}
```

**Problems:**
- Hardcoded strings break with multiple instances (`Device_GPU0`, `Device_GPU1`)
- Manual dependency tracking error-prone
- Can't handle dynamic graph topologies

### New System (Automatic Dependencies)

```cpp
void DeviceNode::Compile() {
    // ... create VulkanDevice ...
    
    // Set outputs (tracker will register these)
    Out(DeviceNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice.get());
    Out(DeviceNodeConfig::INSTANCE, instance);
    
    // ✅ Automatic cleanup registration
    RegisterCleanup();  // No dependencies (root node)
}

void CommandPoolNode::Compile() {
    // Get VulkanDevice from input slot 0
    auto* vulkanDevice = In<VulkanDevice*>(CommandPoolNodeConfig::VULKAN_DEVICE_IN);
    
    // ... create command pool using vulkanDevice ...
    
    // Set outputs
    Out(CommandPoolNodeConfig::COMMAND_POOL, commandPool);
    Out(CommandPoolNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice);
    
    // ✅ Automatic dependency on DeviceNode
    RegisterCleanup();  // Discovers dependency via input slot 0
}
```

**Benefits:**
- Works with multiple instances (`Light0_ShadowMap`, `Light1_ShadowMap`)
- Dependencies discovered from graph topology
- Zero hardcoded strings (except node instance names)

## Dynamic Multi-Instance Example

**Scenario:** Multiple shadow maps, each needing device + texture resources

```cpp
// Build graph dynamically
for (int i = 0; i < numLights; ++i) {
    std::string shadowName = "ShadowMap_Light" + std::to_string(i);
    std::string textureName = "ShadowTexture_Light" + std::to_string(i);
    
    // Create texture
    auto texNode = graph.AddNode("TextureLoader", textureName);
    graph.ConnectNodes(deviceNode, 0, texNode, 0);  // Device → Texture
    
    // Create shadow map pass
    auto shadowNode = graph.AddNode("ShadowMapPass", shadowName);
    graph.ConnectNodes(deviceNode, 0, shadowNode, 0);    // Device → ShadowMap
    graph.ConnectNodes(texNode, 0, shadowNode, 1);        // Texture → ShadowMap
    
    // NO manual cleanup registration needed!
    // Each node calls RegisterCleanup() in Compile()
}

// Cleanup order automatically resolved:
// 1. ShadowMap_Light0, ShadowMap_Light1 (depend on Device + Texture)
// 2. ShadowTexture_Light0, ShadowTexture_Light1 (depend on Device)
// 3. DeviceNode (root - no dependencies)
```

## Implementation Checklist for New Nodes

When creating a new node type:

1. **In `Compile()` method:**
   ```cpp
   void MyNode::Compile() {
       // Get inputs
       auto* device = In<VulkanDevice*>(0);
       auto* texture = In<VkImageView>(1);
       
       // Create Vulkan resources...
       
       // Set outputs
       Out(0, myResource);
       
       // ✅ Register cleanup at END of Compile()
       RegisterCleanup();
   }
   ```

2. **In `Cleanup()` method:**
   ```cpp
   void MyNode::Cleanup() {
       if (myVulkanHandle != VK_NULL_HANDLE) {
           vkDestroyMyResource(device->device, myVulkanHandle, nullptr);
           myVulkanHandle = VK_NULL_HANDLE;
       }
   }
   ```

3. **No manual CleanupStack registration needed!**

## Edge Cases

### Root Nodes (No Dependencies)
```cpp
// DeviceNode has no inputs → no dependencies
RegisterCleanup();  // Empty dependency list
```

### Shared Resources
```cpp
// Multiple nodes share same Device output
CommandPoolNode → connects to Device
SwapChainNode → connects to Device

// Both automatically depend on Device cleanup
// Both cleanup before Device
```

### Array Inputs
```cpp
// Node with multiple textures in slot 0 (array)
inputs[0] = [texture0, texture1, texture2];

// RegisterCleanup() scans ALL array elements
// Discovers all 3 texture producers
// Registers all as dependencies
```

## Migration Guide

To migrate existing nodes:

1. **Remove manual CleanupStack calls:**
   ```cpp
   // DELETE THIS:
   if (GetOwningGraph()) {
       GetOwningGraph()->GetCleanupStack().Register(
           GetInstanceName() + "_Cleanup",
           [this]() { this->Cleanup(); },
           { "DeviceNode_Cleanup", "OtherNode_Cleanup" }
       );
   }
   ```

2. **Add RegisterCleanup() at end of Compile():**
   ```cpp
   // ADD THIS:
   RegisterCleanup();
   ```

3. **Ensure outputs set BEFORE RegisterCleanup():**
   ```cpp
   // Set outputs first (so tracker can register them)
   Out(0, myResource);
   Out(1, anotherResource);
   
   // Then register cleanup
   RegisterCleanup();
   ```

## Debugging

**Enable cleanup order logging:**
```cpp
// In CleanupNode::ExecuteCleanup()
std::cout << "[Cleanup] Executing: " << nodeName << std::endl;
```

**Check dependency tracking:**
```cpp
// After graph compilation
auto& tracker = graph.GetDependencyTracker();
std::cout << "Tracked resources: " << tracker.GetTrackedResourceCount() << std::endl;
```

## Performance

- **Tracker overhead:** `O(R)` memory for R resources (one pointer per resource)
- **Dependency resolution:** `O(S * A)` per node (S slots × A array size)
- **Cleanup execution:** `O(N)` for N nodes (same as before)

Negligible overhead—tracking done once during compilation, not per-frame.

## Future Enhancements

1. **Partial cleanup:** `ExecuteFrom(nodeName)` for selective recompilation
2. **Cleanup visualization:** Export cleanup dependency graph to DOT format
3. **Cycle detection:** Validate no circular cleanup dependencies
4. **Resource aliasing:** Track multiple nodes sharing same resource handle

## Summary

✅ **Automatic dependency resolution** from graph topology  
✅ **Dynamic multi-instance support** (unlimited shadow maps, lights, etc.)  
✅ **Zero hardcoded cleanup names** (uses instance names)  
✅ **Safer cleanup order** (graph-derived, not manually specified)  
✅ **Migration:** Replace manual registration with `RegisterCleanup()`
