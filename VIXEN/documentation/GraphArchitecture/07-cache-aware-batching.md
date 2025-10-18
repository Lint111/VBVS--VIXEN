# Cache-Aware Batching

## Overview

Cache-aware batching optimizes node execution by grouping instances to maximize GPU cache locality, reducing memory bandwidth and improving performance.

---

## GPU Cache Hierarchy

### Cache Levels

```
┌─────────────────────────────────────────────────────────┐
│  GPU Cores (SMs/CUs)                                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │ L1 Cache     │  │ L1 Cache     │  │ L1 Cache     │ │
│  │ 128 KB       │  │ 128 KB       │  │ 128 KB       │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘ │
│         │                 │                 │          │
└─────────┼─────────────────┼─────────────────┼──────────┘
          │                 │                 │
          └─────────────────┼─────────────────┘
                            │
                 ┌──────────▼──────────┐
                 │   L2 Cache          │
                 │   4-96 MB           │  ← Target for batching
                 │   (shared)          │
                 └──────────┬──────────┘
                            │
                 ┌──────────▼──────────┐
                 │   VRAM              │
                 │   8-24 GB           │
                 └─────────────────────┘
```

### Cache Properties per Device

```cpp
struct DeviceCacheInfo {
    // L1 Cache (per SM/CU)
    size_t l1CacheSize;                // Typically 16-128 KB per SM
    uint32_t l1CacheLineSize;          // Typically 128 bytes

    // L2 Cache (shared across GPU)
    size_t l2CacheSize;                // Typically 512 KB - 96 MB
    uint32_t l2CacheLineSize;          // Typically 128 bytes

    // Derived metrics
    size_t effectiveWorkingSet;        // 75% of L2 (conservative)
    float cacheLineUtilization;        // How efficiently we use cache lines
};
```

**Common GPU Cache Sizes:**

| GPU | L1 per SM | L2 Total | Notes |
|-----|-----------|----------|-------|
| NVIDIA RTX 4090 | 128 KB | 96 MB | Ada Lovelace |
| NVIDIA RTX 3090 | 128 KB | 6 MB | Ampere |
| NVIDIA RTX 2080 Ti | 96 KB | 5.5 MB | Turing |
| AMD RX 7900 XTX | 128 KB | 6 MB | RDNA 3 |
| AMD RX 6800 XT | 128 KB | 4 MB | RDNA 2 |

---

## Batching Strategy

### Goal

Group node instances so their **combined working set** fits within the GPU's L2 cache, maximizing data reuse and minimizing VRAM access.

### Working Set Calculation

```cpp
class NodeInstance {
    size_t GetWorkingSet() const {
        // Working set = data touched during execution
        return inputMemoryFootprint +        // Input resources
               GetOutputMemorySize() +       // Output resources
               GetDescriptorMemorySize() +   // Descriptor data
               nodeType->workloadMetrics.estimatedInternalMemory;  // Scratch
    }
};

class NodeBatch {
    size_t CalculateWorkingSet() const {
        // Track unique resources to avoid double-counting
        std::set<const Resource*> uniqueResources;

        for (auto* node : nodes) {
            for (const auto& input : node->inputs) {
                uniqueResources.insert(&input);
            }
            for (const auto& output : node->outputs) {
                uniqueResources.insert(&output);
            }
        }

        // Sum unique resource sizes
        size_t total = 0;
        for (const auto* resource : uniqueResources) {
            total += GetResourceSize(*resource);
        }

        // Add internal memory (per-instance, not shared)
        for (auto* node : nodes) {
            total += node->nodeType->workloadMetrics.estimatedInternalMemory;
        }

        return total;
    }
};
```

### Batch Creation Algorithm

