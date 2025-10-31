# CashSystem Usage Guide

**Last Updated**: October 31, 2025

## Quick Start

### 1. Register Cachers (Application Initialization)

```cpp
#include <CashSystem/MainCacher.h>
#include <CashSystem/ShaderModuleCacher.h>
#include <CashSystem/PipelineCacher.h>
#include <CashSystem/PipelineLayoutCacher.h>

// Get MainCacher instance
auto& mainCacher = MainCacher::GetInstance();

// Register device-dependent cachers
mainCacher.RegisterCacher<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
    typeid(ShaderModuleWrapper),
    "ShaderModule",
    true  // Device-dependent
);

mainCacher.RegisterCacher<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
    typeid(PipelineWrapper),
    "Pipeline",
    true
);

mainCacher.RegisterCacher<PipelineLayoutCacher, PipelineLayoutWrapper, PipelineLayoutCreateParams>(
    typeid(PipelineLayoutWrapper),
    "PipelineLayout",
    true
);
```

### 2. Use Cachers in Nodes

```cpp
// GraphicsPipelineNode::Compile()
void GraphicsPipelineNode::Compile() {
    auto& mainCacher = MainCacher::GetInstance();
    auto* device = In(DeviceConfig::DEVICE);

    // Get cachers
    auto* shaderModuleCacher = mainCacher.GetCacher<ShaderModuleCacher,
                                                     ShaderModuleWrapper,
                                                     ShaderModuleCreateParams>(
        typeid(ShaderModuleWrapper), device
    );

    auto* layoutCacher = mainCacher.GetCacher<PipelineLayoutCacher,
                                               PipelineLayoutWrapper,
                                               PipelineLayoutCreateParams>(
        typeid(PipelineLayoutWrapper), device
    );

    auto* pipelineCacher = mainCacher.GetCacher<PipelineCacher,
                                                  PipelineWrapper,
                                                  PipelineCreateParams>(
        typeid(PipelineWrapper), device
    );

    // Create shader modules (cached)
    ShaderModuleCreateParams vertParams{vertexSpirv, device};
    auto vertModule = shaderModuleCacher->GetOrCreate(vertParams);
    // Console: "CACHE MISS: ShaderModule" (first call)
    // Console: "CACHE HIT: ShaderModule" (subsequent calls)

    // Create pipeline layout (cached)
    PipelineLayoutCreateParams layoutParams{descriptorSetLayout, pushConstants};
    auto pipelineLayout = layoutCacher->GetOrCreate(layoutParams);

    // Create pipeline (cached)
    PipelineCreateParams pipelineParams{
        .pipelineLayoutWrapper = pipelineLayout,
        .shaderStages = {vertModule, fragModule},
        .vertexInputState = vertexInputInfo
        // ... other pipeline state
    };
    auto pipeline = pipelineCacher->GetOrCreate(pipelineParams);

    // Store for rendering
    Out(PipelineConfig::PIPELINE, pipeline->pipeline);
}
```

### 3. Cleanup (Device Destruction)

```cpp
// DeviceNode::Cleanup()
void DeviceNode::Cleanup() {
    // Clear all device-dependent caches for this device
    MainCacher::GetInstance().ClearDeviceCaches(device);

    // Then destroy device
    vkDestroyDevice(device->device, nullptr);
}
```

## Common Patterns

### Pattern 1: ShaderModuleCacher

**Use Case**: Cache VkShaderModule from SPIR-V bytecode

```cpp
// Create params
ShaderModuleCreateParams params;
params.spirvCode = spirvBytecode;  // std::vector<uint32_t>
params.device = vulkanDevice;

// Get or create (returns shared_ptr)
auto wrapper = shaderModuleCacher->GetOrCreate(params);

// Use
VkShaderModule module = wrapper->shaderModule;
VkPipelineShaderStageCreateInfo stageInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = module,
    .pName = "main"
};
```

**Cache Key**: Hash of SPIR-V bytecode

**Lifetime**: Until device destroyed or `ClearDeviceCaches()` called

### Pattern 2: PipelineLayoutCacher (Transparent Mode)

**Use Case**: Share VkPipelineLayout across pipelines, transparent architecture

```cpp
// Mode 1: Explicit (recommended - transparent)
PipelineLayoutCreateParams layoutParams;
layoutParams.descriptorSetLayouts = {descriptorSetLayout};
layoutParams.pushConstantRanges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}};

auto layoutWrapper = layoutCacher->GetOrCreate(layoutParams);

// Use in pipeline params (explicit dependency tracking)
PipelineCreateParams pipelineParams;
pipelineParams.pipelineLayoutWrapper = layoutWrapper;  // Clear what's being used

// Mode 2: Convenience fallback
PipelineCreateParams pipelineParams;
pipelineParams.descriptorSetLayout = descriptorSetLayout;  // Cacher auto-creates layout
```

**Cache Key**: Hash of descriptor layouts + push constant ranges

**Design Philosophy**: Transparent - users see what's being cached

### Pattern 3: PipelineCacher

**Use Case**: Cache complete VkPipeline

```cpp
PipelineCreateParams params;
params.pipelineLayoutWrapper = layoutWrapper;  // From PipelineLayoutCacher
params.shaderStages = {vertModule, fragModule};  // From ShaderModuleCacher
params.vertexInputState = vertexInputInfo;
params.inputAssemblyState = inputAssemblyInfo;
params.viewportState = viewportInfo;
params.rasterizationState = rasterInfo;
params.multisampleState = multisampleInfo;
params.depthStencilState = depthStencilInfo;
params.colorBlendState = colorBlendInfo;
params.renderPass = renderPass;
params.subpass = 0;

auto wrapper = pipelineCacher->GetOrCreate(params);

// Use
VkPipeline pipeline = wrapper->pipeline;
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
```

