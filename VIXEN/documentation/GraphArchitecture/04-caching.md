# Caching System

## Overview

The Render Graph employs a multi-level caching strategy to maximize pipeline reuse, reduce compilation time, and minimize resource allocation overhead.

---

## Multi-Level Cache Strategy

### Cache Levels

1. **Pipeline Cache** (VkPipelineCache)
   - Shader modules
   - Pipeline state objects
   - Persistent across application runs
   - Highest performance impact

2. **Descriptor Set Cache**
   - Reuse descriptor sets with identical layouts
   - Track resource bindings
   - Invalidate on resource changes
   - Pool-based allocation

3. **Resource Cache**
   - Rendered outputs from previous frames
   - Static geometry buffers
   - Pre-processed textures
   - LRU eviction policy

---

## Pipeline Cache (VkPipelineCache)

### Implementation

```cpp
class PipelineCache {
public:
    PipelineCache(VulkanDevice* device, const std::string& cacheFilePath)
        : device(device), cacheFilePath(cacheFilePath) {
        LoadFromFile();
    }

    ~PipelineCache() {
        SaveToFile();
        vkDestroyPipelineCache(device->device, vkPipelineCache, nullptr);
    }

    VkPipeline GetOrCreate(const PipelineDescription& desc) {
        uint64_t key = HashPipelineDescription(desc);

        if (auto it = cache.find(key); it != cache.end()) {
            cacheHits++;
            return it->second;
        }

        // Create new pipeline using VkPipelineCache
        VkPipeline pipeline = CreatePipeline(desc, vkPipelineCache);
        cache[key] = pipeline;
        cacheMisses++;

        return pipeline;
    }

    void SaveToFile() {
        size_t dataSize;
        vkGetPipelineCacheData(device->device, vkPipelineCache, &dataSize, nullptr);

        std::vector<uint8_t> data(dataSize);
        vkGetPipelineCacheData(device->device, vkPipelineCache, &dataSize, data.data());

        WriteToFile(cacheFilePath, data);
    }

    void LoadFromFile() {
        if (!FileExists(cacheFilePath)) {
            CreateEmptyCache();
            return;
        }

        auto data = ReadFile(cacheFilePath);

        VkPipelineCacheCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        createInfo.initialDataSize = data.size();
        createInfo.pInitialData = data.data();

        vkCreatePipelineCache(device->device, &createInfo, nullptr, &vkPipelineCache);
    }

    float GetHitRate() const {
        if (cacheHits + cacheMisses == 0) return 0.0f;
        return static_cast<float>(cacheHits) / (cacheHits + cacheMisses);
    }

private:
    VulkanDevice* device;
    std::string cacheFilePath;
    VkPipelineCache vkPipelineCache;
    std::map<uint64_t, VkPipeline> cache;

    uint32_t cacheHits = 0;
    uint32_t cacheMisses = 0;

    void CreateEmptyCache() {
        VkPipelineCacheCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        vkCreatePipelineCache(device->device, &createInfo, nullptr, &vkPipelineCache);
    }
};
```

### Pipeline Cache Key Generation

Hash all aspects of pipeline state:

```cpp
uint64_t HashPipelineDescription(const PipelineDescription& desc) {
    uint64_t hash = 0;

    // Shader stages
    for (const auto& stage : desc.shaderStages) {
        hash = CombineHash(hash, HashShaderModule(stage.module));
        hash = CombineHash(hash, stage.stage);
    }

    // Vertex input
    hash = CombineHash(hash, HashVertexInput(desc.vertexInputState));

    // Viewport/scissor
    hash = CombineHash(hash, HashViewportState(desc.viewportState));

    // Rasterization
    hash = CombineHash(hash, HashRasterizationState(desc.rasterizationState));

    // Depth/stencil
    hash = CombineHash(hash, HashDepthStencilState(desc.depthStencilState));

    // Color blend
    hash = CombineHash(hash, HashColorBlendState(desc.colorBlendState));

    // Dynamic state
    for (auto dynamicState : desc.dynamicStates) {
        hash = CombineHash(hash, dynamicState);
    }

    // Render pass compatibility
    hash = CombineHash(hash, desc.renderPassFormat);
    hash = CombineHash(hash, desc.subpass);

    return hash;
}
```

### Benefits

- **Cold start optimization**: Load pre-compiled pipelines from disk
- **Runtime reuse**: Share pipelines across instances of same type
- **Reduced stuttering**: Avoid pipeline compilation during gameplay

**Target:** > 90% cache hit rate for stable scenes

---

## Descriptor Set Cache

### Descriptor Pool Management

