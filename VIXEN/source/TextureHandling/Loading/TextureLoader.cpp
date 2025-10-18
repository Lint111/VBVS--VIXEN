#include "TextureHandling/Loading/TextureLoader.h"
#include "VulkanResources/VulkanDevice.h"
#include "wrapper.h"
#include <iostream>

namespace Vixen::TextureHandling {

TextureLoader::TextureLoader(VulkanDevice* device, VkCommandPool commandPool)
    : deviceObj(device), cmdPool(commandPool) {
    std::cout << "[TextureLoader] Base constructor - device=" << (void*)device << ", cmdPool=" << (void*)(uintptr_t)commandPool << std::endl;
}

TextureData TextureLoader::Load(const char* fileName, const TextureLoadConfig& config) {
    std::cout << "[TextureLoader::Load] START" << std::endl;
    TextureData texture{};

    std::cout << "[TextureLoader::Load] Calling LoadPixelData..." << std::endl;
    // Load pixel data (library-specific)
    PixelData pixelData = LoadPixelData(fileName);

    // Upload based on config mode
    if (config.uploadMode == TextureLoadConfig::UploadMode::Linear) {
        UploadLinear(pixelData, &texture, config);
    } else {
        UploadOptimal(pixelData, &texture, config);
    }

    // Free pixel data (library-specific)
    FreePixelData(pixelData);

    return texture;
}

void TextureLoader::UploadLinear(
    const PixelData& pixelData,
    TextureData* texture,
    const TextureLoadConfig& config
) {
    // Create image with linear tiling
    CreateImage(
        texture,
        config.usage,
        config.format,
        VK_IMAGE_TILING_LINEAR,
        pixelData.width,
        pixelData.height,
        pixelData.mipLevels
    );

    // Map and copy directly to image memory
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = 0;
    subresource.arrayLayer = 0;

    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(deviceObj->device, texture->image, &subresource, &layout);

    void* data;
    VkResult result = vkMapMemory(deviceObj->device, texture->mem, 0, texture->memAllocInfo.allocationSize, 0, &data);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to map image memory! Error: " << result << std::endl;
        exit(1);
    }

    memcpy(data, pixelData.pixels, static_cast<size_t>(pixelData.size));
    vkUnmapMemory(deviceObj->device, texture->mem);

    // Transition layout using command buffer
    CommandBufferMgr::AllocateCommandBuffer(&deviceObj->device, cmdPool, &texture->cmdTexture);
    CommandBufferMgr::BeginCommandBuffer(texture->cmdTexture);

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = pixelData.mipLevels;
    subresourceRange.layerCount = 1;

    texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    SetImageLayout(
        texture->image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_PREINITIALIZED,
        texture->imageLayout,
        subresourceRange,
        texture->cmdTexture
    );

    CommandBufferMgr::EndCommandBuffer(texture->cmdTexture);

    // Submit
    VkFence fence;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(deviceObj->device, &fenceCI, nullptr, &fence);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create fence! Error: " << result << std::endl;
        exit(1);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &texture->cmdTexture;

    result = vkQueueSubmit(deviceObj->queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to submit queue! Error: " << result << std::endl;
        exit(1);
    }

    result = vkWaitForFences(deviceObj->device, 1, &fence, VK_TRUE, 10000000000);
    if (result != VK_SUCCESS) {
        std::cerr << "Wait for fence timeout! Error: " << result << std::endl;
        exit(1);
    }

    vkDestroyFence(deviceObj->device, fence, nullptr);

    // Create image view and sampler
    CreateImageView(texture, config.format, pixelData.mipLevels);
    CreateSampler(texture, pixelData.mipLevels);
}