```cpp
class GraphCompiler {
    std::vector<NodeBatch> CreateCacheAwareBatches(
        const std::vector<NodeInstance*>& executionOrder,
        VulkanDevice* device) {

        const size_t effectiveCache = device->GetCacheInfo().effectiveWorkingSet;
        std::vector<NodeBatch> batches;

        NodeBatch currentBatch;

        for (auto* node : executionOrder) {
            // Try to add node to current batch
            currentBatch.nodes.push_back(node);
            size_t newWorkingSet = currentBatch.CalculateWorkingSet();

            if (newWorkingSet <= effectiveCache) {
                // Fits in cache - keep in batch
                currentBatch.totalWorkingSet = newWorkingSet;
                currentBatch.fitsInCache = true;
            } else {
                // Exceeds cache - remove node and start new batch
                currentBatch.nodes.pop_back();

                if (!currentBatch.nodes.empty()) {
                    batches.push_back(std::move(currentBatch));
                }

                // Start new batch with this node
                currentBatch = NodeBatch();
                currentBatch.nodes.push_back(node);
                currentBatch.totalWorkingSet = node->GetWorkingSet();
                currentBatch.fitsInCache = (currentBatch.totalWorkingSet <= effectiveCache);
            }
        }

        // Add final batch
        if (!currentBatch.nodes.empty()) {
            batches.push_back(std::move(currentBatch));
        }

        return batches;
    }
};
```

---

## Optimization Techniques

### 1. Resource Sharing Detection

Prioritize grouping nodes that share resources:

```cpp
float CalculateResourceAffinity(NodeInstance* a, NodeInstance* b) {
    size_t sharedBytes = 0;
    size_t totalBytes = 0;

    // Find shared inputs
    for (const auto& inputA : a->inputs) {
        totalBytes += GetResourceSize(inputA);
        for (const auto& inputB : b->inputs) {
            if (SameResource(inputA, inputB)) {
                sharedBytes += GetResourceSize(inputA);
                break;
            }
        }
    }

    // Check if B reads A's output
    for (const auto& outputA : a->outputs) {
        for (const auto& inputB : b->inputs) {
            if (inputB.sourceNode == a) {
                sharedBytes += GetResourceSize(outputA);
            }
        }
    }

    // Return affinity score (0.0 = no sharing, 1.0 = all shared)
    return (totalBytes > 0) ? (float)sharedBytes / totalBytes : 0.0f;
}

// Pre-sort execution order to group related nodes
void OptimizeExecutionOrder(std::vector<NodeInstance*>& executionOrder) {
    // Greedy clustering: place nodes with high affinity together
    std::vector<NodeInstance*> optimized;
    std::set<NodeInstance*> placed;

    // Start with first node
    optimized.push_back(executionOrder[0]);
    placed.insert(executionOrder[0]);

    while (placed.size() < executionOrder.size()) {
        NodeInstance* lastPlaced = optimized.back();
        NodeInstance* bestNext = nullptr;
        float bestAffinity = -1.0f;

        // Find unplaced node with highest affinity to last placed
        for (auto* node : executionOrder) {
            if (placed.contains(node)) continue;

            // Must respect dependencies
            bool dependenciesSatisfied = true;
            for (auto* dep : node->dependencies) {
                if (!placed.contains(dep)) {
                    dependenciesSatisfied = false;
                    break;
                }
            }

            if (dependenciesSatisfied) {
                float affinity = CalculateResourceAffinity(lastPlaced, node);
                if (affinity > bestAffinity) {
                    bestAffinity = affinity;
                    bestNext = node;
                }
            }
        }

        if (bestNext) {
            optimized.push_back(bestNext);
            placed.insert(bestNext);
        } else {
            // No node with affinity found - pick any valid node
            for (auto* node : executionOrder) {
                if (!placed.contains(node)) {
                    bool canPlace = true;
                    for (auto* dep : node->dependencies) {
                        if (!placed.contains(dep)) {
                            canPlace = false;
                            break;
                        }
                    }
                    if (canPlace) {
                        optimized.push_back(node);
                        placed.insert(node);
                        break;
                    }
                }
            }
        }
    }

    executionOrder = std::move(optimized);
}
```

### 2. Pipeline Grouping Within Batches

Minimize pipeline state changes within a batch:

