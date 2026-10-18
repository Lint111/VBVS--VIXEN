#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Textures/STBTextureLoader.h"
#include "Logger.h"
#include <iostream>
#include <string>

namespace Vixen::TextureHandling {

STBTextureLoader::STBTextureLoader(VulkanDevice* device, VkCommandPool commandPool)
    : TextureLoader(device, commandPool) {
    // Constructor - removed debug output
}

VulkanResult<STBTextureLoader::DecodedImage> STBTextureLoader::DecodeFile(const char* fileName) {
    if (fileName == nullptr || fileName[0] == '\0') {
        return std::unexpected(VulkanError{
            VK_ERROR_INITIALIZATION_FAILED,
            "STBTextureLoader::DecodeFile: empty file name"});
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(fileName, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::string reason = std::string("Failed to load texture file: ") + fileName
            + ". STB Error: " + stbi_failure_reason();
        LOG_ERROR(reason);
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, reason});
    }

    DecodedImage decoded{
        std::vector<uint8_t>(pixels, pixels + static_cast<size_t>(width) * height * 4),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)};
    stbi_image_free(pixels);
    return decoded;
}

VulkanResult<PixelData> STBTextureLoader::LoadPixelData(const char* fileName) {
    PixelData data;

    int width, height, channels;
    stbi_uc* pixels = stbi_load(fileName, &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        std::string reason = std::string("Failed to load texture file: ") + fileName + ". STB Error: " + stbi_failure_reason();
        LOG_ERROR(reason);
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, reason});
    }
    data.pixels = pixels;
    data.width = static_cast<uint32_t>(width);
    data.height = static_cast<uint32_t>(height);
    data.mipLevels = 1; // STB doesn't load mipmaps
    data.size = static_cast<VkDeviceSize>(width) * height * 4; // RGBA

    return data;
}

void STBTextureLoader::FreePixelData(PixelData& data) {
    if (data.pixels) {
        stbi_image_free(data.pixels);
        data.pixels = nullptr;
    }
}

} // namespace Vixen::TextureHandling