void TextureLoader::UploadOptimal(
    const PixelData& pixelData,
    TextureData* texture,
    const TextureLoadConfig& config
) {
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = pixelData.size;
    bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(deviceObj->device, &bufferCI, nullptr, &stagingBuffer);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create staging buffer! Error: " << result << std::endl;
        exit(1);
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(deviceObj->device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    auto memTypeResult = deviceObj->MemoryTypeFromProperties(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (!memTypeResult.has_value()) {
        std::cerr << "Failed to find suitable memory type!" << std::endl;
        vkDestroyBuffer(deviceObj->device, stagingBuffer, nullptr);
        exit(1);
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(deviceObj->device, &allocInfo, nullptr, &stagingMemory);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate staging memory! Error: " << result << std::endl;
        vkDestroyBuffer(deviceObj->device, stagingBuffer, nullptr);
        exit(1);
    }

    result = vkBindBufferMemory(deviceObj->device, stagingBuffer, stagingMemory, 0);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to bind staging buffer memory! Error: " << result << std::endl;
        vkFreeMemory(deviceObj->device, stagingMemory, nullptr);
        vkDestroyBuffer(deviceObj->device, stagingBuffer, nullptr);
        exit(1);
    }

    // Copy pixel data to staging buffer
    void* data;
    result = vkMapMemory(deviceObj->device, stagingMemory, 0, pixelData.size, 0, &data);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to map staging memory! Error: " << result << std::endl;
        vkFreeMemory(deviceObj->device, stagingMemory, nullptr);
        vkDestroyBuffer(deviceObj->device, stagingBuffer, nullptr);
        exit(1);
    }
    memcpy(data, pixelData.pixels, static_cast<size_t>(pixelData.size));
    vkUnmapMemory(deviceObj->device, stagingMemory);

    // Create image with optimal tiling
    CreateImage(
        texture,
        config.usage,
        config.format,
        VK_IMAGE_TILING_OPTIMAL,
        pixelData.width,
        pixelData.height,
        pixelData.mipLevels
    );

    // Allocate command buffer for texture upload
    CommandBufferMgr::AllocateCommandBuffer(&deviceObj->device, cmdPool, &texture->cmdTexture);
    CommandBufferMgr::BeginCommandBuffer(texture->cmdTexture);

    // Transition to transfer destination
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = pixelData.mipLevels;
    subresourceRange.layerCount = 1;

    SetImageLayout(
        texture->image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        subresourceRange,
        texture->cmdTexture
    );

    // Copy buffer to image (handle mipmaps if needed)
    size_t bufferOffset = 0;
    for (uint32_t mip = 0; mip < pixelData.mipLevels; ++mip) {
        VkBufferImageCopy region{};
        region.bufferOffset = bufferOffset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            std::max(1u, pixelData.width >> mip),
            std::max(1u, pixelData.height >> mip),
            1
        };

        vkCmdCopyBufferToImage(
            texture->cmdTexture,
            stagingBuffer,
            texture->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        bufferOffset += region.imageExtent.width * region.imageExtent.height * 4; // RGBA
    }

    // Transition to shader read
    texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    SetImageLayout(
        texture->image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        texture->imageLayout,
        subresourceRange,
        texture->cmdTexture
    );

    CommandBufferMgr::EndCommandBuffer(texture->cmdTexture);

    // Submit and wait
    VkFence fence;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(deviceObj->device, &fenceCI, nullptr, &fence);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create fence! Error: " << result << std::endl;
        exit(1);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &texture->cmdTexture;

    CommandBufferMgr::SubmitCommandBuffer(deviceObj->queue, &texture->cmdTexture, &submitInfo, fence);

    result = vkWaitForFences(deviceObj->device, 1, &fence, VK_TRUE, 10000000000);
    if (result != VK_SUCCESS) {
        std::cerr << "Wait for fence timeout! Error: " << result << std::endl;
        exit(1);
    }

    vkDestroyFence(deviceObj->device, fence, nullptr);
    vkFreeMemory(deviceObj->device, stagingMemory, nullptr);
    vkDestroyBuffer(deviceObj->device, stagingBuffer, nullptr);

    // Create image view and sampler
    CreateImageView(texture, config.format, pixelData.mipLevels);
    CreateSampler(texture, pixelData.mipLevels);
}

void TextureLoader::CreateImage(
    TextureData* texture,
    VkImageUsageFlags usage,
    VkFormat format,
    VkImageTiling tiling,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels
) {
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent.width = width;
    imageCI.extent.height = height;
    imageCI.extent.depth = 1;
    imageCI.mipLevels = mipLevels;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = tiling;
    imageCI.usage = usage;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = (tiling == VK_IMAGE_TILING_LINEAR)
        ? VK_IMAGE_LAYOUT_PREINITIALIZED
        : VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(deviceObj->device, &imageCI, nullptr, &texture->image);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create image! Error: " << result << std::endl;
        exit(1);
    }

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(deviceObj->device, texture->image, &memReqs);

    texture->memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    texture->memAllocInfo.allocationSize = memReqs.size;

    // Linear needs host-visible memory, optimal needs device-local
    VkMemoryPropertyFlags memProps = (tiling == VK_IMAGE_TILING_LINEAR)
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto memTypeResult = deviceObj->MemoryTypeFromProperties(memReqs.memoryTypeBits, memProps);
    if (!memTypeResult.has_value()) {
        std::cerr << "Failed to find suitable memory type!" << std::endl;
        vkDestroyImage(deviceObj->device, texture->image, nullptr);
        exit(1);
    }
    texture->memAllocInfo.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(deviceObj->device, &texture->memAllocInfo, nullptr, &texture->mem);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate image memory! Error: " << result << std::endl;
        exit(1);
    }

    result = vkBindImageMemory(deviceObj->device, texture->image, texture->mem, 0);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to bind image memory! Error: " << result << std::endl;
        exit(1);
    }

    texture->textureWidth = width;
    texture->textureHeight = height;
    texture->minMapLevels = mipLevels;
}

void TextureLoader::CreateImageView(
    TextureData* texture,
    VkFormat format,
    uint32_t mipLevels
) {
    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = texture->image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = mipLevels;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(deviceObj->device, &viewCI, nullptr, &texture->view);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create image view! Error: " << result << std::endl;
        exit(1);
    }
}

void TextureLoader::CreateSampler(
    TextureData* texture,
    uint32_t mipLevels
) {
    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCI.mipLodBias = 0.0f;
    samplerCI.anisotropyEnable = VK_TRUE;
    samplerCI.maxAnisotropy = 16.0f;
    samplerCI.compareEnable = VK_FALSE;
    samplerCI.compareOp = VK_COMPARE_OP_NEVER;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = static_cast<float>(mipLevels);
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerCI.unnormalizedCoordinates = VK_FALSE;

    VkResult result = vkCreateSampler(deviceObj->device, &samplerCI, nullptr, &texture->sampler);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create sampler! Error: " << result << std::endl;
        exit(1);
    }

    texture->descsImageInfo.sampler = texture->sampler;
    texture->descsImageInfo.imageView = texture->view;
    texture->descsImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void TextureLoader::SetImageLayout(
    VkImage image,
    VkImageAspectFlags aspectMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageSubresourceRange& subresourceRange,
    VkCommandBuffer cmdBuf
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = 0; // To be set
    barrier.dstAccessMask = 0; // To be set
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    // Source access mask
    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcAccessMask = 0;
            break;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        default:
            break;
    }

    // Destination access mask
    switch (newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        default:
            break;
    }

    // Determine appropriate pipeline stages based on layouts and access masks
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    // Source stage
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        srcStage = VK_PIPELINE_STAGE_HOST_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    // Destination stage
    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else {
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(
        cmdBuf,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

} // namespace Vixen::TextureHandling
