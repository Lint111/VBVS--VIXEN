# CashSystem Cleanup Architecture

**Last Updated**: October 31, 2025

## Overview

The CashSystem cleanup architecture uses a **virtual polymorphic pattern** to enable MainCacher to orchestrate resource destruction without knowing implementation details of individual cachers. This provides clean separation of concerns and proper Vulkan resource lifecycle management.

## Architecture Components

### 1. Virtual Cleanup() Method

**Base Class** (`CashSystem/include/CashSystem/CacherBase.h`):
```cpp
class CacherBase {
public:
    // Pure virtual - all cachers must implement
    virtual void Cleanup() = 0;
};
```

**Template Base** (`CashSystem/include/CashSystem/TypedCacher.h`):
```cpp
template<typename ResourceT, typename CreateInfoT>
class TypedCacher : public CacherBase {
public:
    // Default implementation - derived classes override
    void Cleanup() override {
        Clear();  // Just clears cache entries
    }
};
```

### 2. Cacher-Specific Implementations

**ShaderModuleCacher** (`CashSystem/src/shader_module_cacher.cpp`):
```cpp
void ShaderModuleCacher::Cleanup() {
    // Destroy all cached VkShaderModule handles
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource && entry.resource->shaderModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(GetDevice()->device,
                                     entry.resource->shaderModule, nullptr);
                entry.resource->shaderModule = VK_NULL_HANDLE;
            }
        }
    }
    Clear();
}
```

**PipelineCacher** (`CashSystem/src/pipeline_cacher.cpp`):
```cpp
void PipelineCacher::Cleanup() {
    // Destroy all cached Vulkan pipeline resources
    if (GetDevice()) {
        for (auto& [key, entry] : m_entries) {
            if (entry.resource) {
                if (entry.resource->pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(GetDevice()->device,
                                     entry.resource->pipeline, nullptr);
                }
                if (entry.resource->layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(GetDevice()->device,
                                           entry.resource->layout, nullptr);
                }
                if (entry.resource->cache != VK_NULL_HANDLE) {
                    vkDestroyPipelineCache(GetDevice()->device,
                                          entry.resource->cache, nullptr);
                }
            }
        }
    }
    Clear();
}
```

### 3. MainCacher Orchestration

**Device-Dependent Cleanup** (`CashSystem/src/main_cacher.cpp`):
```cpp
void MainCacher::ClearDeviceCaches(VulkanDevice* device) {
    std::lock_guard lock(m_deviceRegistriesMutex);
    auto deviceId = DeviceIdentifier(device);

    auto it = m_deviceRegistries.find(deviceId);
    if (it != m_deviceRegistries.end()) {
        // Call Cleanup() on all cachers in this device registry
        for (auto& cacher : it->second.m_deviceCachers) {
            if (cacher) {
                cacher->Cleanup();
            }
        }
        m_deviceRegistries.erase(it);
    }
}
```

**Global Cleanup** (`CashSystem/src/main_cacher.cpp`):
```cpp
void MainCacher::CleanupGlobalCaches() {
    std::lock_guard lock(m_globalRegistryMutex);
    for (auto& [typeIndex, cacher] : m_globalCachers) {
        if (cacher) {
            cacher->Cleanup();
        }
    }
}
```

### 4. Integration Points

**DeviceNode** (`RenderGraph/src/Nodes/DeviceNode.cpp`):
```cpp
void DeviceNode::CleanupImpl() {
    // Cleanup all device-dependent caches BEFORE destroying the device
    // DeviceNode manages the device lifecycle, so it's responsible for cache cleanup
    if (vulkanDevice) {
        auto& mainCacher = GetOwningGraph()->GetMainCacher();
        mainCacher.ClearDeviceCaches(vulkanDevice.get());
    }

    // VulkanDevice destructor handles device destruction
    vulkanDevice.reset();
}
```

**RenderGraph** (`RenderGraph/src/Core/RenderGraph.cpp`):
```cpp
RenderGraph::~RenderGraph() {
    // Note: Device-dependent cache cleanup happens in DeviceNode::CleanupImpl()
    // Only cleanup global (device-independent) caches here
    if (mainCacher) {
        mainCacher->CleanupGlobalCaches();
    }

    // ... rest of destructor
}
```

## Cleanup Sequence

