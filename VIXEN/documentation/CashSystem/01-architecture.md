# CashSystem Architecture

**Last Updated**: October 31, 2025

## Overview

Type-safe resource caching system eliminating redundant Vulkan object creation via hash-based deduplication. Supports both device-dependent (VkShaderModule, VkPipeline) and device-independent (SPIR-V bytecode) caching.

## Core Components

### 1. MainCacher - Hybrid Registry

Central orchestrator managing both device-dependent and device-independent caching.

**Responsibilities**:
- Device-dependent cacher registration and retrieval
- Device-independent global cache management
- Cleanup orchestration
- Thread-safe cacher access

**Key Methods**:
```cpp
// Register device-dependent cacher
template<typename CacherT, typename ResourceT, typename CreateInfoT>
void RegisterCacher(std::type_index typeIndex, std::string name, bool isDeviceDependent);

// Get cacher (automatically routes to correct registry)
template<typename CacherT, typename ResourceT, typename CreateInfoT>
CacherT* GetCacher(std::type_index typeIndex, VulkanDevice* device);

// Cleanup
void ClearDeviceCaches(VulkanDevice* device);  // Device-specific
void CleanupGlobalCaches();                     // Global
```

### 2. TypedCacher<ResourceT, CreateInfoT> - Template Base

Unified template for all cachers (device-dependent and device-independent).

**Template Parameters**:
- `ResourceT`: Wrapper type (e.g., `ShaderModuleWrapper`, `PipelineWrapper`)
- `CreateInfoT`: Creation parameters for cache key generation

**Core API**:
```cpp
// Get or create cached resource
std::shared_ptr<ResourceT> GetOrCreate(const CreateInfoT& createInfo);

// Virtual cleanup (override for Vulkan resource destruction)
virtual void Cleanup() = 0;

// Clear cache entries
void Clear();
```

### 3. DeviceRegistry - Per-Device Isolation

Isolates device-specific Vulkan resources.

**Purpose**: Prevents cross-device resource sharing for VkPipeline, VkShaderModule, etc.

**Structure**:
```cpp
struct DeviceRegistry {
    VulkanDevice* device;
    std::vector<std::shared_ptr<CacherBase>> m_deviceCachers;
};

// Map: DeviceIdentifier → DeviceRegistry
std::unordered_map<DeviceIdentifier, DeviceRegistry> m_deviceRegistries;
```

### 4. CacherBase - Polymorphic Interface

Abstract base enabling MainCacher to call `Cleanup()` without knowing cacher type.

```cpp
class CacherBase {
public:
    virtual void Cleanup() = 0;
    virtual ~CacherBase() = default;
};
```

## Caching Strategies

### Device-Dependent Caching

**Use Case**: Vulkan objects tied to specific device (VkShaderModule, VkPipeline, VkDescriptorSet)

**Behavior**:
- Cached per-device (isolated)
- Requires device context
- Destroyed when device destroyed

**Example Cachers**:
- **ShaderModuleCacher**: VkShaderModule from SPIR-V bytecode
- **PipelineCacher**: VkPipeline from pipeline state
- **PipelineLayoutCacher**: VkPipelineLayout from descriptor layouts

### Device-Independent Caching

**Use Case**: Universal resources (compiled SPIR-V, file hashes, config data)

**Behavior**:
- Globally shared across all devices
- No device context required
- Persists across device lifetime

**Example Cachers** (future):
- ShaderCompilationCacher: GLSL → SPIR-V
- FileHashCacher: File content hashes

## Architecture Patterns

### 1. Virtual Cleanup Pattern

**Problem**: MainCacher needs to destroy resources without knowing specific types

**Solution**: Virtual `Cleanup()` method on `CacherBase`

```cpp
// Base declares interface
class CacherBase {
    virtual void Cleanup() = 0;
};

// Template provides default
template<typename ResourceT, typename CreateInfoT>
class TypedCacher : public CacherBase {
    void Cleanup() override { Clear(); }  // Default: just clear entries
};

// Concrete override for Vulkan cleanup
class ShaderModuleCacher : public TypedCacher<...> {
    void Cleanup() override {
        // Destroy VkShaderModule handles
        for (auto& [key, entry] : m_entries) {
            vkDestroyShaderModule(device, entry.resource->shaderModule, nullptr);
        }
        Clear();
    }
};

// MainCacher orchestrates
void MainCacher::ClearDeviceCaches(VulkanDevice* device) {
    for (auto& cacher : deviceRegistry.m_deviceCachers) {
        cacher->Cleanup();  // Polymorphic call
    }
}
```

