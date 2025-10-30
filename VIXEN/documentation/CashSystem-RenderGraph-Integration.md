# CashSystem RenderGraph Integration

**Date**: October 30, 2025
**Status**: ✅ Complete - Localized Registration Pattern Implemented

## Summary

Successfully integrated CashSystem with RenderGraph using a decentralized, localized registration pattern. Nodes independently register cachers during their `Compile()` phase, with idempotent registration ensuring no conflicts when multiple nodes use the same cacher type.

## Integration Architecture

### Graph-Level Access Pattern

RenderGraph provides centralized access to MainCacher through a public method:

```cpp
// RenderGraph.h
class RenderGraph {
public:
    /**
     * @brief Get the main cacher instance (for nodes to register and access caches)
     *
     * Nodes can use this to register cachers during Setup/Compile and access them.
     * Registration is idempotent - multiple nodes can call RegisterCacher for the same type.
     */
    CashSystem::MainCacher& GetMainCacher() {
        return CashSystem::MainCacher::Instance();
    }
};
```

Nodes access MainCacher through their parent graph:
- `GetOwningGraph()->GetMainCacher()` - same pattern as `GetTime()`, `GetMessageBus()`, etc.
- No global singletons accessed directly from nodes
- Clean dependency injection through graph ownership

### Localized Registration Pattern

**Key Principle**: Each node independently registers the cachers it needs during `Compile()`.

**Benefits**:
- ✅ **No centralized initialization** - No single registration function needed
- ✅ **Self-contained nodes** - Nodes don't depend on external setup
- ✅ **Idempotent registration** - Safe for multiple nodes to register same type
- ✅ **No inter-node dependencies** - Nodes don't need to know about each other
- ✅ **Lazy initialization** - Cachers only registered when nodes that need them compile

**Pattern**:
```cpp
void SomeNode::Compile() {
    // 1. Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // 2. Register cacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::SomeWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::SomeCacher,
            CashSystem::SomeWrapper,
            CashSystem::SomeCreateParams
        >(
            typeid(CashSystem::SomeWrapper),
            "SomeName",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("SomeNode: Registered SomeCacher");
    }

    // 3. Get the cacher for this device
    auto* cacher = mainCacher.GetCacher<
        CashSystem::SomeCacher,
        CashSystem::SomeWrapper,
        CashSystem::SomeCreateParams
    >(typeid(CashSystem::SomeWrapper), device);

    // 4. Use the cacher
    if (cacher) {
        // Cache operations here
    }
}
```

## Integrated Nodes

### 1. ShaderLibraryNode
**File**: `RenderGraph/src/Nodes/ShaderLibraryNode.cpp:72-116`

**Registers**: `ShaderModuleCacher` (device-dependent)
**Resource Type**: `VkShaderModule`
**Purpose**: Cache shader modules created from SPIR-V bytecode

```cpp
// Register ShaderModuleCacher
if (!mainCacher.IsRegistered(typeid(CashSystem::ShaderModuleWrapper))) {
    mainCacher.RegisterCacher<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(typeid(CashSystem::ShaderModuleWrapper), "ShaderModule", true);
}
```

**Status**: ✅ Integrated, compilation ready (actual caching TODO)

### 2. GraphicsPipelineNode
**File**: `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp:413-459`

**Registers**: `PipelineCacher` (device-dependent)
**Resource Type**: `VkPipeline` + `VkPipelineLayout` + `VkPipelineCache`
**Purpose**: Cache graphics pipelines with associated layouts

```cpp
// Register PipelineCacher
if (!mainCacher.IsRegistered(typeid(CashSystem::PipelineWrapper))) {
    mainCacher.RegisterCacher<
        CashSystem::PipelineCacher,
        CashSystem::PipelineWrapper,
        CashSystem::PipelineCreateParams
    >(typeid(CashSystem::PipelineWrapper), "Pipeline", true);
}
```

**Status**: ✅ Integrated, compilation ready (actual caching TODO)

### 3. DescriptorSetNode
**File**: `RenderGraph/src/Nodes/DescriptorSetNode.cpp:68-103`

