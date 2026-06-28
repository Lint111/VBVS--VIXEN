#include <gtest/gtest.h>
#include "Recipe/SdfRecipeEval.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>   // glm::quat — for M4b Transform oracle
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

// --- M3b-1 instruction helpers (5 no-position leaf primitives) ---
static SdfInstruction capsuleOp(float halfH, float r) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Capsule;
    in.data[0]=halfH; in.data[1]=r; return in; }
static SdfInstruction cylinderOp(float halfH, float r) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Cylinder;
    in.data[0]=halfH; in.data[1]=r; return in; }
static SdfInstruction torusOp(float majorR, float minorR) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Torus;
    in.data[0]=majorR; in.data[1]=minorR; return in; }
static SdfInstruction boxRoundedOp(glm::vec3 he, float rr) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::BoxRounded;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr; return in; }
static SdfInstruction planeOp(glm::vec3 n, float d) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Plane;
    in.data[0]=n.x; in.data[1]=n.y; in.data[2]=n.z; in.data[3]=d; return in; }

// --- M3a instruction helpers ---
static SdfInstruction subtractOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Subtract; return in; }
static SdfInstruction intersectOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Intersect; return in; }
static SdfInstruction xorOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Xor; return in; }
static SdfInstruction smoothSubtractOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothSubtract; in.data[2]=k; return in; }
static SdfInstruction smoothIntersectOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothIntersect; in.data[2]=k; return in; }
static SdfInstruction smoothMaxOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothMax; in.data[2]=k; return in; }
static SdfInstruction smoothUnionCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothUnionCubic; in.data[2]=k; return in; }
static SdfInstruction smoothSubtractCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothSubtractCubic; in.data[2]=k; return in; }
static SdfInstruction smoothIntersectCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothIntersectCubic; in.data[2]=k; return in; }
static SdfInstruction roundOp(float radius) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Round; in.data[0]=radius; return in; }
static SdfInstruction onionOp(float thickness) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Onion; in.data[0]=thickness; return in; }

// Analytic primitives used in parity oracles (mirror evalRecipe exactly).
static float boxDist(glm::vec3 p, glm::vec3 hext) {
    glm::vec3 d = glm::abs(p) - hext;
    return glm::length(glm::max(d, glm::vec3(0.0f)))
         + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
}
static float sphereDist(glm::vec3 p, glm::vec3 c, float r) {
    return glm::length(p - c) - r;
}

// Oracle helpers for M3b-1 leaf primitives (re-derived from SDFPrimitives.cs — NOT calling SdfCore_*).
static float capsuleDist(glm::vec3 p, float halfH, float r) {
    // mirrors SDFPrimitives.CapsuleVertical:96
    glm::vec3 lp = p;
    lp.y -= std::max(-halfH, std::min(lp.y, halfH));
    return glm::length(lp) - r;
}
static float cylinderDist(glm::vec3 p, float halfH, float r) {
    // mirrors SDFPrimitives.Cylinder:210
    glm::vec2 d(glm::length(glm::vec2(p.x, p.z)) - r, std::abs(p.y) - halfH);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
}
static float torusDist(glm::vec3 p, float majorR, float minorR) {
    // mirrors SDFPrimitives.Torus:293
    glm::vec2 q(glm::length(glm::vec2(p.x, p.z)) - majorR, p.y);
    return glm::length(q) - minorR;
}
static float boxRoundedDist(glm::vec3 p, glm::vec3 he, float rr) {
    // mirrors SDFPrimitives.BoxRounded:194
    glm::vec3 q = glm::abs(p) - he + rr;
    return glm::length(glm::max(q, glm::vec3(0.0f)))
         + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - rr;
}
static float planeDist(glm::vec3 p, glm::vec3 n, float d) {
    // mirrors SDFPrimitives.Plane:267
    return glm::dot(p, n) + d;
}

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

// ============================================================
// P2.4 M3a — 11 new CSG + modifier parity cases.
// Oracles are independent C++ formulas (Global Constraints) — NOT calling SdfCore_*.
// Asymmetric primitives for non-commutative ops:
//   A = Box halfExtents(0.8,0.5,0.5)  (deeper on stack = graph input A = base)
//   B = Sphere center=origin r=0.4    (top of stack    = graph input B = cutter)
// ============================================================

// sat/lerp helpers for inline oracles (clamp to [0,1], linear interpolation).
static float m3a_sat (float x)              { return x<0.f?0.f:(x>1.f?1.f:x); }
static float m3a_lerp(float a,float b,float t){ return a + t*(b-a); }

