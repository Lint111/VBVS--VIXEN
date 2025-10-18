#pragma once

#include "TextureLoader.h"

namespace Vixen::TextureHandling {

// Texture loader for compressed formats (DDS, KTX 1.0) using GLI library
class GLITextureLoader : public TextureLoader {
public:
    using TextureLoader::TextureLoader; // Inherit constructor

protected:
    PixelData LoadPixelData(const char* fileName) override;
    void FreePixelData(PixelData& data) override;
};

} // namespace Vixen::TextureHandling