**Registers**: `DescriptorCacher` (device-dependent)
**Resource Type**: `VkDescriptorSetLayout`
**Purpose**: Cache descriptor set layouts

```cpp
// Register DescriptorCacher
if (!mainCacher.IsRegistered(typeid(CashSystem::DescriptorWrapper))) {
    mainCacher.RegisterCacher<
        CashSystem::DescriptorCacher,
        CashSystem::DescriptorWrapper,
        CashSystem::DescriptorCreateParams
    >(typeid(CashSystem::DescriptorWrapper), "Descriptor", true);
}
```

**Status**: ✅ Integrated, compilation ready (actual caching TODO)

### 4. TextureLoaderNode
**File**: `RenderGraph/src/Nodes/TextureLoaderNode.cpp:89-122`

**Registers**: `TextureCacher` (device-dependent)
**Resource Type**: `VkImage` + `VkImageView` + `VkSampler` + `VkDeviceMemory`
**Purpose**: Cache loaded and processed textures

```cpp
// Register TextureCacher
if (!mainCacher.IsRegistered(typeid(CashSystem::TextureWrapper))) {
    mainCacher.RegisterCacher<
        CashSystem::TextureCacher,
        CashSystem::TextureWrapper,
        CashSystem::TextureCreateParams
    >(typeid(CashSystem::TextureWrapper), "Texture", true);
}
```

**Status**: ✅ Integrated, compilation ready (actual caching TODO)

## Registration Flow

### Scenario: Graph with Multiple Nodes

```
Graph Construction:
  └─> Add ShaderLibraryNode ("ShaderLib1")
  └─> Add ShaderLibraryNode ("ShaderLib2")
  └─> Add GraphicsPipelineNode ("Pipeline1")
  └─> Add TextureLoaderNode ("Texture1")

Graph Compilation:
  └─> Compile ShaderLib1
      ├─> GetMainCacher()
      ├─> Check: IsRegistered(ShaderModuleWrapper)? NO
      ├─> RegisterCacher(ShaderModuleCacher) ✅ REGISTERED
      └─> GetCacher(ShaderModuleWrapper, device) → cacher instance

  └─> Compile ShaderLib2
      ├─> GetMainCacher()
      ├─> Check: IsRegistered(ShaderModuleWrapper)? YES
      ├─> Skip registration (already registered)
      └─> GetCacher(ShaderModuleWrapper, device) → same cacher instance

  └─> Compile Pipeline1
      ├─> GetMainCacher()
      ├─> Check: IsRegistered(PipelineWrapper)? NO
      ├─> RegisterCacher(PipelineCacher) ✅ REGISTERED
      └─> GetCacher(PipelineWrapper, device) → cacher instance

  └─> Compile Texture1
      ├─> GetMainCacher()
      ├─> Check: IsRegistered(TextureWrapper)? NO
      ├─> RegisterCacher(TextureCacher) ✅ REGISTERED
      └─> GetCacher(TextureWrapper, device) → cacher instance

Result:
  MainCacher registered types:
    - ShaderModuleWrapper → ShaderModuleCacher (device-dependent)
    - PipelineWrapper → PipelineCacher (device-dependent)
    - TextureWrapper → TextureCacher (device-dependent)
```

## Build Status

**RenderGraph**: ✅ Compiles cleanly
**All Integration Points**: ✅ Building successfully
**Includes**: ✅ All cacher headers added
**Zero Errors**: ✅ Clean build

### Modified Files
1. `RenderGraph/include/Core/RenderGraph.h` - Added `GetMainCacher()` and `#include "CashSystem/MainCacher.h"`
2. `RenderGraph/src/Nodes/ShaderLibraryNode.cpp` - Localized registration pattern
3. `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp` - Localized registration pattern
4. `RenderGraph/src/Nodes/DescriptorSetNode.cpp` - Localized registration pattern
5. `RenderGraph/src/Nodes/TextureLoaderNode.cpp` - Localized registration pattern

## Next Steps

### Phase 1: Implement Actual Caching (Per Node)
Each node currently has the infrastructure but needs implementation:

