# Textures

This library is the VIXEN runtime for the kernel texture primitive. It owns STB
decode, the existing RGBA8 Vulkan upload path, resident texture caching, and the
default checkerboard fallback. It does not define a parallel texture type or
descriptor model.

The `stb` FetchContent dependency is pinned to commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20` (the stb image v2.30 tree). stb is
dual-licensed: users may apply the MIT license or the public-domain dedication;
the source notice is retained in `stb_image.h`.

Mip generation is intentionally not performed in this slice. The upload code
still assumes four bytes per texel for each supplied mip, and still submits then
waits on the graphics queue; those are follow-up slices documented by R649.
