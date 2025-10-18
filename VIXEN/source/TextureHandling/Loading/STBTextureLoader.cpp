#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TextureHandling/Loading/STBTextureLoader.h"
#include <iostream>

namespace Vixen::TextureHandling {

STBTextureLoader::STBTextureLoader(VulkanDevice* device, VkCommandPool commandPool)
    : TextureLoader(device, commandPool) {
    printf("[STBTextureLoader] Constructor called\n");
    fflush(stdout);
}

PixelData STBTextureLoader::LoadPixelData(const char* fileName) {
    printf("[STB] LoadPixelData ENTRY\n");
    fflush(stdout);
    PixelData data;

    std::cout << "[STB] Loading: " << fileName << std::endl;
    int width, height, channels;
    stbi_uc* pixels = stbi_load(fileName, &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        std::cerr << "Failed to load texture file: " << fileName << std::endl;
        std::cerr << "STB Error: " << stbi_failure_reason() << std::endl;
        exit(1);
    }

    std::cout << "[STB] Loaded " << width << "x" << height << ", " << channels << " channels" << std::endl;
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
