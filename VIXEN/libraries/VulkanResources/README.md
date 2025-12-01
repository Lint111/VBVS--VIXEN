# VulkanResources Library

Low-level Vulkan wrapper classes providing resource management and utilities.

## Purpose

Encapsulates Vulkan boilerplate for:
- Instance and device creation
- Swapchain management
- Command buffer utilities
- GPU performance timing

## Key Components

### VulkanInstance
Creates and manages `VkInstance` with validation layers and extensions.

```cpp
VulkanInstance instance;
instance.CreateInstance(appInfo, extensions, layers);
```

### VulkanDevice
Physical and logical device selection with queue family management.

```cpp
VulkanDevice device(&instance);
device.SelectPhysicalDevice();
device.CreateLogicalDevice(extensions, features);
```

### VulkanSwapChain
Swapchain creation, image acquisition, and presentation.

```cpp
VulkanSwapChain swapchain(&device, surface, width, height);
uint32_t imageIndex = swapchain.AcquireNextImage(semaphore);
swapchain.Present(queue, imageIndex, renderComplete);
```

### GPUTimestampQuery
Per-frame GPU timestamp queries for accurate performance measurement.

```cpp
GPUTimestampQuery query(&device, framesInFlight, maxTimestamps);
query.ResetQueries(cmdBuffer, frameIndex);
query.WriteTimestamp(cmdBuffer, frameIndex, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
// ... dispatch ...
query.WriteTimestamp(cmdBuffer, frameIndex, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1);

// Later (after fence wait):
if (query.ReadResults(frameIndex)) {
    float ms = query.GetElapsedMs(frameIndex, 0, 1);
}
```

### CommandBufferUtility
Single-use command buffer helpers for one-shot operations.

```cpp
VkCommandBuffer cmd = CommandBufferUtility::BeginSingleTimeCommands(device, pool);
// ... record commands ...
CommandBufferUtility::EndSingleTimeCommands(device, pool, queue, cmd);
```

## Directory Structure

```
libraries/VulkanResources/
├── include/
│   ├── VulkanInstance.h
│   ├── VulkanDevice.h
│   ├── VulkanSwapChain.h
│   ├── GPUTimestampQuery.h
│   ├── CommandBufferUtility.h
│   ├── VulkanLayerAndExtension.h
│   ├── VulkanGlobalNames.h
│   └── error/VulkanError.h
├── src/
│   ├── VulkanInstance.cpp
│   ├── VulkanDevice.cpp
│   ├── VulkanSwapChain.cpp
│   ├── GPUTimestampQuery.cpp
│   ├── CommandBufferUtility.cpp
│   └── VulkanLayerAndExtension.cpp
└── README.md
```

## Integration

```cmake
add_subdirectory(libraries/VulkanResources)
target_link_libraries(MyApp PRIVATE VulkanResources)
```

## Related Documentation

- [GPUPerformanceSystem.md](../../documentation/VulkanResources/GPUPerformanceSystem.md) - Detailed timing system docs
- [VulkanDevice.md](../../documentation/VulkanResources/VulkanDevice.md) - Device architecture
