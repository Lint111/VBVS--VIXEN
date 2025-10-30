# CashSystem Hybrid Caching Documentation

## Overview

The CashSystem is a comprehensive caching framework that supports **both device-dependent and device-independent caching**. This hybrid architecture optimizes performance by sharing device-independent resources (like compiled shaders) across all devices while keeping device-specific resources (like Vulkan objects) isolated per device.

## Architecture

### Core Components

1. **MainCacher** - Hybrid registry managing both device-dependent and device-independent caching
2. **DeviceRegistry** - Device-specific caching system for Vulkan resources
3. **DeviceIdentifier** - Device identification and hashing system
4. **TypeRegistry** - Type-based registry for managing cachers within device contexts
5. **TypedCacher<T, CI>** - Template base used for both device-dependent and device-independent caching

### Hybrid Caching Architecture Principles

#### 1. Device-Dependent vs Device-Independent Caching

**Device-Dependent Resources** (Isolated per device):
- **VkImage, VkTexture** - Different devices have different memory layouts
- **VkPipeline** - Device-specific pipeline state
- **VkDescriptorSet** - Device-specific descriptor management
- **VkFramebuffer** - Device-specific framebuffer configuration

**Device-Independent Resources** (Shared across devices):
- **Compiled SPIR-V** - Once compiled, works on any compatible device
- **File hashes** - File content is device-agnostic
- **Configuration data** - Application settings don't depend on device
- **Shader source code** - Source code is universal

#### 2. Unified TypedCacher System
Both caching types use the same `TypedCacher<T, CI>` system - no separate base classes needed:

```cpp
// Device-dependent (inherits TypedCacher)
class ShaderModuleCacher : public TypedCacher<ShaderModuleWrapper, ShaderModuleCreateParams> {
protected:
    void OnInitialize() override {
        // Needs device context
        m_device->CreateShaderModule(/* ... */);
    }
};

// Device-independent (also inherits TypedCacher)
class ShaderCompilationCacher : public TypedCacher<CompiledShaderWrapper, ShaderCompilationParams> {
protected:
    void OnInitialize() override {
        // No device context needed - compilation is universal
    }
};
```

#### 3. Automatic Caching Strategy Selection

The `MainCacher` automatically routes cache requests to the appropriate registry:

```cpp
// Register device-dependent cache (requires device context)
mainCacher.RegisterCacher<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
    typeid(ShaderModuleWrapper), 
    "ShaderModule", 
    true  // isDeviceDependent = true
);

// Register device-independent cache (shared across devices)
mainCacher.RegisterCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
    typeid(CompiledShaderWrapper), 
    "ShaderCompilation", 
    false  // isDeviceDependent = false
);

// Usage - automatically chooses correct strategy
void GraphicsPipelineNode::Compile() {
    auto* device = this->GetDevice();
    
    // Gets device-dependent ShaderModuleCacher for this device
    auto* shaderModuleCacher = mainCacher.GetCacher<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
        typeid(ShaderModuleWrapper), device
    );
    
    // Gets device-independent ShaderCompilationCacher (device param ignored)
    auto* shaderCompilationCacher = mainCacher.GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper), device  // Device param ignored for device-independent
    );
}
```

### Resource Wrappers

Each cacher manages a wrapper type that contains:
- **Device-dependent wrappers**: Vulkan handles + device context
- **Device-independent wrappers**: Pure data + metadata for cache identification

## Key Features

### 1. Hybrid Caching Strategy
- **Device-dependent**: Per-device isolation for Vulkan resources
- **Device-independent**: Global shared cache for universal resources
- **Automatic selection**: No manual registry management required
- **Unified API**: Same interface for both caching types

### 2. Type-Safe Caching
- Template-based `TypedCacher` ensures compile-time type safety
- Automatic cache key generation from creation parameters
- Zero-overhead abstraction layer
- Both caching types use identical interface

### 3. Thread-Safe Operations
- **Device-dependent**: `std::shared_mutex` within device registry
- **Device-independent**: Global shared registry with thread safety
- Lock-free fast path for cache hits
- Promise-based async creation for expensive operations

### 4. Dynamic Registration System
- **Global registration**: Cacher types registered once with dependency flag
- **Automatic routing**: Requests routed to appropriate registry
- **Device-aware factories**: Device-dependent cachers created per device

## Usage Patterns

### Basic Hybrid Caching Setup

```cpp
#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderCompilationCacher.h"

auto& mainCacher = CashSystem::MainCacher::Instance();

// Register device-dependent caches
mainCacher.RegisterCacher<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
    typeid(ShaderModuleWrapper), "ShaderModule", true  // device-dependent
);

mainCacher.RegisterCacher<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
    typeid(PipelineWrapper), "Pipeline", true  // device-dependent
);

// Register device-independent caches
mainCacher.RegisterCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
    typeid(CompiledShaderWrapper), "ShaderCompilation", false  // device-independent
);
```

