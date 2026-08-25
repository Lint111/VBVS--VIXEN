#ifndef VIXEN_FOOTPRINT_REGIME_GLSL
#define VIXEN_FOOTPRINT_REGIME_GLSL

// Keep these values in lockstep with Recipe/FootprintRegime.h.  The classifier uses
// the same scale-free body-radius footprint as the CPU path; no camera/FOV-specific
// tuning is allowed to split the two consumers.
const float kSurfaceDetailDistanceOverRadius = 8.0;
const float kOrbitalDistanceOverRadius = 64.0;
const float kSystemDistanceOverRadius = 512.0;

// Values mirror FootprintRegime's explicit underlying values:
// 0 = SurfaceDetail, 1 = Orbital, 2 = System, 3 = DeepField.
uint classifyFootprintRegime(float cameraDistance, float bodyRadius) {
    if (!(bodyRadius > 0.0) || isinf(bodyRadius) || isnan(bodyRadius)) return 3u;

    float distance = max(0.0, cameraDistance);
    if (isinf(distance) || isnan(distance)) return 3u;

    float distanceOverRadius = distance / bodyRadius;
    if (distanceOverRadius < kSurfaceDetailDistanceOverRadius) return 0u;
    if (distanceOverRadius < kOrbitalDistanceOverRadius) return 1u;
    if (distanceOverRadius < kSystemDistanceOverRadius) return 2u;
    return 3u;
}

#endif
