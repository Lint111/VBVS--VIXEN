#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfRecipes.h"

#include <algorithm>
#include <cmath>

using namespace Vixen::SVO;

namespace {
constexpr float kEps = 1e-3f;
RecipeParams sphereParams(float r) { return RecipeParams{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; }
}

// --- Pure sphere SDF: distance is exact. ---
TEST(SdfRecipes, SphereSignedDistanceIsExact) {
    const glm::vec3 c(10.0f, 0.0f, 0.0f);
    const RecipeParams rp = sphereParams(4.0f);
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c, c, rp), -4.0f, kEps);                       // centre: -radius
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c + glm::vec3(4.0f,0,0), c, rp), 0.0f, kEps);  // surface: 0
    EXPECT_NEAR(evalSdf(RECIPE_SPHERE, c + glm::vec3(6.0f,0,0), c, rp), 2.0f, kEps);  // outside: +2
}

// --- Gradient at the surface is the outward radial normal. ---
TEST(SdfRecipes, SphereGradientIsRadialNormal) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp = sphereParams(3.0f);
    const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 2.0f, -2.0f));
    const glm::vec3 surf = c + dir * 3.0f;
    const glm::vec3 n = sdfGradient(RECIPE_SPHERE, surf, c, rp);
    EXPECT_NEAR(n.x, dir.x, 2e-2f);
    EXPECT_NEAR(n.y, dir.y, 2e-2f);
    EXPECT_NEAR(n.z, dir.z, 2e-2f);
}

// --- Sphere-trace: a ray through the centre hits the near surface; normal radial. ---
TEST(SdfRecipes, TraceHitsNearSurface) {
    const glm::vec3 c(0.0f, 0.0f, 20.0f);
    const RecipeParams rp = sphereParams(5.0f);
    const glm::vec3 ro(0.0f);
    const glm::vec3 rd(0.0f, 0.0f, 1.0f);
    const TraceHit h = traceProcedural(RECIPE_SPHERE, ro, rd, c, rp);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.t, 15.0f, 5e-2f);                 // 20 - radius 5
    EXPECT_NEAR(h.normal.z, -1.0f, 2e-2f);          // faces the camera
}

// --- Sphere-trace miss: ray that passes outside the bounding sphere. ---
TEST(SdfRecipes, TraceMissesWhenOffAxis) {
    const glm::vec3 c(0.0f, 0.0f, 20.0f);
    const RecipeParams rp = sphereParams(5.0f);
    const TraceHit h = traceProcedural(RECIPE_SPHERE, glm::vec3(0.0f), glm::vec3(0,1,0), c, rp);
    EXPECT_FALSE(h.hit);
}

// --- Displaced sphere: deterministic and bounded by the amplitude. ---
TEST(SdfRecipes, DisplacedSphereIsDeterministicAndBounded) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp{6.0f, 0.5f, 0.7f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 p = c + glm::vec3(6.0f, 0.0f, 0.0f);
    const float a = evalSdf(RECIPE_DISPLACED_SPHERE, p, c, rp);
    const float b = evalSdf(RECIPE_DISPLACED_SPHERE, p, c, rp);
    EXPECT_FLOAT_EQ(a, b);                                            // deterministic
    const float plain = evalSdf(RECIPE_SPHERE, p, c, rp);
    EXPECT_LE(std::abs(a - plain), rp.displaceAmp + kEps);            // bounded by amplitude
}

// Conservative Lipschitz step must land the trace ON the iso-surface at the LIVE
// displaced-planet params (amp=2, freq=0.5, r=24) — guards against tunneling/speckle.
TEST(SdfRecipes, DisplacedTraceLandsOnIsoSurfaceAtLiveParams) {
    const glm::vec3 c(0.0f, 0.0f, 50.0f);
    const RecipeParams rp{24.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f};
    const TraceHit h = traceProcedural(RECIPE_DISPLACED_SPHERE, glm::vec3(0.0f),
                                       glm::vec3(0.0f, 0.0f, 1.0f), c, rp);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(evalSdf(RECIPE_DISPLACED_SPHERE, h.point, c, rp), 0.0f, 5e-3f);  // on surface
    EXPECT_NEAR(glm::length(h.normal), 1.0f, 1e-3f);                            // unit normal, no NaN
}

