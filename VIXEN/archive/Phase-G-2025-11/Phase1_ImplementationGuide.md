# Phase 1 Implementation Guide
**Goal:** Black window with cleared framebuffer
**Status:** In Progress

## ✅ Completed: SwapChainNode

**What was implemented:**
```cpp
// Setup() - Create semaphores
void SwapChainNode::Setup() {
    uint32_t imageCount = swapChainWrapper->scPublicVars.swapChainImageCount;
    imageAvailableSemaphores.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < imageCount; i++) {
        vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
    }
    currentFrame = 0;
}

// Execute() - Acquire image
void SwapChainNode::Execute(VkCommandBuffer commandBuffer) {
    const uint32_t frameIndex = currentFrame % imageAvailableSemaphores.size();
    currentImageIndex = AcquireNextImage(imageAvailableSemaphores[frameIndex]);
    currentFrame++;
}

// Cleanup() - Destroy semaphores
void SwapChainNode::Cleanup() {
    for (auto& semaphore : imageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device->device, semaphore, nullptr);
        }
    }
    imageAvailableSemaphores.clear();
}
```

**Header additions:**
```cpp
private:
    std::vector<VkSemaphore> imageAvailableSemaphores;
    uint32_t currentFrame = 0;

public:
    VkSemaphore GetImageAvailableSemaphore() const;
```

**Status:** ✅ Complete. Swapchain now acquires images and signals semaphores.

---

## Remaining Nodes for Phase 1

### Summary
For Phase 1 (black window), we need MINIMAL implementations:
1. ✅ SwapChainNode - Acquire image, signal semaphore
2. ⚠️ DepthBufferNode - Just create a depth buffer (stub for now)
3. ⚠️ RenderPassNode - Basic clear-only render pass
4. ⚠️ FramebufferNode - Attach color + depth
5. ⚠️ GeometryRenderNode - Record ONLY `vkCmdBeginRenderPass` + clear + `vkCmdEndRenderPass`
6. ⚠️ PresentNode - Present with semaphore wait

We can skip: ShaderNode, PipelineNode, DescriptorSetNode, VertexBufferNode, TextureLoaderNode for now (no drawing).

---

## Implementation Pattern

Each node follows this pattern:

### 1. Setup() - Allocate Persistent Resources
- Command pools
- Semaphores
- Fences
- **NOT** pipelines, framebuffers (those go in Compile())

### 2. Compile() - Create Render Resources
- Pipelines
- Framebuffers
- Render passes
- Descriptor sets
- Get inputs from other nodes

### 3. Execute() - Per-Frame Work
- Acquire images
- Record commands
- Submit queues
- Present
- **Access** resources created in Setup/Compile

### 4. Cleanup() - Destroy Everything
- Reverse order of Setup/Compile
- Check for VK_NULL_HANDLE before destroying

---

## Next: DepthBufferNode (Minimal for Phase 1)

**File:** `source/RenderGraph/Nodes/DepthBufferNode.cpp`

**Minimal implementation:**

```cpp
void DepthBufferNode::Setup() {
    // Nothing persistent needed
}

void DepthBufferNode::Compile() {
    // Get parameters
    width = GetParameterValue<uint32_t>("width", 0);
    height = GetParameterValue<uint32_t>("height", 0);
    std::string formatStr = GetParameterValue<std::string>("format", "D32");

    VkFormat format = VK_FORMAT_D32_SFLOAT;  // Simplified

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device->device, &imageInfo, nullptr, &depthImage.image);

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device, depthImage.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;

    // Find memory type (DEVICE_LOCAL)
    uint32_t memoryTypeIndex = 0;
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(device->gpu, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    vkAllocateMemory(device->device, &allocInfo, nullptr, &depthImage.mem);
    vkBindImageMemory(device->device, depthImage.image, depthImage.mem, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(device->device, &viewInfo, nullptr, &depthImage.view);

    depthImage.format = format;
}

void DepthBufferNode::Execute(VkCommandBuffer) {
    // Depth buffer is passive, nothing to execute
}

void DepthBufferNode::Cleanup() {
    if (depthImage.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device->device, depthImage.view, nullptr);
        depthImage.view = VK_NULL_HANDLE;
    }
    if (depthImage.image != VK_NULL_HANDLE) {
        vkDestroyImage(device->device, depthImage.image, nullptr);
        depthImage.image = VK_NULL_HANDLE;
    }
    if (depthImage.mem != VK_NULL_HANDLE) {
        vkFreeMemory(device->device, depthImage.mem, nullptr);
        depthImage.mem = VK_NULL_HANDLE;
    }
}
```

**Reference:** `VulkanRenderer::CreateDepthImage()` in VulkanRenderer.cpp:276-347

---

## Simplified Approach for Phase 1

To get a black window QUICKLY, we can use a "stub + reference" approach:

1. **Stub out most nodes** - Just throw runtime_error("Not implemented")
2. **Fully implement ONLY:**
   - SwapChainNode ✅ (done)
   - PresentNode (minimal - just present)
   - GeometryRenderNode (minimal - just clear, no render pass even)

This gets us to a crash-free black window in < 1 hour.

Then we can add render pass, depth buffer, etc. incrementally.

---

## Alternative: Direct Copy Approach

For each node, literally copy code from existing classes:

| Node | Copy From | Function |
|------|-----------|----------|
| DepthBufferNode | VulkanRenderer.cpp:276-347 | CreateDepthImage() |
| RenderPassNode | VulkanRenderer.cpp:194-274 | CreateRenderPass() |
| FramebufferNode | VulkanRenderer.cpp:349-382 | CreateFrameBuffer() |
| GeometryRenderNode | VulkanDrawable.cpp:78-90 | Prepare() (command recording) |
| PresentNode | VulkanDrawable.cpp:182-200 | Render() (present only) |

Just:
1. Copy function body
2. Replace `this->member` with `member`
3. Move resource creation to Setup/Compile as appropriate
4. Add Cleanup() code

This is mechanical and fast.

---

## Current Build Status

**Files Modified:**
- ✅ `source/RenderGraph/Nodes/SwapChainNode.cpp` - Complete
- ✅ `include/RenderGraph/Nodes/SwapChainNode.h` - Complete
- ✅ `source/RenderGraph/RenderGraph.cpp` - Has RenderFrame()
- ✅ `source/VulkanGraphApplication.cpp` - Calls RenderFrame()

**Next to Modify:**
- ⚠️ `source/RenderGraph/Nodes/PresentNode.cpp` - Update Execute()
- ⚠️ `source/RenderGraph/Nodes/GeometryRenderNode.cpp` - Minimal clear
- (Optional for black window) DepthBufferNode, RenderPassNode, FramebufferNode

---

## Recommendation: Minimal Viable Black Window

**Absolute minimum to test:**

1. ✅ SwapChainNode - Acquires image, signals semaphore
2. ⚠️ GeometryRenderNode - Allocates command buffer, records nothing (empty submit)
3. ⚠️ PresentNode - Presents acquired image

**Skip for now:**
- Render pass (can present without rendering)
- Depth buffer (not needed for clear)
- Framebuffer (not needed without render pass)

This gets us:
- Window opens ✅
- No crash ✅
- Undefined image content (garbage on screen) ⚠️
- But proves the graph works! ✅

Then we add render pass + clear in Phase 1b.

---

## Next Steps

1. **Option A: Minimal test** - Implement PresentNode + GeometryRenderNode stubs → Test
2. **Option B: Complete Phase 1** - Implement all 6 nodes → Black window with clear

Recommend **Option A** to verify architecture first, then **Option B** for proper rendering.

Ready to continue?
