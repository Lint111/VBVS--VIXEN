# Phase A: Cacher Implementation Plan

**Created**: November 1, 2025
**Status**: Planning

## Overview

Phase A completes the caching infrastructure by implementing cachers for all cacheable resources. The infrastructure (MainCacher, TypedCacher, async I/O, device registry) is already complete. This phase focuses on identifying cacheable resources and implementing cachers following the established pattern.

## Existing Cachers ✅

1. **ShaderModuleCacher** - SPIR-V bytecode → VkShaderModule
2. **PipelineCacher** - Pipeline state → VkPipeline
3. **PipelineLayoutCacher** - Descriptor layouts + push constants → VkPipelineLayout
4. **DescriptorSetLayoutCacher** - Binding descriptions → VkDescriptorSetLayout

## Proposed Cachers (Priority Order)

### High Priority (P0) - Expensive Creation, High Reuse

#### 1. RenderPassCacher
**Node**: RenderPassNode
**Input**: Attachment formats, load/store ops, subpass dependencies
**Output**: VkRenderPass
**Key Benefits**:
- Expensive creation (driver validation)
- High reuse (same render pass used many times)
- Stable input (formats rarely change)

**Key Components**:
```cpp
struct RenderPassKey {
    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat;
    VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp;
    // Hash all fields
};
```

**Estimated Effort**: 4-6 hours
**Files to Create**:
- `CashSystem/include/CashSystem/RenderPassCacher.h`
- `CashSystem/src/RenderPassCacher.cpp`
- Update `cacher_registry.txt`

---

#### 2. TextureCacher (ImageCacher)
**Node**: TextureLoaderNode
**Input**: File path, format, mip levels
**Output**: VkImage + VkImageView + decoded pixel data
**Key Benefits**:
- Heavy I/O (file loading)
- Expensive decode (PNG, JPEG, KTX decompression)
- Large memory footprint (cache decoded pixels, not just VkImage)

**Key Components**:
```cpp
struct TextureKey {
    std::string filePath;
    VkFormat format;
    uint32_t mipLevels;
    // Hash file path + format
};

struct TextureWrapper {
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    std::vector<uint8_t> pixelData;  // Cache decoded data
    uint32_t width;
    uint32_t height;
};
```

**Estimated Effort**: 6-8 hours (complex due to pixel data caching)
**Files to Create**:
- `CashSystem/include/CashSystem/TextureCacher.h`
- `CashSystem/src/TextureCacher.cpp`

---

#### 3. MeshCacher (GeometryCacher)
**Node**: VertexBufferNode
**Input**: Mesh file path (or vertex/index data hash)
**Output**: VkBuffer (vertex + index) + staging data
**Key Benefits**:
- Heavy I/O (OBJ, GLTF parsing)
- Large binary data (cache parsed vertex/index arrays)
- Reusable across instances

**Key Components**:
```cpp
struct MeshKey {
    std::string filePath;
    // Or hash of vertex/index data if procedural
};

struct MeshWrapper {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    VkDeviceMemory vertexMemory;
    VkDeviceMemory indexMemory;
    std::vector<float> vertices;  // Cache parsed data
    std::vector<uint32_t> indices;
    uint32_t vertexCount;
    uint32_t indexCount;
};
```

**Estimated Effort**: 6-8 hours
**Files to Create**:
- `CashSystem/include/CashSystem/MeshCacher.h`
- `CashSystem/src/MeshCacher.cpp`

---

### Medium Priority (P1) - Moderate Benefit

#### 4. SamplerCacher
**Node**: TextureLoaderNode (currently creates samplers inline)
**Input**: Filter modes, address modes, anisotropy
**Output**: VkSampler
**Key Benefits**:
- Small resource but frequently reused
- Limited combinations (good cache hit rate)

**Key Components**:
```cpp
struct SamplerKey {
    VkFilter minFilter;
    VkFilter magFilter;
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
    float maxAnisotropy;
};
```

**Estimated Effort**: 3-4 hours
**Files to Create**:
- `CashSystem/include/CashSystem/SamplerCacher.h`
- `CashSystem/src/SamplerCacher.cpp`

---

#### 5. DescriptorPoolCacher
**Node**: DescriptorSetNode
**Input**: Pool sizes (UBO count, sampler count, etc.)
**Output**: VkDescriptorPool configuration (not the pool itself - just sizing strategy)
**Key Benefits**:
- Reusable pool sizing calculations
- Reduces redundant size computation

**Note**: VkDescriptorPool itself is NOT cached (lifetime tied to descriptor sets).
This cacher would store **optimal pool size configurations** based on shader reflection.

**Key Components**:
```cpp
struct DescriptorPoolSizeKey {
    std::vector<VkDescriptorPoolSize> poolSizes;
    uint32_t maxSets;
};

// Cache computes optimal sizing, doesn't create VkDescriptorPool
```

**Estimated Effort**: 4-5 hours
**Files to Create**:
- `CashSystem/include/CashSystem/DescriptorPoolSizingCache.h`
- `CashSystem/src/DescriptorPoolSizingCache.cpp`

---

### Low Priority (P2) - Limited Benefit or Complexity

