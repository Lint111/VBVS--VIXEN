#pragma once
// ============================================================================
// SdfRecipes.h — CPU mirror of shaders/SdfRecipes.glsl (Increment 1).
// Analytic SDF recipe library for the Procedural body provider.
// MUST stay 1:1 with the GLSL: same formulas, same operation order, same
// constants. Parity is unit-tested here and visually gated by the live app run.
// ============================================================================
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdint>
#include <cmath>

namespace Vixen::SVO {

// Recipe ids (mirror SdfRecipes.glsl #defines).
enum RecipeId : uint32_t {
    RECIPE_SPHERE           = 0u,
    RECIPE_DISPLACED_SPHERE = 1u,
};

// Provider kinds (mirror BodyInstanceRayMarch.comp #defines + ShellOctreeGpu.h).
enum ProviderKind : uint32_t {
    PROVIDER_STORED     = 0u,   // existing ESVO octree path
    PROVIDER_PROCEDURAL = 1u,   // analytic SDF recipe (this file)
};

// recipeParams layout: .x = radius, .y = displaceAmp, .z = displaceFreq, rest spare.
struct RecipeParams {
    float radius;
    float displaceAmp;
    float displaceFreq;
    float spare3;
    float spare4;
    float spare5;
};

// Deterministic, LUT-free displacement (parity-friendly: pure transcendental).
inline float sdfDisplacement(const glm::vec3& d, float freq) {
    return std::sin(freq * d.x) * std::sin(freq * d.y) * std::sin(freq * d.z);
}

// Signed distance for a recipe, evaluated in WORLD space about `center`.
inline float evalSdf(uint32_t recipeId, const glm::vec3& p,
                     const glm::vec3& center, const RecipeParams& rp) {
    const glm::vec3 d = p - center;
    float dist = glm::length(d) - rp.radius;
    if (recipeId == RECIPE_DISPLACED_SPHERE) {
        dist += rp.displaceAmp * sdfDisplacement(d, rp.displaceFreq);
    }
    return dist;
}

// Outward surface normal = normalized gradient (central differences).
inline glm::vec3 sdfGradient(uint32_t recipeId, const glm::vec3& p,
                             const glm::vec3& center, const RecipeParams& rp) {
    const float h = 1e-3f;
    const glm::vec3 dx(h, 0, 0), dy(0, h, 0), dz(0, 0, h);
    const float gx = evalSdf(recipeId, p + dx, center, rp) - evalSdf(recipeId, p - dx, center, rp);
    const float gy = evalSdf(recipeId, p + dy, center, rp) - evalSdf(recipeId, p - dy, center, rp);
    const float gz = evalSdf(recipeId, p + dz, center, rp) - evalSdf(recipeId, p - dz, center, rp);
    return glm::normalize(glm::vec3(gx, gy, gz));
}

struct TraceHit {
    bool      hit    = false;
    float     t      = 0.0f;
    glm::vec3 point  = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
};

// Sphere-trace the recipe within its bounding sphere. Returns the nearest hit.
inline TraceHit traceProcedural(uint32_t recipeId, const glm::vec3& ro, const glm::vec3& rd,
                                const glm::vec3& center, const RecipeParams& rp) {
    const float maxDisp = (recipeId == RECIPE_DISPLACED_SPHERE) ? rp.displaceAmp : 0.0f;
    const float boundR  = rp.radius + maxDisp + 0.01f;

    // Ray vs bounding sphere → [tNear, tFar].
    const glm::vec3 oc = ro - center;
    const float b  = glm::dot(oc, rd);
    const float c  = glm::dot(oc, oc) - boundR * boundR;
    const float disc = b * b - c;
    TraceHit miss;
    if (disc < 0.0f) return miss;
    const float sq    = std::sqrt(disc);
    const float tNear = std::max(-b - sq, 0.0f);
    const float tFar  = -b + sq;
    if (tFar < 0.0f) return miss;

    float t = tNear;
    const int   MAX_STEPS  = 128;
    const float EPS        = 1e-3f;
    const float stepScale  = (recipeId == RECIPE_DISPLACED_SPHERE) ? 0.7f : 1.0f; // Lipschitz guard
    for (int i = 0; i < MAX_STEPS; ++i) {
        const glm::vec3 p = ro + rd * t;
        const float d = evalSdf(recipeId, p, center, rp);
        if (d < EPS) {
            return TraceHit{true, t, p, sdfGradient(recipeId, p, center, rp)};
        }
        t += d * stepScale;
        if (t > tFar) break;
    }
    return miss;
}

}  // namespace Vixen::SVO