```cpp
class DescriptorSetCache {
public:
    DescriptorSetCache(VulkanDevice* device, uint32_t maxSets = 1000)
        : device(device), maxSets(maxSets) {
        CreatePool();
    }

    VkDescriptorSet Allocate(const DescriptorSetLayout& layout,
                             const std::vector<ResourceBinding>& bindings) {
        uint64_t key = HashDescriptorBindings(layout, bindings);

        // Check cache
        if (auto it = cache.find(key); it != cache.end()) {
            // Found cached descriptor set with identical bindings
            return it->second;
        }

        // Allocate new descriptor set
        VkDescriptorSet descriptorSet = AllocateFromPool(layout);
        UpdateDescriptorSet(descriptorSet, bindings);

        cache[key] = descriptorSet;
        return descriptorSet;
    }

    void Reset() {
        // Reset pool, invalidating all allocated descriptor sets
        vkResetDescriptorPool(device->device, descriptorPool, 0);
        cache.clear();
    }

private:
    VulkanDevice* device;
    uint32_t maxSets;
    VkDescriptorPool descriptorPool;
    std::map<uint64_t, VkDescriptorSet> cache;

    void CreatePool() {
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 10 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 10 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 5 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets * 5 }
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;

        vkCreateDescriptorPool(device->device, &poolInfo, nullptr, &descriptorPool);
    }

    uint64_t HashDescriptorBindings(const DescriptorSetLayout& layout,
                                    const std::vector<ResourceBinding>& bindings) {
        uint64_t hash = HashDescriptorSetLayout(layout);

        for (const auto& binding : bindings) {
            hash = CombineHash(hash, binding.binding);
            hash = CombineHash(hash, reinterpret_cast<uint64_t>(binding.resource));
        }

        return hash;
    }
};
```

### Invalidation Strategy

Descriptor sets must be invalidated when resources change:

```cpp
class DescriptorSetCache {
    void InvalidateResource(const Resource* resource) {
        // Find all descriptor sets using this resource
        for (auto it = cache.begin(); it != cache.end(); ) {
            if (DescriptorSetUsesResource(it->second, resource)) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    void InvalidateAll() {
        Reset();
    }
};
```

---

## Resource Output Cache

### Cacheable Node Outputs

Some node outputs can be cached if inputs are unchanged:

```cpp
class ResourceCache {
public:
    struct CacheEntry {
        uint64_t key;
        Resource resource;
        std::chrono::steady_clock::time_point lastAccess;
        size_t memorySize;
        uint32_t accessCount;
    };

    std::optional<Resource> Get(uint64_t cacheKey) {
        if (auto it = cache.find(cacheKey); it != cache.end()) {
            it->second.lastAccess = std::chrono::steady_clock::now();
            it->second.accessCount++;
            return it->second.resource;
        }
        return std::nullopt;
    }

    void Store(uint64_t cacheKey, const Resource& resource, size_t memorySize) {
        // Check if we need to evict
        while (currentMemoryUsage + memorySize > maxMemoryUsage && !cache.empty()) {
            EvictLRU();
        }

        CacheEntry entry;
        entry.key = cacheKey;
        entry.resource = resource;
        entry.lastAccess = std::chrono::steady_clock::now();
        entry.memorySize = memorySize;
        entry.accessCount = 1;

        cache[cacheKey] = entry;
        currentMemoryUsage += memorySize;
    }

    void Invalidate(uint64_t cacheKey) {
        if (auto it = cache.find(cacheKey); it != cache.end()) {
            currentMemoryUsage -= it->second.memorySize;
            DestroyResource(it->second.resource);
            cache.erase(it);
        }
    }

private:
    std::map<uint64_t, CacheEntry> cache;
    size_t maxMemoryUsage;
    size_t currentMemoryUsage = 0;

    void EvictLRU() {
        auto oldest = std::min_element(cache.begin(), cache.end(),
            [](const auto& a, const auto& b) {
                return a.second.lastAccess < b.second.lastAccess;
            });

        if (oldest != cache.end()) {
            currentMemoryUsage -= oldest->second.memorySize;
            DestroyResource(oldest->second.resource);
            cache.erase(oldest);
        }
    }
};
```

### Node-Level Caching

```cpp
class NodeInstance {
    bool IsCacheable() const {
        // Node is cacheable if:
        // 1. All inputs are constant or from cacheable nodes
        // 2. Node has no side effects (pure function)
        // 3. Output is deterministic
        return cacheable && AllInputsConstantOrCached();
    }

    uint64_t GenerateCacheKey() const {
        uint64_t key = HashNodeType(nodeType->typeId);

        // Hash all inputs
        for (const auto& input : inputs) {
            key = CombineHash(key, HashResource(input));
        }

        // Hash parameters
        for (const auto& [name, value] : parameters) {
            key = CombineHash(key, HashParameter(name, value));
        }

        return key;
    }
};
```

---

## Cache Invalidation

### Triggers

Cache invalidation occurs when:

1. **Input Resource Modification**
   - Geometry buffer updated
   - Texture changed
   - Uniform values modified

