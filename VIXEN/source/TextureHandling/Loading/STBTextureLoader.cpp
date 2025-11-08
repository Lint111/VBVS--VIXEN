#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TextureHandling/Loading/STBTextureLoader.h"
#include <iostream>

namespace Vixen::TextureHandling {

STBTextureLoader::STBTextureLoader(VulkanDevice* device, VkCommandPool commandPool)
    : TextureLoader(device, commandPool) {
    // Constructor - removed debug output
}

PixelData STBTextureLoader::LoadPixelData(const char* fileName) {
    PixelData data;

    int width, height, channels;
    stbi_uc* pixels = stbi_load(fileName, &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        // TODO: Add logger - texture load failure
        std::cerr << "[STBTextureLoader] ERROR: Failed to load texture file: " << fileName << std::endl;
        std::cerr << "[STBTextureLoader]        STB Error: " << stbi_failure_reason() << std::endl;
        exit(1);
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
