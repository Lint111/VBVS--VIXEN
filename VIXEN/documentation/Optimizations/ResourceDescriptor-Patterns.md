# Resource Descriptors: Pre-Configuration and On-the-Spot Creation

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Design Pattern
**Related**: RequestResource-API-v2-Corrected.md

---

## Leveraging Existing Descriptor System

The ResourceVariant system already defines descriptors for all resource types:
- `ImageDescriptor` - VkImage resources
- `BufferDescriptor` - VkBuffer resources
- `HandleDescriptor` - Simple handle types
- `CommandPoolDescriptor` - VkCommandPool
- etc.

These descriptors can be:
1. **Pre-configured in node configs** (stored in NodeConfig structs)
2. **Passed as node parameters** (configured by user/graph builder)
3. **Created on-the-spot** (dynamically computed in node logic)

---

## Pattern 1: Pre-Configured Descriptors in NodeConfig

### Example: DepthBufferNodeConfig

```cpp
// In Data/Nodes/DepthBufferNodeConfig.h
struct DepthBufferNodeConfig {
    // Input slots
    DEFINE_INPUT_SLOT(DEVICE, VulkanDevicePtr, 0);
    DEFINE_INPUT_SLOT(WIDTH, uint32_t, 1);
    DEFINE_INPUT_SLOT(HEIGHT, uint32_t, 2);

    // Output slots
    DEFINE_OUTPUT_SLOT(DEPTH_IMAGE, VkImage, 0);
    DEFINE_OUTPUT_SLOT(DEPTH_VIEW, VkImageView, 1);

    // PRE-CONFIGURED descriptor (can be customized via parameters)
    ImageDescriptor depthImageDescriptor{
        .width = 1920,          // Default, overridden by input
        .height = 1080,         // Default, overridden by input
        .format = VK_FORMAT_D32_SFLOAT,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT
    };
};
```

### Node Implementation

```cpp
void DepthBufferNode::CompileImpl(TypedCompileContext& ctx) override {
    auto* device = ctx.In(DepthBufferNodeConfig::DEVICE);

    // Get dynamic dimensions from inputs
    uint32_t width = ctx.In(DepthBufferNodeConfig::WIDTH);
    uint32_t height = ctx.In(DepthBufferNodeConfig::HEIGHT);

    // Update pre-configured descriptor with runtime values
    auto descriptor = config.depthImageDescriptor;  // Copy from config
    descriptor.width = width;                       // Override with input
    descriptor.height = height;

    // Request URM-managed resource with configured descriptor
    Resource* depthImage = ctx.RequestResource<VkImage>(descriptor);

    // Create Vulkan resource
    VkImage vkImage;
    vkCreateImage(device->device, &createInfo, nullptr, &vkImage);
    depthImage->SetHandle(vkImage);

    // Wire to output slot
    ctx.Out(DepthBufferNodeConfig::DEPTH_IMAGE, depthImage);
}
```

---

## Pattern 2: Parameterized Descriptors

Descriptors can be exposed as node parameters for graph-builder customization:

```cpp
// Graph setup
graph.AddNode("DepthBuffer", "mainDepth");
graph.SetParameter("mainDepth", "format", VK_FORMAT_D24_UNORM_S8_UINT);  // Override format
graph.SetParameter("mainDepth", "samples", VK_SAMPLE_COUNT_4_BIT);       // Enable MSAA

// Node reads parameters
void CompileImpl(TypedCompileContext& ctx) override {
    auto descriptor = config.depthImageDescriptor;

    // Override from parameters
    descriptor.format = GetParameterValue<VkFormat>("format", descriptor.format);
    descriptor.samples = GetParameterValue<VkSampleCountFlagBits>("samples", descriptor.samples);

    Resource* image = ctx.RequestResource<VkImage>(descriptor);
    // ...
}
```

---

## Pattern 3: On-the-Spot Dynamic Descriptors

Compute descriptor entirely from runtime information:

