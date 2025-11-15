# CashSystem Consumer Analysis

**Date**: October 30, 2025
**Purpose**: Analyze current and future integration points for the CashSystem

## Overview

Two primary subsystems consume caching capabilities:
1. **RenderGraph** - Device-dependent Vulkan resource caching
2. **ShaderManagement** - Device-independent SPIR-V compilation caching

## Current State

### RenderGraph Integration (4 Nodes)

All CashSystem integration is **currently disabled** (commented out) with TODOs marking re-enablement points.

#### 1. DescriptorSetNode (`RenderGraph/src/Nodes/DescriptorSetNode.cpp:71-92`)
**Purpose**: Cache descriptor set layouts

**Commented Code Pattern**:
```cpp
// auto& mainCacher = CashSystem::MainCacher::Instance();
// auto* descriptorCacher = mainCacher.GetDescriptorCacher();
```

**Resource Type**: `VkDescriptorSetLayout`
**Device Dependency**: Device-dependent (Vulkan handle)
**Cache Key**: Descriptor bindings configuration

#### 2. GraphicsPipelineNode (`RenderGraph/src/Nodes/GraphicsPipelineNode.cpp:413-457`)
**Purpose**: Cache graphics pipelines

**Commented Code Pattern**:
```cpp
// auto& mainCacher = CashSystem::MainCacher::Instance();
// auto* pipelineCacher = mainCacher.GetPipelineCacher();
// auto cachedPipeline = pipelineCacher->GetOrCreatePipeline(...);
```

**Resource Type**: `VkPipeline` + `VkPipelineLayout` + `VkPipelineCache`
**Device Dependency**: Device-dependent
**Cache Key**: Shader keys + layout key + render pass key + state (depth, cull, polygon mode)

#### 3. ShaderLibraryNode (`RenderGraph/src/Nodes/ShaderLibraryNode.cpp:75-89`)
**Purpose**: Cache shader modules

**Commented Code Pattern**:
```cpp
// auto& mainCacher = CashSystem::MainCacher::Instance();
// auto* shaderModuleCacher = mainCacher.GetShaderModuleCacher();
// auto module = shaderModuleCacher->GetOrCreateShaderModule(...);
```

**Resource Type**: `VkShaderModule`
**Device Dependency**: Device-dependent (Vulkan handle wrapping SPIR-V)
**Cache Key**: Shader source path + entry point + stage

**Note**: Should integrate with ShaderManagement's compilation cache

#### 4. TextureLoaderNode (`RenderGraph/src/Nodes/TextureLoaderNode.cpp:89-125`)
**Purpose**: Cache loaded textures

**Commented Code Pattern**:
```cpp
// auto& mainCacher = CashSystem::MainCacher::Instance();
// auto* textureCacher = mainCacher.GetTextureCacher();
// auto cachedTexture = textureCacher->GetOrCreateTexture(...);
```

**Resource Type**: `VkImage` + `VkImageView` + `VkSampler` + `VkDeviceMemory`
**Device Dependency**: Device-dependent
**Cache Key**: File path + format + mipmap settings + filter modes + address mode

### ShaderManagement Integration

**Current State**: Has its own independent caching system
**Location**: `ShaderManagement/include/ShaderManagement/ShaderCacheManager.h`

#### ShaderCacheManager (Independent System)
**Purpose**: Persistent SPIR-V bytecode caching to disk

**Key Features**:
- **Device-agnostic**: Stores SPIR-V only (no Vulkan objects)
- **Content-addressable**: Cache keys = hash(source + stage + defines + entry point)
- **Thread-safe**: Internal synchronization
- **Persistent**: Disk-based storage with validation
- **LRU eviction**: Size-based cache management
- **Statistics**: Hit rate, bytes read/written tracking

**API**:
```cpp
class ShaderCacheManager {
    std::optional<std::vector<uint32_t>> Lookup(const std::string& cacheKey);
    bool Store(const std::string& cacheKey, const std::vector<uint32_t>& spirv);
    bool Contains(const std::string& cacheKey) const;
    ShaderCacheStats GetStatistics() const;
};

std::string GenerateCacheKey(
    const std::string& source,
    const std::filesystem::path& sourcePath,
    uint32_t stage,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const std::string& entryPoint
);
```

#### ShaderCompiler (Compilation System)
**Purpose**: GLSL → SPIR-V compilation using glslang

**Key Features**:
- Thread-safe stateless compilation
- Returns SPIR-V bytecode (no VkShaderModule creation)
- Validation and disassembly support
- Performance/size optimization modes

