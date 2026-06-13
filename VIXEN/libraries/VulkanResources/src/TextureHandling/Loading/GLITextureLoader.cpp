#include "TextureHandling/Loading/GLITextureLoader.h"
#include "Logger.h"
#include <iostream>

namespace Vixen::TextureHandling {

VulkanResult<PixelData> GLITextureLoader::LoadPixelData(const char* fileName) {
    PixelData data;

    gli::texture2d image2D(gli::load(fileName));
    if (image2D.empty()) {
        std::string reason = std::string("Failed to load texture file: ") + fileName + ". GLI only supports DDS, KTX 1.0, and KMG formats";
        LOG_ERROR(reason);
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, reason});
    }

    data.width = static_cast<uint32_t>(image2D.extent().x);
    data.height = static_cast<uint32_t>(image2D.extent().y);
    data.mipLevels = static_cast<uint32_t>(image2D.levels());
    data.size = static_cast<VkDeviceSize>(image2D.size());

    // GLI stores data contiguously - copy it for ownership
    void* pixelsCopy = malloc(data.size);
    if (!pixelsCopy) {
        LOG_ERROR("Failed to allocate memory for pixel data!");
        return std::unexpected(VulkanError{VK_ERROR_INITIALIZATION_FAILED, "Failed to allocate memory for pixel data (GLI LoadPixelData)"});
    }
    memcpy(pixelsCopy, image2D.data(), data.size);
    data.pixels = pixelsCopy;

    return data;
}

void GLITextureLoader::FreePixelData(PixelData& data) {
    if (data.pixels) {
        free(data.pixels);
        data.pixels = nullptr;
    }
}

} // namespace Vixen::TextureHandling
