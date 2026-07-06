#pragma once
// FrustumCull.h — Sparse-Mip ESVO LOD Inc1, M4b (Task 10 part 2, frustum piece).
//
// Per-tree frustum containment test for the brick-residency gate. This project has
// no existing wired view-frustum culling test to reuse (verified 2026-07-06):
// SVOStreaming.h's isBrickInFrustum is a declared-but-never-defined/never-called
// stub (no SVOStreaming.cpp, no callers anywhere in libraries/ or application/), and
// no plane-extraction-from-view-projection code exists anywhere in the codebase. This
// is a new, minimal, standard 6-plane frustum built directly from the camera basis
// vectors already carried in CameraData/the shader push-constant block (cameraPos,
// cameraDir, cameraUp, cameraRight, fov, aspect) — no new camera state needed.
//
// Pure CPU, evaluated once per tree per residency re-check (NOT per-ray/per-pixel) —
// same call cadence as ResolvableLevel.h's minResolvableLevel.

#include <glm/glm.hpp>
#include <cmath>
#include <array>

namespace Vixen::SVO {

// A view frustum as 6 inward-facing planes (n.p + d >= 0 means "inside"),
// stored as vec4(nx, ny, nz, d).
struct Frustum {
    std::array<glm::vec4, 6> planes;
};

// Hysteresis margin (Task 10/M4b): the residency-check frustum is built with an
// extra half-angle pad on top of the actual render/draw-culling FOV, so panning the
// camera back and forth near the frustum boundary doesn't thrash brick upload/evict
// every frame. This is a tunable, not a derived quantity (unlike minResolvableLevel),
// so it is a named constant rather than inlined. 5 degrees is wide enough to absorb a
// normal look-around's frame-to-frame jitter (typical mouse-look deltas are well under
// 1 degree/frame at usual sensitivity) while still being negligible next to any normal
// FOV (45-90 degrees) — i.e. it meaningfully damps boundary thrash without perceptibly
// widening the tested cone. Kept separate from the render/draw-culling frustum (which
// must stay tight for correctness) — this pad only applies to the residency decision.
constexpr float kResidencyFrustumHysteresisDeg = 5.0f;

// Build a frustum from camera basis vectors + FOV/aspect, with an optional extra
// half-angle pad added to the vertical FOV (aspect widens the horizontal FOV to
// match, same as the shader's getRayDir()). nearDist/farDist bound the frustum along
// cameraDir; pass 0/very-large to effectively disable near/far culling.
inline Frustum BuildFrustum(
    const glm::vec3& cameraPos,
    const glm::vec3& cameraDir,
    const glm::vec3& cameraUp,
    const glm::vec3& cameraRight,
    float fovDegrees,
    float aspect,
    float nearDist,
    float farDist,
    float extraHalfAngleDegPad = 0.0f) {
    const float halfVFov = (fovDegrees * 0.5f + extraHalfAngleDegPad) * (3.14159265358979323846f / 180.0f);
    const float halfHFov = std::atan(std::tan(halfVFov) * aspect);

    const glm::vec3 fwd   = glm::normalize(cameraDir);
    const glm::vec3 right = glm::normalize(cameraRight);
    const glm::vec3 up    = glm::normalize(cameraUp);

    auto planeFromNormalPoint = [](const glm::vec3& n, const glm::vec3& p) {
        return glm::vec4(n, -glm::dot(n, p));
    };

    Frustum f{};
    // Near / far planes (normals point inward, toward +fwd / -fwd respectively).
    f.planes[0] = planeFromNormalPoint(fwd, cameraPos + fwd * nearDist);
    f.planes[1] = planeFromNormalPoint(-fwd, cameraPos + fwd * farDist);

    // Left / right planes: rotate fwd toward +-right by halfHFov, inward normal
    // points back toward the frustum interior (toward -right for the left plane,
    // toward +right for the right plane).
    const glm::vec3 leftDir  = fwd * std::cos(halfHFov) - right * std::sin(halfHFov);
    const glm::vec3 rightDir = fwd * std::cos(halfHFov) + right * std::sin(halfHFov);
    f.planes[2] = planeFromNormalPoint(glm::normalize(glm::cross(up, leftDir)), cameraPos);
    f.planes[3] = planeFromNormalPoint(glm::normalize(glm::cross(rightDir, up)), cameraPos);

    // Top / bottom planes.
    const glm::vec3 topDir    = fwd * std::cos(halfVFov) + up * std::sin(halfVFov);
    const glm::vec3 bottomDir = fwd * std::cos(halfVFov) - up * std::sin(halfVFov);
    f.planes[4] = planeFromNormalPoint(glm::normalize(glm::cross(right, topDir)), cameraPos);
    f.planes[5] = planeFromNormalPoint(glm::normalize(glm::cross(bottomDir, right)), cameraPos);

    return f;
}

// Sphere-vs-frustum containment: true if the sphere is at least partially inside
// every plane (standard conservative frustum test — no false negatives; may have
// false positives for spheres just outside a corner, which is the correct
// conservative direction for a residency gate: never wrongly evict something
// actually visible).
inline bool SphereIntersectsFrustum(const Frustum& f, const glm::vec3& center, float radius) {
    for (const glm::vec4& p : f.planes) {
        const float dist = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

}  // namespace Vixen::SVO
