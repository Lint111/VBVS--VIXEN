// Example: How to update TypedNode to use ResourceVariant

// BEFORE - TypedNodeInstance.h (old SetOutput implementation)
template<typename SlotType>
void SetOutput(SlotType /*slot*/, size_t arrayIndex, typename SlotType::Type value) {
    static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
    EnsureOutputSlot(SlotType::index, arrayIndex);
    Resource* res = NodeInstance::GetOutput(SlotType::index, arrayIndex);
    SetResourceHandle(res, value);  // Uses manual type-punning
}

// AFTER - TypedNodeInstance.h (new variant-based implementation)
template<typename SlotType>
void SetOutput(SlotType /*slot*/, size_t arrayIndex, typename SlotType::Type value) {
    static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
    EnsureOutputSlot(SlotType::index, arrayIndex);
    Resource* res = NodeInstance::GetOutput(SlotType::index, arrayIndex);
    
    // Type-safe variant access - compiler knows the type from SlotType::Type
    res->SetHandle<typename SlotType::Type>(value);
}

// BEFORE - TypedNodeInstance.h (old GetInput implementation)
template<typename T, typename SlotType>
T GetInput(SlotType /*slot*/, size_t arrayIndex = 0) const {
    Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);
    if (!res) return static_cast<T>(VK_NULL_HANDLE);
    return GetResourceHandle<T>(res);  // Manual type extraction
}

// AFTER - TypedNodeInstance.h (new variant-based implementation)
template<typename T, typename SlotType>
T GetInput(SlotType /*slot*/, size_t arrayIndex = 0) const {
    Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);
    if (!res) return T{};  // Return null handle
    
    // Type-safe variant access
    return res->GetHandle<T>();
}

// ============================================================================
// MIGRATION EXAMPLE: SwapChainNode
// ============================================================================

// BEFORE - SwapChainNodeConfig.h (using old descriptors)
#include "Core/ResourceConfig.h"

CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig, 1, 3, false) {
    CONSTEXPR_INPUT(SURFACE, VkSurfaceKHR, 0, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN, VkSwapchainKHR, 0, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImage, 1, true);  // Array
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGE_VIEWS, VkImageView, 2, true);  // Array
};

// AFTER - SwapChainNodeConfig.h (using ResourceVariant)
#include "Core/ResourceVariant.h"  // Changed include
#include "Core/ResourceConfig.h"

// No changes needed to config! Macro system is compatible
CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig, 1, 3, false) {
    CONSTEXPR_INPUT(SURFACE, VkSurfaceKHR, 0, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN, VkSwapchainKHR, 0, false);
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImage, 1, true);
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGE_VIEWS, VkImageView, 2, true);
};

// ============================================================================
// MIGRATION EXAMPLE: Creating Resources in RenderGraph
// ============================================================================

// BEFORE - RenderGraph.cpp
std::unique_ptr<Resource> RenderGraph::CreateResource(const ResourceDescriptor& resourceDesc) {
    std::unique_ptr<Resource> resource;
    
    switch (resourceDesc.type) {
        case ResourceType::Image:
            if (auto* imageDesc = dynamic_cast<ImageDescription*>(resourceDesc.description.get())) {
                resource = std::make_unique<Resource>(resourceDesc.type, resourceDesc.lifetime, *imageDesc);
            }
            break;
        // ... more cases
    }
    return resource;
}

// AFTER - RenderGraph.cpp (using ResourceVariant)
std::unique_ptr<Resource> RenderGraph::CreateResource(const ResourceSlotDescriptor& slotDesc) {
    auto resource = std::make_unique<Resource>();
    
    // Visit the variant to create the correct resource type
    std::visit([&](auto&& desc) {
        using DescType = std::decay_t<decltype(desc)>;
        
        if constexpr (!std::is_same_v<DescType, std::monostate>) {
            // Descriptor has actual data - create resource from it
            // (Implementation depends on descriptor type)
            resource->SetLifetime(slotDesc.lifetime);
        }
    }, slotDesc.descriptor);
    
    return resource;
}

// ============================================================================
// COMPLETE EXAMPLE: DepthBufferNode Migration
// ============================================================================

// BEFORE - DepthBufferNode.cpp
void DepthBufferNode::Compile() {
    // Get output resource
    Resource* depthRes = GetOutput(0);
    
    // Get descriptor
    auto* imgDesc = dynamic_cast<ImageDescription*>(depthRes->description.get());
    if (!imgDesc) {
        NODE_LOG_ERROR("Invalid descriptor type for depth buffer");
        return;
    }
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = imgDesc->width;
    imageInfo.extent.height = imgDesc->height;
    imageInfo.format = imgDesc->format;
    
    VkImage depthImage;
    vkCreateImage(device->device, &imageInfo, nullptr, &depthImage);
    
    // Store handle
    depthRes->SetImage(depthImage);
}

// AFTER - DepthBufferNode.cpp (using ResourceVariant)
void DepthBufferNode::Compile() {
    // Get output resource
    Resource* depthRes = GetOutput(DepthBufferConfig::DEPTH_IMAGE_Slot::index);
    
    // Get descriptor (type-safe)
    const ImageDescriptor* imgDesc = depthRes->GetDescriptor<ImageDescriptor>();
    if (!imgDesc) {
        NODE_LOG_ERROR("Invalid descriptor type for depth buffer");
        return;
    }
    
    // Create image (same as before)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = imgDesc->width;
    imageInfo.extent.height = imgDesc->height;
    imageInfo.format = imgDesc->format;
    
    VkImage depthImage;
    vkCreateImage(device->device, &imageInfo, nullptr, &depthImage);
    
    // Store handle (type-safe)
    depthRes->SetHandle<VkImage>(depthImage);
}

// ============================================================================
// TESTING: Compile-Time Safety
// ============================================================================

void TestTypeSafety() {
    // Create image resource
    auto res = Resource::Create<VkImage>(ImageDescriptor{1920, 1080, VK_FORMAT_R8G8B8A8_UNORM});
    
    // Set handle - CORRECT
    res.SetHandle<VkImage>(myImage);
    
    // Get handle - CORRECT
    VkImage img = res.GetHandle<VkImage>();
    
    // Type mismatch - COMPILE ERROR
    // res.SetHandle<VkBuffer>(myBuffer);  // ERROR: cannot convert VkBuffer to VkImage
    
    // Wrong getter - Returns VK_NULL_HANDLE (runtime safe fallback)
    VkBuffer buf = res.GetHandle<VkBuffer>();  // Returns null, but compiles
    
    // Best practice: Use slot types
    // Compiler enforces VkImage because slot definition says so
    SetOutput(DepthBufferConfig::DEPTH_IMAGE, 0, myImage);  // VkImage required
}
