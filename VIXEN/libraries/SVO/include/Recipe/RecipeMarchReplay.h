#pragma once
// ============================================================================
// RecipeMarchReplay.h - CPU replay of the registry recipe march.
// 1:1 MIRROR of traceUberRecipeBodyImpl, recipeEntryFaceGradient, recipeFieldGradient and
// sampleRecipeOccupancy in shaders/SdfRecipes.glsl. Keep the constants, the operation order and
// the exits identical: a capture pixel is diagnosed by replaying its camera ray and its shadow
// ray here with the field the GPU walks (evalRecipe over the registry's packed rows, R619.1).
//
// Diagnosis only. Single precision like the shader, but a GPU may contract a multiply-add, so a
// replay agrees with the capture to rounding, not to the bit; the caller compares the replay
// with the pixel's HitRecord before it trusts a classification.
// ============================================================================
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Vixen::SVO::Recipe {

struct RecipeMarchReplayInput {
    glm::vec3 boundCenter{0.0f};
    float boundRadius = 0.0f;
    float relaxation = 1.0f;
    glm::vec3 ro{0.0f};
    glm::vec3 rd{0.0f, 0.0f, 1.0f};
    float conservativeStepScale = 1.0f;
    float pixelAngle = 0.0f;
    float tMin = 0.0f;
    float tMax = 3.0e38f;
    bool occlusionOnly = false;
    // The recipe's own occupancy grid (dim^3 values, x fastest); dim 0 = none.
    std::uint32_t gridDim = 0;
    glm::vec3 gridAabbMin{0.0f};
    float gridCellSize = 0.0f;
    std::span<const float> grid;
};

enum class RecipeMarchExit : std::uint8_t {
    BoundMiss,    // the ray misses the bound sphere or its interval is empty
    Hit,          // a sample at or below EPS_MIN
    RefinedHit,   // a sign bracket found by the cone refinement
    LeftInterval, // marched past min(tFar, tMax)
    StartInside,  // an any-hit ray that never left its start surface inside the interval
    Budget,       // step budget used up
};

struct RecipeMarchSample {
    float t = 0.0f;
    float d = 0.0f;
    float step = 0.0f;       // step taken after this sample (0 on the last)
    bool startSurface = false; // any-hit: still inside the surface the ray starts on
};

struct RecipeMarchReplay {
    bool hit = false;
    RecipeMarchExit exit = RecipeMarchExit::BoundMiss;
    float hitT = 0.0f;
    float outsideT = 0.0f;       // last sample known outside, as the normal rule received it
    glm::vec3 gradient{0.0f};    // nearest-hit rays only, not normalised
    bool gradientRetreated = false; // the entry-face rule took the normal behind the hit
    float tNear = 0.0f;
    float tEnd = 0.0f;
    std::vector<RecipeMarchSample> samples;
};

namespace MarchReplayDetail {
inline constexpr float kNoGridSentinel = 1.0e30f;

inline float SampleOccupancy(const RecipeMarchReplayInput& in, glm::vec3 p) {
    if (in.gridDim == 0u || in.gridCellSize <= 0.0f) return kNoGridSentinel;
    const glm::vec3 local = (p - in.gridAabbMin) / in.gridCellSize;
    const float dim = static_cast<float>(in.gridDim);
    if (local.x < 0.0f || local.y < 0.0f || local.z < 0.0f ||
        local.x >= dim || local.y >= dim || local.z >= dim) return kNoGridSentinel;
    const glm::ivec3 cell = glm::clamp(glm::ivec3(glm::floor(local)), glm::ivec3(0),
                                       glm::ivec3(static_cast<int>(in.gridDim) - 1));
    const std::size_t index = (static_cast<std::size_t>(cell.z) * in.gridDim +
                               static_cast<std::size_t>(cell.y)) * in.gridDim +
                              static_cast<std::size_t>(cell.x);
    return index < in.grid.size() ? in.grid[index] : kNoGridSentinel;
}

template <class Field>
glm::vec3 FieldGradient(Field& field, glm::vec3 p) {
    const float h = 1e-3f;
    return glm::vec3(field(p + glm::vec3(h, 0, 0)) - field(p - glm::vec3(h, 0, 0)),
                     field(p + glm::vec3(0, h, 0)) - field(p - glm::vec3(0, h, 0)),
                     field(p + glm::vec3(0, 0, h)) - field(p - glm::vec3(0, 0, h)));
}

template <class Field>
glm::vec3 EntryFaceGradient(Field& field, glm::vec3 ro, glm::vec3 rd, float hitT, float outsideT,
                            bool& retreated) {
    const float kTap = 1e-3f;
    const float kCreaseLengthSquared = (0.9f * 2.0f * kTap) * (0.9f * 2.0f * kTap);
    const float kRetreat = 4.0f * kTap;
    glm::vec3 samplePoint = ro + rd * hitT;
    glm::vec3 gradient(0.0f);
    retreated = false;
    for (int pass = 0; pass < 2; ++pass) {
        const glm::vec3 candidate = FieldGradient(field, samplePoint);
        if (pass == 0) {
            gradient = candidate;
            if (glm::dot(candidate, rd) < 0.0f && glm::dot(candidate, candidate) >= kCreaseLengthSquared) break;
            samplePoint = ro + rd * (std::min(outsideT, hitT) - kRetreat);
        } else if (glm::dot(candidate, rd) < 0.0f) {
            gradient = candidate;
            retreated = true;
        }
    }
    return gradient;
}
} // namespace MarchReplayDetail

// `field(p)` is the recipe's distance at the instance-local point p.
template <class Field>
[[nodiscard]] RecipeMarchReplay ReplayRecipeMarch(const RecipeMarchReplayInput& in, Field&& field) {
    using namespace MarchReplayDetail;
    RecipeMarchReplay out;
    const glm::vec3 ro = in.ro;
    const glm::vec3 rd = in.rd;
    const glm::vec3 oc = ro - in.boundCenter;
    const float b = glm::dot(oc, rd);
    const float c = glm::dot(oc, oc) - in.boundRadius * in.boundRadius;
    const float disc = b * b - c;
    if (disc < 0.0f) return out;
    const float sq = std::sqrt(disc);
    const float tNear = std::max(-b - sq, std::max(0.0f, in.tMin));
    const float tFar = -b + sq;
    if (tFar < 0.0f) return out;
    const float tEnd = std::min(tFar, in.tMax);
    out.tNear = tNear;
    out.tEnd = tEnd;
    if (tEnd < tNear) return out;

    float t = tNear;
    const float EPS_MIN = 5e-6f;
    const float CONE_EPSILON_SCALE = 0.25f;
    const float CONE_PROBE_SCALE = 1.0f;
    const float raySpan = std::max(tEnd - tNear, 0.0f);
    const int stepBudget = std::clamp(32 + static_cast<int>(std::ceil(raySpan * 160.0f)), 32, 2048);
    float lastRefinementT = -1e30f;
    bool leftStartSurface = !in.occlusionOnly;
    for (int i = 0; i < stepBudget; ++i) {
        glm::vec3 p = ro + rd * t;
        const float d = field(p);
        out.samples.push_back({t, d, 0.0f, !leftStartSurface});
        const float hitEpsilon = std::max(EPS_MIN, std::max(t, 0.0f) * std::max(in.pixelAngle, 0.0f) *
                                                       CONE_EPSILON_SCALE);
        if (!leftStartSurface) {
            if (d > EPS_MIN) {
                leftStartSurface = true;
                out.samples.back().startSurface = false;
            } else {
                const float step = std::max(-d, std::max(4.0f * EPS_MIN, t));
                out.samples.back().step = step;
                t += step;
                if (t > tEnd) {
                    out.exit = RecipeMarchExit::StartInside;
                    return out;
                }
                continue;
            }
        }
        if (d <= EPS_MIN) {
            if (!in.occlusionOnly)
                out.gradient = EntryFaceGradient(field, ro, rd, t, t, out.gradientRetreated);
            out.hit = true;
            out.hitT = t;
            out.outsideT = t;
            out.exit = RecipeMarchExit::Hit;
            return out;
        }
        if (d <= hitEpsilon) {
            const float probeSpan = std::max(std::max(t, 0.0f) * std::max(in.pixelAngle, 0.0f) *
                                             CONE_PROBE_SCALE, 1e-3f);
            if (t - lastRefinementT >= probeSpan) {
                lastRefinementT = t;
                float loT = t;
                float loD = d;
                float hiT = t;
                float hiD = d;
                bool bracketFound = false;
                const float beforeT = t - probeSpan;
                const float afterT = t + probeSpan;
                if (beforeT >= tNear && afterT <= tEnd) {
                    const float beforeD = field(ro + rd * beforeT);
                    const float afterD = field(ro + rd * afterT);
                    const float raySlope = (afterD - beforeD) / (2.0f * probeSpan);
                    const float rayCurvature = (afterD - 2.0f * d + beforeD) / (probeSpan * probeSpan);
                    if (rayCurvature > 1e-8f && raySlope < 0.0f) {
                        const float discriminant = raySlope * raySlope - 2.0f * rayCurvature * d;
                        if (discriminant >= 0.0f) {
                            const float rootOffset = (-raySlope - std::sqrt(discriminant)) / rayCurvature;
                            if (rootOffset > 0.0f && rootOffset <= 2.0f * probeSpan) {
                                hiT = t + rootOffset;
                                hiD = field(ro + rd * hiT);
                                if (hiD <= 0.0f) bracketFound = true;
                            }
                        }
                    }
                }
                if (!bracketFound) {
                    for (int scan = 1; scan <= 4; ++scan) {
                        const float sampleT = t + probeSpan * (0.5f * static_cast<float>(scan));
                        if (sampleT > tEnd) break;
                        const float sampleD = field(ro + rd * sampleT);
                        if (sampleD <= 0.0f) {
                            hiT = sampleT;
                            hiD = sampleD;
                            bracketFound = true;
                            break;
                        }
                        loT = sampleT;
                        loD = sampleD;
                    }
                }
                if (bracketFound) {
                    const float midT = 0.5f * (loT + hiT);
                    const float midD = field(ro + rd * midT);
                    if (midD > 0.0f) {
                        loT = midT;
                        loD = midD;
                    } else {
                        hiT = midT;
                        hiD = midD;
                    }
                    const float rootFraction = std::clamp(loD / (loD - hiD), 0.0f, 1.0f);
                    t = loT + (hiT - loT) * rootFraction;
                    if (!in.occlusionOnly)
                        out.gradient = EntryFaceGradient(field, ro, rd, t, loT, out.gradientRetreated);
                    out.hit = true;
                    out.hitT = t;
                    out.outsideT = loT;
                    out.exit = RecipeMarchExit::RefinedHit;
                    out.samples.push_back({t, field(ro + rd * t), 0.0f, false});
                    return out;
                }
            }
        }
        const float gridBound = SampleOccupancy(in, p);
        const bool gridBoundIsReal = !in.occlusionOnly || gridBound < kNoGridSentinel;
        float step = (in.gridDim != 0u && gridBoundIsReal)
            ? std::max(d * in.relaxation, std::min(gridBound, d * in.relaxation * 8.0f))
            : d * in.relaxation;
        const float stepScale = std::clamp(in.conservativeStepScale, 0.0f, 1.0f);
        if (stepScale < 1.0f) step *= stepScale;
        out.samples.back().step = step;
        t += step;
        if (t > tEnd) {
            out.exit = RecipeMarchExit::LeftInterval;
            return out;
        }
    }
    out.exit = RecipeMarchExit::Budget;
    return out;
}

} // namespace Vixen::SVO::Recipe