**Cache Key**: Hash of all pipeline state

**Note**: Expensive creation, high cache hit value

## Cache Logging

**Console Output** (enabled by default):
```
CACHE MISS: ShaderModule (key: 0x1a2b3c4d)
CACHE HIT: ShaderModule (key: 0x1a2b3c4d)
CACHE MISS: PipelineLayout (key: 0x5e6f7890)
CACHE HIT: Pipeline (key: 0xabcdef12)
```

**Disable Logging** (future):
```cpp
shaderModuleCacher->SetLogging(false);
```

## Error Handling

### Cache Miss → Creation Failure

```cpp
auto wrapper = shaderModuleCacher->GetOrCreate(params);
if (!wrapper || wrapper->shaderModule == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to create shader module");
}
```

### Invalid Device

```cpp
auto* cacher = mainCacher.GetCacher<ShaderModuleCacher, ...>(typeIndex, nullptr);
if (!cacher) {
    throw std::runtime_error("Device required for device-dependent cacher");
}
```

## Performance Considerations

### Cache Hit Optimization

**Good** (maximizes cache hits):
```cpp
// Reuse same SPIR-V for multiple pipelines
auto vertModule = shaderModuleCacher->GetOrCreate({vertexSpirv, device});
auto pipeline1 = pipelineCacher->GetOrCreate({..., {vertModule, fragModule1}, ...});
auto pipeline2 = pipelineCacher->GetOrCreate({..., {vertModule, fragModule2}, ...});
// vertModule cached once, reused twice
```

**Bad** (cache misses):
```cpp
// Recompiling SPIR-V creates different bytecode each time
auto vertSpirv1 = CompileGLSL(vertexSource);
auto vertSpirv2 = CompileGLSL(vertexSource);  // Different bytecode!
auto module1 = shaderModuleCacher->GetOrCreate({vertSpirv1, device});  // MISS
auto module2 = shaderModuleCacher->GetOrCreate({vertSpirv2, device});  // MISS (different hash)
```

### Thread Safety

All cachers are thread-safe for concurrent reads and writes:
```cpp
// Thread 1
auto module1 = shaderModuleCacher->GetOrCreate(params1);

// Thread 2 (concurrent)
auto module2 = shaderModuleCacher->GetOrCreate(params2);
// Safe - internal mutex protects cache map
```

### Memory Usage

**Cache Growth**: Unlimited (no eviction policy yet)

**Mitigation**:
```cpp
// Manual cache clear
shaderModuleCacher->Clear();  // Removes all entries (calls Cleanup() first)
```

**Future**: LRU eviction policy

## Integration with RenderGraph

### DeviceNode Cleanup

```cpp
void DeviceNode::Cleanup() override {
    if (device) {
        // Clean all device-dependent caches BEFORE destroying device
        MainCacher::GetInstance().ClearDeviceCaches(device);

        vkDestroyDevice(device->device, nullptr);
        device = nullptr;
    }
}
```

### GraphicsPipelineNode Usage

```cpp
void GraphicsPipelineNode::Compile() override {
    auto& mainCacher = MainCacher::GetInstance();
    auto* device = In(DeviceConfig::DEVICE);

    // All cachers retrieved from MainCacher
    auto* shaderCacher = mainCacher.GetCacher<ShaderModuleCacher, ...>(..., device);
    auto* layoutCacher = mainCacher.GetCacher<PipelineLayoutCacher, ...>(..., device);
    auto* pipelineCacher = mainCacher.GetCacher<PipelineCacher, ...>(..., device);

    // Cache pipeline creation (logged CACHE HIT/MISS)
    auto pipeline = pipelineCacher->GetOrCreate(pipelineParams);

    Out(PipelineConfig::PIPELINE, pipeline->pipeline);
}
```

## Debugging

### Enable Cache Statistics (Future)

```cpp
auto stats = shaderModuleCacher->GetStatistics();
std::cout << "Cache hits: " << stats.hits << "\n";
std::cout << "Cache misses: " << stats.misses << "\n";
std::cout << "Hit rate: " << stats.GetHitRate() * 100 << "%\n";
```

### Verify Cache Keys

```cpp
size_t key1 = params1.ComputeHash();
size_t key2 = params2.ComputeHash();
if (key1 == key2) {
    std::cout << "Same cache key - will hit\n";
} else {
    std::cout << "Different keys - will miss\n";
}
```

## Best Practices

1. **Register cachers early** - During application initialization
2. **Reuse SPIR-V** - Compile once, cache the bytecode
3. **Explicit layouts** - Use `pipelineLayoutWrapper` for transparency
4. **Clean on device destruction** - Call `ClearDeviceCaches()`
5. **Check wrapper validity** - Verify `!= VK_NULL_HANDLE` after `GetOrCreate()`
6. **Thread-safe by default** - No manual locking needed

## Related

- [`01-architecture.md`](01-architecture.md) - System architecture
- [`memory-bank/systemPatterns.md`](../../memory-bank/systemPatterns.md) - Caching patterns
- [`RenderGraph/src/Nodes/GraphicsPipelineNode.cpp`](../../RenderGraph/src/Nodes/GraphicsPipelineNode.cpp) - Real usage example
