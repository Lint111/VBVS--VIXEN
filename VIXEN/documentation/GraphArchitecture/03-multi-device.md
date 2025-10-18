# Multi-Device Support

## Overview

The Render Graph system provides first-class support for multi-GPU configurations, with automatic device affinity propagation, cross-device transfers, and synchronized parallel execution.

---

## Device Affinity Rules

### Automatic Device Assignment

```cpp
// Rule 1: Explicit device assignment takes precedence
auto node = graph.AddNode(NodeType::GeometryPass, "Scene", specificDevice);

// Rule 2: Inherit device from input nodes
// If nodeB reads from nodeA's output, nodeB inherits device from nodeA
graph.ConnectNodes(nodeA, 0, nodeB, 0);  // nodeB->device = nodeA->device

// Rule 3: Multiple inputs - conflict resolution
// If inputs come from different devices, node must explicitly specify device
// OR a transfer node is automatically inserted
```

### Device Inheritance Example

```cpp
RenderGraph graph(device0);  // Primary device

auto geometryPass = graph.AddNode(NodeType::GeometryPass, "Scene");
// geometryPass->device = device0 (inherited from graph)

auto shadowMap = graph.AddNode(NodeType::ShadowMap, "Shadow");
graph.ConnectNodes(geometryPass, 0, shadowMap, 0);
// shadowMap->device = device0 (inherited from geometryPass input)

auto postProcess = graph.AddNode(NodeType::PostProcess, "Bloom", device1);
// postProcess->device = device1 (explicitly set)
graph.ConnectNodes(shadowMap, 0, postProcess, 0);
// Transfer node automatically inserted between shadowMap and postProcess
```

---

## Cross-Device Transfer Nodes

When data flows from a node on Device A to a node on Device B, the compiler automatically inserts a transfer node.

### DeviceTransferNode

```cpp
class DeviceTransferNode : public RenderGraphNode {
public:
    DeviceTransferNode(VulkanDevice* srcDevice, VulkanDevice* dstDevice)
        : sourceDevice(srcDevice)
        , destinationDevice(dstDevice) {}

    void Execute(VkCommandBuffer cmd) override {
        // 1. Create staging buffer/image on source device
        // 2. Copy resource to staging
        // 3. Transfer ownership to destination device
        // 4. Copy from staging to destination resource
        // 5. Signal semaphore for cross-device synchronization
    }

private:
    VulkanDevice* sourceDevice;
    VulkanDevice* destinationDevice;
    VkSemaphore transferCompleteSemaphore;  // Cross-device sync
};
```

### Transfer Workflow

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│ Node A       │         │ Transfer     │         │ Node B       │
│ (Device 0)   │────────▶│ Node         │────────▶│ (Device 1)   │
└──────────────┘         │ (Auto Insert)│         └──────────────┘
                         └──────────────┘
                                │
                         ┌──────▼──────┐
                         │ Semaphore   │
                         │ Sync        │
                         └─────────────┘
```

---

## Multi-Device Execution

### Parallel Execution Per Device

```cpp
void RenderGraph::Execute(VkCommandBuffer cmd) {
    // Group nodes by device
    std::map<VulkanDevice*, std::vector<RenderGraphNode*>> deviceGroups;
    for (auto& node : executionOrder) {
        deviceGroups[node->device].push_back(node.get());
    }

    // Execute in parallel on each device
    std::vector<std::thread> deviceThreads;
    for (auto& [device, nodes] : deviceGroups) {
        deviceThreads.emplace_back([device, nodes]() {
            VkCommandBuffer deviceCmd = AllocateCommandBuffer(device);

            for (auto* node : nodes) {
                // Wait for cross-device dependencies
                if (node->HasCrossDeviceDependencies()) {
                    WaitForCrossDeviceSemaphores(node);
                }

                node->Execute(deviceCmd);

                // Signal if other devices depend on this node
                if (node->HasCrossDeviceDependents()) {
                    SignalCrossDeviceSemaphore(node);
                }
            }

            SubmitCommandBuffer(device, deviceCmd);
        });
    }

    // Wait for all devices to complete
    for (auto& thread : deviceThreads) {
        thread.join();
    }
}
```

### Execution Timeline

```
Device 0:  [GeometryPass]────────[Signal]──────────[Compositor]
                                     │                    ▲
                                     │                    │