### Multi-Device with Mixed Caching

```cpp
class MultiDeviceRenderer {
    void RenderFrame(VulkanDevice* deviceA, VulkanDevice* deviceB) {
        // Device A - main rendering
        auto* deviceAPipelineCacher = mainCacher.GetCacher<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
            typeid(PipelineWrapper), deviceA  // Device-dependent cache
        );
        
        auto* deviceAShaderCompilation = mainCacher.GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
            typeid(CompiledShaderWrapper), deviceA  // Same instance as deviceB
        );
        
        // Device B - shadow rendering
        auto* deviceBPipelineCacher = mainCacher.GetCacher<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
            typeid(PipelineWrapper), deviceB  // Different cache from deviceA
        );
        
        auto* deviceBShaderCompilation = mainCacher.GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
            typeid(CompiledShaderWrapper), deviceB  // Same cache as deviceA
        );
        
        // Verify shared compilation cache
        assert(deviceAShaderCompilation == deviceBShaderCompilation);  // Same instance
        
        // Verify separate device caches
        assert(deviceAPipelineCacher != deviceBPipelineCacher);  // Different instances
    }
};
```

### Shader Compilation Workflow

```cpp
class ShaderLibraryNode : public TypedNode<ShaderLibraryNodeConfig> {
protected:
    void Execute() override {
        auto* device = GetDevice();
        auto& mainCacher = CashSystem::MainCacher::Instance();
        
        // Get device-independent shader compilation cache
        auto* compilationCacher = mainCacher.GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
            typeid(CompiledShaderWrapper), device
        );
        
        // Get device-dependent shader module cache
        auto* moduleCacher = mainCacher.GetCacher<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
            typeid(ShaderModuleWrapper), device
        );
        
        if (compilationCacher && moduleCacher) {
            // Step 1: Compile shader (device-independent, cached globally)
            auto compiledShader = compilationCacher->GetOrCreateShaderModule(
                Config().sourcePath,
                Config().entryPoint,
                Config().macroDefinitions,
                Config().stage
            );
            
            // Step 2: Create module (device-dependent, cached per device)
            auto shaderModule = moduleCacher->GetOrCreateShaderModule(
                compiledShader->spirvCode,
                Config().shaderName,
                Config().stage
            );
            
            Out(ShaderModuleSlot) = shaderModule;
        }
    }
};
```

## Device-Independent Cacher Examples

### Shader Compilation Cacher
```cpp
class ShaderCompilationCacher : public TypedCacher<CompiledShaderWrapper, ShaderCompilationParams> {
protected:
    CompiledShaderWrapper::ResourcePtrT Create(const ShaderCompilationParams& params) override {
        // Compile GLSL/HLSL to SPIR-V (device-independent)
        return CompileShaderSource(params);
    }
    
    std::uint64_t ComputeKey(const ShaderCompilationParams& params) const override {
        // Hash based on source content and compilation settings
        return Hash(params.sourceChecksum + params.compilerVersion + params.compileFlags);
    }
    
    void OnInitialize() override {
        // No device initialization needed
    }
};
```

### File Hash Cacher
```cpp
class FileHashCacher : public TypedCacher<FileHashWrapper, FileHashParams> {
protected:
    FileHashWrapper::ResourcePtrT Create(const FileHashParams& params) override {
        // Compute hash of file content (device-independent)
        auto hash = ComputeFileHash(params.filePath, params.hashAlgorithm);
        auto lastModified = std::filesystem::last_write_time(params.filePath);
        
        return std::make_shared<FileHashWrapper>(hash, lastModified);
    }
    
    std::uint64_t ComputeKey(const FileHashParams& params) const override {
        // Hash based on file path and algorithm
        return Hash(params.filePath + params.hashAlgorithm);
    }
    
    void OnInitialize() override {
        // No device initialization needed
    }
};
```

## Performance Characteristics

### Device-Dependent Caching Performance
- **O(1)** lookup time within device registry
- **Device isolation**: No cross-device interference
- **Memory usage**: Proportional to number of active devices
- **Creation overhead**: Single creation per unique combination per device

### Device-Independent Caching Performance
- **O(1)** lookup time in global registry
- **Shared benefits**: One compilation serves all devices
- **Memory efficiency**: Single cache shared across devices
- **Network effects**: More devices = better cache hit rates