```cpp
void OptimizeBatchOrder(NodeBatch& batch) {
    // Sort by pipeline to reduce state changes
    std::stable_sort(batch.nodes.begin(), batch.nodes.end(),
        [](NodeInstance* a, NodeInstance* b) {
            if (a->pipeline != b->pipeline) {
                return a->pipeline < b->pipeline;
            }
            // If same pipeline, group by descriptor set layout
            return a->pipelineLayout < b->pipelineLayout;
        });
}
```

### 3. Descriptor Set Batching

Group nodes with similar descriptor sets:

```cpp
struct DescriptorBatch {
    VkDescriptorSet descriptorSet;
    std::vector<NodeInstance*> nodes;
};

std::vector<DescriptorBatch> GroupByDescriptorSet(const NodeBatch& batch) {
    std::map<VkDescriptorSet, DescriptorBatch> groups;

    for (auto* node : batch.nodes) {
        groups[node->descriptorSet].nodes.push_back(node);
        groups[node->descriptorSet].descriptorSet = node->descriptorSet;
    }

    std::vector<DescriptorBatch> result;
    for (auto& [_, descBatch] : groups) {
        result.push_back(std::move(descBatch));
    }

    return result;
}
```

---

## Command Buffer Recording with Batching

### Optimized Recording

```cpp
void GraphCompiler::RecordCommandBuffer(VkCommandBuffer cmd,
                                       const std::vector<NodeBatch>& batches,
                                       VulkanDevice* device) {
    for (size_t batchIdx = 0; batchIdx < batches.size(); batchIdx++) {
        const auto& batch = batches[batchIdx];

        // Log batch info (debug)
        LOG_DEBUG("Batch {}: {} nodes, {:.2f} MB working set, fits in cache: {}",
                  batchIdx,
                  batch.nodes.size(),
                  batch.totalWorkingSet / (1024.0 * 1024.0),
                  batch.fitsInCache ? "YES" : "NO");

        // Warn if batch exceeds cache
        if (!batch.fitsInCache) {
            LOG_WARN("Batch {} working set ({:.2f} MB) exceeds L2 cache ({:.2f} MB)",
                     batchIdx,
                     batch.totalWorkingSet / (1024.0 * 1024.0),
                     device->GetCacheInfo().l2CacheSize / (1024.0 * 1024.0));
        }

        // Optional: Insert cache control hints
        if (batchIdx > 0) {
            // Between batches, may want to flush/invalidate
            // (vendor-specific extensions)
            InsertCacheControlHints(cmd, device);
        }

        // Optimize node order within batch
        auto optimizedBatch = batch;
        OptimizeBatchOrder(optimizedBatch);

        // Record nodes
        VkPipeline lastPipeline = VK_NULL_HANDLE;
        VkDescriptorSet lastDescriptorSet = VK_NULL_HANDLE;

        for (auto* node : optimizedBatch.nodes) {
            // Insert barriers
            InsertResourceBarriers(cmd, node);

            // Bind pipeline (only if changed)
            if (node->pipeline != lastPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, node->pipeline);
                lastPipeline = node->pipeline;
            }

            // Bind descriptor set (only if changed)
            if (node->descriptorSet != lastDescriptorSet) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       node->pipelineLayout, 0, 1,
                                       &node->descriptorSet, 0, nullptr);
                lastDescriptorSet = node->descriptorSet;
            }

            // Record node commands
            node->RecordCommands(cmd);
        }
    }
}
```

---

## Performance Analysis

### Batch Statistics