**ShaderLibraryNode**:
```cpp
// TODO: Implement shader module creation from SPIR-V
CashSystem::ShaderModuleCreateParams params;
params.spirvCode = compiledSpirv;
params.entryPoint = "main";
params.stage = VK_SHADER_STAGE_VERTEX_BIT;

auto shaderModule = shaderModuleCacher->GetOrCreate(params);
```

**GraphicsPipelineNode**:
```cpp
// TODO: Gather pipeline parameters
CashSystem::PipelineCreateParams params;
params.vertexShader = vertexShaderModule;
params.fragmentShader = fragmentShaderModule;
params.renderPass = renderPass;
params.descriptorSetLayout = descriptorLayout;
params.enableDepthTest = enableDepthTest;
params.cullMode = cullMode;
params.polygonMode = polygonMode;

auto pipeline = pipelineCacher->GetOrCreate(params);
if (pipeline) {
    this->pipeline = pipeline->pipeline;
    this->pipelineLayout = pipeline->layout;
    // Use cached pipeline
}
```

**DescriptorSetNode**:
```cpp
// TODO: Create descriptor layout spec
CashSystem::DescriptorCreateParams params;
params.bindings = GetDescriptorBindings();

auto descriptor = descriptorCacher->GetOrCreate(params);
```

**TextureLoaderNode**:
```cpp
// TODO: Create texture parameters
CashSystem::TextureCreateParams params;
params.filePath = filePath;
params.format = VK_FORMAT_R8G8B8A8_UNORM;
params.generateMipmaps = generateMipmaps;
params.minFilter = VK_FILTER_LINEAR;
params.magFilter = VK_FILTER_LINEAR;
params.addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

auto texture = textureCacher->GetOrCreate(params);
if (texture) {
    this->textureImage = texture->image;
    this->textureView = texture->view;
    this->textureSampler = texture->sampler;
    // Use cached texture
}
```

### Phase 2: Implement Cacher Create() Methods
Each cacher needs its `Create()` method implemented in `CashSystem/src/`:

1. `ShaderModuleCacher::Create()` - Create VkShaderModule from SPIR-V
2. `PipelineCacher::Create()` - Create VkPipeline + VkPipelineLayout
3. `DescriptorCacher::Create()` - Create VkDescriptorSetLayout
4. `TextureCacher::Create()` - Load and create VkImage + VkImageView + VkSampler

### Phase 3: Testing
- Unit tests for each cacher's Create() method
- Integration tests with actual RenderGraph execution
- Verify cache hits/misses
- Performance benchmarks

## Design Validation

### ✅ Goals Achieved

1. **Decentralized Registration**: No centralized initialization function needed
2. **Idempotent**: Multiple nodes can safely register same cacher type
3. **Self-Contained**: Each node manages its own cacher requirements
4. **Graph Integration**: Clean access through `GetOwningGraph()->GetMainCacher()`
5. **Zero Conflicts**: Nodes don't need to coordinate registration
6. **Lazy Initialization**: Cachers only created when first node needs them
7. **Clean Build**: All integration points compile successfully

### Design Trade-offs

**Chosen Approach**: Localized node-level registration
- ✅ Self-contained nodes
- ✅ No global coordination needed
- ✅ Scales with arbitrary node types
- ✅ Clear ownership (node registers what it uses)
- ⚠️ Registration code duplicated in each node (acceptable)

**Alternative (Not Chosen)**: Centralized registration function
- ❌ Single point of failure
- ❌ Nodes depend on external setup
- ❌ Hard to maintain as node types grow
- ❌ Violates separation of concerns
- ✅ Single location for all registrations

## References

- **Architecture**: `documentation/CashSystem-Integration.md`
- **Consumer Analysis**: `documentation/CashSystem-Consumer-Analysis.md`
- **Migration Status**: `documentation/CashSystem-Migration-Status.md`
- **Main Header**: `CashSystem/include/CashSystem/MainCacher.h`
- **Test Suite**: `tests/CashSystem/test_registration_api.cpp`