2. **Node Parameter Change**
   - Resolution changed
   - Filter radius modified
   - Render state altered

3. **Explicit Invalidation Request**
   - User requests fresh render
   - Scene reset

4. **LRU Eviction**
   - Memory pressure
   - Cache size limits exceeded

### Invalidation Propagation

```cpp
class RenderGraph {
    void InvalidateNode(NodeInstance* node) {
        // Invalidate this node's cache
        resourceCache.Invalidate(node->cacheKey);

        // Propagate to dependents
        for (auto* dependent : node->dependents) {
            if (dependent->IsCacheable()) {
                InvalidateNode(dependent);
            }
        }
    }

    void InvalidateResource(const Resource* resource) {
        // Find all nodes using this resource
        for (auto& node : instances) {
            if (node->UsesResource(resource)) {
                InvalidateNode(node.get());
            }
        }
    }
};
```

---

## Cache Statistics and Monitoring

### Metrics

```cpp
struct CacheStatistics {
    // Pipeline cache
    uint32_t pipelineCacheHits;
    uint32_t pipelineCacheMisses;
    float pipelineHitRate;

    // Descriptor cache
    uint32_t descriptorCacheHits;
    uint32_t descriptorCacheMisses;
    float descriptorHitRate;

    // Resource cache
    uint32_t resourceCacheHits;
    uint32_t resourceCacheMisses;
    float resourceHitRate;
    size_t resourceCacheMemoryUsage;

    // Overall
    float overallHitRate;
};

class CacheManager {
    CacheStatistics GetStatistics() const {
        CacheStatistics stats;

        stats.pipelineCacheHits = pipelineCache.GetHits();
        stats.pipelineCacheMisses = pipelineCache.GetMisses();
        stats.pipelineHitRate = pipelineCache.GetHitRate();

        stats.descriptorCacheHits = descriptorCache.GetHits();
        stats.descriptorCacheMisses = descriptorCache.GetMisses();
        stats.descriptorHitRate = descriptorCache.GetHitRate();

        stats.resourceCacheHits = resourceCache.GetHits();
        stats.resourceCacheMisses = resourceCache.GetMisses();
        stats.resourceHitRate = resourceCache.GetHitRate();
        stats.resourceCacheMemoryUsage = resourceCache.GetMemoryUsage();

        uint32_t totalHits = stats.pipelineCacheHits + stats.descriptorCacheHits + stats.resourceCacheHits;
        uint32_t totalRequests = totalHits + stats.pipelineCacheMisses + stats.descriptorCacheMisses + stats.resourceCacheMisses;
        stats.overallHitRate = totalRequests > 0 ? static_cast<float>(totalHits) / totalRequests : 0.0f;

        return stats;
    }
};
```

### Logging

```cpp
void LogCacheStatistics(const CacheStatistics& stats) {
    LOG_INFO("=== Cache Statistics ===");
    LOG_INFO("Pipeline Cache: {}/{} hits ({:.1f}%)",
             stats.pipelineCacheHits,
             stats.pipelineCacheHits + stats.pipelineCacheMisses,
             stats.pipelineHitRate * 100.0f);
    LOG_INFO("Descriptor Cache: {}/{} hits ({:.1f}%)",
             stats.descriptorCacheHits,
             stats.descriptorCacheHits + stats.descriptorCacheMisses,
             stats.descriptorHitRate * 100.0f);
    LOG_INFO("Resource Cache: {}/{} hits ({:.1f}%), {:.1f} MB used",
             stats.resourceCacheHits,
             stats.resourceCacheHits + stats.resourceCacheMisses,
             stats.resourceHitRate * 100.0f,
             stats.resourceCacheMemoryUsage / (1024.0f * 1024.0f));
    LOG_INFO("Overall Hit Rate: {:.1f}%", stats.overallHitRate * 100.0f);
}
```

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Pipeline cache hit rate | > 90% for stable scenes |
| Descriptor cache hit rate | > 85% for stable scenes |
| Resource cache hit rate | > 70% for static content |
| Overall cache hit rate | > 80% |
| Pipeline cache load time | < 50ms on startup |

---

## Best Practices

1. **Save pipeline cache on exit**: Persist VkPipelineCache to disk for fast startup
2. **Pre-warm caches**: Load commonly-used pipelines during initialization
3. **Monitor cache performance**: Track hit rates and adjust strategies
4. **Tune memory limits**: Balance cache size vs memory pressure
5. **Invalidate sparingly**: Only invalidate when truly necessary
6. **Use timeline semaphores**: Reduce synchronization overhead in multi-device scenarios

---

**See also:**
- [Node System](01-node-system.md) - Pipeline sharing across instances
- [Graph Compilation](02-graph-compilation.md) - Pipeline cache integration
- [Implementation Guide](05-implementation.md) - Cache manager implementation details
