#pragma once

#include "Textures/TextureLoader.h"

#include <cstdint>
#include <vector>

namespace Vixen::TextureHandling {

// Texture loader for uncompressed formats (PNG, JPG, BMP, TGA) using STB library
class STBTextureLoader : public TextureLoader {
public:
    STBTextureLoader(VulkanDevice* device, VkCommandPool commandPool);

    // CPU-only decode seam used by the module tests and by the future pack
    // reader. stb_image is MIT/public-domain dual licensed; see the module's
    // third-party notice for the pinned revision and attribution.
    struct DecodedImage {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    static VulkanResult<DecodedImage> DecodeFile(const char* fileName);

protected:
    VulkanResult<PixelData> LoadPixelData(const char* fileName) override;
    void FreePixelData(PixelData& data) override;
};

} // namespace Vixen::TextureHandling