// R716: the displaced recipe's step bound L = 1 + amp*freq is sound (no sampled gradient
// exceeds it) and sharp (the sampled maximum reaches it), at the live params.
TEST(SdfRecipes, DisplacedLipschitzBoundIsSoundAndSharp) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp{24.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f};
    const float bound = recipeLipschitzBound(RECIPE_DISPLACED_SPHERE, rp);
    EXPECT_FLOAT_EQ(bound, 2.0f);
    EXPECT_FLOAT_EQ(recipeLipschitzBound(RECIPE_SPHERE, sphereParams(5.0f)), 1.0f);
    float maxDisplacementSlope = 0.0f;
    float maxFieldSlope = 0.0f;
    const float h = 1e-2f;
    for (int i = 0; i < 64; ++i)
        for (int j = 0; j < 64; ++j)
            for (int k = 0; k < 16; ++k) {
                const glm::vec3 p(-26.0f + 52.0f * i / 63.0f, -26.0f + 52.0f * j / 63.0f,
                                  20.0f + 6.0f * k / 15.0f);
                glm::vec3 gd, gf;
                for (int a = 0; a < 3; ++a) {
                    glm::vec3 e(0.0f); e[a] = h;
                    gd[a] = rp.displaceAmp * (sdfDisplacement(p + e, rp.displaceFreq) -
                                              sdfDisplacement(p - e, rp.displaceFreq)) / (2.0f * h);
                    gf[a] = (evalSdf(RECIPE_DISPLACED_SPHERE, p + e, c, rp) -
                             evalSdf(RECIPE_DISPLACED_SPHERE, p - e, c, rp)) / (2.0f * h);
                }
                maxDisplacementSlope = std::max(maxDisplacementSlope, glm::length(gd));
                maxFieldSlope = std::max(maxFieldSlope, glm::length(gf));
            }
    EXPECT_LE(maxFieldSlope, bound * 1.001f);
    EXPECT_GE(maxDisplacementSlope, 0.98f * (bound - 1.0f));
}

// R716: the default scene's owner view (camera 236 units from the displaced body, 1080
// rows at 45 degrees). Every ray over the body's top-right quadrant, where the owner
// capture showed background lines, resolves without exhausting its budget, and every hit
// sits on the surface within the ray's footprint.
TEST(SdfRecipes, DisplacedGrazingRaysNeverExhaustAtOwnerView) {
    const glm::vec3 c(0.0f);
    const RecipeParams rp{24.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 ro(0.0f, 0.0f, 236.0f);
    const float coef = 2.0f * std::tan(glm::radians(45.0f) / 1080.0f / 2.0f);
    const float tanHalf = std::tan(glm::radians(22.5f));
    int exhausted = 0, hits = 0, misses = 0;
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 160; ++x) {
            const float ndcX = (x + 0.5f) / 540.0f, ndcY = (y + 0.5f) / 540.0f;
            const glm::vec3 rd = glm::normalize(glm::vec3(ndcX * tanHalf, ndcY * tanHalf, -1.0f));
            const TraceHit h = traceProcedural(RECIPE_DISPLACED_SPHERE, ro, rd, c, rp, 0.0f, coef);
            exhausted += h.budgetExhausted ? 1 : 0;
            if (!h.hit) { ++misses; continue; }
            ++hits;
            EXPECT_LE(std::fabs(evalSdf(RECIPE_DISPLACED_SPHERE, h.point, c, rp)), h.t * coef)
                << "pixel " << x << "," << y;
        }
    EXPECT_EQ(exhausted, 0);
    EXPECT_GT(hits, 10000);
    EXPECT_GT(misses, 1000);
}
