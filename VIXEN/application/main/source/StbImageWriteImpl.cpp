// View Contract Inc-2 Task 5: the ONE translation unit in VixenApp's link that provides the
// stb_image_write implementation (RenderTargetReadback.h only declares/calls stbi_write_png,
// header-only, and deliberately does not define STB_IMAGE_WRITE_IMPLEMENTATION itself -- doing so
// in a header would emit a duplicate-definition link error the moment more than one TU includes
// it). Mirrors application/editor/source/StbImageWriteImpl.cpp -- VixenApp needs its own copy
// because CaptureHudFrameToPng (VulkanGraphApplication.cpp) is the first VixenApp caller of
// RenderTargetReadback.h, and Profiler's FrameCapture.cpp (the OTHER source of this symbol in the
// tree) is not on VixenApp's link line.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