#### 6. CommandPoolCacher
**Node**: CommandPoolNode
**Input**: Queue family index, flags
**Output**: VkCommandPool
**Benefits**: Low (cheap creation, device-specific, limited reuse)
**Verdict**: **SKIP** - Not worth caching

---

#### 7. FramebufferCacher
**Node**: FramebufferNode
**Input**: RenderPass, attachments (image views), dimensions
**Output**: VkFramebuffer
**Benefits**: Low (often dynamic per-swapchain-image, cheap creation)
**Verdict**: **SKIP** or **DEFER** - Framebuffers are typically per-image and recreated on resize

---

#### 8. SwapchainCacher
**Node**: SwapChainNode
**Benefits**: None (swapchain is window-dependent, cannot persist across runs)
**Verdict**: **SKIP**

---

## Implementation Pattern

Each cacher follows this 5-step pattern:

### 1. Create Header File
```cpp
// CashSystem/include/CashSystem/XyzCacher.h
#pragma once
#include "CashSystem/TypedCacher.h"

namespace CashSystem {

struct XyzKey {
    // Input parameters
    size_t Hash() const;
};

struct XyzWrapper {
    VkXyz handle;
    // Additional cached data
    void Cleanup(VkDevice device);
};

class XyzCacher : public TypedCacher<XyzWrapper> {
public:
    XyzCacher();
    virtual ~XyzCacher() override;

protected:
    std::string CreateKey(const XyzKey& params) override;
    XyzWrapper CreateResource(const XyzKey& params) override;
    virtual void Cleanup() override;
};

} // namespace CashSystem
```

### 2. Implement Source File
```cpp
// CashSystem/src/XyzCacher.cpp
#include "CashSystem/XyzCacher.h"

namespace CashSystem {

XyzCacher::XyzCacher() : TypedCacher("XyzCacher") {}

std::string XyzCacher::CreateKey(const XyzKey& params) {
    return std::to_string(params.Hash());
}

XyzWrapper XyzCacher::CreateResource(const XyzKey& params) {
    // Create VkXyz resource
    XyzWrapper wrapper;
    // ... Vulkan creation code
    return wrapper;
}

void XyzCacher::Cleanup() {
    for (auto& [key, wrapper] : cache) {
        wrapper.Cleanup(device->device);
    }
}

} // namespace CashSystem
```

### 3. Register in Manifest
```
# cacher_registry.txt
XyzCacher
```

### 4. Integrate in Node
```cpp
// In XyzNode::Compile()
auto& cacher = GetCacher<XyzCacher>();
XyzKey key{/* ... */};
XyzWrapper* wrapper = cacher.GetOrCreate(key);
vkXyz = wrapper->handle;
```

### 5. Add CMake Entry
```cmake
# CashSystem/CMakeLists.txt
src/XyzCacher.cpp
```

---

## Execution Plan

### Week 1: High Priority Cachers
- **Day 1**: RenderPassCacher (4-6 hours)
- **Day 2**: TextureCacher (6-8 hours)
- **Day 3**: MeshCacher (6-8 hours)
- **Day 4**: Testing & validation

### Week 2: Medium Priority Cachers (Optional)
- **Day 1**: SamplerCacher (3-4 hours)
- **Day 2**: DescriptorPoolSizingCache (4-5 hours)
- **Day 3**: Integration testing, cache metrics

### Post-Phase A: Enhancements
- Cache statistics dashboard
- Cache eviction policy (LRU)
- Cache warming strategies
- Persistent cache validation (checksum verification)

---

## Success Metrics

| Cacher | Target Hit Rate | Creation Time Saved | Priority |
|--------|----------------|---------------------|----------|
| RenderPassCacher | >90% | 5-10ms → <1ms | P0 |
| TextureCacher | >80% | 50-200ms → <5ms | P0 |
| MeshCacher | >70% | 20-100ms → <2ms | P0 |
| SamplerCacher | >95% | <1ms → <0.1ms | P1 |
| DescriptorPoolSizing | >85% | <1ms → <0.1ms | P1 |

---

## Dependencies

**Required Before Implementation**:
- ✅ MainCacher infrastructure
- ✅ TypedCacher template
- ✅ Device registry with stable IDs
- ✅ Async save/load system
- ✅ Manifest-based registration

**All dependencies met - ready to implement!**

---

## Files to Track

### New Files (Per Cacher)
- `CashSystem/include/CashSystem/{Name}Cacher.h`
- `CashSystem/src/{Name}Cacher.cpp`

### Modified Files (Per Cacher)
- `CashSystem/cacher_registry.txt` (add cacher name)
- `CashSystem/CMakeLists.txt` (add source file)
- `RenderGraph/src/Nodes/{Node}.cpp` (integrate cacher)

### Documentation
- This file (track progress)
- `memory-bank/activeContext.md` (update current focus)
- `memory-bank/progress.md` (mark Phase A complete when done)

---

## Next Steps

1. ✅ Create this planning document
2. ⏳ Implement RenderPassCacher (first P0 cacher)
3. ⏳ Implement TextureCacher
4. ⏳ Implement MeshCacher
5. ⏳ Implement SamplerCacher (if time permits)
6. ⏳ Testing & validation
7. ⏳ Update memory bank with Phase A completion