```cpp
struct BatchStatistics {
    uint32_t totalBatches;
    uint32_t batchesInCache;
    uint32_t batchesExceedingCache;
    size_t averageBatchSize;
    size_t largestBatchSize;
    float averageCacheUtilization;  // % of L2 used on average
};

BatchStatistics AnalyzeBatches(const std::vector<NodeBatch>& batches,
                               VulkanDevice* device) {
    BatchStatistics stats = {};
    stats.totalBatches = batches.size();

    size_t totalSize = 0;

    for (const auto& batch : batches) {
        if (batch.fitsInCache) {
            stats.batchesInCache++;
        } else {
            stats.batchesExceedingCache++;
        }

        totalSize += batch.totalWorkingSet;
        stats.largestBatchSize = std::max(stats.largestBatchSize, batch.totalWorkingSet);
    }

    stats.averageBatchSize = totalSize / batches.size();
    stats.averageCacheUtilization =
        (float)stats.averageBatchSize / device->GetCacheInfo().l2CacheSize;

    return stats;
}

void PrintBatchStatistics(const BatchStatistics& stats, VulkanDevice* device) {
    LOG_INFO("=== Cache-Aware Batching Statistics ===");
    LOG_INFO("Total batches: {}", stats.totalBatches);
    LOG_INFO("Batches in cache: {} ({:.1f}%)",
             stats.batchesInCache,
             100.0f * stats.batchesInCache / stats.totalBatches);
    LOG_INFO("Batches exceeding cache: {} ({:.1f}%)",
             stats.batchesExceedingCache,
             100.0f * stats.batchesExceedingCache / stats.totalBatches);
    LOG_INFO("Average batch size: {:.2f} MB",
             stats.averageBatchSize / (1024.0 * 1024.0));
    LOG_INFO("Largest batch size: {:.2f} MB",
             stats.largestBatchSize / (1024.0 * 1024.0));
    LOG_INFO("Average cache utilization: {:.1f}%",
             100.0f * stats.averageCacheUtilization);
    LOG_INFO("L2 cache size: {:.2f} MB",
             device->GetCacheInfo().l2CacheSize / (1024.0 * 1024.0));
}
```

**Example Output:**
```
=== Cache-Aware Batching Statistics ===
Total batches: 8
Batches in cache: 7 (87.5%)
Batches exceeding cache: 1 (12.5%)
Average batch size: 3.45 MB
Largest batch size: 8.20 MB
Average cache utilization: 57.5%
L2 cache size: 6.00 MB
```

---

## Examples

### Example 1: Shadow Map Batching

```cpp
// Scenario: 16 shadow maps, each 2 MB working set
// RTX 3090: 6 MB L2 cache, effective = 4.5 MB

RenderGraph graph(device, &registry);

for (int i = 0; i < 16; i++) {
    auto shadow = graph.AddNode("ShadowMapPass", "Shadow_" + std::to_string(i));
}

graph.Compile();

// Compiler creates batches:
// Batch 0: Shadow_0, Shadow_1 (4 MB)  ✓ Fits in cache
// Batch 1: Shadow_2, Shadow_3 (4 MB)  ✓ Fits in cache
// Batch 2: Shadow_4, Shadow_5 (4 MB)  ✓ Fits in cache
// ... 8 total batches
```

### Example 2: Resource Sharing Optimization

```cpp
// Geometry pass outputs shared by multiple nodes
auto geometry = graph.AddNode("GeometryPass", "Scene");

auto shadow1 = graph.AddNode("ShadowMapPass", "Shadow1");
auto shadow2 = graph.AddNode("ShadowMapPass", "Shadow2");
auto ssao = graph.AddNode("SSAOPass", "SSAO");

// All read from geometry output
graph.ConnectNodes(geometry, 0, shadow1, 0);
graph.ConnectNodes(geometry, 0, shadow2, 0);
graph.ConnectNodes(geometry, 0, ssao, 0);

// Compiler batches shadow1, shadow2, ssao together
// because they share geometry output (already in cache)
```

---

## Best Practices

1. **Target 75% L2 utilization** - Leave headroom for driver overhead
2. **Pre-sort by resource affinity** - Group nodes sharing resources
3. **Minimize pipeline changes** - Sort by pipeline within batches
4. **Monitor cache miss rates** - Use GPU profilers to validate
5. **Warn on cache overflow** - Log when batches exceed cache
6. **Consider descriptor set reuse** - Group similar descriptor layouts

---

**See also:**
- [Node System](01-node-system.md) - Unit of work tracking
- [Graph Compilation](02-graph-compilation.md) - Compilation phases
- [Multi-Device Support](03-multi-device.md) - Device cache information
