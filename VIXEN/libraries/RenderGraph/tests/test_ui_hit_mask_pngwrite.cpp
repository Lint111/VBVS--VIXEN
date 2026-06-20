// test_ui_hit_mask_pngwrite.cpp — tiny PNG writer for test_ui_hit_mask's Image test ONLY.
//
// stb_image_write's IMPLEMENTATION is not in RenderGraph's link closure (the read side, stbi_load,
// comes from VulkanResources' STBTextureLoader; the write side lives in Profiler, which this test
// target does not link). So we compile the writer here, in a dedicated translation unit, to emit a
// 2x2 RGBA mask to disk for the round-trip. Isolated to avoid any STB_IMAGE_*_IMPLEMENTATION ODR
// clash with the read implementation linked from VulkanResources.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdint>
#include <vector>

// Returns non-zero on success. `px` must be a tightly-packed 2x2 RGBA buffer (16 bytes).
int WriteTestPng2x2(const char* path, const std::vector<uint8_t>& px) {
    if (px.size() != static_cast<size_t>(2 * 2 * 4)) {
        return 0;
    }
    // width=2, height=2, comp=4 (RGBA), row stride = 2*4 bytes.
    return stbi_write_png(path, 2, 2, 4, px.data(), 2 * 4);
}