Device 1:  ─────────[Wait]───[PostProcess]───[Signal]────┘
```

---

## Synchronization Strategies

### Semaphore-Based Sync (Vulkan 1.0+)

```cpp
// Device 0 renders shadow map
VkSemaphore shadowComplete;
vkCreateSemaphore(device0, &semaphoreInfo, nullptr, &shadowComplete);

// Queue on device 0
vkQueueSubmit(device0Queue, &submitInfo, VK_NULL_HANDLE);
vkQueueSubmit(device0Queue, ..., shadowComplete);  // Signal

// Queue on device 1 waits
VkSubmitInfo waitSubmitInfo = {};
waitSubmitInfo.waitSemaphoreCount = 1;
waitSubmitInfo.pWaitSemaphores = &shadowComplete;  // Wait
vkQueueSubmit(device1Queue, &waitSubmitInfo, VK_NULL_HANDLE);
```

### Timeline Semaphores (Vulkan 1.2+)

More efficient for complex dependencies:

```cpp
// Create timeline semaphore
VkSemaphoreTypeCreateInfo timelineInfo = {};
timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
timelineInfo.initialValue = 0;

VkSemaphoreCreateInfo createInfo = {};
createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
createInfo.pNext = &timelineInfo;
VkSemaphore timelineSemaphore;
vkCreateSemaphore(device, &createInfo, nullptr, &timelineSemaphore);

// Device 0 signals value 1
VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {};
timelineSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
uint64_t signalValue = 1;
timelineSubmitInfo.signalSemaphoreValueCount = 1;
timelineSubmitInfo.pSignalSemaphoreValues = &signalValue;

VkSubmitInfo submitInfo = {};
submitInfo.pNext = &timelineSubmitInfo;
submitInfo.signalSemaphoreCount = 1;
submitInfo.pSignalSemaphores = &timelineSemaphore;
vkQueueSubmit(device0Queue, 1, &submitInfo, VK_NULL_HANDLE);

// Device 1 waits for value 1
uint64_t waitValue = 1;
VkTimelineSemaphoreSubmitInfo waitTimelineInfo = {};
waitTimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
waitTimelineInfo.waitSemaphoreValueCount = 1;
waitTimelineInfo.pWaitSemaphoreValues = &waitValue;

VkSubmitInfo waitSubmitInfo = {};
waitSubmitInfo.pNext = &waitTimelineInfo;
waitSubmitInfo.waitSemaphoreCount = 1;
waitSubmitInfo.pWaitSemaphores = &timelineSemaphore;
vkQueueSubmit(device1Queue, 1, &waitSubmitInfo, VK_NULL_HANDLE);
```

**Benefits:**
- Single semaphore can coordinate multiple operations
- More efficient than creating many binary semaphores
- Enables fine-grained synchronization

---

## Resource Ownership Transfer

For optimal performance, resources can transfer ownership between queue families (devices):

### Ownership Transfer Barriers

```cpp
struct ResourceOwnership {
    VulkanDevice* currentOwner;
    VkAccessFlags currentAccessMask;
    VkPipelineStageFlags currentStageMask;

    void TransferTo(VulkanDevice* newOwner) {
        // Release barrier on current owner
        VkImageMemoryBarrier releaseBarrier = {};
        releaseBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        releaseBarrier.srcAccessMask = currentAccessMask;
        releaseBarrier.dstAccessMask = 0;
        releaseBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        releaseBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        releaseBarrier.srcQueueFamilyIndex = currentOwner->queueFamilyIndex;
        releaseBarrier.dstQueueFamilyIndex = newOwner->queueFamilyIndex;
        releaseBarrier.image = image;
        releaseBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(currentOwnerCmd,
                            currentStageMask,
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &releaseBarrier);

        // Acquire barrier on new owner
        VkImageMemoryBarrier acquireBarrier = {};
        acquireBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        acquireBarrier.srcAccessMask = 0;
        acquireBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        acquireBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acquireBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acquireBarrier.srcQueueFamilyIndex = currentOwner->queueFamilyIndex;
        acquireBarrier.dstQueueFamilyIndex = newOwner->queueFamilyIndex;
        acquireBarrier.image = image;
        acquireBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(newOwnerCmd,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &acquireBarrier);

        currentOwner = newOwner;
        currentAccessMask = VK_ACCESS_SHADER_READ_BIT;
        currentStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
};
```

### Transfer Workflow

1. **Release** on source device (release barrier)
2. **Signal** semaphore on source device
3. **Wait** semaphore on destination device
4. **Acquire** on destination device (acquire barrier)

---

## Device Capabilities Validation

Ensure nodes are assigned to compatible devices:

```cpp
struct DeviceCacheInfo {
    // L1 Cache (per SM/CU)
    size_t l1CacheSize;                // Typically 16-128 KB per SM
    uint32_t l1CacheLineSize;          // Typically 128 bytes

