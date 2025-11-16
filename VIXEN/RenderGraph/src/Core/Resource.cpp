#include "Core/Resource.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace Vixen::RenderGraph {

// Constructor moved to header (template)

Resource::~Resource() {
    // Resources should be explicitly destroyed via Destroy()
    // This is to ensure proper device context
}

Resource::Resource(Resource&& other) noexcept
    : type(other.type)
    , lifetime(other.lifetime)
    , description(std::move(other.description))
    , image(other.image)
    , buffer(other.buffer)
    , memory(other.memory)
    , imageView(other.imageView)
    , commandPool(other.commandPool)
    , device(other.device)
    , memorySize(other.memorySize)
    , currentLayout(other.currentLayout)
    , owningNode(other.owningNode)
    , deviceDependency(other.deviceDependency)
{
    // Invalidate the moved-from object
    other.image = VK_NULL_HANDLE;
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.imageView = VK_NULL_HANDLE;
    other.commandPool = VK_NULL_HANDLE;
    other.device = VK_NULL_HANDLE;
    other.memorySize = 0;
    other.owningNode = nullptr;
    other.deviceDependency = nullptr;
}

Resource& Resource::operator=(Resource&& other) noexcept {
    if (this != &other) {
        type = other.type;
        lifetime = other.lifetime;
        description = std::move(other.description);
        image = other.image;
        buffer = other.buffer;
        memory = other.memory;
        imageView = other.imageView;
        commandPool = other.commandPool;
        device = other.device;
        memorySize = other.memorySize;
        currentLayout = other.currentLayout;
        owningNode = other.owningNode;
        deviceDependency = other.deviceDependency;

        // Invalidate the moved-from object
        other.image = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.imageView = VK_NULL_HANDLE;
        other.commandPool = VK_NULL_HANDLE;
        other.device = VK_NULL_HANDLE;
        other.memorySize = 0;
        other.owningNode = nullptr;
        other.deviceDependency = nullptr;
    }
    return *this;
}

// Get* methods moved to header (template)

void Resource::AllocateImage(VkDevice device, const ImageDescription& desc) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    
    // Image type based on resource type
    if (type == ResourceType::Image3D) {
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
    } else {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
    }
    
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = desc.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.samples = desc.samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Convert usage flags
    imageInfo.usage = 0;
    if (HasUsage(desc.usage, ResourceUsage::TransferSrc))
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasUsage(desc.usage, ResourceUsage::TransferDst))
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (HasUsage(desc.usage, ResourceUsage::Sampled))
        imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasUsage(desc.usage, ResourceUsage::Storage))
        imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasUsage(desc.usage, ResourceUsage::ColorAttachment))
        imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasUsage(desc.usage, ResourceUsage::DepthStencilAttachment))
        imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasUsage(desc.usage, ResourceUsage::InputAttachment))
        imageInfo.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    // Cubemap flag
    if (type == ResourceType::CubeMap) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VkResult result = vkCreateImage(device, &imageInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image resource");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        VK_NULL_HANDLE, // Will need to pass physical device
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(device, image, memory, 0);
    memorySize = memRequirements.size;
    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void Resource::AllocateBuffer(VkDevice device, const BufferDescription& desc) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Convert usage flags
    bufferInfo.usage = 0;
    if (HasUsage(desc.usage, ResourceUsage::TransferSrc))
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (HasUsage(desc.usage, ResourceUsage::TransferDst))
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (HasUsage(desc.usage, ResourceUsage::VertexBuffer))
        bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasUsage(desc.usage, ResourceUsage::IndexBuffer))
        bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasUsage(desc.usage, ResourceUsage::UniformBuffer))
        bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasUsage(desc.usage, ResourceUsage::StorageBuffer))
        bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasUsage(desc.usage, ResourceUsage::IndirectBuffer))
        bufferInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer resource");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        VK_NULL_HANDLE, // Will need to pass physical device
        memRequirements.memoryTypeBits,
        desc.memoryProperties
    );

    result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, memory, 0);
    memorySize = memRequirements.size;
}

void Resource::CreateImageView(VkDevice device, VkImageAspectFlags aspectMask) {
    if (image == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create image view: image not allocated");
    }

    const ImageDescription* desc = GetImageDescription();
    if (!desc) {
        throw std::runtime_error("Cannot create image view: invalid description");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    
    if (type == ResourceType::CubeMap) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (type == ResourceType::Image3D) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    } else {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    }
    
    viewInfo.format = desc->format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc->mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc->arrayLayers;

    VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }
}

void Resource::Destroy(VkDevice device) {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }

    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }

    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    memorySize = 0;
    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

uint32_t Resource::FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    // TODO: This needs access to physical device
    // For now, return 0 - this will be handled by ResourceAllocator
    // which has access to the device object
    return 0;
}

} // namespace Vixen::RenderGraph
