#pragma once

// GLM configuration split out of Headers.h. glm latches these macros at its FIRST include in a
// translation unit (setup.hpp is include-guarded), so the position of this include relative to the
// first glm header is behaviour: a header that formerly included Headers.h includes this at the same
// place so every translation unit keeps the clip-control configuration it had before the split.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan uses [0,1] depth range