    // L2 Cache (shared across GPU)
    size_t l2CacheSize;                // Typically 512 KB - 6 MB
    uint32_t l2CacheLineSize;          // Typically 128 bytes

    // Derived metrics
    size_t effectiveWorkingSet;        // Size that fits in L2 for good locality
    float cacheLineUtilization;        // How efficiently we use cache lines
};

struct DeviceCapabilities {
    bool supportsGeometry;
    bool supportsCompute;
    bool supportsRayTracing;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    uint32_t maxImageDimension2D;
    uint32_t maxComputeWorkGroupCount[3];

    // Cache information (easy access)
    DeviceCacheInfo cacheInfo;

    // ... more capabilities
};

class VulkanDevice {
public:
    const DeviceCacheInfo& GetCacheInfo() const { return capabilities.cacheInfo; }

    void QueryCacheProperties() {
        // Query from Vulkan physical device properties
        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

        // NVIDIA-specific cache info (if available)
        #ifdef VK_NV_device_diagnostic_checkpoints
        // Query NVIDIA cache sizes
        #endif

        // AMD-specific cache info (if available)
        #ifdef VK_AMD_shader_core_properties
        // Query AMD cache sizes
        #endif

        // Fallback: Conservative estimates based on device type
        if (props2.properties.vendorID == 0x10DE) {  // NVIDIA
            capabilities.cacheInfo = EstimateNVIDIACache(props2.properties);
        } else if (props2.properties.vendorID == 0x1002) {  // AMD
            capabilities.cacheInfo = EstimateAMDCache(props2.properties);
        } else {
            capabilities.cacheInfo = EstimateGenericCache(props2.properties);
        }

        // Conservative effective working set = 75% of L2
        capabilities.cacheInfo.effectiveWorkingSet =
            static_cast<size_t>(capabilities.cacheInfo.l2CacheSize * 0.75f);
    }

private:
    DeviceCacheInfo EstimateNVIDIACache(const VkPhysicalDeviceProperties& props) {
        DeviceCacheInfo info;

        // NVIDIA cache estimates based on architecture
        // RTX 3000/4000 series: ~6 MB L2
        // RTX 2000 series: ~4-6 MB L2
        // GTX 1000 series: ~2-3 MB L2
        if (strstr(props.deviceName, "RTX 40") != nullptr) {
            info.l2CacheSize = 96 * 1024 * 1024;  // 96 MB (Ada Lovelace)
        } else if (strstr(props.deviceName, "RTX 30") != nullptr) {
            info.l2CacheSize = 6 * 1024 * 1024;   // 6 MB
        } else {
            info.l2CacheSize = 4 * 1024 * 1024;   // 4 MB (conservative)
        }

        info.l1CacheSize = 128 * 1024;  // 128 KB per SM (typical)
        info.l1CacheLineSize = 128;
        info.l2CacheLineSize = 128;

        return info;
    }

    DeviceCacheInfo EstimateAMDCache(const VkPhysicalDeviceProperties& props) {
        DeviceCacheInfo info;

        // AMD RDNA 2/3 cache estimates
        // RX 6000/7000 series: ~4-6 MB L2 + large Infinity Cache
        if (strstr(props.deviceName, "RX 7") != nullptr) {
            info.l2CacheSize = 6 * 1024 * 1024;   // 6 MB L2
        } else {
            info.l2CacheSize = 4 * 1024 * 1024;   // 4 MB (conservative)
        }

        info.l1CacheSize = 128 * 1024;  // 128 KB per CU
        info.l1CacheLineSize = 128;
        info.l2CacheLineSize = 128;

        return info;
    }

