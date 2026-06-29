#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "SdfRecipes.h"

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