```cpp
void TextureLoaderNode::CompileImpl(TypedCompileContext& ctx) override {
    std::string texturePath = ctx.In(TextureLoaderNodeConfig::FILE_PATH);

    // Load texture metadata to determine descriptor
    TextureMetadata meta = LoadTextureMetadata(texturePath);

    // Create descriptor dynamically from file
    ImageDescriptor descriptor{
        .width = meta.width,
        .height = meta.height,
        .format = meta.format,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = CalculateMipLevels(meta.width, meta.height),
        .arrayLayers = meta.isCubemap ? 6 : 1,
        .samples = VK_SAMPLE_COUNT_1_BIT
    };

    // Request resource with dynamic descriptor
    Resource* texture = ctx.RequestResource<VkImage>(descriptor);

    // Load actual texture data
    LoadTextureData(texturePath, texture);

    ctx.Out(TextureLoaderNodeConfig::TEXTURE, texture);
}
```

---

## Pattern 4: Container Resources with Descriptors

For std::vector and other containers, use HandleDescriptor:

```cpp
// Request vector of image views
HandleDescriptor containerDesc{};  // Minimal descriptor for containers

Resource* viewsContainer = ctx.RequestResource<std::vector<VkImageView>>(
    containerDesc,
    AllocStrategy::Heap  // Vectors live on heap
);

// Populate container
auto& views = viewsContainer->GetHandle<std::vector<VkImageView>>();
views.reserve(swapchainImageCount);
for (uint32_t i = 0; i < swapchainImageCount; ++i) {
    views.push_back(CreateImageView(swapchainImages[i]));
}

ctx.Out(SwapChainNodeConfig::IMAGE_VIEWS, viewsContainer);
```

---

## Benefits of Descriptor-Based Approach

✅ **Reusable Configs** - Descriptors in NodeConfig can be reused across node instances
✅ **Parameter Override** - Graph builder can customize via parameters
✅ **Type Safety** - ResourceTypeTraits ensures descriptor matches resource type
✅ **Flexibility** - Supports static, parameterized, and dynamic configuration
✅ **Documentation** - Descriptor in config serves as documentation of resource requirements
✅ **Validation** - Descriptors can be validated before resource creation

---

## Example: SwapChain Image Views (Array Pattern)

```cpp
// Config with pre-configured array descriptor
struct SwapChainNodeConfig {
    // Pre-configured descriptor for image view array
    HandleDescriptor imageViewArrayDescriptor{};

    static constexpr size_t MAX_SWAPCHAIN_IMAGES = 4;
};

// Node implementation
void SwapChainNode::CompileImpl(TypedCompileContext& ctx) override {
    // Get swapchain images
    std::vector<VkImage> swapchainImages = GetSwapchainImages();

    // Request container resource
    Resource* viewsRes = ctx.RequestResource<std::vector<VkImageView>>(
        config.imageViewArrayDescriptor,
        AllocStrategy::Heap
    );

    // Populate views
    auto& views = viewsRes->GetHandle<std::vector<VkImageView>>();
    views.reserve(swapchainImages.size());

    for (VkImage image : swapchainImages) {
        VkImageView view = CreateImageView(image);
        views.push_back(view);
    }

    // URM tracks the vector allocation
    // UpdateResourceSize called automatically via SetHandle

    ctx.Out(SwapChainNodeConfig::IMAGE_VIEWS, viewsRes);
}
```

---

## Integration with RequestResource API

The complete flow:

```cpp
// 1. Define descriptor (in config, parameter, or on-the-spot)
ImageDescriptor desc = config.defaultDescriptor;
desc.width = runtimeWidth;

// 2. Request URM-managed resource
Resource* image = ctx.RequestResource<VkImage>(desc, AllocStrategy::Device);
// ↓ Calls: budgetManager->CreateResource<VkImage>(desc, strategy)
// ↓ URM creates Resource, stores in pool, tracks metadata

// 3. Populate resource
VkImage vkImage;
vkCreateImage(device, &createInfo, nullptr, &vkImage);
image->SetHandle(vkImage);
// ↓ Optionally updates URM size via callback

// 4. Wire to slot
ctx.Out(OUTPUT_SLOT, image);
// ↓ bundles[arrayIndex].outputs[slotIndex] = image
```

---

## Summary

**Descriptors are the key to flexible resource configuration:**

- Store in **NodeConfig** for reusable defaults
- Override via **parameters** for per-instance customization
- Compute **dynamically** for runtime-determined resources
- Pass to **CreateResource()** which already accepts descriptors

This leverages the existing ResourceVariant descriptor system and provides maximum flexibility for resource configuration!

---

**End of Document**
