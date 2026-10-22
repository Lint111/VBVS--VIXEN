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

// This header uses std::max/glm::length heavily. When included after <windows.h> (pulled in
// transitively on the Windows build via Vulkan/GTest), the `min`/`max`/`abs` function-like
// macros mangle every `std::max(`/`glm::min(` into a syntax error. NOMINMAX only helps before
// windows.h is seen, which a header cannot guarantee, so drop the macros outright — no C++
// code wants them. Same convention as GpuTraversalMirror.h.
#undef min
#undef max
#undef abs

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

// R716: the recipe's own bound on how far evalSdf can overstate the true distance.
// |grad(length(d) - r)| = 1. For s = sin(fx)sin(fy)sin(fz), |grad s|^2 / f^2 =
// (1-a)bc + a(1-b)c + ab(1-c) with a,b,c the three sin^2 terms in [0,1]; it is multilinear,
// so its maximum over [0,1]^3 is at a vertex, where it is 1. Hence L = 1 + amp * freq
// (sharp: reached where two sines are +-1 and the third crosses zero).
inline float recipeLipschitzBound(uint32_t recipeId, const RecipeParams& rp) {
    return recipeId == RECIPE_DISPLACED_SPHERE
        ? 1.0f + std::fabs(rp.displaceAmp) * std::fabs(rp.displaceFreq) : 1.0f;
}

struct TraceHit {
    bool      hit    = false;
    float     t      = 0.0f;
    glm::vec3 point  = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    bool      budgetExhausted = false;  // R716: counted, resolved by closest approach
};

// Sphere-trace the recipe within its bounding sphere. Returns the nearest hit.
// footprintBase + t * footprintPerT is the ray's footprint width at t (the pixel cone).
// Where the Lipschitz-bounded step falls below the footprint the estimate is inconclusive
// at pixel scale, so the march samples at footprint spacing and resolves the first sign
// change (last known-outside to known-inside) by bisection. A zero footprint keeps the
// plain bounded sphere-trace. Mirrors traceProceduralBody in SdfRecipes.glsl.
inline TraceHit traceProcedural(uint32_t recipeId, const glm::vec3& ro, const glm::vec3& rd,
                                const glm::vec3& center, const RecipeParams& rp,
                                float footprintBase = 0.0f, float footprintPerT = 0.0f) {
    const float maxDisp = (recipeId == RECIPE_DISPLACED_SPHERE) ? std::fabs(rp.displaceAmp) : 0.0f;
    const float shellR  = rp.radius + maxDisp;
    const float boundR  = shellR + 0.01f;

    // Ray vs bounding sphere → [tNear, tFar].
    const glm::vec3 oc = ro - center;
    const float b  = glm::dot(oc, rd);
    const float c  = glm::dot(oc, oc) - boundR * boundR;
    const float disc = b * b - c;
    TraceHit miss;
    if (disc < 0.0f) return miss;
    const float sq    = std::sqrt(disc);
    // glm::max, not std::max: avoids the windows.h max() macro collision when this header
    // is compiled in a translation unit that transitively pulls in windows.h (e.g. via
    // vulkan.h/GLFW in a test that also links BodyOctreeSceneNode.h) — the rest of this
    // file already uses glm:: for its vector math, so this is consistent, not a workaround.
    const float tNear = glm::max(-b - sq, 0.0f);
    const float tFar  = -b + sq;
    if (tFar < 0.0f) return miss;

    float t = tNear;
    // Every step advances at least the footprint, which only grows with t, so a positive
    // entry footprint bounds the steps the interval can take. A zero footprint keeps the
    // plain march's budget, and its exhaustion is reported.
    const int   MAX_STEPS  = 128;
    const float entryFootprint = footprintBase + tNear * footprintPerT;
    const int   stepBudget = entryFootprint > 0.0f
        ? static_cast<int>(glm::min(std::ceil((tFar - tNear) / entryFootprint), 1048576.0f)) + 1
        : MAX_STEPS;
    const float stepScale  = 1.0f / recipeLipschitzBound(recipeId, rp);
    bool  haveOutside = false;
    float outsideT = tNear;
    float closestT = tNear;
    float closestGap = 1e30f;
    for (int i = 0; i < stepBudget; ++i) {
        glm::vec3 p = ro + rd * t;
        const float d = evalSdf(recipeId, p, center, rp);
        if (d <= 0.0f) {
            if (haveOutside) {
                float insideT = t;
                for (int refine = 0; refine < 32; ++refine) {
                    const float midT = 0.5f * (outsideT + insideT);
                    if (midT <= outsideT || midT >= insideT) break;
                    if (evalSdf(recipeId, ro + rd * midT, center, rp) > 0.0f) outsideT = midT;
                    else insideT = midT;
                }
                t = outsideT;
                p = ro + rd * t;
            }
            return TraceHit{true, t, p, sdfGradient(recipeId, p, center, rp)};
        }
        const float footprint = glm::max(footprintBase + t * footprintPerT, 0.0f);
        if (d - footprint < closestGap) {
            closestGap = d - footprint;
            closestT = t;
        }
        haveOutside = true;
        outsideT = t;
        // Outside the displacement shell the surface is at least |d| - shellR away.
        const float shellGap = glm::length(p - center) - shellR;
        const float step = glm::max(glm::max(d * stepScale, shellGap), footprint);
        const float nextT = t + step;
        if (nextT <= t) return TraceHit{true, t, p, sdfGradient(recipeId, p, center, rp)};
        if (t >= tFar) return miss;
        t = glm::min(nextT, tFar);
    }
    TraceHit exhausted = miss;
    exhausted.budgetExhausted = true;
    if (closestGap <= 0.0f) {
        const glm::vec3 p = ro + rd * closestT;
        exhausted.hit = true;
        exhausted.t = closestT;
        exhausted.point = p;
        exhausted.normal = sdfGradient(recipeId, p, center, rp);
    }
    return exhausted;
}

}  // namespace Vixen::SVO
