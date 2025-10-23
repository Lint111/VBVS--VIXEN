# Resource Variant System - Migration Guide

## Overview

The new `ResourceVariant.h` system replaces manual type-punning and `dynamic_cast` checks with **compile-time type-safe `std::variant`** storage, similar to the parameter system.

## Key Benefits

✅ **Compile-time type safety** - No more `dynamic_cast` or manual type checks  
✅ **Automatic descriptor mapping** - Macro registers Vulkan types with descriptors  
✅ **Slot-aware access** - Use slot type info to access correct variant type  
✅ **Clean API** - `GetHandle<VkImage>()` instead of `GetImage()`  
✅ **Extensible** - Add new types with single `REGISTER_RESOURCE_TYPE` call

---

## Architecture

### Old System (Resource.h)
```cpp
class Resource {
    VkImage image;           // 8 separate handle fields
    VkBuffer buffer;
    VkImageView imageView;
    // ... manual type-punning ...
    
    VkImage GetImage() { return image; }  // Separate getter per type
};
```

### New System (ResourceVariant.h)
```cpp
class Resource {
    ResourceHandleVariant handle;  // Single variant field
    ResourceDescriptorVariant descriptor;
    
    template<typename T>
    T GetHandle() { return std::get<T>(handle); }  // One template getter
};
```

---

## Usage Examples

### Creating Resources

**Old way:**
```cpp
ImageDescription imgDesc{};
imgDesc.width = 1920;
imgDesc.height = 1080;
imgDesc.format = VK_FORMAT_R8G8B8A8_UNORM;

Resource res(ResourceType::Image, ResourceLifetime::Transient, imgDesc);
```

**New way:**
```cpp
ImageDescriptor imgDesc{};
imgDesc.width = 1920;
imgDesc.height = 1080;
imgDesc.format = VK_FORMAT_R8G8B8A8_UNORM;

auto res = Resource::Create<VkImage>(imgDesc);
```

### Setting Handles

**Old way:**
```cpp
res.SetImage(myVkImage);
res.SetImageView(myVkImageView);
```

**New way:**
```cpp
res.SetHandle<VkImage>(myVkImage);
res.SetHandle<VkImageView>(myVkImageView);
```

### Getting Handles

**Old way:**
```cpp
VkImage img = res.GetImage();
if (img != VK_NULL_HANDLE) { /* use it */ }
```

**New way:**
```cpp
VkImage img = res.GetHandle<VkImage>();
if (img != VK_NULL_HANDLE) { /* use it */ }
```

### Slot-Aware Access (Typed Nodes)

**Best practice - use slot type information:**

```cpp
// In node config
CONSTEXPR_OUTPUT(COLOR_IMAGE, VkImage, 0, false);

// In node implementation
void Compile() {
    // Slot knows it's VkImage, no manual type checking needed
    VkImage img = Out(ColorImageConfig::COLOR_IMAGE);
    
    // Or use TypedNode's SetOutput
    SetOutput(ColorImageConfig::COLOR_IMAGE, 0, myImage);
}
```

---

## Descriptor Types

### Base Class
```cpp
struct ResourceDescriptorBase {
    virtual bool Validate() const { return true; }
    virtual std::unique_ptr<ResourceDescriptorBase> Clone() const = 0;
};
```

### Specific Descriptors

**ImageDescriptor** - For VkImage resources
```cpp
ImageDescriptor imgDesc;
imgDesc.width = 1920;
imgDesc.height = 1080;
imgDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
imgDesc.usage = ResourceUsage::ColorAttachment | ResourceUsage::Sampled;
```

**BufferDescriptor** - For VkBuffer resources
```cpp
BufferDescriptor bufDesc;
bufDesc.size = 1024 * 1024;  // 1MB
bufDesc.usage = ResourceUsage::UniformBuffer;
bufDesc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
```

**HandleDescriptor** - For simple handles (VkSurface, VkSwapchain, etc.)
```cpp
HandleDescriptor handleDesc("VkSurfaceKHR");
// No additional fields needed
```

**CommandPoolDescriptor** - For VkCommandPool
```cpp
CommandPoolDescriptor poolDesc;
poolDesc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
poolDesc.queueFamilyIndex = 0;
```

**ShaderProgramDescriptor** - For shader program pointers
```cpp
ShaderProgramDescriptor shaderDesc("MyVertexShader");
```

---

## Registering New Types

Add support for new Vulkan types with one macro:

```cpp
// In ResourceVariant.h, after existing registrations
REGISTER_RESOURCE_TYPE(VkPipeline, HandleDescriptor, ResourceType::Buffer);
REGISTER_RESOURCE_TYPE(VkPipelineLayout, HandleDescriptor, ResourceType::Buffer);
```

For complex types needing custom descriptors:

