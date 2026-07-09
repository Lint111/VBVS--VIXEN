// Inc-2b Task 3: the ONE translation unit in vixen_editor's link that provides the stb_image_write
// implementation (RenderTargetReadback.h only declares/calls stbi_write_png, header-only, and
// deliberately does not define STB_IMAGE_WRITE_IMPLEMENTATION itself -- doing so in a header would
// emit a duplicate-definition link error the moment more than one TU includes it). vixen_editor has
// no other link-time source of this symbol (unlike the RenderGraph test binaries, which each define
// it locally in their own single test .cpp, or application/benchmark, which links Profiler's
// FrameCapture.cpp -- neither is on vixen_editor's link line).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