**API**:
```cpp
class ShaderCompiler {
    CompilationOutput Compile(
        ShaderStage stage,
        const std::string& source,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );

    CompilationOutput CompileFile(
        ShaderStage stage,
        const std::filesystem::path& filePath,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );
};
```

## Integration Strategy

### Two-Tier Caching Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
└────────────────────────────┬────────────────────────────────┘
                             │
         ┌───────────────────┴───────────────────┐
         │                                       │
         ▼                                       ▼
┌────────────────────────┐           ┌─────────────────────────┐
│  RenderGraph Nodes     │           │  ShaderManagement       │
│  (Device-Dependent)    │           │  (Device-Independent)   │
├────────────────────────┤           ├─────────────────────────┤
│ • DescriptorSetNode    │           │ • ShaderCacheManager    │
│ • GraphicsPipelineNode │           │ • ShaderCompiler        │
│ • ShaderLibraryNode    │           │ • ShaderLibrary         │
│ • TextureLoaderNode    │           └─────────┬───────────────┘
└───────────┬────────────┘                     │
            │                                  │
            ▼                                  ▼
┌─────────────────────────────────────────────────────────────┐
│              CashSystem::MainCacher (Unified)                │
├─────────────────────────────────────────────────────────────┤
│  Device-Dependent Registry         Device-Independent Cache  │
│  ┌──────────────────────────┐     ┌──────────────────────┐  │
│  │ Per-Device Isolation:    │     │ Global Shared Cache: │  │
│  │ • PipelineCacher         │     │ • ShaderCompilation  │  │
│  │ • ShaderModuleCacher     │     │   Cacher             │  │
│  │ • TextureCacher          │     │                      │  │
│  │ • DescriptorCacher       │     │ (SPIR-V bytecode)    │  │
│  └──────────────────────────┘     └──────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
            │                                  │
            ▼                                  ▼
     VkPipeline, etc.                    .spv files
```

### Proposed Integration Layers

#### Layer 1: Device-Independent SPIR-V Compilation
**Owner**: ShaderManagement
**Consumer**: RenderGraph (ShaderLibraryNode)

```cpp
// ShaderManagement owns compilation and SPIR-V caching
class ShaderCacheManager {
    // Existing implementation - no changes needed
    std::optional<std::vector<uint32_t>> Lookup(const std::string& cacheKey);
    bool Store(const std::string& cacheKey, const std::vector<uint32_t>& spirv);
};

// Option A: Keep independent (current)
// ShaderManagement continues managing its own cache
// Pros: Already working, no dependencies
// Cons: Separate from CashSystem, no unified management

// Option B: Integrate with CashSystem (proposed)
// Wrap ShaderCacheManager in ShaderCompilationCacher
class ShaderCompilationCacher : public TypedCacher<CompiledShaderWrapper, ShaderCompilationParams> {
    // Delegates to ShaderManagement::ShaderCacheManager
    std::shared_ptr<ShaderManagement::ShaderCacheManager> m_internalCache;
};

// Register as device-independent
mainCacher.RegisterCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
    typeid(CompiledShaderWrapper), "ShaderCompilation", false  // device-independent
);
```

#### Layer 2: Device-Dependent Vulkan Resources
**Owner**: CashSystem
**Consumers**: RenderGraph nodes

```cpp
// 1. ShaderModuleCacher - VkShaderModule from SPIR-V
class ShaderModuleCacher : public TypedCacher<ShaderModuleWrapper, ShaderModuleCreateParams> {
    // Takes SPIR-V (from Layer 1), creates VkShaderModule per device
    std::shared_ptr<ShaderModuleWrapper> Create(const ShaderModuleCreateParams& ci) override;
};

// 2. PipelineCacher - VkPipeline from shader modules + state
class PipelineCacher : public TypedCacher<PipelineWrapper, PipelineCreateParams> {
    // Creates VkPipeline from VkShaderModules + pipeline state
    std::shared_ptr<PipelineWrapper> Create(const PipelineCreateParams& ci) override;
};

// 3. TextureCacher - VkImage + VkImageView + VkSampler
class TextureCacher : public TypedCacher<TextureWrapper, TextureCreateParams> {
    // Loads image file, creates Vulkan texture resources
    std::shared_ptr<TextureWrapper> Create(const TextureCreateParams& ci) override;
};