// --- 1. Subtract (non-commutative) ---
// Recipe [Box, Sphere, Subtract] → Subtract(box, sphere) = max(box, -sphere)
TEST(RecipeEvalParity, M3a_Subtract_MatchesAnalytic) {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f;
    SdfInstruction fwd[] = { boxOp(bH), sphere(sC,sR), subtractOp() };
    SdfInstruction bwd[] = { sphere(sC,sR), boxOp(bH), subtractOp() };  // swapped
    const glm::vec3 pts[] = {
        glm::vec3(0.6f,0.f,0.f), glm::vec3(1.2f,0.f,0.f),
        glm::vec3(0.f,0.6f,0.f), glm::vec3(0.3f,0.3f,0.2f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        EXPECT_NEAR(evalRecipe(fwd,3,p), std::max(a,-b), 1e-5f)
            << "Subtract(box,sphere) at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
    // Non-commutative: Subtract(A,B) != Subtract(B,A) at some point.
    bool asymm = false;
    for (const glm::vec3& p : pts)
        if (std::abs(evalRecipe(fwd,3,p)-evalRecipe(bwd,3,p))>1e-4f){asymm=true;break;}
    EXPECT_TRUE(asymm) << "Subtract must be non-commutative";
}

// --- 2. Intersect ---
// Recipe [Box, Sphere, Intersect] → max(box, sphere)
TEST(RecipeEvalParity, M3a_Intersect_MatchesAnalytic) {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.6f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), intersectOp() };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(1.f,0.f,0.f),
        glm::vec3(0.f,0.5f,0.f), glm::vec3(0.3f,0.2f,0.1f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        EXPECT_NEAR(evalRecipe(prog,3,p), std::max(a,b), 1e-5f)
            << "Intersect at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 3. Xor ---
// Xor(a,b) = max(min(a,b), -max(a,b))
TEST(RecipeEvalParity, M3a_Xor_MatchesAnalytic) {
    const glm::vec3 bH(0.7f,0.7f,0.7f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.6f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), xorOp() };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(0.9f,0.f,0.f),
        glm::vec3(0.f,0.4f,0.f), glm::vec3(0.3f,0.3f,0.3f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        EXPECT_NEAR(evalRecipe(prog,3,p), std::max(std::min(a,b),-std::max(a,b)), 1e-5f)
            << "Xor at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 4. SmoothSubtract (non-commutative, Yeroket form) ---
// h = sat(0.5 - 0.5*(b+a)/k); lerp(a,-b,h) + k*h*(1-h)
TEST(RecipeEvalParity, M3a_SmoothSubtract_MatchesAnalytic) {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f; const float k=0.3f;
    SdfInstruction fwd[] = { boxOp(bH), sphere(sC,sR), smoothSubtractOp(k) };
    SdfInstruction bwd[] = { sphere(sC,sR), boxOp(bH), smoothSubtractOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.6f,0.f,0.f), glm::vec3(1.1f,0.f,0.f),
        glm::vec3(0.f,0.55f,0.f), glm::vec3(0.35f,0.25f,0.1f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=m3a_sat(0.5f - 0.5f*(b+a)/k);
        float expected = m3a_lerp(a,-b,h) + k*h*(1.f-h);
        EXPECT_NEAR(evalRecipe(fwd,3,p), expected, 1e-5f)
            << "SmoothSubtract(box,sphere) at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
    bool asymm = false;
    for (const glm::vec3& p : pts)
        if (std::abs(evalRecipe(fwd,3,p)-evalRecipe(bwd,3,p))>1e-4f){asymm=true;break;}
    EXPECT_TRUE(asymm) << "SmoothSubtract must be non-commutative";
}

// --- 5. SmoothIntersect ---
// h = sat(0.5 - 0.5*(b-a)/k); lerp(b,a,h) + k*h*(1-h)
TEST(RecipeEvalParity, M3a_SmoothIntersect_MatchesAnalytic) {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.25f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), smoothIntersectOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(0.9f,0.f,0.f),
        glm::vec3(0.f,0.6f,0.f), glm::vec3(0.4f,0.3f,0.2f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=m3a_sat(0.5f - 0.5f*(b-a)/k);
        float expected = m3a_lerp(b,a,h) + k*h*(1.f-h);
        EXPECT_NEAR(evalRecipe(prog,3,p), expected, 1e-5f)
            << "SmoothIntersect at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 6. SmoothMax ---
// h = max(k - abs(a-b), 0)/k;  max(a,b) + h^3*k*(1/6)
TEST(RecipeEvalParity, M3a_SmoothMax_MatchesAnalytic) {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), smoothMaxOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(0.9f,0.f,0.f),
        glm::vec3(0.f,0.55f,0.f), glm::vec3(0.3f,0.3f,0.15f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=std::max(k-std::abs(a-b),0.f)/k;
        float expected = std::max(a,b) + h*h*h*k*(1.f/6.f);
        EXPECT_NEAR(evalRecipe(prog,3,p), expected, 1e-5f)
            << "SmoothMax at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 7. SmoothUnionCubic ---
// h = max(k - abs(a-b), 0)/k;  min(a,b) - h^3*k*(1/6)
TEST(RecipeEvalParity, M3a_SmoothUnionCubic_MatchesAnalytic) {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), smoothUnionCubicOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(0.9f,0.f,0.f),
        glm::vec3(0.f,0.6f,0.f), glm::vec3(0.4f,0.3f,0.2f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=std::max(k-std::abs(a-b),0.f)/k;
        float expected = std::min(a,b) - h*h*h*k*(1.f/6.f);
        EXPECT_NEAR(evalRecipe(prog,3,p), expected, 1e-5f)
            << "SmoothUnionCubic at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 8. SmoothSubtractCubic (non-commutative) ---
// h = max(k - abs(a+b), 0)/k;  max(a,-b) + h^3*k*(1/6)
TEST(RecipeEvalParity, M3a_SmoothSubtractCubic_MatchesAnalytic) {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f; const float k=0.3f;
    SdfInstruction fwd[] = { boxOp(bH), sphere(sC,sR), smoothSubtractCubicOp(k) };
    SdfInstruction bwd[] = { sphere(sC,sR), boxOp(bH), smoothSubtractCubicOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.6f,0.f,0.f), glm::vec3(1.1f,0.f,0.f),
        glm::vec3(0.f,0.55f,0.f), glm::vec3(0.35f,0.25f,0.1f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=std::max(k-std::abs(a+b),0.f)/k;
        float expected = std::max(a,-b) + h*h*h*k*(1.f/6.f);
        EXPECT_NEAR(evalRecipe(fwd,3,p), expected, 1e-5f)
            << "SmoothSubtractCubic(box,sphere) at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
    bool asymm = false;
    for (const glm::vec3& p : pts)
        if (std::abs(evalRecipe(fwd,3,p)-evalRecipe(bwd,3,p))>1e-4f){asymm=true;break;}
    EXPECT_TRUE(asymm) << "SmoothSubtractCubic must be non-commutative";
}

// --- 9. SmoothIntersectCubic ---
// h = max(k - abs(a-b), 0)/k;  max(a,b) + h^3*k*(1/6)
TEST(RecipeEvalParity, M3a_SmoothIntersectCubic_MatchesAnalytic) {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    SdfInstruction prog[] = { boxOp(bH), sphere(sC,sR), smoothIntersectCubicOp(k) };
    const glm::vec3 pts[] = {
        glm::vec3(0.5f,0.f,0.f), glm::vec3(0.9f,0.f,0.f),
        glm::vec3(0.f,0.55f,0.f), glm::vec3(0.3f,0.3f,0.15f),
    };
    for (const glm::vec3& p : pts) {
        float a=boxDist(p,bH), b=sphereDist(p,sC,sR);
        float h=std::max(k-std::abs(a-b),0.f)/k;
        float expected = std::max(a,b) + h*h*h*k*(1.f/6.f);
        EXPECT_NEAR(evalRecipe(prog,3,p), expected, 1e-5f)
            << "SmoothIntersectCubic at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 10. Round: recipe [Sphere, Round(r)] → sphere(p) - radius ---
TEST(RecipeEvalParity, M3a_Round_MatchesAnalytic) {
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float rad=0.1f;
    SdfInstruction prog[] = { sphere(sC,sR), roundOp(rad) };
    const glm::vec3 pts[] = {
        glm::vec3(0.f,0.f,0.f), glm::vec3(0.5f,0.f,0.f),
        glm::vec3(0.7f,0.f,0.f), glm::vec3(0.3f,0.3f,0.f),
    };
    for (const glm::vec3& p : pts) {
        float d = sphereDist(p,sC,sR);
        EXPECT_NEAR(evalRecipe(prog,2,p), d-rad, 1e-5f)
            << "Round at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// --- 11. Onion: recipe [Sphere, Onion(t)] → abs(sphere(p)) - thickness ---
TEST(RecipeEvalParity, M3a_Onion_MatchesAnalytic) {
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float thick=0.05f;
    SdfInstruction prog[] = { sphere(sC,sR), onionOp(thick) };
    const glm::vec3 pts[] = {
        glm::vec3(0.f,0.f,0.f),   // inside sphere: d < 0, abs gives shell
        glm::vec3(0.5f,0.f,0.f),  // on surface
        glm::vec3(0.7f,0.f,0.f),  // outside
        glm::vec3(0.3f,0.3f,0.f),
    };
    for (const glm::vec3& p : pts) {
        float d = sphereDist(p,sC,sR);
        EXPECT_NEAR(evalRecipe(prog,2,p), std::abs(d)-thick, 1e-5f)
            << "Onion at (" << p.x <<","<< p.y <<","<< p.z <<")";
    }
}

// ============================================================
// P2.4 M3b-1 — 5 no-position leaf primitive parity cases.
// Oracles: re-derived from SDFPrimitives.cs in C++ (NOT calling SdfCore_*).
// ≥4 sample points per primitive; params chosen for non-degenerate shapes.
// ============================================================

// --- Capsule: recipe [Capsule(halfH=0.5, r=0.3)] ---
TEST(RecipeEvalParity, M3b1_Capsule_MatchesAnalytic) {
    const float halfH = 0.5f, r = 0.3f;
    SdfInstruction prog[] = { capsuleOp(halfH, r) };
    const glm::vec3 pts[] = {
        glm::vec3(0.0f,  0.0f,  0.0f),   // inside cap shaft
        glm::vec3(0.0f,  0.6f,  0.0f),   // above top cap
        glm::vec3(0.4f,  0.0f,  0.0f),   // outside shaft radially
        glm::vec3(0.3f,  0.5f,  0.1f),   // near top cap edge
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), capsuleDist(p, halfH, r), 1e-5f)
            << "Capsule at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Cylinder: recipe [Cylinder(halfH=0.6, r=0.4)] ---
TEST(RecipeEvalParity, M3b1_Cylinder_MatchesAnalytic) {
    const float halfH = 0.6f, r = 0.4f;
    SdfInstruction prog[] = { cylinderOp(halfH, r) };
    const glm::vec3 pts[] = {
        glm::vec3(0.0f,  0.0f,  0.0f),   // inside cylinder centre
        glm::vec3(0.5f,  0.0f,  0.0f),   // outside radially, within height
        glm::vec3(0.0f,  0.8f,  0.0f),   // above cap
        glm::vec3(0.3f,  0.7f,  0.2f),   // outside both radially and axially
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), cylinderDist(p, halfH, r), 1e-5f)
            << "Cylinder at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Torus: recipe [Torus(majorR=0.6, minorR=0.2)] ---
TEST(RecipeEvalParity, M3b1_Torus_MatchesAnalytic) {
    const float majorR = 0.6f, minorR = 0.2f;
    SdfInstruction prog[] = { torusOp(majorR, minorR) };
    const glm::vec3 pts[] = {
        glm::vec3(0.6f,  0.0f,  0.0f),   // on major ring in XZ, at center of tube cross-section
        glm::vec3(0.8f,  0.0f,  0.0f),   // outside tube on major ring axis
        glm::vec3(0.0f,  0.5f,  0.0f),   // above ring centre (away from tube)
        glm::vec3(0.6f,  0.25f, 0.0f),   // near tube surface, slightly above
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), torusDist(p, majorR, minorR), 1e-5f)
            << "Torus at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- BoxRounded: recipe [BoxRounded(he=(0.5,0.4,0.3), rr=0.05)] ---
TEST(RecipeEvalParity, M3b1_BoxRounded_MatchesAnalytic) {
    const glm::vec3 he(0.5f, 0.4f, 0.3f); const float rr = 0.05f;
    SdfInstruction prog[] = { boxRoundedOp(he, rr) };
    const glm::vec3 pts[] = {
        glm::vec3(0.0f,  0.0f,  0.0f),   // inside
        glm::vec3(0.6f,  0.0f,  0.0f),   // outside along X
        glm::vec3(0.5f,  0.4f,  0.3f),   // near a corner
        glm::vec3(0.2f,  0.5f,  0.1f),   // outside along Y
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), boxRoundedDist(p, he, rr), 1e-5f)
            << "BoxRounded at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Plane: recipe [Plane(normal=(0,1,0), d=-0.5)] → points above/below y=0.5 plane ---
TEST(RecipeEvalParity, M3b1_Plane_MatchesAnalytic) {
    const glm::vec3 n(0.0f, 1.0f, 0.0f); const float d = -0.5f;
    SdfInstruction prog[] = { planeOp(n, d) };
    const glm::vec3 pts[] = {
        glm::vec3(0.0f,  0.5f,  0.0f),   // on the plane (dot=0.5, +d=0 → distance=0)
        glm::vec3(0.0f,  1.0f,  0.0f),   // above plane
        glm::vec3(0.0f,  0.0f,  0.0f),   // below plane
        glm::vec3(1.0f,  0.7f,  2.0f),   // above plane, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), planeDist(p, n, d), 1e-5f)
            << "Plane at (" << p.x << "," << p.y << "," << p.z << ")";
}

// ============================================================
// P2.4 M3b-2 — 9 position-offset leaf primitive parity cases.
// All use a non-zero position offset in data[4..6] (= {0.5f, 0.3f, 0.2f}).
// Each includes a point where a wrong/missing offset would produce a clearly different result.
// Oracles are independent C++ formulas (Global Constraints) — NOT calling SdfCore_*.
// ============================================================

// --- M3b-2 instruction helpers ---
static SdfInstruction ellipsoidOp(glm::vec3 radii, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Ellipsoid;
    in.data[0]=radii.x; in.data[1]=radii.y; in.data[2]=radii.z;
    in.data[4]=off.x;   in.data[5]=off.y;   in.data[6]=off.z;  return in; }
static SdfInstruction hollowCylinderOp(float halfLen, float outerR, float wall, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::HollowCylinder;
    in.data[0]=halfLen; in.data[1]=outerR; in.data[2]=wall;
    in.data[4]=off.x;   in.data[5]=off.y;  in.data[6]=off.z;  return in; }
static SdfInstruction taperedCylinderOp(float height, float r1, float r2, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::TaperedCylinder;
    in.data[0]=height; in.data[1]=r1; in.data[2]=r2;
    in.data[4]=off.x;  in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction coneOp(float sinA, float cosA, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Cone;
    in.data[0]=sinA; in.data[1]=cosA; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction cappedTorusOp(float sinA, float cosA, float majorR, float minorR, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::CappedTorus;
    in.data[0]=sinA; in.data[1]=cosA; in.data[2]=majorR; in.data[3]=minorR;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction linkOp(float halfLen, float majorR, float minorR, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Link;
    in.data[0]=halfLen; in.data[1]=majorR; in.data[2]=minorR;
    in.data[4]=off.x;   in.data[5]=off.y;  in.data[6]=off.z;  return in; }
static SdfInstruction panelOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Panel;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction plankOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Plank;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction roundedBoxOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RoundedBox;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }

// --- M3b-2 oracle helpers (re-derived from SDFPrimitives.cs — NOT calling SdfCore_*) ---
static float sign1f(float x) { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }

static float ellipsoidDist_oracle(glm::vec3 p, glm::vec3 off, glm::vec3 radii) {
    // mirrors SDFPrimitives.Ellipsoid:62 — original formula with branch (used as oracle)
    glm::vec3 q = p - off;
    glm::vec3 safeR = glm::max(radii, 0.0001f);
    float k0 = glm::length(q / safeR);
    float k1 = glm::length(q / (safeR * safeR));
    if (k1 < 0.0001f) return k0 - 1.0f;
    return k0 * (k0 - 1.0f) / k1;
}
static float hollowCylinderDist(glm::vec3 p, glm::vec3 off, float halfLen, float outerR, float wall) {
    // mirrors Cylinder (SDFPrimitives.cs:210) + Onion (SDFOperations.cs:228)
    glm::vec3 q = p - off;
    glm::vec2 d(glm::length(glm::vec2(q.x, q.z)) - outerR, std::abs(q.y) - halfLen);
    float cyl = std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
    return std::abs(cyl) - wall;
}
static float taperedCylinderDist(glm::vec3 p, glm::vec3 off, float height, float r1, float r2) {
    // mirrors SDFPrimitives.ConeCapped:429
    glm::vec3 p0 = p - off;
    glm::vec2 q(glm::length(glm::vec2(p0.x, p0.z)), p0.y);
    glm::vec2 k1(r2, height);
    glm::vec2 k2(r2 - r1, 2.0f * height);
    glm::vec2 ca(q.x - std::min(q.x, (q.y < 0.0f) ? r1 : r2), std::abs(q.y) - height);
    float t = glm::clamp(glm::dot(k1 - q, k2) / glm::dot(k2, k2), 0.0f, 1.0f);
    glm::vec2 cb = q - k1 + k2 * t;
    float s = (cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f;
    return s * std::sqrt(std::min(glm::dot(ca, ca), glm::dot(cb, cb)));
}
static float coneDist(glm::vec3 p, glm::vec3 off, float sinA, float cosA, float height) {
    // mirrors SDFPrimitives.Cone:354
    glm::vec3 p0 = p - off;
    glm::vec2 angle(sinA, cosA);
    glm::vec2 q = height * glm::vec2(angle.x / angle.y, -1.0f);
    glm::vec2 w(glm::length(glm::vec2(p0.x, p0.z)), p0.y);
    glm::vec2 a = w - q * glm::clamp(glm::dot(w, q) / glm::dot(q, q), 0.0f, 1.0f);
    glm::vec2 b = w - q * glm::vec2(glm::clamp(w.x / q.x, 0.0f, 1.0f), 1.0f);
    float k = sign1f(q.y);
    float d = std::min(glm::dot(a, a), glm::dot(b, b));
    float s = std::max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return std::sqrt(d) * sign1f(s);
}
static float cappedTorusDist(glm::vec3 p, glm::vec3 off, float sinA, float cosA, float majorR, float minorR) {
    // mirrors SDFPrimitives.TorusCapped:336
    glm::vec3 q = p - off;
    q.x = std::abs(q.x);
    glm::vec2 sc(sinA, cosA);
    float k = (sc.y * q.x > sc.x * q.z)
        ? glm::dot(glm::vec2(q.x, q.z), sc)
        : glm::length(glm::vec2(q.x, q.z));
    return std::sqrt(glm::dot(q, q) + majorR * majorR - 2.0f * majorR * k) - minorR;
}
static float linkDist(glm::vec3 p, glm::vec3 off, float halfLen, float majorR, float minorR) {
    // mirrors SDFPrimitives.Link:606
    glm::vec3 q3 = p - off;
    glm::vec3 q(q3.x, std::max(std::abs(q3.y) - halfLen, 0.0f), q3.z);
    return glm::length(glm::vec2(glm::length(glm::vec2(q.x, q.y)) - majorR, q.z)) - minorR;
}
static float posBoxRoundedDist(glm::vec3 p, glm::vec3 off, glm::vec3 he, float rr) {
    // Panel/Plank/RoundedBox: positioned BoxRounded — mirrors SDFPrimitives.BoxRounded:194
    glm::vec3 q = glm::abs(p - off) - he + rr;
    return glm::length(glm::max(q, glm::vec3(0.0f)))
         + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - rr;
}

// Shared offset used in all M3b-2 tests — non-zero so offset-insensitive points fail.
static const glm::vec3 kOff(0.5f, 0.3f, 0.2f);

// --- 1. Ellipsoid (branchless form parity) ---
// Oracle = ORIGINAL formula. Points away from center so k1 is NOT near zero (branch not taken).
TEST(RecipeEvalParity, M3b2_Ellipsoid_MatchesOracle) {
    const glm::vec3 radii(0.6f, 0.4f, 0.5f);
    SdfInstruction prog[] = { ellipsoidOp(radii, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.7f,  0.0f,  0.0f),   // outside along X from offset
        kOff + glm::vec3(0.0f,  0.5f,  0.0f),   // outside along Y from offset
        kOff + glm::vec3(0.3f,  0.2f,  0.25f),  // near surface, off-axis
        kOff + glm::vec3(1.0f,  0.8f,  0.6f),   // far outside
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), ellipsoidDist_oracle(p, kOff, radii), 1e-5f)
            << "Ellipsoid at (" << p.x << "," << p.y << "," << p.z << ")";
    // Position-offset sensitivity: query at world-origin — wrong answer if offset not applied
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    float withOffset    = evalRecipe(prog, 1, pWorld);
    float withoutOffset = ellipsoidDist_oracle(pWorld, glm::vec3(0.0f), radii);
    EXPECT_GT(std::abs(withOffset - withoutOffset), 0.1f)
        << "Ellipsoid offset not applied: results match (offset missing?)";
}

// --- 2. HollowCylinder ---
TEST(RecipeEvalParity, M3b2_HollowCylinder_MatchesAnalytic) {
    const float halfLen=0.5f, outerR=0.4f, wall=0.05f;
    SdfInstruction prog[] = { hollowCylinderOp(halfLen, outerR, wall, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.4f,  0.0f,  0.0f),   // on outer surface (XZ radius = outerR)
        kOff + glm::vec3(0.0f,  0.7f,  0.0f),   // above cap
        kOff + glm::vec3(0.35f, 0.0f,  0.0f),   // inside hollow cylinder (near inner wall)
        kOff + glm::vec3(0.6f,  0.3f,  0.2f),   // outside both radially and axially
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), hollowCylinderDist(p, kOff, halfLen, outerR, wall), 1e-5f)
            << "HollowCylinder at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - hollowCylinderDist(pWorld, glm::vec3(0.0f), halfLen, outerR, wall)), 0.1f)
        << "HollowCylinder offset not applied";
}

// --- 3. TaperedCylinder ---
TEST(RecipeEvalParity, M3b2_TaperedCylinder_MatchesAnalytic) {
    const float height=1.0f, r1=0.4f, r2=0.15f;
    SdfInstruction prog[] = { taperedCylinderOp(height, r1, r2, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.4f,  0.0f,  0.0f),   // near base radius
        kOff + glm::vec3(0.0f,  1.2f,  0.0f),   // above top cap
        kOff + glm::vec3(0.15f, 1.0f,  0.0f),   // near top edge
        kOff + glm::vec3(0.5f,  0.5f,  0.3f),   // outside, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), taperedCylinderDist(p, kOff, height, r1, r2), 1e-5f)
            << "TaperedCylinder at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - taperedCylinderDist(pWorld, glm::vec3(0.0f), height, r1, r2)), 0.1f)
        << "TaperedCylinder offset not applied";
}

// --- 4. Cone (30° half-angle → sinA≈0.5, cosA≈0.866) ---
TEST(RecipeEvalParity, M3b2_Cone_MatchesAnalytic) {
    const float sinA=0.5f, cosA=0.866f, height=1.0f;
    SdfInstruction prog[] = { coneOp(sinA, cosA, height, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f,  0.5f,  0.0f),   // on axis, mid-height (inside cone)
        kOff + glm::vec3(0.6f,  0.5f,  0.0f),   // outside cone surface at mid-height
        kOff + glm::vec3(0.0f,  1.2f,  0.0f),   // above tip
        kOff + glm::vec3(0.3f,  0.0f,  0.2f),   // near base, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), coneDist(p, kOff, sinA, cosA, height), 1e-4f)
            << "Cone at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - coneDist(pWorld, glm::vec3(0.0f), sinA, cosA, height)), 0.2f)
        << "Cone offset not applied";
}

// --- 5. CappedTorus (cap angle ≈75° → sinA≈0.966, cosA≈0.259) ---
TEST(RecipeEvalParity, M3b2_CappedTorus_MatchesAnalytic) {
    const float sinA=0.966f, cosA=0.259f, majorR=0.6f, minorR=0.15f;
    SdfInstruction prog[] = { cappedTorusOp(sinA, cosA, majorR, minorR, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.6f,  0.0f,  0.0f),   // on major ring in XZ
        kOff + glm::vec3(0.6f,  0.2f,  0.0f),   // near tube surface
        kOff + glm::vec3(0.0f,  0.0f,  0.8f),   // behind torus, outside
        kOff + glm::vec3(1.0f,  0.3f,  0.0f),   // outside ring
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), cappedTorusDist(p, kOff, sinA, cosA, majorR, minorR), 1e-5f)
            << "CappedTorus at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - cappedTorusDist(pWorld, glm::vec3(0.0f), sinA, cosA, majorR, minorR)), 0.1f)
        << "CappedTorus offset not applied";
}

// --- 6. Link ---
TEST(RecipeEvalParity, M3b2_Link_MatchesAnalytic) {
    const float halfLen=0.3f, majorR=0.4f, minorR=0.1f;
    SdfInstruction prog[] = { linkOp(halfLen, majorR, minorR, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.4f,  0.0f,  0.0f),   // on major ring in XY at y=0
        kOff + glm::vec3(0.4f,  0.5f,  0.0f),   // above halfLen (y clamped to halfLen)
        kOff + glm::vec3(0.0f,  0.0f,  0.15f),  // near Z tube
        kOff + glm::vec3(0.8f,  0.4f,  0.2f),   // outside link
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), linkDist(p, kOff, halfLen, majorR, minorR), 1e-5f)
            << "Link at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - linkDist(pWorld, glm::vec3(0.0f), halfLen, majorR, minorR)), 0.1f)
        << "Link offset not applied";
}

// --- 7. Panel (positioned BoxRounded, opcode=10) ---
TEST(RecipeEvalParity, M3b2_Panel_MatchesAnalytic) {
    const glm::vec3 he(0.6f, 0.05f, 0.4f); const float rr=0.02f;
    SdfInstruction prog[] = { panelOp(he, rr, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f,  0.0f,  0.0f),   // inside
        kOff + glm::vec3(0.7f,  0.0f,  0.0f),   // outside X
        kOff + glm::vec3(0.0f,  0.1f,  0.0f),   // above (thin panel)
        kOff + glm::vec3(0.6f,  0.06f, 0.4f),   // near corner
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), posBoxRoundedDist(p, kOff, he, rr), 1e-5f)
            << "Panel at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - posBoxRoundedDist(pWorld, glm::vec3(0.0f), he, rr)), 0.05f)
        << "Panel offset not applied";
}

// --- 8. Plank (positioned BoxRounded, opcode=11) ---
TEST(RecipeEvalParity, M3b2_Plank_MatchesAnalytic) {
    const glm::vec3 he(0.8f, 0.08f, 0.12f); const float rr=0.02f;
    SdfInstruction prog[] = { plankOp(he, rr, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f,  0.0f,  0.0f),   // inside
        kOff + glm::vec3(0.9f,  0.0f,  0.0f),   // outside X
        kOff + glm::vec3(0.0f,  0.12f, 0.0f),   // outside Y
        kOff + glm::vec3(0.5f,  0.09f, 0.15f),  // outside corner region
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), posBoxRoundedDist(p, kOff, he, rr), 1e-5f)
            << "Plank at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - posBoxRoundedDist(pWorld, glm::vec3(0.0f), he, rr)), 0.05f)
        << "Plank offset not applied";
}

// --- 9. RoundedBox (positioned BoxRounded, opcode=12) ---
TEST(RecipeEvalParity, M3b2_RoundedBox_MatchesAnalytic) {
    const glm::vec3 he(0.4f, 0.3f, 0.35f); const float rr=0.05f;
    SdfInstruction prog[] = { roundedBoxOp(he, rr, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f,  0.0f,  0.0f),   // inside
        kOff + glm::vec3(0.5f,  0.0f,  0.0f),   // outside X
        kOff + glm::vec3(0.0f,  0.4f,  0.0f),   // outside Y
        kOff + glm::vec3(0.45f, 0.35f, 0.4f),   // near corner
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), posBoxRoundedDist(p, kOff, he, rr), 1e-5f)
            << "RoundedBox at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - posBoxRoundedDist(pWorld, glm::vec3(0.0f), he, rr)), 0.05f)
        << "RoundedBox offset not applied";
}

// ============================================================
// P2.4 M3b-3 — 6 prism/cone-family leaf primitive parity cases.
// Offset group (pos-off=YES): TriangularPrism, HexPrism, Pyramid, FakeRoundCone, RoundCone.
// No-offset: Segment (pointA=data[0..2], radius=data[3], pointB=data[4..6]).
// Oracles are independent C++ formulas — NOT calling SdfCore_*.
// ============================================================

// --- M3b-3 instruction helpers ---
static SdfInstruction triangularPrismOp(float hx, float hy, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::TriangularPrism;
    in.data[0]=hx; in.data[1]=hy;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction hexPrismOp(float hx, float hy, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::HexPrism;
    in.data[0]=hx; in.data[1]=hy;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction pyramidOp(float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Pyramid;
    in.data[0]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction segmentOp(glm::vec3 ptA, float radius, glm::vec3 ptB) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Segment;
    in.data[0]=ptA.x; in.data[1]=ptA.y; in.data[2]=ptA.z;
    in.data[3]=radius;
    in.data[4]=ptB.x; in.data[5]=ptB.y; in.data[6]=ptB.z;  return in; }
static SdfInstruction fakeRoundConeOp(float r1, float r2, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::FakeRoundCone;
    in.data[0]=r1; in.data[1]=r2; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
static SdfInstruction roundConeOp(float r1, float r2, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RoundCone;
    in.data[0]=r1; in.data[1]=r2; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }

// --- M3b-3 oracle helpers (re-derived from SDFPrimitives.cs — NOT calling SdfCore_*) ---
static float triangularPrismDist(glm::vec3 p, glm::vec3 off, float hx, float hy) {
    // mirrors SDFPrimitives.TriangularPrism:530 — uses signed p.y, abs on x/z
    glm::vec3 lp = p - off;
    glm::vec3 q = glm::abs(lp);
    return std::max(q.z - hy, std::max(q.x * 0.866025f + lp.y * 0.5f, -lp.y) - hx * 0.5f);
}
static float hexPrismDist(glm::vec3 p, glm::vec3 off, float hx, float hy) {
    // mirrors SDFPrimitives.HexPrism:580
    const float k0 = 0.8660254f; // sqrt(3)/2
    const float kz = 0.57735f;   // 1/sqrt(3)
    glm::vec3 lp = p - off;
    glm::vec3 q = glm::abs(lp);
    float dotVal = std::min(glm::dot(glm::vec2(-k0, 0.5f), glm::vec2(q.x, q.z)), 0.0f);
    float qx = q.x - 2.0f * dotVal * (-k0);
    float qz = q.z - 2.0f * dotVal * 0.5f;
    glm::vec2 d(
        glm::length(glm::vec2(qx, qz) - glm::vec2(glm::clamp(qx, -kz * hx, kz * hx), hx))
            * sign1f(qz - hx),
        q.y - hy);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
}
static float pyramidDist_oracle(glm::vec3 p, glm::vec3 off, float height) {
    // Independent port of published IQ sdPyramid (iquilezles.org/articles/distfunctions).
    // NOT derived from the kernel — keeps the parity test from becoming a circular oracle.
    glm::vec3 lp = p - off;
    float m2 = height * height + 0.25f;
    float px = std::abs(lp.x), pz = std::abs(lp.z), py = lp.y;
    if (pz > px) { float tmp = px; px = pz; pz = tmp; }  // fold so px >= pz
    px -= 0.5f; pz -= 0.5f;
    glm::vec3 q(pz, height * py - 0.5f * px, height * px + 0.5f * py);
    float s = std::max(-q.x, 0.0f);
    float t = glm::clamp((q.y - 0.5f * pz) / (m2 + 0.25f), 0.0f, 1.0f);
    float da = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
    float db = m2 * (q.x + 0.5f * t) * (q.x + 0.5f * t) + (q.y - m2 * t) * (q.y - m2 * t);
    float d2 = (std::min(q.y, -q.x * m2 - q.y * 0.5f) > 0.0f) ? 0.0f : std::min(da, db);
    float signVal = std::max(q.z, -py) >= 0.0f ? 1.0f : -1.0f;
    return std::sqrt((d2 + q.z * q.z) / m2) * signVal;
}
static float segmentDist(glm::vec3 p, glm::vec3 ptA, glm::vec3 ptB, float radius) {
    // mirrors SDFPrimitives.Capsule:82 in segment (line segment) form
    glm::vec3 pa = p - ptA, ba = ptB - ptA;
    float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - radius;
}
static float fakeRoundConeDist(glm::vec3 p, glm::vec3 off, float r1, float r2, float height) {
    // mirrors SDFPrimitives.FakeRoundCone:468
    glm::vec3 lp = p - off;
    glm::vec2 q(glm::length(glm::vec2(lp.x, lp.z)), lp.y);
    float h = glm::clamp(q.y / height, 0.0f, 1.0f);
    float r = glm::mix(r1, r2, h);
    return glm::length(glm::vec2(q.x, q.y - height * h)) - r;
}
static float roundConeDist_oracle(glm::vec3 p, glm::vec3 off, float r1, float r2, float height) {
    // mirrors SDFPrimitives.ConeRounded:447 — original branched form (oracle for branchless kernel)
    glm::vec3 lp = p - off;
    glm::vec2 q(glm::length(glm::vec2(lp.x, lp.z)), lp.y);
    float b = (r1 - r2) / height;
    float a = std::sqrt(1.0f - b * b);
    float k = glm::dot(q, glm::vec2(-b, a));
    if (k < 0.0f)           return glm::length(q) - r1;
    if (k > a * height)     return glm::length(q - glm::vec2(0.0f, height)) - r2;
    return glm::dot(q, glm::vec2(a, b)) - r1;
}

// Shared offset — same kOff as M3b-2 (non-zero so offset-insensitive queries fail).

// --- 1. TriangularPrism (pos-off=YES) ---
TEST(RecipeEvalParity, M3b3_TriangularPrism_MatchesOracle) {
    const float hx=0.5f, hy=0.4f;
    SdfInstruction prog[] = { triangularPrismOp(hx, hy, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f, -0.2f,  0.0f),   // inside (below axis, within base)
        kOff + glm::vec3(0.7f,  0.0f,  0.0f),   // outside along X
        kOff + glm::vec3(0.0f,  0.0f,  0.6f),   // outside along Z (beyond hy)
        kOff + glm::vec3(0.3f,  0.25f, 0.2f),   // outside, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), triangularPrismDist(p, kOff, hx, hy), 1e-5f)
            << "TriangularPrism at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - triangularPrismDist(pWorld, glm::vec3(0.0f), hx, hy)), 0.1f)
        << "TriangularPrism offset not applied";
}

// --- 2. HexPrism (pos-off=YES) ---
TEST(RecipeEvalParity, M3b3_HexPrism_MatchesOracle) {
    const float hx=0.4f, hy=0.5f;
    SdfInstruction prog[] = { hexPrismOp(hx, hy, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f,  0.0f,  0.0f),   // center (inside)
        kOff + glm::vec3(0.5f,  0.0f,  0.0f),   // outside X (beyond hex radius)
        kOff + glm::vec3(0.0f,  0.7f,  0.0f),   // outside top cap
        kOff + glm::vec3(0.3f,  0.3f,  0.3f),   // outside, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), hexPrismDist(p, kOff, hx, hy), 1e-5f)
            << "HexPrism at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - hexPrismDist(pWorld, glm::vec3(0.0f), hx, hy)), 0.1f)
        << "HexPrism offset not applied";
}

// --- 3. Pyramid (pos-off=YES, branchless rewrite oracle) ---
TEST(RecipeEvalParity, M3b3_Pyramid_MatchesOracle) {
    const float height=1.0f;
    SdfInstruction prog[] = { pyramidOp(height, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.2f,  0.3f,  0.1f),   // inside (q.z <= q.x: no fold)
        kOff + glm::vec3(0.1f,  0.3f,  0.35f),  // inside (q.z > q.x: fold triggered)
        kOff + glm::vec3(0.0f,  1.2f,  0.0f),   // above apex
        kOff + glm::vec3(0.7f, -0.2f,  0.3f),   // outside, below base
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), pyramidDist_oracle(p, kOff, height), 1e-5f)
            << "Pyramid at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - pyramidDist_oracle(pWorld, glm::vec3(0.0f), height)), 0.1f)
        << "Pyramid offset not applied";
}

// --- 4. Segment (no pos-offset: data[0..2]=ptA, data[3]=radius, data[4..6]=ptB) ---
TEST(RecipeEvalParity, M3b3_Segment_MatchesOracle) {
    const glm::vec3 ptA(0.1f, -0.3f, 0.2f), ptB(0.7f, 0.5f, -0.1f);
    const float radius = 0.08f;
    SdfInstruction prog[] = { segmentOp(ptA, radius, ptB) };
    const glm::vec3 pts[] = {
        ptA + glm::vec3(0.0f, -0.4f, 0.0f),        // past ptA end (h clamped to 0)
        ptB + glm::vec3(0.0f,  0.4f, 0.0f),         // past ptB end (h clamped to 1)
        (ptA + ptB) * 0.5f,                          // midpoint (on axis — inside tube)
        (ptA + ptB) * 0.5f + glm::vec3(0.3f, 0.0f, 0.3f), // off-axis from midpoint
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), segmentDist(p, ptA, ptB, radius), 1e-5f)
            << "Segment at (" << p.x << "," << p.y << "," << p.z << ")";
    // No pos-offset test — Segment has no position field; samples pos directly.
}

// --- 5. FakeRoundCone (pos-off=YES) ---
TEST(RecipeEvalParity, M3b3_FakeRoundCone_MatchesOracle) {
    const float r1=0.3f, r2=0.1f, height=1.0f;
    SdfInstruction prog[] = { fakeRoundConeOp(r1, r2, height, kOff) };
    const glm::vec3 pts[] = {
        kOff + glm::vec3(0.0f, 0.5f,  0.0f),   // on axis, mid-height (inside)
        kOff + glm::vec3(0.4f, 0.5f,  0.0f),   // outside at mid-height
        kOff + glm::vec3(0.0f, 1.3f,  0.0f),   // above top cap
        kOff + glm::vec3(0.3f, 0.0f,  0.3f),   // near base, off-axis
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), fakeRoundConeDist(p, kOff, r1, r2, height), 1e-5f)
            << "FakeRoundCone at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - fakeRoundConeDist(pWorld, glm::vec3(0.0f), r1, r2, height)), 0.1f)
        << "FakeRoundCone offset not applied";
}

// --- 6. RoundCone (pos-off=YES, branchless rewrite oracle — one point per region) ---
TEST(RecipeEvalParity, M3b3_RoundCone_MatchesOracle) {
    const float r1=0.3f, r2=0.1f, height=1.0f;
    SdfInstruction prog[] = { roundConeOp(r1, r2, height, kOff) };
    const glm::vec3 pts[] = {
        // Compute b = (r1-r2)/height = 0.2, a = sqrt(1-0.04) ≈ 0.98
        // k = dot(q, (-b, a)) = -b*rho + a*qy
        kOff + glm::vec3(0.5f, -0.5f, 0.0f),   // k < 0 region: below base sphere
        kOff + glm::vec3(0.05f, 0.5f, 0.0f),   // middle region: on the cone slope
        kOff + glm::vec3(0.0f,  1.5f, 0.0f),   // k > a*h region: above top sphere
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 1, p), roundConeDist_oracle(p, kOff, r1, r2, height), 1e-5f)
            << "RoundCone at (" << p.x << "," << p.y << "," << p.z << ")";
    glm::vec3 pWorld(0.0f, 0.0f, 0.0f);
    EXPECT_GT(std::abs(evalRecipe(prog, 1, pWorld) - roundConeDist_oracle(pWorld, glm::vec3(0.0f), r1, r2, height)), 0.1f)
        << "RoundCone offset not applied";
}

// ===========================================================================
// P2.4 M4a — 4 domain-transform kernels + distScaleStack scaffold.
// Oracles are INDEPENDENT (first-principles geometric derivations, NOT calling SdfCore_*).
// Each recipe: [<transform>, Sphere(origin, r=0.5f), RestorePos]
// distScale=1.0f for all M4a ops → RestorePos multiplies by 1; existing values unchanged.
// ===========================================================================

// M4a instruction helpers
static SdfInstruction mirrorYOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorY; return in; }
static SdfInstruction mirrorZOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorZ; return in; }
static SdfInstruction elongateOp(glm::vec3 h) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Elongate;
    in.data[0]=h.x; in.data[1]=h.y; in.data[2]=h.z; return in; }
static SdfInstruction revolutionOp(float offset, glm::vec3 center) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Revolution;
    in.data[0]=offset;
    in.data[4]=center.x; in.data[5]=center.y; in.data[6]=center.z; return in; }

// --- MirrorY ---
// Child sphere is off-axis at (0,0.5,0) so |p.y| vs p.y is observable for -y inputs.
// Oracle: length((p.x, |p.y|, p.z) - (0,0.5,0)) - r, computed by hand.
TEST(RecipeEvalParity, M4a_MirrorY_MatchesOracle) {
    const float r = 0.5f;
    const glm::vec3 center(0.f, 0.5f, 0.f);  // off-axis so mirror is detectable
    SdfInstruction prog[] = { mirrorYOp(), sphere(center, r), restorePosOp() };
    // Independent oracle: mirror p.y by hand, compute sphere dist from scratch
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 pm(p.x, std::abs(p.y), p.z);  // mirror Y by hand
        return glm::length(pm - center) - r;     // sphere at center, computed directly
    };
    const glm::vec3 pts[] = {
        glm::vec3( 0.0f,  1.0f,  0.0f),   // +y: p.y>0 so |p.y|=p.y, no flip
        glm::vec3( 0.0f, -1.0f,  0.0f),   // -y: mirror flips to +y, changes dist
        glm::vec3( 0.5f, -0.5f,  0.3f),   // off-axis, -y side
        glm::vec3(-0.3f,  0.8f, -0.2f),   // +y, multi-component
        glm::vec3( 0.0f, -0.5f,  0.0f),   // directly below center: mirrors onto center
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "MirrorY at (" << p.x << "," << p.y << "," << p.z << ")";
    // Symmetry: f(0,+y,0) == f(0,-y,0) for any y
    EXPECT_NEAR(evalRecipe(prog,3,glm::vec3(0.f,0.7f,0.f)),
                evalRecipe(prog,3,glm::vec3(0.f,-0.7f,0.f)), 1e-5f) << "MirrorY symmetry";
}

// --- MirrorZ ---
// Child sphere is off-axis at (0,0,0.5) so |p.z| vs p.z is observable for -z inputs.
// Oracle: length((p.x, p.y, |p.z|) - (0,0,0.5)) - r, computed by hand.
TEST(RecipeEvalParity, M4a_MirrorZ_MatchesOracle) {
    const float r = 0.5f;
    const glm::vec3 center(0.f, 0.f, 0.5f);  // off-axis so mirror is detectable
    SdfInstruction prog[] = { mirrorZOp(), sphere(center, r), restorePosOp() };
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 pm(p.x, p.y, std::abs(p.z));  // mirror Z by hand
        return glm::length(pm - center) - r;     // sphere at center, computed directly
    };
    const glm::vec3 pts[] = {
        glm::vec3( 0.0f,  0.0f,  1.0f),   // +z: p.z>0 so |p.z|=p.z, no flip
        glm::vec3( 0.0f,  0.0f, -1.0f),   // -z: mirror flips to +z, changes dist
        glm::vec3( 0.3f,  0.2f, -0.7f),   // off-axis, -z side
        glm::vec3(-0.4f, -0.1f,  0.9f),   // +z, multi-component
        glm::vec3( 0.0f,  0.0f, -0.5f),   // directly behind center: mirrors onto center
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "MirrorZ at (" << p.x << "," << p.y << "," << p.z << ")";
    // Symmetry: f(0,0,+z) == f(0,0,-z) for any z
    EXPECT_NEAR(evalRecipe(prog,3,glm::vec3(0.f,0.f,0.7f)),
                evalRecipe(prog,3,glm::vec3(0.f,0.f,-0.7f)), 1e-5f) << "MirrorZ symmetry";
}

// --- Elongate ---
// Oracle: Elongate(p,h) = p - clamp(p,-h,h), derived from SDFOperations.Elongate.
// Then sphere(q) — independent derivation using std::clamp.
TEST(RecipeEvalParity, M4a_Elongate_MatchesOracle) {
    const glm::vec3 h(0.4f, 0.2f, 0.3f);
    const float r = 0.3f;
    SdfInstruction prog[] = { elongateOp(h), sphere(glm::vec3(0.f), r), restorePosOp() };
    // Independent oracle: clamp each component by hand (SDFOperations.Elongate formula)
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 clamped(
            std::max(-h.x, std::min(p.x, h.x)),
            std::max(-h.y, std::min(p.y, h.y)),
            std::max(-h.z, std::min(p.z, h.z)));
        glm::vec3 q = p - clamped;  // elongated position
        return glm::length(q) - r;
    };
    const glm::vec3 pts[] = {
        glm::vec3( 0.0f,  0.0f,  0.0f),   // inside elongation box → q near 0
        glm::vec3( 0.6f,  0.0f,  0.0f),   // beyond elongation along x
        glm::vec3( 0.0f,  0.5f,  0.0f),   // beyond elongation along y
        glm::vec3(-0.5f,  0.3f,  0.4f),   // off-axis, beyond all components
        glm::vec3( 0.2f, -0.1f, -0.2f),   // inside elongation box in all axes
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "Elongate at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Revolution ---
// Oracle: derived first-principles from RevolutionNode.EvalBurst formula.
// Revolution maps p → q=(length(xz)-offset, y) then evaluates child at float3(q,0)+center.
// Independent formula: compute length(xz) by hand, subtract offset, compute sphere at origin.
TEST(RecipeEvalParity, M4a_Revolution_MatchesOracle) {
    const float offset = 0.8f;
    const glm::vec3 center(0.1f, 0.0f, 0.2f);
    const float r = 0.15f;
    SdfInstruction prog[] = { revolutionOp(offset, center), sphere(center, r), restorePosOp() };
    // Independent oracle: hand-derive the revolution transform and sphere eval.
    // RevolutionNode: pp = p - center; q = (length(pp.xz) - offset, pp.y); child at float3(q.x,q.y,0)+center
    // sphere at center with radius r: length(float3(q.x,q.y,0)) - r
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 pp = p - center;
        float xz_len = std::sqrt(pp.x * pp.x + pp.z * pp.z);  // length of xz by hand
        float qx = xz_len - offset;
        float qy = pp.y;
        // The transformed point seen by child (sphere at center, radius r):
        // child receives float3(qx,qy,0)+center; sphere center is 'center'; sphere SDF = length(float3(qx,qy,0))-r
        return std::sqrt(qx * qx + qy * qy) - r;
    };
    const glm::vec3 pts[] = {
        center + glm::vec3(offset + 0.3f, 0.0f, 0.0f),  // on the revolution ring, +x
        center + glm::vec3(0.0f, 0.0f, offset + 0.3f),  // on the ring, +z
        center + glm::vec3(offset,  0.2f, 0.0f),         // on ring, slightly above equator
        center + glm::vec3(0.2f,    0.5f, 0.1f),         // off ring, general position
        center + glm::vec3(-offset, 0.0f, 0.0f),         // opposite side: xz_len=offset → ring
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "Revolution at (" << p.x << "," << p.y << "," << p.z << ")";
    // Verify revolution is rotationally symmetric around Y: f(offset+dr, y, 0) == f(0, y, offset+dr)
    float dr = 0.3f;
    EXPECT_NEAR(evalRecipe(prog,3, center+glm::vec3(offset+dr,0.f,0.f)),
                evalRecipe(prog,3, center+glm::vec3(0.f,0.f,offset+dr)), 1e-5f)
        << "Revolution Y-axis symmetry";
}

// ===========================================================================
// P2.4 M4b — warp transforms + Transform + DistScale APPLICATION.
// All oracles are INDEPENDENT: never transcribe the kernel body into the oracle.
// Twist/Bend: oracle via std::cos/std::sin by hand (independent of SdfCore_ implementation).
// RepeatInfinite/RepeatLimited: oracle via std::fmod/std::round by hand.
// Transform: oracle via glm::quat rotation (independent of cross-product kernel form).
// DistScale: Transform pushes data[11]; RestorePos scales TOS distance by distScale.
// TOOTHLESS-ORACLE TRAP: use OFF-AXIS children + probe points where the op is OBSERVABLE.
// ===========================================================================

// M4b instruction helpers
static SdfInstruction twistOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Twist;
    in.data[0]=k; return in; }
static SdfInstruction bendOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Bend;
    in.data[0]=k; return in; }
static SdfInstruction repeatInfiniteOp(glm::vec3 sp) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RepeatInfinite;
    in.data[0]=sp.x; in.data[1]=sp.y; in.data[2]=sp.z; return in; }
static SdfInstruction repeatLimitedOp(float s, glm::vec3 lim) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RepeatLimited;
    in.data[0]=s; in.data[1]=lim.x; in.data[2]=lim.y; in.data[3]=lim.z; return in; }
// Transform: trans=data[0..2], invRot xyzw=data[4..7], invScale=data[8..10], distScale=data[11]
static SdfInstruction transformOp(glm::vec3 trans, glm::vec4 invRotXYZW, glm::vec3 invScale, float distScale) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Transform;
    in.data[0]=trans.x;    in.data[1]=trans.y;    in.data[2]=trans.z;
    in.data[4]=invRotXYZW.x; in.data[5]=invRotXYZW.y; in.data[6]=invRotXYZW.z; in.data[7]=invRotXYZW.w;
    in.data[8]=invScale.x; in.data[9]=invScale.y; in.data[10]=invScale.z;
    in.data[11]=distScale; return in; }

// --- Twist ---
// Oracle: twist around Y by k rad/unit using std::cos/std::sin — independent of SdfCore_Twist.
// OFF-AXIS child at (0.6, 0, 0.0) + points with non-zero y so rotation actually moves the surface.
TEST(RecipeEvalParity, M4b_Twist_MatchesOracle) {
    const float k = 1.2f;
    const glm::vec3 center(0.6f, 0.0f, 0.0f);  // off-axis: twist around Y is observable
    const float r = 0.2f;
    SdfInstruction prog[] = { twistOp(k), sphere(center, r), restorePosOp() };
    // Independent oracle: rotate (x,z) by k*y using std::trig (NOT SdfCore_Twist formula)
    auto oracle = [&](glm::vec3 p) {
        float c = std::cos(k * p.y);
        float s = std::sin(k * p.y);
        glm::vec3 tp(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
        return glm::length(tp - center) - r;
    };
    const glm::vec3 pts[] = {
        glm::vec3( 0.6f,  0.0f,  0.0f),  // no twist at y=0 → at center surface
        glm::vec3( 0.6f,  1.0f,  0.0f),  // y=1 → twist rotates the child; probe moves off-center
        glm::vec3( 0.0f,  0.5f,  0.6f),  // p.z component + non-zero y
        glm::vec3(-0.4f,  0.7f,  0.2f),  // negative x + twist
        glm::vec3( 1.0f, -0.5f,  0.0f),  // negative y twist direction
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "Twist at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Bend ---
// Oracle: bend around X by k using std::cos/std::sin — independent of SdfCore_Bend.
// OFF-AXIS child at (0, 0.5, 0.3) + points with non-zero x so the rotation is observable.
TEST(RecipeEvalParity, M4b_Bend_MatchesOracle) {
    const float k = 0.8f;
    const glm::vec3 center(0.0f, 0.5f, 0.3f);  // off-axis child
    const float r = 0.2f;
    SdfInstruction prog[] = { bendOp(k), sphere(center, r), restorePosOp() };
    // Independent oracle: rotate (x,y) by k*x using std::trig (NOT SdfCore_Bend formula)
    auto oracle = [&](glm::vec3 p) {
        float c = std::cos(k * p.x);
        float s = std::sin(k * p.x);
        glm::vec3 tp(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
        return glm::length(tp - center) - r;
    };
    const glm::vec3 pts[] = {
        glm::vec3( 0.0f,  0.5f,  0.3f),  // at child center, x=0 → no bend
        glm::vec3( 0.8f,  0.5f,  0.3f),  // non-zero x → bend rotates position
        glm::vec3(-0.6f,  0.2f,  0.3f),  // negative x
        glm::vec3( 1.0f, -0.2f,  0.1f),  // bend and off-axis y
        glm::vec3( 0.5f,  1.0f,  0.3f),  // large y with bend
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "Bend at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- RepeatInfinite ---
// Oracle: std::fmod(abs(p)+sp*0.5, sp)-sp*0.5 by hand — NOT SdfCore_RepeatInfinite.
// Points OUTSIDE the central cell (|p| > spacing) so tiling changes the result.
TEST(RecipeEvalParity, M4b_RepeatInfinite_MatchesOracle) {
    const glm::vec3 sp(1.0f, 1.5f, 0.8f);
    const float r = 0.2f;
    SdfInstruction prog[] = { repeatInfiniteOp(sp), sphere(glm::vec3(0.f), r), restorePosOp() };
    // Independent oracle: glm::mod semantics (non-neg dividend → same as std::fmod)
    auto oracle = [&](glm::vec3 p) {
        float x = std::fmod(std::abs(p.x) + sp.x * 0.5f, sp.x) - sp.x * 0.5f;
        float y = std::fmod(std::abs(p.y) + sp.y * 0.5f, sp.y) - sp.y * 0.5f;
        float z = std::fmod(std::abs(p.z) + sp.z * 0.5f, sp.z) - sp.z * 0.5f;
        return glm::length(glm::vec3(x, y, z)) - r;  // sphere at origin in each cell
    };
    const glm::vec3 pts[] = {
        glm::vec3( 1.3f,  0.0f,  0.0f),  // outside central cell in x
        glm::vec3( 0.0f,  1.8f,  0.0f),  // outside central cell in y
        glm::vec3( 0.0f,  0.0f,  0.9f),  // outside central cell in z
        glm::vec3( 2.0f,  3.0f,  1.6f),  // far outside in all axes
        glm::vec3(-1.3f,  0.5f, -0.9f),  // negative coords (abs applied)
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "RepeatInfinite at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- RepeatLimited ---
// Oracle: p - s*clamp(round(p/s), -lim, lim) by hand — NOT SdfCore_RepeatLimited.
// Points OUTSIDE the limited range so clamping changes the result.
TEST(RecipeEvalParity, M4b_RepeatLimited_MatchesOracle) {
    const float s = 1.2f;
    const glm::vec3 lim(2.0f, 1.0f, 1.5f);
    const float r = 0.3f;
    SdfInstruction prog[] = { repeatLimitedOp(s, lim), sphere(glm::vec3(0.f), r), restorePosOp() };
    // Independent oracle: round component-wise, clamp, subtract (NOT SdfCore_RepeatLimited)
    auto oracle = [&](glm::vec3 p) {
        float rx = std::round(p.x / s);
        float ry = std::round(p.y / s);
        float rz = std::round(p.z / s);
        float cx = std::max(-lim.x, std::min(rx, lim.x));
        float cy = std::max(-lim.y, std::min(ry, lim.y));
        float cz = std::max(-lim.z, std::min(rz, lim.z));
        glm::vec3 rp = p - s * glm::vec3(cx, cy, cz);
        return glm::length(rp) - r;
    };
    const glm::vec3 pts[] = {
        glm::vec3( 2.5f,  0.0f,  0.0f),  // within limit in x (round(2.5/1.2)=round(2.08)=2 ≤ 2)
        glm::vec3( 3.5f,  0.0f,  0.0f),  // clamped in x (round(3.5/1.2)=3 > 2 → clamp to 2)
        glm::vec3( 0.0f,  2.0f,  0.0f),  // clamped in y (round(2/1.2)=2 > 1 → clamp to 1)
        glm::vec3( 0.0f,  0.0f,  3.0f),  // clamped in z (round(3/1.2)=2 ≤ 1.5 → no, 3/1.2=2.5→round=3>1.5 → clamp)
        glm::vec3(-4.0f,  1.5f, -2.0f),  // clamped in x (negative) and z
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "RepeatLimited at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- Transform ---
// Oracle uses glm::quat rotation — INDEPENDENT of the cross-product kernel implementation.
// Exercise translation AND non-uniform scale AND a non-identity rotation with off-center child.
TEST(RecipeEvalParity, M4b_Transform_MatchesOracle) {
    // Setup: a 45-degree rotation around Y axis as a quaternion
    // glm::quat from angle-axis: quat(cos(θ/2), sin(θ/2)*axis)
    float halfAngle = glm::radians(45.0f) * 0.5f;
    glm::quat rot = glm::quat(std::cos(halfAngle), glm::vec3(0.f, std::sin(halfAngle), 0.f));
    glm::quat invRot = glm::inverse(rot);
    glm::vec3 trans(0.5f, 0.2f, 0.1f);
    glm::vec3 scale(2.0f, 1.5f, 0.8f);  // non-uniform scale (non-trivial distScale)
    glm::vec3 invScale(1.0f/scale.x, 1.0f/scale.y, 1.0f/scale.z);
    float distScale = std::min({scale.x, scale.y, scale.z});  // min component = distScale

    // Encode as flat data[]: trans=[0..2], invRot.xyzw=[4..7], invScale=[8..10], distScale=[11]
    glm::vec4 invRotV(invRot.x, invRot.y, invRot.z, invRot.w);
    SdfInstruction xf = transformOp(trans, invRotV, invScale, distScale);
    const float r = 0.3f;
    const glm::vec3 childCenter(0.4f, 0.3f, 0.2f);  // off-center child
    SdfInstruction prog[] = { xf, sphere(childCenter, r), restorePosOp() };

    // Independent oracle: glm::quat * vec3 rotation (NOT cross-product form)
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 v = p - trans;
        glm::vec3 rotated = invRot * v;  // glm quaternion rotate — independent
        glm::vec3 tp = rotated * invScale;
        float sphereDist = glm::length(tp - childCenter) - r;
        return distScale * sphereDist;  // RestorePos applies distScale
    };
    const glm::vec3 pts[] = {
        trans + glm::vec3(0.4f, 0.3f, 0.2f),  // near child-center in world space
        trans + glm::vec3(1.0f, 0.0f, 0.0f),  // along local x
        trans + glm::vec3(0.0f, 0.0f, 1.0f),  // along local z (45° rot makes this interesting)
        trans + glm::vec3(-0.5f, 0.3f, 0.4f), // off-axis
        glm::vec3(0.0f, 0.0f, 0.0f),          // world origin (far from child after transform)
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-4f)
            << "Transform at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- DistScale APPLICATION: scaled Transform changes distances ---
// A [Transform(uniform scale=2 → invScale=0.5, distScale=2), Sphere(r=0.5), RestorePos]
// must return 2 * sphere_at(transformed_p). Proves distScale is applied, not ignored.
TEST(RecipeEvalParity, M4b_DistScale_ScaledTransformScalesDistance) {
    // Identity rotation (no rotation), translation zero, scale=2 → invScale=0.5, distScale=2
    glm::vec4 identRotXYZW(0.0f, 0.0f, 0.0f, 1.0f);  // identity quaternion
    glm::vec3 noTrans(0.0f, 0.0f, 0.0f);
    glm::vec3 invScale(0.5f, 0.5f, 0.5f);  // 1/scale = 0.5 (scale = 2)
    float distScale = 2.0f;
    SdfInstruction xf = transformOp(noTrans, identRotXYZW, invScale, distScale);
    const float r = 0.5f;
    SdfInstruction prog[] = { xf, sphere(glm::vec3(0.f), r), restorePosOp() };
    // Oracle: distScale * sphere(p * invScale) = 2 * (length(p*0.5) - 0.5)
    auto oracle = [&](glm::vec3 p) {
        return distScale * (glm::length(p * invScale) - r);
    };
    const glm::vec3 pts[] = {
        glm::vec3(3.0f, 0.0f, 0.0f),  // dist = 2*(1.5-0.5) = 2.0
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(0.1f, 0.1f, 0.1f),
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 3, p), oracle(p), 1e-5f)
            << "ScaledTransform at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- DistScale STACK COMPOSITION: nested Transforms compose distScale correctly ---
// [Transform(s1), Transform(s2), Sphere, RestorePos, RestorePos] → s1*s2*sphere(…).
// Proves the STACK (not a single register) composes nested scales.
TEST(RecipeEvalParity, M4b_DistScale_NestedTransformsComposeScale) {
    glm::vec4 identRotXYZW(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 noTrans(0.0f, 0.0f, 0.0f);
    // Outer: scale=3 → invScale=1/3, distScale=3
    glm::vec3 invScale1(1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f);
    float distScale1 = 3.0f;
    // Inner: scale=2 → invScale=0.5, distScale=2
    glm::vec3 invScale2(0.5f, 0.5f, 0.5f);
    float distScale2 = 2.0f;
    SdfInstruction xf1 = transformOp(noTrans, identRotXYZW, invScale1, distScale1);
    SdfInstruction xf2 = transformOp(noTrans, identRotXYZW, invScale2, distScale2);
    const float r = 0.5f;
    // [Transform(s1), Transform(s2), Sphere, RestorePos(s2), RestorePos(s1)]
    SdfInstruction prog[] = { xf1, xf2, sphere(glm::vec3(0.f), r), restorePosOp(), restorePosOp() };
    // Oracle: combined scale = distScale1 * distScale2 = 6; sphere at p * invScale1 * invScale2
    auto oracle = [&](glm::vec3 p) {
        glm::vec3 tp = p * invScale1 * invScale2;  // apply both scales
        float rawDist = glm::length(tp) - r;
        return distScale1 * distScale2 * rawDist;
    };
    const glm::vec3 pts[] = {
        glm::vec3(6.0f, 0.0f, 0.0f),   // expected: 6*(6*1/6 - 0.5) = 6*(1-0.5) = 3.0
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(3.0f, 4.0f, 0.0f),
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog, 5, p), oracle(p), 1e-4f)
            << "NestedTransform at (" << p.x << "," << p.y << "," << p.z << ")";
}

// --- DistScale no-op sanity: MirrorX still correct after distScaleStack change ---
// Proves the MirrorX+RestorePos still returns same result as pre-M4a (distScale=1.0f is identity).
TEST(RecipeEvalParity, M4a_DistScaleNoOp_MirrorXNonRegression) {
    // Same recipe as MirrorXSmoothUnionBoxSphereMatchesAnalytic — must equal pre-M4a oracle.
    const glm::vec3 b(0.8f, 0.5f, 0.5f);
    const glm::vec3 c(1.5f, 0.0f, 0.0f);
    const float r = 0.5f, k = 0.3f;
    SdfInstruction prog[] = { mirrorXOp(), boxOp(b), sphere(c,r), smoothUnionOp(k), restorePosOp() };
    auto mirrorX = [](glm::vec3 q){ return glm::vec3(std::abs(q.x), q.y, q.z); };
    auto bD = [](glm::vec3 q, glm::vec3 hext){
        glm::vec3 d=glm::abs(q)-hext;
        return glm::length(glm::max(d,glm::vec3(0.f)))+std::min(std::max(d.x,std::max(d.y,d.z)),0.f); };
    auto sD = [](glm::vec3 q, glm::vec3 ctr, float rad){ return glm::length(q-ctr)-rad; };
    auto su = [](float a, float b_, float k_){
        float h=glm::clamp(0.5f+0.5f*(b_-a)/k_,0.f,1.f);
        return glm::mix(b_,a,h)-k_*h*(1.f-h); };
    auto oracle = [&](glm::vec3 p){
        glm::vec3 mp=mirrorX(p); return su(bD(mp,b),sD(mp,c,r),k); };
    const glm::vec3 pts[]={
        glm::vec3( 2.f,0.f,0.f), glm::vec3(-2.f,0.f,0.f),
        glm::vec3( 0.f,0.f,0.f), glm::vec3(-1.5f,-0.3f,0.2f),
    };
    for (const glm::vec3& p : pts)
        EXPECT_NEAR(evalRecipe(prog,5,p), oracle(p), 1e-4f)
            << "MirrorX non-regression at (" << p.x << "," << p.y << "," << p.z << ")";
}