### 2. Hash-Based Deduplication

**Key Generation**: FNV-1a hash of creation parameters

```cpp
// ShaderModuleCacher key
size_t ShaderModuleCreateParams::ComputeHash() const {
    size_t hash = 14695981039346656037ULL;  // FNV offset
    for (uint32_t code : spirvCode) {
        hash ^= code;
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}
```

### 3. Transparent Two-Mode API (PipelineLayoutCacher)

**Problem**: Users want explicit control but also convenience

**Solution**: Two modes - explicit and fallback

```cpp
struct PipelineCreateParams {
    std::shared_ptr<PipelineLayoutWrapper> pipelineLayoutWrapper;  // Explicit
    VkDescriptorSetLayout descriptorSetLayout;                      // Fallback
};

// Mode 1: Explicit (transparent - user sees what's cached)
params.pipelineLayoutWrapper = layoutCacher->GetOrCreate(layoutParams);

// Mode 2: Convenience (auto-creates layout if not provided)
params.descriptorSetLayout = myLayout;  // Cacher auto-creates PipelineLayout
```

## Resource Wrappers

### ShaderModuleWrapper
```cpp
struct ShaderModuleWrapper {
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::vector<uint32_t> spirvCode;  // For debugging
};
```

### PipelineWrapper
```cpp
struct PipelineWrapper {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineCache cache = VK_NULL_HANDLE;
};
```

### PipelineLayoutWrapper
```cpp
struct PipelineLayoutWrapper {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;  // NOT owned
    std::vector<VkPushConstantRange> pushConstantRanges;
};
```

## Thread Safety

- **MainCacher**: Mutex-protected device registry access
- **TypedCacher**: Cache entry map protected per-cacher
- **GetOrCreate**: Lock during cache lookup and insertion

## Cleanup Flow

### Device Destruction
```
DeviceNode::Cleanup()
    ↓
MainCacher::ClearDeviceCaches(device)
    ↓
For each cacher in DeviceRegistry:
    cacher->Cleanup()  // Virtual call
        ↓
    ShaderModuleCacher::Cleanup() → vkDestroyShaderModule()
    PipelineCacher::Cleanup() → vkDestroyPipeline()
    PipelineLayoutCacher::Cleanup() → vkDestroyPipelineLayout()
```

### Application Shutdown
```
MainCacher::CleanupGlobalCaches()
    ↓
For each global cacher:
    cacher->Cleanup()
```

## Integration Points

**RenderGraph Nodes**:
- `GraphicsPipelineNode`: Uses ShaderModuleCacher, PipelineCacher, PipelineLayoutCacher
- `DeviceNode`: Calls `ClearDeviceCaches()` on cleanup
- `ShaderLibraryNode`: Uses ShaderModuleCacher for VkShaderModule creation

**Logging**:
- CACHE HIT/MISS logs activated via `std::cout`
- Future: Integrate with Logger system

## Key Files

- `CashSystem/include/CashSystem/MainCacher.h` - Hybrid registry
- `CashSystem/include/CashSystem/TypedCacher.h` - Template base
- `CashSystem/include/CashSystem/CacherBase.h` - Polymorphic interface
- `CashSystem/src/shader_module_cacher.cpp` - VkShaderModule caching
- `CashSystem/src/pipeline_cacher.cpp` - VkPipeline caching
- `CashSystem/src/pipeline_layout_cacher.cpp` - VkPipelineLayout caching

## Design Principles

1. **Type Safety**: Template parameters ensure compile-time type checking
2. **Transparency**: Clear dependency tracking (explicit `pipelineLayoutWrapper` field)
3. **Polymorphism**: Virtual cleanup enables MainCacher orchestration
4. **Isolation**: Device-dependent resources never cross device boundaries
5. **Sharing**: Device-independent resources shared globally for efficiency
