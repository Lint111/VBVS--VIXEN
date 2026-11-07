#pragma once

// glm's clip convention is set once, by the build: GLM_FORCE_DEPTH_ZERO_TO_ONE and GLM_FORCE_RADIANS
// are INTERFACE compile definitions of the glm targets (dependencies/native/CMakeLists.txt). glm
// latches its configuration at the first include, so a source-level #define would make the convention
// depend on include order. This header only verifies the result; never #define the macros here.
#include <glm/detail/setup.hpp>

static_assert((GLM_CONFIG_CLIP_CONTROL & GLM_CLIP_CONTROL_ZO_BIT) != 0,
              "glm must use Vulkan's 0..1 depth range: link glm::glm (it carries GLM_FORCE_DEPTH_ZERO_TO_ONE)");