The cleanup architecture follows this order:

```
1. RenderGraph::ExecuteCleanup()
   ├── Node cleanup callbacks (via CleanupStack)
   │   ├── GraphicsPipelineNode::CleanupImpl() - Release shared_ptr references
   │   ├── ShaderLibraryNode::CleanupImpl() - Release shared_ptr references
   │   └── DeviceNode::CleanupImpl()
   │       └── MainCacher::ClearDeviceCaches(device)
   │           ├── ShaderModuleCacher::Cleanup() - Destroy VkShaderModule handles
   │           └── PipelineCacher::Cleanup() - Destroy VkPipeline/Layout/Cache handles
   └── vulkanDevice.reset() - Destroy VkDevice

2. RenderGraph::~RenderGraph()
   └── MainCacher::CleanupGlobalCaches()
       └── Iterate device-independent cachers, call Cleanup()
```

## Key Design Principles

### 1. Separation of Concerns
- **MainCacher** doesn't know implementation details of individual cachers
- **Cachers** implement their own resource destruction logic
- **DeviceNode** owns device lifecycle and triggers cache cleanup

### 2. Resource Ownership
- **Cachers** own Vulkan resource handles (VkShaderModule, VkPipeline, etc.)
- **Nodes** hold references via `shared_ptr`
- **Nodes** release references, **cachers** destroy resources

### 3. Cleanup Graph Integration
- DeviceNode is a **root node** (no input dependencies)
- CleanupStack properly handles root nodes via `ExecuteAll()`
- DeviceNode::CleanupImpl() is called by cleanup graph

### 4. No SetDevice() Workaround
- DeviceNode directly manages `vulkanDevice` member variable
- No need to call `SetDevice()` on base class
- DeviceNode is special - it **provides** devices, not consumes them

## Validation

Zero Vulkan validation errors achieved:
- ✅ All VkShaderModule destroyed before vkDestroyDevice
- ✅ All VkPipeline destroyed before vkDestroyDevice
- ✅ All VkPipelineLayout destroyed before vkDestroyDevice
- ✅ All VkPipelineCache destroyed before vkDestroyDevice

## Future Extensions

### Multi-Device Support
When DeviceNode outputs multiple devices:
```cpp
void DeviceNode::CleanupImpl() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Iterate all managed devices
    for (auto& device : createdDevices) {
        mainCacher.ClearDeviceCaches(device.get());
        device.reset();
    }
}
```

### Custom Cacher Types
To add a new cacher:
1. Inherit from `TypedCacher<ResourceT, CreateInfoT>`
2. Override `Cleanup()` method
3. Destroy Vulkan resources, then call `Clear()`
4. Register with MainCacher (device-dependent or independent)

Example:
```cpp
class TextureCacher : public TypedCacher<TextureWrapper, TextureCreateParams> {
public:
    void Cleanup() override {
        if (GetDevice()) {
            for (auto& [key, entry] : m_entries) {
                if (entry.resource) {
                    vkDestroyImage(GetDevice()->device, entry.resource->image, nullptr);
                    vkDestroyImageView(GetDevice()->device, entry.resource->view, nullptr);
                    vkFreeMemory(GetDevice()->device, entry.resource->memory, nullptr);
                }
            }
        }
        Clear();
    }
};
```

## References

**Key Files**:
- `CashSystem/include/CashSystem/CacherBase.h` - Base class with virtual Cleanup()
- `CashSystem/include/CashSystem/TypedCacher.h` - Template with default implementation
- `CashSystem/include/CashSystem/MainCacher.h` - Orchestration API
- `CashSystem/src/main_cacher.cpp` - ClearDeviceCaches(), CleanupGlobalCaches()
- `CashSystem/src/shader_module_cacher.cpp` - ShaderModuleCacher::Cleanup()
- `CashSystem/src/pipeline_cacher.cpp` - PipelineCacher::Cleanup()
- `RenderGraph/src/Nodes/DeviceNode.cpp` - DeviceNode::CleanupImpl()
- `RenderGraph/src/Core/RenderGraph.cpp` - RenderGraph destructor

**Related Documentation**:
- `documentation/CashSystem-RenderGraph-Integration.md` - Integration guide
- `RenderGraph/include/CleanupStack.h` - Cleanup graph system