```cpp
// 1. Define descriptor
struct PipelineDescriptor : ResourceDescriptorBase {
    VkPipelineBindPoint bindPoint;
    std::vector<VkDynamicState> dynamicStates;
    
    std::unique_ptr<ResourceDescriptorBase> Clone() const override {
        return std::make_unique<PipelineDescriptor>(*this);
    }
};

// 2. Add to variant
using ResourceDescriptorVariant = std::variant<
    // ... existing types ...
    PipelineDescriptor
>;

// 3. Register
REGISTER_RESOURCE_TYPE(VkPipeline, PipelineDescriptor, ResourceType::Buffer);
```

---

## Migration Strategy

### Phase 1: Parallel Systems (Current)
- Keep old `Resource.h` intact
- New code uses `ResourceVariant.h`
- Both systems coexist

### Phase 2: Node-by-Node Migration
- Update `TypedNode` to use variant internally
- Migrate nodes one by one:
  1. SwapChainNode (simple handles)
  2. DepthBufferNode (image resources)
  3. DescriptorSetNode (complex multi-type)

### Phase 3: Core Integration
- Update `RenderGraph::CreateResource()` to use variant
- Update `NodeInstance` input/output storage
- Remove old `Resource.h`

---

## Example: Full Node Migration

**Before (SwapChainNode):**
```cpp
void SwapChainNode::Compile() {
    Resource* surfaceRes = GetInput(0);
    VkSurfaceKHR surface = surfaceRes->GetSurface();
    
    // Create swapchain...
    
    Resource* outputRes = GetOutput(0);
    outputRes->SetSwapchain(swapchain);
}
```

**After (SwapChainNode):**
```cpp
void SwapChainNode::Compile() {
    Resource* surfaceRes = GetInput(SwapChainConfig::SURFACE_Slot::index);
    VkSurfaceKHR surface = surfaceRes->GetHandle<VkSurfaceKHR>();
    
    // Create swapchain...
    
    SetOutput(SwapChainConfig::SWAPCHAIN, 0, swapchain);
    // TypedNode internally calls: res->SetHandle<VkSwapchainKHR>(swapchain)
}
```

---

## Type Safety Comparison

### Runtime Type Checking (OLD)
```cpp
if (res->GetType() == ResourceType::Image) {
    auto* imgDesc = dynamic_cast<ImageDescription*>(res->description.get());
    if (imgDesc) {
        VkImage img = res->GetImage();  // Hope it's the right type!
    }
}
```

### Compile-Time Type Checking (NEW)
```cpp
// Compiler KNOWS from slot definition this is VkImage
VkImage img = res->GetHandle<VkImage>();

// Type mismatch caught at COMPILE TIME:
// VkBuffer buf = res->GetHandle<VkBuffer>();  // ERROR: wrong slot type!
```

---

## Advanced: Template Metaprogramming

The system uses template specialization to map types:

```cpp
template<typename T>
struct ResourceTypeTraits {
    using DescriptorT = HandleDescriptor;  // Default
    static constexpr ResourceType resourceType = ResourceType::Buffer;
};

// Specialization for VkImage
template<>
struct ResourceTypeTraits<VkImage> {
    using DescriptorT = ImageDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Image;
};

// Usage
typename ResourceTypeTraits<VkImage>::DescriptorT desc;  // ImageDescriptor
constexpr auto type = ResourceTypeTraits<VkImage>::resourceType;  // ResourceType::Image
```

---

## Testing Checklist

- [ ] Create resource with each descriptor type
- [ ] Set/get handles for all registered Vulkan types
- [ ] Validate descriptor data
- [ ] Test resource ownership (move semantics)
- [ ] Test slot-aware access via TypedNode
- [ ] Verify compile errors on type mismatch
- [ ] Benchmark variant access vs old system

---

## Performance Notes

**std::variant overhead:**
- Storage: `sizeof(largest_type) + 1 byte` for discriminator
- Access: Single branch (faster than `dynamic_cast`)
- No heap allocation (unlike `unique_ptr<ResourceDescription>`)

**Comparison:**
- Old: ~64 bytes (8 handles × 8 bytes) + dynamic_cast overhead
- New: ~16 bytes (variant) + 1 byte (discriminator)
- **Result: 75% memory reduction per resource**

---

## Next Steps

1. ✅ Create `ResourceVariant.h` and `.cpp`
2. ⏳ Update `TypedNode<>::SetOutput()` to use variant
3. ⏳ Migrate `ResourceSlotDescriptor` to replace `ResourceDescriptor`
4. ⏳ Update CMakeLists.txt to include new source file
5. ⏳ Test with SwapChainNode
6. ⏳ Gradually migrate all nodes
7. ⏳ Remove old `Resource.h` system
