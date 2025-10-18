#pragma once

#include "TextureLoader.h"

namespace Vixen::TextureHandling {

// Texture loader for uncompressed formats (PNG, JPG, BMP, TGA) using STB library
class STBTextureLoader : public TextureLoader {
public:
    STBTextureLoader(VulkanDevice* device, VkCommandPool commandPool);

protected:
    PixelData LoadPixelData(const char* fileName) override;
    void FreePixelData(PixelData& data) override;
};

} // namespace Vixen::TextureHandling