// 4. DescriptorCacher - VkDescriptorSetLayout
class DescriptorCacher : public TypedCacher<DescriptorWrapper, DescriptorCreateParams> {
    // Creates descriptor set layouts
    std::shared_ptr<DescriptorWrapper> Create(const DescriptorCreateParams& ci) override;
};
```

## Integration Implementation Plan

### Phase 1: Registration Setup (CURRENT)
✅ Core registration API complete
✅ Test suite passing
- Create centralized registration function

### Phase 2: RenderGraph Integration (NEXT)
**Priority**: HIGH - Main blocker for production use

**Steps**:
1. Create registration initialization in application startup
2. Uncomment and update DescriptorSetNode
3. Uncomment and update GraphicsPipelineNode
4. Uncomment and update ShaderLibraryNode
5. Uncomment and update TextureLoaderNode
6. Test with actual RenderGraph execution

**Migration Pattern**:
```cpp
// OLD (commented out)
// auto* cacher = mainCacher.GetPipelineCacher();

// NEW (registration-based)
auto* cacher = mainCacher.GetCacher<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
    typeid(PipelineWrapper), device
);
```

### Phase 3: ShaderManagement Integration (OPTIONAL)
**Priority**: MEDIUM - Nice to have for unified management

**Decision Required**:
- **Option A**: Keep ShaderManagement cache independent (simpler, working)
- **Option B**: Integrate with CashSystem (unified, more complex)

**Recommendation**: Option A for now
- ShaderCacheManager is well-designed and working
- Adding CashSystem integration adds complexity without clear benefit
- Can integrate later if unified statistics/management becomes important

### Phase 4: Advanced Features (FUTURE)
- Persistent disk caching for device-dependent resources
- Cache warming strategies
- Multi-threaded cache access optimization
- Cache statistics dashboard

## Key Design Decisions

### 1. Device Dependency Classification
| Resource Type | Device-Dependent? | Rationale |
|--------------|-------------------|-----------|
| SPIR-V Bytecode | ❌ No | Device-agnostic, can be shared |
| VkShaderModule | ✅ Yes | Vulkan handle, per-device |
| VkPipeline | ✅ Yes | Device-specific state |
| VkImage/VkImageView | ✅ Yes | Device memory allocation |
| VkDescriptorSetLayout | ✅ Yes | Vulkan handle, per-device |

### 2. Cache Key Generation
**Device-Independent** (SPIR-V):
- Hash(source code + preprocessor defines + entry point + compiler version + stage)
- Managed by ShaderManagement::GenerateCacheKey()

**Device-Dependent** (Vulkan Resources):
- Hash(input handles + creation parameters + state)
- Managed by each TypedCacher::ComputeKey()

### 3. Thread Safety
- **MainCacher**: Thread-safe (shared_mutex for registration, device registries)
- **DeviceRegistry**: Thread-safe (per-device isolation)
- **ShaderCacheManager**: Thread-safe (internal mutex)
- **Individual Cachers**: Thread-safe through parent registry

### 4. Lifecycle Management
**Initialization Order**:
1. MainCacher singleton created
2. Register all cacher types (application startup)
3. Create device-specific registries on first device access
4. Create global cachers on first non-device access

**Cleanup Order**:
1. Application shutdown
2. DeviceRegistry destructors clean device-specific resources
3. Global cachers destructors clean shared resources
4. MainCacher singleton cleanup (automatic)

## Testing Strategy

### Unit Tests
- ✅ Registration API (7 tests passing)
- ⏹️ Device-dependent cacher creation (deferred)
- ⏹️ Device-independent cacher creation (deferred)
- ⏹️ Multi-device isolation (deferred)
- ⏹️ Cache hit/miss scenarios (deferred)

### Integration Tests
- ⏹️ RenderGraph node execution with caching
- ⏹️ ShaderManagement compilation with caching
- ⏹️ Multi-threaded cache access
- ⏹️ Cache persistence across runs

### Performance Tests
- ⏹️ Cache lookup latency
- ⏹️ Memory usage with large caches
- ⏹️ Multi-device overhead
- ⏹️ Thread contention under load

## Open Questions

1. **ShaderManagement Integration**: Keep independent or integrate with CashSystem?
   - **Recommendation**: Keep independent (simpler, working)

2. **Persistent Device-Dependent Caching**: Store VkPipeline bytecode to disk?
   - **Recommendation**: Future enhancement, VkPipelineCache already handles this

3. **Centralized Statistics**: Unified dashboard for all caches?
   - **Recommendation**: Future enhancement, useful for debugging

4. **Hot Reload**: Support runtime shader recompilation?
   - **Recommendation**: ShaderLibrary already supports this, no CashSystem changes needed

## References

- **Architecture**: `documentation/CashSystem-Integration.md`
- **Migration Status**: `documentation/CashSystem-Migration-Status.md`
- **Main Header**: `CashSystem/include/CashSystem/MainCacher.h`
- **ShaderManagement**: `ShaderManagement/include/ShaderManagement/ShaderCacheManager.h`
- **RenderGraph Nodes**: `RenderGraph/src/Nodes/*.cpp`