    DeviceCacheInfo EstimateGenericCache(const VkPhysicalDeviceProperties& props) {
        DeviceCacheInfo info;
        // Conservative fallback
        info.l2CacheSize = 2 * 1024 * 1024;  // 2 MB
        info.l1CacheSize = 64 * 1024;        // 64 KB
        info.l1CacheLineSize = 128;
        info.l2CacheLineSize = 128;
        return info;
    }
};

class GraphCompiler {
    void ValidateNodeDeviceCompatibility(NodeInstance* node) {
        auto& caps = node->device->GetCapabilities();

        switch (node->nodeType->typeId) {
            case NodeTypeId::RayTracingPass:
                if (!caps.supportsRayTracing) {
                    throw CompilationError(
                        "Node '" + node->instanceName + "' requires ray tracing, "
                        "but assigned device doesn't support it"
                    );
                }
                break;

            case NodeTypeId::ComputeShaderPass:
                if (!caps.supportsCompute) {
                    throw CompilationError(
                        "Node '" + node->instanceName + "' requires compute, "
                        "but assigned device doesn't support it"
                    );
                }
                break;

            // ... validate other node types
        }
    }
};
```

---

## Multi-GPU Usage Patterns

### Pattern 1: Main + Helper GPU

Primary GPU renders scene, secondary GPU handles post-processing:

```cpp
RenderGraph graph(mainGPU);

auto scene = graph.AddNode("GeometryPass", "MainScene");      // mainGPU
auto shadows = graph.AddNode("ShadowMapPass", "Shadows");     // mainGPU

auto postFX = graph.AddNode("PostProcess", "Effects", helperGPU);  // helperGPU
graph.ConnectNodes(scene, 0, postFX, 0);  // Auto-insert transfer

auto final = graph.AddNode("Compositor", "Final");            // mainGPU
graph.ConnectNodes(postFX, 0, final, 0);  // Auto-insert transfer
```

### Pattern 2: Load Balancing

Distribute independent work across multiple GPUs:

```cpp
RenderGraph graph(gpu0);

std::vector<VulkanDevice*> gpus = {gpu0, gpu1, gpu2, gpu3};

// Distribute 16 shadow maps across 4 GPUs (4 per GPU)
for (int i = 0; i < 16; i++) {
    VulkanDevice* targetGPU = gpus[i % gpus.size()];
    auto shadow = graph.AddNode("ShadowMapPass", "Shadow_" + std::to_string(i), targetGPU);
}

// All shadow maps render in parallel
graph.Compile();
graph.Execute(cmd);
```

### Pattern 3: Split Frame Rendering (AFR - Alternate Frame Rendering)

```cpp
// Frame N on GPU 0
RenderGraph graphA(gpu0);
// ... build graph

// Frame N+1 on GPU 1 (while GPU 0 works on frame N)
RenderGraph graphB(gpu1);
// ... build graph

// Alternate execution
if (frameNumber % 2 == 0) {
    graphA.Execute(cmdA);
} else {
    graphB.Execute(cmdB);
}
```

---

## Performance Considerations

### Transfer Overhead

Cross-device transfers have overhead:
- PCIe bandwidth limitations
- Synchronization latency
- Memory copying

**Optimization:**
- Minimize cross-device transfers
- Batch transfers when possible
- Use timeline semaphores for fine-grained sync
- Consider compute vs transfer cost trade-offs

### Device Selection Heuristics

```cpp
VulkanDevice* SelectOptimalDevice(NodeInstance* node,
                                  const std::vector<VulkanDevice*>& availableDevices) {
    // Prefer device with most inputs already present (minimize transfers)
    VulkanDevice* bestDevice = nullptr;
    int maxLocalInputs = -1;

    for (auto* device : availableDevices) {
        int localInputs = CountInputsOnDevice(node, device);
        if (localInputs > maxLocalInputs) {
            maxLocalInputs = localInputs;
            bestDevice = device;
        }
    }

    return bestDevice;
}
```

---

## Debugging Multi-GPU Issues

### Validation

Enable Vulkan validation layers to catch:
- Queue family ownership violations
- Missing synchronization
- Invalid device indices

### Profiling

Use tools to analyze:
- Per-device GPU utilization
- Cross-device transfer bandwidth
- Synchronization stalls

**Tools:**
- Nsight Graphics (NVIDIA)
- RenderDoc
- Vulkan Timeline Semaphore queries

---

**See also:**
- [Node System](01-node-system.md) - Device affinity in node instances
- [Graph Compilation](02-graph-compilation.md) - Device affinity propagation phase
- [Usage Examples](06-examples.md) - Multi-GPU usage examples
