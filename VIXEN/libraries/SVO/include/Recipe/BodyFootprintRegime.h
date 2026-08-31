#pragma once

#include <algorithm>
#include <cmath>

namespace Vixen::SVO {

// The rendering regime is selected from the body's scale-free apparent footprint.
// `bodyRadius / cameraDistance` is intentionally independent of world units, so the
// same policy applies to a moon, planet, or star.  The thresholds are powers of two
// because they line up with the renderer's scale ladder and are easy to audit against
// the GLSL twin in shaders/BodyFootprintRegime.glsl.
enum class BodyFootprintRegime : unsigned int {
    SurfaceDetail = 0,
    Orbital = 1,
    System = 2,
    DeepField = 3,
};

inline constexpr float kSurfaceDetailDistanceOverRadius = 8.0f;
inline constexpr float kOrbitalDistanceOverRadius = 64.0f;
inline constexpr float kSystemDistanceOverRadius = 512.0f;

// Classify the body from camera distance and body radius.  Thresholds are strict:
// equality belongs to the coarser regime, which makes adjacent callers agree at a
// transition and preserves monotonicity as distance increases.  Invalid or non-positive
// radii conservatively select DeepField because they cannot safely request SDF detail.
[[nodiscard]] inline BodyFootprintRegime ClassifyBodyFootprintRegime(
    float cameraDistance, float bodyRadius) noexcept {
    if (!(bodyRadius > 0.0f) || !std::isfinite(bodyRadius))
        return BodyFootprintRegime::DeepField;

    const float distance = std::max(0.0f, cameraDistance);
    if (!std::isfinite(distance)) return BodyFootprintRegime::DeepField;

    const float distanceOverRadius = distance / bodyRadius;
    if (distanceOverRadius < kSurfaceDetailDistanceOverRadius)
        return BodyFootprintRegime::SurfaceDetail;
    if (distanceOverRadius < kOrbitalDistanceOverRadius)
        return BodyFootprintRegime::Orbital;
    if (distanceOverRadius < kSystemDistanceOverRadius)
        return BodyFootprintRegime::System;
    return BodyFootprintRegime::DeepField;
}

} // namespace Vixen::SVO
