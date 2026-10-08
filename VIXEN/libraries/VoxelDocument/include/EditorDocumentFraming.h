#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Vixen::Editor {

struct EditorCameraFrame {
    glm::vec3 center{0.0f};
    float distance = 1.0f;
    float worldRadius = 0.0f;
};

// Convert a conservative recipe-space sphere into the world frame used by the baked octree,
// then place an orbit camera far enough away for the entire sphere to fit vertically. Recipe
// coordinates are voxel units around bakeCenter; the octree spans worldGridSize units and the
// editor instance applies renderScale after that mapping.
inline EditorCameraFrame FitEditorCameraToBounds(
    const glm::vec3& recipeCenter, float recipeRadius,
    const glm::vec3& bakeCenter, int bakeResolution,
    float worldGridSize = 10.0f, float renderScale = 5.0f,
    float verticalFovDegrees = 45.0f, float padding = 1.15f) {
    EditorCameraFrame frame{};
    if (bakeResolution <= 0 || !std::isfinite(recipeRadius) ||
        !std::isfinite(worldGridSize) || !std::isfinite(renderScale) ||
        !std::isfinite(verticalFovDegrees) || !std::isfinite(padding)) {
        return frame;
    }

    const float gridToWorld = (worldGridSize / static_cast<float>(bakeResolution)) * renderScale;
    frame.center = (bakeCenter + recipeCenter) * gridToWorld;
    frame.worldRadius = std::max(0.01f, std::abs(recipeRadius) * gridToWorld);

    constexpr float kPi = 3.14159265358979323846f;
    const float halfFov = std::clamp(verticalFovDegrees, 1.0f, 179.0f) * (kPi / 360.0f);
    const float sine = std::sin(halfFov);
    const float safeSine = std::max(sine, 1e-4f);
    frame.distance = std::clamp(frame.worldRadius / safeSine * std::max(padding, 1.0f),
                                0.1f, 120.0f);
    return frame;
}

}  // namespace Vixen::Editor
