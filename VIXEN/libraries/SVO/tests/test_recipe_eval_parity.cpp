#include <gtest/gtest.h>
#include "Recipe/SdfRecipeEval.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
using namespace Vixen::SVO::Recipe;

static SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0]=c.x; in.data[1]=c.y; in.data[2]=c.z; in.data[3]=r; return in;
}
static SdfInstruction unionOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Union; return in; }
static SdfInstruction boxOp(glm::vec3 b) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0]=b.x; in.data[1]=b.y; in.data[2]=b.z; return in;
}
static SdfInstruction smoothUnionOp(float k) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::SmoothUnion;
    in.data[2] = k; return in;  // k = Data0.z
}
static SdfInstruction mirrorXOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorX; return in; }
static SdfInstruction restorePosOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RestorePos; return in; }

TEST(RecipeEvalParity, SphereUnionMatchesAnalytic) {
    const glm::vec3 c0(-1,0,0), c1(1,0,0); const float r0=1.0f, r1=1.0f;
    SdfInstruction prog[] = { sphere(c0,r0), sphere(c1,r1), unionOp() };
    auto golden = [&](glm::vec3 p){ return std::min(glm::length(p-c0)-r0, glm::length(p-c1)-r1); };
    for (glm::vec3 p : { glm::vec3(-1,0,0), glm::vec3(1,0,0), glm::vec3(0,0,0), glm::vec3(0,2,0), glm::vec3(3,1,-1) })
        EXPECT_NEAR(evalRecipe(prog, 3, p), golden(p), 1e-5f) << "at " << p.x << "," << p.y << "," << p.z;
}

// P2.4 M2b — MirrorX(SmoothUnion(Box, Sphere)) parity against analytic oracle.
// Recipe: [MirrorX, Box(b), Sphere(c,r), SmoothUnion(k), RestorePos]
// Oracle: su( box(mirrorX(p), b), sph(mirrorX(p), c, r), k )
// Includes a negative-x point to exercise the mirror fold.
TEST(RecipeEvalParity, MirrorXSmoothUnionBoxSphereMatchesAnalytic) {
    const glm::vec3 b(0.8f, 0.5f, 0.5f);  // box halfExtents
    const glm::vec3 c(1.5f, 0.0f, 0.0f);  // sphere center (in mirrored space, offset +x)
    const float r = 0.5f;
    const float k = 0.3f;

    // Recipe: MirrorX, Box, Sphere, SmoothUnion, RestorePos  (5 instructions)
    SdfInstruction prog[] = {
        mirrorXOp(),
        boxOp(b),
        sphere(c, r),
        smoothUnionOp(k),
        restorePosOp()
    };

    // Analytic oracle — mirrors evalRecipe exactly
    auto mirrorX = [](glm::vec3 q) { return glm::vec3(std::abs(q.x), q.y, q.z); };
    auto boxDist = [](glm::vec3 q, glm::vec3 hext) {
        glm::vec3 d = glm::abs(q) - hext;
        return glm::length(glm::max(d, glm::vec3(0.0f)))
             + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
    };
    auto sphDist = [](glm::vec3 q, glm::vec3 ctr, float rad) {
        return glm::length(q - ctr) - rad;
    };
    auto su = [](float a, float b_, float k_) {
        float h = glm::clamp(0.5f + 0.5f * (b_ - a) / k_, 0.0f, 1.0f);
        return glm::mix(b_, a, h) - k_ * h * (1.0f - h);
    };

    auto oracle = [&](glm::vec3 p) {
        glm::vec3 mp = mirrorX(p);
        return su(boxDist(mp, b), sphDist(mp, c, r), k);
    };

    // ≥4 sample points; one has negative x to exercise the mirror
    const glm::vec3 pts[] = {
        glm::vec3( 2.0f,  0.0f,  0.0f),   // +x: near sphere
        glm::vec3(-2.0f,  0.0f,  0.0f),   // -x: mirror folds to same as +x
        glm::vec3( 0.0f,  0.0f,  0.0f),   // origin: inside box region
        glm::vec3( 3.0f,  1.0f,  0.5f),   // outside, off-axis
        glm::vec3(-1.5f, -0.3f,  0.2f),   // -x, slightly off-axis
    };
    for (const glm::vec3& p : pts) {
        EXPECT_NEAR(evalRecipe(prog, 5, p), oracle(p), 1e-4f)
            << "at p=(" << p.x << "," << p.y << "," << p.z << ")";
    }
}
