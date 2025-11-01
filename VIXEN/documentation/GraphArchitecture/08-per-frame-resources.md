# Per-Frame Resource Pattern

**Phase**: 0.1 - Execution Model Correctness
**Status**: ✅ Implemented (DescriptorSetNode, GeometryRenderNode)

## Problem

Single shared GPU resources cause CPU-GPU race conditions when multiple frames are in flight:

```cpp
// ❌ BUGGY: Single UBO shared across all frames
Frame 0: CPU writes UBO → GPU reads UBO
Frame 1: CPU writes UBO → GPU STILL READING from Frame 0 → RACE!
```

## Solution: Ring Buffer Pattern

Allocate N copies of mutable resources (one per swapchain image). CPU and GPU operate on different copies:

```
Resources: [UBO_0, UBO_1, UBO_2]

Frame 0: CPU writes UBO_0, GPU idle
Frame 1: CPU writes UBO_1, GPU reads UBO_0  ✅ No conflict
Frame 2: CPU writes UBO_2, GPU reads UBO_1  ✅ No conflict
Frame 3: CPU writes UBO_0, GPU reads UBO_2  ✅ No conflict (wraps)
```

## Implementation

### PerFrameResources Helper Class

`RenderGraph/include/Core/PerFrameResources.h`:

```cpp
class PerFrameResources {
public:
    struct FrameData {
        VkBuffer uniformBuffer;
        VkDeviceMemory uniformMemory;
        void* uniformMappedData;
        VkDescriptorSet descriptorSet;
        VkCommandBuffer commandBuffer;
    };

    void Initialize(VulkanDevice* device, uint32_t frameCount);
    VkBuffer CreateUniformBuffer(uint32_t frameIndex, VkDeviceSize size);
    void* GetUniformBufferMapped(uint32_t frameIndex) const;
    void Cleanup();
};
```

### Usage Pattern

```cpp
// 1. Compile: Initialize per-frame resources
void MyNode::Compile() {
    auto* swapchainPublic = In(SWAPCHAIN_PUBLIC);
    uint32_t imageCount = swapchainPublic->swapChainImageCount;

    perFrameResources.Initialize(device, imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        perFrameResources.CreateUniformBuffer(i, sizeof(MyUBO));
    }
}

// 2. Execute: Update current frame's resources only
void MyNode::Execute(VkCommandBuffer cmd) {
    uint32_t imageIndex = In(IMAGE_INDEX);
    void* mapped = perFrameResources.GetUniformBufferMapped(imageIndex);
    memcpy(mapped, &ubo, sizeof(ubo));  // ✅ Safe!
}

// 3. Cleanup: Destroy all frames
void MyNode::CleanupImpl() {
    perFrameResources.Cleanup();
}
```

## Ownership Rules

| Rule | Description |
|------|-------------|
| **One Owner** | Each resource type has single owner (PerFrameResources, VkCommandPool, VkDescriptorPool) |
| **Query Count** | Always get `imageCount` from SWAPCHAIN_PUBLIC (never hardcode) |
| **Index Access** | Always use IMAGE_INDEX input (never manual counters) |
| **No Cross-Frame** | Frame N resources must not depend on frame N±1 |
| **Persistent Map** | UBOs use HOST_VISIBLE\|HOST_COHERENT (map once, no flush) |

## Current Implementation

### DescriptorSetNode
- **File**: `RenderGraph/src/Nodes/DescriptorSetNode.cpp:237`
- **Resources**: Per-frame UBOs with persistent mapping
- **Strategy**: ✅ Correct (uses PerFrameResources)

### GeometryRenderNode
- **File**: `RenderGraph/src/Nodes/GeometryRenderNode.cpp:101`
- **Resources**: Per-frame command buffers + semaphores
- **Strategy**: ✅ Correct (uses std::vector - appropriate for command buffers)

**Note**: GeometryRenderNode uses `std::vector<VkCommandBuffer>` instead of PerFrameResources because command buffers are opaque handles managed by VkCommandPool (no persistent mapping needed).

## Common Pitfalls

```cpp
// ❌ Wrong: Hardcoded count
perFrameResources.Initialize(device, 3);

// ✅ Correct: Dynamic query
auto* sc = In(SWAPCHAIN_PUBLIC);
perFrameResources.Initialize(device, sc->swapChainImageCount);

// ❌ Wrong: Hardcoded index
void* data = perFrameResources.GetUniformBufferMapped(0);

// ✅ Correct: Runtime IMAGE_INDEX
uint32_t idx = In(IMAGE_INDEX);
void* data = perFrameResources.GetUniformBufferMapped(idx);

// ❌ Wrong: Partial cleanup
vkDestroyBuffer(device, buffers[0], nullptr);

// ✅ Correct: All frames
for (auto buf : buffers) vkDestroyBuffer(device, buf, nullptr);
```

## Testing

**Verified** (2025-11-01):
- ✅ 3 distinct UBO handles created (0x21, 0x23, 0x25)
- ✅ Cleanup during swapchain resize (old destroyed, new created)
- ✅ Zero validation errors related to memory synchronization

## Future (Phase 0.3)

**Conditional Re-Recording**: Currently command buffers are re-recorded every frame (dynamic). Phase 0.3 will add dirty flags for conditional re-recording (only when pipeline/state changes).

## See Also

- `RenderGraph/include/Core/PerFrameResources.h` - Helper class implementation
- `documentation/ArchitecturalReview-2025-11-01.md` - BS-1: Per-frame resource management
- `documentation/ArchitecturalPhases-Checkpoint.md` - Phase 0.1 details