### Hybrid Performance Optimization
- **Device-independent first**: Check global cache before device-specific
- **Compile once, use everywhere**: Shader compilation cached globally
- **Device-specific optimization**: Pipeline state optimized per device
- **Automatic routing**: No manual cache management required

## Serialization Strategy

### Organized Storage
```
cache_directory/
├── devices/              # Device-dependent caches
│   ├── device_12345/    # Device-specific subdirectory
│   │   ├── pipelines/
│   │   ├── textures/
│   │   └── descriptors/
│   └── device_67890/
├── global/               # Device-independent caches
│   ├── shader_compilation/
│   ├── file_hashes/
│   └── configurations/
└── metadata/
    ├── device_registry.json
    └── cache_manifest.json
```

### Cross-Device Cache Sharing
```cpp
// Device-independent caches can be loaded once and shared
auto globalDir = cacheDir / "global";
auto* shaderCompilationCacher = mainCacher.GetDeviceIndependentCacher<ShaderCompilationCacher>(
    typeid(CompiledShaderWrapper)
);

// Load once, available to all devices
shaderCompilationCacher->DeserializeFromFile(globalDir / "shader_compilation.dat");
```

## Migration Guide

### From Single-Device Architecture

1. **Identify caching patterns**:
   ```cpp
   // Device-dependent: VkPipeline, VkImage, etc.
   // Device-independent: compiled shaders, file hashes, configs
   ```

2. **Update registration**:
   ```cpp
   // Before (assume device-dependent)
   mainCacher.RegisterCacher<ShaderModuleCacher>(typeid(ShaderModuleWrapper), "ShaderModule");
   
   // After (explicit dependency)
   mainCacher.RegisterCacher<ShaderModuleCacher>(typeid(ShaderModuleWrapper), "ShaderModule", true);   // device-dependent
   mainCacher.RegisterCacher<ShaderCompilationCacher>(typeid(CompiledShader), "ShaderCompilation", false); // device-independent
   ```

3. **Update cache usage**:
   ```cpp
   // Automatic routing handles both types
   auto* cache = mainCacher.GetCacher<SomeCacher>(typeid(SomeResource), device);
   ```

### Best Practices

### 1. Cache Type Classification
- **Ask**: "Does this resource depend on specific Vulkan device state?"
  - **Yes** → Device-dependent
  - **No** → Device-independent

### 2. Performance Optimization
- **Maximize device-independent caching**: Reduces per-device work
- **Minimize device-dependent overhead**: Only cache truly device-specific resources
- **Leverage shared benefits**: Device-independent caches improve with more devices

### 3. Memory Management
- **Monitor global cache size**: Device-independent caches grow with application scope
- **Clear device caches**: Remove caches for disconnected devices
- **Balance sharing**: Don't cache everything globally if memory is constrained

## Future Enhancements

### Planned Hybrid Features
1. **Cache migration**: Move device-independent caches between devices
2. **Cross-compilation**: Compile for multiple device types simultaneously
3. **Distributed caching**: Share device-independent caches across machines
4. **Smart classification**: Automatic detection of device dependency

### Advanced Optimization
1. **Predictive compilation**: Pre-compile shaders for likely device combinations
2. **Cache analytics**: Track hit rates for device-dependent vs independent
3. **Adaptive strategies**: Learn optimal caching strategies per resource type
4. **Compression**: Reduce device-independent cache storage requirements

## Troubleshooting

### Common Hybrid Issues

1. **Incorrect cache classification**
   ```cpp
   // Wrong: Caching device-specific resource as device-independent
   auto* pipelineCache = mainCacher.GetDeviceIndependentCacher<PipelineCacher>(...);  // Should be device-dependent
   
   // Right: Cache device-specific resources per device
   auto* pipelineCache = mainCacher.GetDeviceDependentCacher<PipelineCacher>(..., device);
   ```

2. **Missing device initialization**
   ```cpp
   // Device-dependent caches require device context
   auto* deviceCacher = mainCacher.GetCacher<PipelineCacher>(..., nullptr);  // Wrong: nullptr device
   auto* deviceCacher = mainCacher.GetCacher<PipelineCacher>(..., validDevice);  // Right
   ```

3. **Memory pressure from global caches**
   ```cpp
   // Monitor and clear global caches periodically
   auto stats = mainCacher.GetStats();
   if (stats.globalCaches > MAX_GLOBAL_CACHES) {
       mainCacher.ClearGlobalCaches();
   }
   ```

### Debug Features

- **Cache type visualization**: Show device-dependent vs independent cache distribution
- **Cross-device cache sharing**: Verify device-independent cache sharing
- **Performance comparison**: Compare hit rates between cache types
- **Memory profiling**: Analyze memory usage per cache type
- **Dependency analysis**: Show which resources are device-dependent