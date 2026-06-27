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
