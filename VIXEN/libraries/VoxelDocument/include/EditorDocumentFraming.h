#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace Vixen::Editor {

// The four look-dev review angles as orbit yaws (radians), the quarter turns the look-dev captures use
// (application/main/tests/test_lookdev_capture.cpp `yaws`); SetViewAngle(index) selects one.
inline constexpr std::array<float, 4> kEditorViewAngleYaws{{0.0f, 1.57079632679f, 3.14159265359f, 4.71238898038f}};

struct EditorCameraFrame {
    glm::vec3 center{0.0f};
    float distance = 1.0f;
    float worldRadius = 0.0f;
};

// The editor can preview the same document through either the procedural recipe or the
// stored material-bearing bake. Keep the affine placement and the camera bounds derived from
// one value object; otherwise the stored octree's grid-centred frame and the procedural origin
// silently disagree when a material is first authored.
struct EditorBodyPlacement {
    glm::vec3 origin{0.0f};
    float renderScale = 1.0f;
    glm::vec3 worldCenter{0.0f};
    float worldRadius = 0.0f;
};

inline EditorBodyPlacement PlaceEditorBody(const glm::vec3& recipeCenter, float recipeRadius,
                                           bool storedMaterialBake,
                                           float worldGridSize = 10.0f,
                                           int bakeResolution = 64,
                                           float storedRenderScale = 6.4f,
                                           float recipeUnitsPerVoxel = 1.0f) {
    EditorBodyPlacement placement{};
    if (!std::isfinite(worldGridSize) || !std::isfinite(storedRenderScale) ||
        !std::isfinite(recipeUnitsPerVoxel) || recipeUnitsPerVoxel <= 0.0f || bakeResolution <= 0)
        return placement;
    if (!storedMaterialBake) {
        placement.worldCenter = recipeCenter;
        placement.worldRadius = std::max(0.01f, std::abs(recipeRadius));
        return placement;
    }

    // One recipe unit occupies 1/recipeUnitsPerVoxel bake voxels.  The instance scale
    // shrinks the fixed [0,10] octree span by the same factor, so the two conversions
    // cancel in document space rather than multiplying the unit scale twice.
    const float effectiveRenderScale = storedRenderScale * recipeUnitsPerVoxel;
    const float gridToWorld = (worldGridSize / static_cast<float>(bakeResolution)) *
                              effectiveRenderScale;
    const float kBakeCenter = static_cast<float>(bakeResolution) * 0.5f;
    placement.renderScale = effectiveRenderScale;
    placement.origin = glm::vec3(-kBakeCenter * gridToWorld);
    placement.worldCenter = placement.origin +
                            (glm::vec3(kBakeCenter) + recipeCenter / recipeUnitsPerVoxel) *
                                gridToWorld;
    placement.worldRadius = std::max(0.01f,
                                     std::abs(recipeRadius / recipeUnitsPerVoxel) * gridToWorld);
    return placement;
}

inline EditorCameraFrame FitEditorCameraToWorldBounds(
    const glm::vec3& worldCenter, float worldRadius,
    float verticalFovDegrees = 45.0f, float padding = 1.15f) {
    EditorCameraFrame frame{};
    if (!std::isfinite(worldRadius) || !std::isfinite(verticalFovDegrees) || !std::isfinite(padding))
        return frame;
    frame.center = worldCenter;
    frame.worldRadius = std::max(0.01f, std::abs(worldRadius));
    constexpr float kPi = 3.14159265358979323846f;
    const float halfFov = std::clamp(verticalFovDegrees, 1.0f, 179.0f) * (kPi / 360.0f);
    const float safeSine = std::max(std::sin(halfFov), 1e-4f);
    frame.distance = std::clamp(frame.worldRadius / safeSine * std::max(padding, 1.0f),
                                0.1f, 120.0f);
    return frame;
}

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
    return FitEditorCameraToWorldBounds((bakeCenter + recipeCenter) * gridToWorld,
                                        std::abs(recipeRadius) * gridToWorld,
                                        verticalFovDegrees, padding);
}

}  // namespace Vixen::Editor
