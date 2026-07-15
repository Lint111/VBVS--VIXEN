#pragma once
// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 9: shared opcode-coverage program corpus,
// extracted from test_recipe_eval_parity.cpp's 91 inline TEST-case program builders (that
// file keeps its own oracle math unchanged; this header only extracts the `prog[]`
// instruction sequences into one reusable table). Iterated by BOTH the existing CPU parity
// test (unchanged, still asserts against its own analytic oracles) and the new GPU
// numerical-parity harness (Task 9), which compares GLSL-on-GPU output against evalRecipe
// (the CPU VM) directly — not against the analytic oracles.
//
// Do not hand-edit entries here without also checking test_recipe_eval_parity.cpp hasn't
// drifted from the corresponding TEST case's prog[] — they are meant to describe the same
// programs (the coverage claim depends on that).

#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/SdfOpCodes.g.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Vixen::SVO::Recipe::ParityCorpus {

struct CorpusProgram {
    std::string name;                    // mirrors the TEST case name, e.g. "SphereUnionMatchesAnalytic"
    std::vector<SdfInstruction> program;  // the prog[] sequence for that test
};

// ── Opcode helper functions, copied verbatim (static -> inline) from
// test_recipe_eval_parity.cpp, in the same order they appear there. ──────────

inline SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0]=c.x; in.data[1]=c.y; in.data[2]=c.z; in.data[3]=r; return in;
}
inline SdfInstruction unionOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Union; return in; }
inline SdfInstruction boxOp(glm::vec3 b) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0]=b.x; in.data[1]=b.y; in.data[2]=b.z; return in;
}
inline SdfInstruction smoothUnionOp(float k) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::SmoothUnion;
    in.data[2] = k; return in;  // k = Data0.z
}
inline SdfInstruction mirrorXOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorX; return in; }
inline SdfInstruction restorePosOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RestorePos; return in; }

// --- M3b-1 instruction helpers (5 no-position leaf primitives) ---
inline SdfInstruction capsuleOp(float halfH, float r) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Capsule;
    in.data[0]=halfH; in.data[1]=r; return in; }
inline SdfInstruction cylinderOp(float halfH, float r) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Cylinder;
    in.data[0]=halfH; in.data[1]=r; return in; }
inline SdfInstruction torusOp(float majorR, float minorR) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Torus;
    in.data[0]=majorR; in.data[1]=minorR; return in; }
inline SdfInstruction boxRoundedOp(glm::vec3 he, float rr) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::BoxRounded;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr; return in; }
inline SdfInstruction planeOp(glm::vec3 n, float d) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Plane;
    in.data[0]=n.x; in.data[1]=n.y; in.data[2]=n.z; in.data[3]=d; return in; }

// --- M3a instruction helpers ---
inline SdfInstruction subtractOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Subtract; return in; }
inline SdfInstruction intersectOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Intersect; return in; }
inline SdfInstruction xorOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Xor; return in; }
inline SdfInstruction smoothSubtractOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothSubtract; in.data[2]=k; return in; }
inline SdfInstruction smoothIntersectOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothIntersect; in.data[2]=k; return in; }
inline SdfInstruction smoothMaxOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothMax; in.data[2]=k; return in; }
inline SdfInstruction smoothUnionCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothUnionCubic; in.data[2]=k; return in; }
inline SdfInstruction smoothSubtractCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothSubtractCubic; in.data[2]=k; return in; }
inline SdfInstruction smoothIntersectCubicOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::SmoothIntersectCubic; in.data[2]=k; return in; }
inline SdfInstruction roundOp(float radius) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Round; in.data[0]=radius; return in; }
inline SdfInstruction onionOp(float thickness) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Onion; in.data[0]=thickness; return in; }

// --- M3b-2 instruction helpers ---
inline SdfInstruction ellipsoidOp(glm::vec3 radii, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Ellipsoid;
    in.data[0]=radii.x; in.data[1]=radii.y; in.data[2]=radii.z;
    in.data[4]=off.x;   in.data[5]=off.y;   in.data[6]=off.z;  return in; }
inline SdfInstruction hollowCylinderOp(float halfLen, float outerR, float wall, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::HollowCylinder;
    in.data[0]=halfLen; in.data[1]=outerR; in.data[2]=wall;
    in.data[4]=off.x;   in.data[5]=off.y;  in.data[6]=off.z;  return in; }
inline SdfInstruction taperedCylinderOp(float height, float r1, float r2, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::TaperedCylinder;
    in.data[0]=height; in.data[1]=r1; in.data[2]=r2;
    in.data[4]=off.x;  in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction coneOp(float sinA, float cosA, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Cone;
    in.data[0]=sinA; in.data[1]=cosA; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction cappedTorusOp(float sinA, float cosA, float majorR, float minorR, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::CappedTorus;
    in.data[0]=sinA; in.data[1]=cosA; in.data[2]=majorR; in.data[3]=minorR;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction linkOp(float halfLen, float majorR, float minorR, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Link;
    in.data[0]=halfLen; in.data[1]=majorR; in.data[2]=minorR;
    in.data[4]=off.x;   in.data[5]=off.y;  in.data[6]=off.z;  return in; }
inline SdfInstruction panelOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Panel;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction plankOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Plank;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction roundedBoxOp(glm::vec3 he, float rr, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RoundedBox;
    in.data[0]=he.x; in.data[1]=he.y; in.data[2]=he.z; in.data[3]=rr;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }

// Shared offset used in all M3b-2/M3b-3 tests — non-zero so offset-insensitive points fail.
inline const glm::vec3 kOff(0.5f, 0.3f, 0.2f);

// --- M3b-3 instruction helpers ---
inline SdfInstruction triangularPrismOp(float hx, float hy, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::TriangularPrism;
    in.data[0]=hx; in.data[1]=hy;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction hexPrismOp(float hx, float hy, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::HexPrism;
    in.data[0]=hx; in.data[1]=hy;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction pyramidOp(float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Pyramid;
    in.data[0]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction segmentOp(glm::vec3 ptA, float radius, glm::vec3 ptB) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Segment;
    in.data[0]=ptA.x; in.data[1]=ptA.y; in.data[2]=ptA.z;
    in.data[3]=radius;
    in.data[4]=ptB.x; in.data[5]=ptB.y; in.data[6]=ptB.z;  return in; }
inline SdfInstruction fakeRoundConeOp(float r1, float r2, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::FakeRoundCone;
    in.data[0]=r1; in.data[1]=r2; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }
inline SdfInstruction roundConeOp(float r1, float r2, float height, glm::vec3 off) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RoundCone;
    in.data[0]=r1; in.data[1]=r2; in.data[2]=height;
    in.data[4]=off.x; in.data[5]=off.y; in.data[6]=off.z;  return in; }

// M4a instruction helpers
inline SdfInstruction mirrorYOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorY; return in; }
inline SdfInstruction mirrorZOp() { SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MirrorZ; return in; }
inline SdfInstruction elongateOp(glm::vec3 h) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Elongate;
    in.data[0]=h.x; in.data[1]=h.y; in.data[2]=h.z; return in; }
inline SdfInstruction revolutionOp(float offset, glm::vec3 center) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Revolution;
    in.data[0]=offset;
    in.data[4]=center.x; in.data[5]=center.y; in.data[6]=center.z; return in; }

// M4b instruction helpers
inline SdfInstruction twistOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Twist;
    in.data[0]=k; return in; }
inline SdfInstruction bendOp(float k) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Bend;
    in.data[0]=k; return in; }
inline SdfInstruction repeatInfiniteOp(glm::vec3 sp) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RepeatInfinite;
    in.data[0]=sp.x; in.data[1]=sp.y; in.data[2]=sp.z; return in; }
inline SdfInstruction repeatLimitedOp(float s, glm::vec3 lim) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::RepeatLimited;
    in.data[0]=s; in.data[1]=lim.x; in.data[2]=lim.y; in.data[3]=lim.z; return in; }
// Transform: trans=data[0..2], invRot xyzw=data[4..7], invScale=data[8..10], distScale=data[11]
inline SdfInstruction transformOp(glm::vec3 trans, glm::vec4 invRotXYZW, glm::vec3 invScale, float distScale) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Transform;
    in.data[0]=trans.x;    in.data[1]=trans.y;    in.data[2]=trans.z;
    in.data[4]=invRotXYZW.x; in.data[5]=invRotXYZW.y; in.data[6]=invRotXYZW.z; in.data[7]=invRotXYZW.w;
    in.data[8]=invScale.x; in.data[9]=invScale.y; in.data[10]=invScale.z;
    in.data[11]=distScale; return in; }

// ── M4c helpers (instruction builders) ──────────────────────────────────────

inline SdfInstruction posChannelOp(int ch) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::PositionChannel; in.data[0]=(float)ch; return in; }
inline SdfInstruction mathSinOp(float freq, float phase, float amp) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSin;
    in.data[0]=freq; in.data[1]=phase; in.data[2]=amp; return in; }
inline SdfInstruction mathCosOp(float freq, float phase, float amp) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathCos;
    in.data[0]=freq; in.data[1]=phase; in.data[2]=amp; return in; }
inline SdfInstruction mathSmoothstepOp(float edge0, float edge1) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSmoothstep;
    in.data[0]=edge0; in.data[1]=edge1; return in; }
inline SdfInstruction mathRemapOp(float iMin, float iMax, float oMin, float oMax) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathRemap;
    in.data[0]=iMin; in.data[1]=iMax; in.data[2]=oMin; in.data[3]=oMax; return in; }
inline SdfInstruction mathClampOp(float lo, float hi) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathClamp;
    in.data[0]=lo; in.data[1]=hi; return in; }
inline SdfInstruction mathAbsOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathAbs; return in; }
inline SdfInstruction mathFracOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathFrac; return in; }
inline SdfInstruction mathPowOp(float power) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathPow; in.data[0]=power; return in; }
inline SdfInstruction mathSqrtOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSqrt; return in; }
inline SdfInstruction mathNegateOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathNegate; return in; }
inline SdfInstruction mathStepOp(float edge) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathStep; in.data[0]=edge; return in; }
inline SdfInstruction mathSignOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSign; return in; }
inline SdfInstruction mathSaturateOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSaturate; return in; }
inline SdfInstruction mathExpOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathExp; return in; }
inline SdfInstruction mathLogOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathLog; return in; }
inline SdfInstruction mathLog2Op() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathLog2; return in; }
inline SdfInstruction mathAddOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathAdd; return in; }
inline SdfInstruction mathSubOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathSub; return in; }
inline SdfInstruction mathMulOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathMul; return in; }
inline SdfInstruction mathDivOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathDiv; return in; }
inline SdfInstruction mathMinOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathMin; return in; }
inline SdfInstruction mathMaxOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathMax; return in; }
inline SdfInstruction mathLerpOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::MathLerp; return in; }
inline SdfInstruction selectOp(float threshold) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Select; in.data[0]=threshold; return in; }
inline SdfInstruction displacementOp(float scale) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Displacement; in.data[0]=scale; return in; }
inline SdfInstruction distanceToOp(glm::vec3 center) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::DistanceTo;
    in.data[0]=center.x; in.data[1]=center.y; in.data[2]=center.z; return in; }

// ── P2.4 M4d ─────────────────────────────────────────────────────────────────
// Helpers for M4d VM-control + Float3 arithmetic ops.

inline SdfInstruction pushParamOp(float val) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::PushParam; in.data[0]=val; return in; }
inline SdfInstruction pushFloat3Op(float x, float y, float z) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::PushFloat3;
    in.data[0]=x; in.data[1]=y; in.data[2]=z; return in; }
inline SdfInstruction composeFloat3Op() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::ComposeFloat3; return in; }
inline SdfInstruction passthroughOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Passthrough; return in; }
inline SdfInstruction outputOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Output; return in; }
inline SdfInstruction decomposeFloat3Op(int ch) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::DecomposeFloat3;
    in.data[0]=(float)ch; return in; }
inline SdfInstruction float3AddOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Add; return in; }
inline SdfInstruction float3SubOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Sub; return in; }
inline SdfInstruction float3MulCWOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3MulComponentWise; return in; }
inline SdfInstruction float3MinOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Min; return in; }
inline SdfInstruction float3MaxOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Max; return in; }
inline SdfInstruction float3ScalarMulOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3ScalarMul; return in; }
inline SdfInstruction float3DotOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Dot; return in; }
inline SdfInstruction float3NormalizeOp() {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::Float3Normalize; return in; }

// Recipe-Parameterization M2 Task 5b: ReadParam/ReadParamFloat3 corpus helpers. paramMask=1
// mirrors RecipeRegistry::Register's convention ("data[0] is a runtime param-array index"),
// even though evalRecipe/EmitProceduralFieldFunctionGlsl themselves dispatch on opCode alone
// and don't inspect paramMask — set for realism/consistency with a genuinely-registerable
// program, not because these two functions require it.
inline SdfInstruction readParamOp(int idx) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::ReadParam; in.paramMask=1;
    in.data[0]=(float)idx; return in; }
inline SdfInstruction readParamFloat3Op(int idx) {
    SdfInstruction in{}; in.opCode=(uint8_t)SdfOpCode::ReadParamFloat3; in.paramMask=1;
    in.data[0]=(float)idx; return in; }

// ────────────────────────────────────────────────────────────────────────────
// One function per TEST case, building the SAME prog[] sequence (verbatim
// literal values) as the corresponding TEST(RecipeEvalParity, <Name>) body.
// ────────────────────────────────────────────────────────────────────────────

inline std::vector<SdfInstruction> Make_SphereUnionMatchesAnalytic() {
    const glm::vec3 c0(-1,0,0), c1(1,0,0); const float r0=1.0f, r1=1.0f;
    return { sphere(c0,r0), sphere(c1,r1), unionOp() };
}

inline std::vector<SdfInstruction> Make_MirrorXSmoothUnionBoxSphereMatchesAnalytic() {
    const glm::vec3 b(0.8f, 0.5f, 0.5f);
    const glm::vec3 c(1.5f, 0.0f, 0.0f);
    const float r = 0.5f;
    const float k = 0.3f;
    return {
        mirrorXOp(),
        boxOp(b),
        sphere(c, r),
        smoothUnionOp(k),
        restorePosOp()
    };
}

inline std::vector<SdfInstruction> Make_M3a_Subtract_MatchesAnalytic() {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f;
    return { boxOp(bH), sphere(sC,sR), subtractOp() };
}

inline std::vector<SdfInstruction> Make_M3a_Intersect_MatchesAnalytic() {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.6f;
    return { boxOp(bH), sphere(sC,sR), intersectOp() };
}

inline std::vector<SdfInstruction> Make_M3a_Xor_MatchesAnalytic() {
    const glm::vec3 bH(0.7f,0.7f,0.7f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.6f;
    return { boxOp(bH), sphere(sC,sR), xorOp() };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothSubtract_MatchesAnalytic() {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f; const float k=0.3f;
    return { boxOp(bH), sphere(sC,sR), smoothSubtractOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothIntersect_MatchesAnalytic() {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.25f;
    return { boxOp(bH), sphere(sC,sR), smoothIntersectOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothMax_MatchesAnalytic() {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    return { boxOp(bH), sphere(sC,sR), smoothMaxOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothUnionCubic_MatchesAnalytic() {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    return { boxOp(bH), sphere(sC,sR), smoothUnionCubicOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothSubtractCubic_MatchesAnalytic() {
    const glm::vec3 bH(0.8f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.4f; const float k=0.3f;
    return { boxOp(bH), sphere(sC,sR), smoothSubtractCubicOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_SmoothIntersectCubic_MatchesAnalytic() {
    const glm::vec3 bH(0.7f,0.5f,0.5f);
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float k=0.3f;
    return { boxOp(bH), sphere(sC,sR), smoothIntersectCubicOp(k) };
}

inline std::vector<SdfInstruction> Make_M3a_Round_MatchesAnalytic() {
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float rad=0.1f;
    return { sphere(sC,sR), roundOp(rad) };
}

inline std::vector<SdfInstruction> Make_M3a_Onion_MatchesAnalytic() {
    const glm::vec3 sC(0.f,0.f,0.f); const float sR=0.5f; const float thick=0.05f;
    return { sphere(sC,sR), onionOp(thick) };
}

inline std::vector<SdfInstruction> Make_M3b1_Capsule_MatchesAnalytic() {
    const float halfH = 0.5f, r = 0.3f;
    return { capsuleOp(halfH, r) };
}

inline std::vector<SdfInstruction> Make_M3b1_Cylinder_MatchesAnalytic() {
    const float halfH = 0.6f, r = 0.4f;
    return { cylinderOp(halfH, r) };
}

inline std::vector<SdfInstruction> Make_M3b1_Torus_MatchesAnalytic() {
    const float majorR = 0.6f, minorR = 0.2f;
    return { torusOp(majorR, minorR) };
}

inline std::vector<SdfInstruction> Make_M3b1_BoxRounded_MatchesAnalytic() {
    const glm::vec3 he(0.5f, 0.4f, 0.3f); const float rr = 0.05f;
    return { boxRoundedOp(he, rr) };
}

inline std::vector<SdfInstruction> Make_M3b1_Plane_MatchesAnalytic() {
    const glm::vec3 n(0.0f, 1.0f, 0.0f); const float d = -0.5f;
    return { planeOp(n, d) };
}

inline std::vector<SdfInstruction> Make_M3b2_Ellipsoid_MatchesOracle() {
    const glm::vec3 radii(0.6f, 0.4f, 0.5f);
    return { ellipsoidOp(radii, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_HollowCylinder_MatchesAnalytic() {
    const float halfLen=0.5f, outerR=0.4f, wall=0.05f;
    return { hollowCylinderOp(halfLen, outerR, wall, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_TaperedCylinder_MatchesAnalytic() {
    const float height=1.0f, r1=0.4f, r2=0.15f;
    return { taperedCylinderOp(height, r1, r2, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_Cone_MatchesAnalytic() {
    const float sinA=0.5f, cosA=0.866f, height=1.0f;
    return { coneOp(sinA, cosA, height, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_CappedTorus_MatchesAnalytic() {
    const float sinA=0.966f, cosA=0.259f, majorR=0.6f, minorR=0.15f;
    return { cappedTorusOp(sinA, cosA, majorR, minorR, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_Link_MatchesAnalytic() {
    const float halfLen=0.3f, majorR=0.4f, minorR=0.1f;
    return { linkOp(halfLen, majorR, minorR, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_Panel_MatchesAnalytic() {
    const glm::vec3 he(0.6f, 0.05f, 0.4f); const float rr=0.02f;
    return { panelOp(he, rr, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_Plank_MatchesAnalytic() {
    const glm::vec3 he(0.8f, 0.08f, 0.12f); const float rr=0.02f;
    return { plankOp(he, rr, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b2_RoundedBox_MatchesAnalytic() {
    const glm::vec3 he(0.4f, 0.3f, 0.35f); const float rr=0.05f;
    return { roundedBoxOp(he, rr, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b3_TriangularPrism_MatchesOracle() {
    const float hx=0.5f, hy=0.4f;
    return { triangularPrismOp(hx, hy, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b3_HexPrism_MatchesOracle() {
    const float hx=0.4f, hy=0.5f;
    return { hexPrismOp(hx, hy, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b3_Pyramid_MatchesOracle() {
    const float height=1.0f;
    return { pyramidOp(height, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b3_Segment_MatchesOracle() {
    const glm::vec3 ptA(0.1f, -0.3f, 0.2f), ptB(0.7f, 0.5f, -0.1f);
    const float radius = 0.08f;
    return { segmentOp(ptA, radius, ptB) };
}

inline std::vector<SdfInstruction> Make_M3b3_FakeRoundCone_MatchesOracle() {
    const float r1=0.3f, r2=0.1f, height=1.0f;
    return { fakeRoundConeOp(r1, r2, height, kOff) };
}

inline std::vector<SdfInstruction> Make_M3b3_RoundCone_MatchesOracle() {
    const float r1=0.3f, r2=0.1f, height=1.0f;
    return { roundConeOp(r1, r2, height, kOff) };
}

inline std::vector<SdfInstruction> Make_M4a_MirrorY_MatchesOracle() {
    const float r = 0.5f;
    const glm::vec3 center(0.f, 0.5f, 0.f);
    return { mirrorYOp(), sphere(center, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4a_MirrorZ_MatchesOracle() {
    const float r = 0.5f;
    const glm::vec3 center(0.f, 0.f, 0.5f);
    return { mirrorZOp(), sphere(center, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4a_Elongate_MatchesOracle() {
    const glm::vec3 h(0.4f, 0.2f, 0.3f);
    const float r = 0.3f;
    return { elongateOp(h), sphere(glm::vec3(0.f), r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4a_Revolution_MatchesOracle() {
    const float offset = 0.8f;
    const glm::vec3 center(0.1f, 0.0f, 0.2f);
    const float r = 0.15f;
    return { revolutionOp(offset, center), sphere(center, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_Twist_MatchesOracle() {
    const float k = 1.2f;
    const glm::vec3 center(0.6f, 0.0f, 0.0f);
    const float r = 0.2f;
    return { twistOp(k), sphere(center, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_Bend_MatchesOracle() {
    const float k = 0.8f;
    const glm::vec3 center(0.0f, 0.5f, 0.3f);
    const float r = 0.2f;
    return { bendOp(k), sphere(center, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_RepeatInfinite_MatchesOracle() {
    const glm::vec3 sp(1.0f, 1.5f, 0.8f);
    const float r = 0.2f;
    return { repeatInfiniteOp(sp), sphere(glm::vec3(0.f), r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_RepeatLimited_MatchesOracle() {
    const float s = 1.2f;
    const glm::vec3 lim(2.0f, 1.0f, 1.5f);
    const float r = 0.3f;
    return { repeatLimitedOp(s, lim), sphere(glm::vec3(0.f), r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_Transform_MatchesOracle() {
    float halfAngle = glm::radians(45.0f) * 0.5f;
    // Independent of glm::quat storage — encode the inverse rotation of a 45deg
    // rotation about Y directly as an xyzw quaternion (matches the TEST's
    // glm::inverse(glm::quat(cos(halfAngle), (0,sin(halfAngle),0))) result).
    float cw = std::cos(halfAngle), sw = std::sin(halfAngle);
    // rot = (x=0, y=sw, z=0, w=cw); inverse of a unit quaternion negates x,y,z.
    glm::vec4 invRotV(0.0f, -sw, 0.0f, cw);
    glm::vec3 trans(0.5f, 0.2f, 0.1f);
    glm::vec3 scale(2.0f, 1.5f, 0.8f);
    glm::vec3 invScale(1.0f/scale.x, 1.0f/scale.y, 1.0f/scale.z);
    float distScale = std::min({scale.x, scale.y, scale.z});
    SdfInstruction xf = transformOp(trans, invRotV, invScale, distScale);
    const float r = 0.3f;
    const glm::vec3 childCenter(0.4f, 0.3f, 0.2f);
    return { xf, sphere(childCenter, r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_DistScale_ScaledTransformScalesDistance() {
    glm::vec4 identRotXYZW(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 noTrans(0.0f, 0.0f, 0.0f);
    glm::vec3 invScale(0.5f, 0.5f, 0.5f);
    float distScale = 2.0f;
    SdfInstruction xf = transformOp(noTrans, identRotXYZW, invScale, distScale);
    const float r = 0.5f;
    return { xf, sphere(glm::vec3(0.f), r), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4b_DistScale_NestedTransformsComposeScale() {
    glm::vec4 identRotXYZW(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 noTrans(0.0f, 0.0f, 0.0f);
    glm::vec3 invScale1(1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f);
    float distScale1 = 3.0f;
    glm::vec3 invScale2(0.5f, 0.5f, 0.5f);
    float distScale2 = 2.0f;
    SdfInstruction xf1 = transformOp(noTrans, identRotXYZW, invScale1, distScale1);
    SdfInstruction xf2 = transformOp(noTrans, identRotXYZW, invScale2, distScale2);
    const float r = 0.5f;
    return { xf1, xf2, sphere(glm::vec3(0.f), r), restorePosOp(), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4a_DistScaleNoOp_MirrorXNonRegression() {
    const glm::vec3 b(0.8f, 0.5f, 0.5f);
    const glm::vec3 c(1.5f, 0.0f, 0.0f);
    const float r = 0.5f, k = 0.3f;
    return { mirrorXOp(), boxOp(b), sphere(c,r), smoothUnionOp(k), restorePosOp() };
}

inline std::vector<SdfInstruction> Make_M4c_PositionChannel_Y() {
    return { posChannelOp(1), posChannelOp(1), mathAddOp() };
}

inline std::vector<SdfInstruction> Make_M4c_PositionChannel_X_and_LenXZ() {
    // TEST evaluates progX first; that is the primary prog[] captured here.
    return { posChannelOp(0), mathNegateOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathSin_MatchesOracle() {
    const float freq=3.f, phase=0.5f, amp=2.f;
    return { posChannelOp(1), mathSinOp(freq, phase, amp) };
}

inline std::vector<SdfInstruction> Make_M4c_MathCos_MatchesOracle() {
    const float freq=2.f, phase=0.f, amp=1.5f;
    return { posChannelOp(1), mathCosOp(freq, phase, amp) };
}

inline std::vector<SdfInstruction> Make_M4c_MathSmoothstep_MatchesOracle() {
    const float e0=0.2f, e1=0.8f;
    return { posChannelOp(1), mathSmoothstepOp(e0, e1) };
}

inline std::vector<SdfInstruction> Make_M4c_MathRemap_MatchesOracle() {
    const float iMin=0.f, iMax=1.f, oMin=-1.f, oMax=1.f;
    return { posChannelOp(1), mathRemapOp(iMin, iMax, oMin, oMax) };
}

inline std::vector<SdfInstruction> Make_M4c_MathClamp_MatchesOracle() {
    const float lo=0.2f, hi=0.7f;
    return { posChannelOp(1), mathClampOp(lo, hi) };
}

inline std::vector<SdfInstruction> Make_M4c_MathAbs_NegativeInput() {
    return { posChannelOp(1), mathAbsOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathFrac_MatchesOracle() {
    return { posChannelOp(1), mathFracOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathPow_MatchesOracle() {
    const float power = 2.5f;
    return { posChannelOp(1), mathPowOp(power) };
}

inline std::vector<SdfInstruction> Make_M4c_MathSqrt_MatchesOracle() {
    return { posChannelOp(1), mathSqrtOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathNegate_MatchesOracle() {
    return { posChannelOp(1), mathNegateOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathStep_MatchesOracle() {
    const float edge = 0.5f;
    return { posChannelOp(1), mathStepOp(edge) };
}

inline std::vector<SdfInstruction> Make_M4c_MathSign_MatchesOracle() {
    return { posChannelOp(1), mathSignOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathSaturate_MatchesOracle() {
    return { posChannelOp(1), mathSaturateOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathExp_MatchesOracle() {
    return { posChannelOp(1), mathExpOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathLog_MatchesOracle() {
    return { posChannelOp(1), mathLogOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathLog2_MatchesOracle() {
    return { posChannelOp(1), mathLog2Op() };
}

inline std::vector<SdfInstruction> Make_M4c_MathAdd_MatchesOracle() {
    return { posChannelOp(0), posChannelOp(1), mathAddOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathSub_NonCommutativeAsymmetric() {
    // TEST evaluates fwd (x - y) as the primary program; bwd is a second array
    // used only to prove non-commutativity, not iterated with the main sample loop.
    return { posChannelOp(0), posChannelOp(1), mathSubOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathMul_MatchesOracle() {
    return { posChannelOp(0), posChannelOp(1), mathMulOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathDiv_NonCommutativeAndSafe() {
    // TEST also builds a second `zero[]` program (x/z with z=0) to test safe div-by-zero;
    // this Make_ function captures the primary `fwd[]` program (x/y).
    return { posChannelOp(0), posChannelOp(1), mathDivOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathMin_MatchesOracle() {
    return { posChannelOp(0), posChannelOp(1), mathMinOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathMax_MatchesOracle() {
    return { posChannelOp(0), posChannelOp(1), mathMaxOp() };
}

inline std::vector<SdfInstruction> Make_M4c_MathLerp_Asymmetric() {
    return { posChannelOp(0), posChannelOp(1), posChannelOp(2), mathLerpOp() };
}

inline std::vector<SdfInstruction> Make_M4c_Select_Asymmetric() {
    const float thresh = 0.0f;
    return { posChannelOp(0), posChannelOp(1), posChannelOp(2), selectOp(thresh) };
}

inline std::vector<SdfInstruction> Make_M4c_Displacement_SphereWithSin() {
    const glm::vec3 ctr(0.f,0.f,0.f); const float rad=0.8f;
    const float freq=3.f, phase=0.f, amp=1.f, scale=0.05f;
    return {
        sphere(ctr, rad),
        posChannelOp(1),
        mathSinOp(freq, phase, amp),
        displacementOp(scale),
    };
}

inline std::vector<SdfInstruction> Make_M4c_DistanceTo_MatchesOracle() {
    const glm::vec3 center(1.f, 2.f, 3.f);
    return { distanceToOp(center) };
}

// NOTE: M4d_N1_Select_KernelProbe_AboveThresh / _BelowThresh / M4d_N1_Displacement_KernelProbe
// are DELIBERATELY excluded from this corpus (and from GetAll() below). Those 3 TEST cases
// call SdfCore_Select/SdfCore_Displacement directly — there is no prog[]/evalRecipe() call,
// so no opcode ever dispatches through them; they aren't part of the opcode-coverage claim.
// The Select/Displacement OPCODES themselves (dispatched via evalRecipe) are still covered by
// M4c_Select_Asymmetric and M4c_Displacement_SphereWithSin below, so the coverage assertion
// (union of corpus opcodes == RecipeRegistry::IsValidSdfOpCode) is unaffected by the omission.

inline std::vector<SdfInstruction> Make_M4d_PushParam_PushesDataValue() {
    return { pushParamOp(42.5f) };
}

inline std::vector<SdfInstruction> Make_M4d_Output_IsPassthrough() {
    return { pushParamOp(3.14f), outputOp() };
}

inline std::vector<SdfInstruction> Make_M4d_Passthrough_IsNoop() {
    return { posChannelOp(0), passthroughOp() };
}

inline std::vector<SdfInstruction> Make_M4d_PushFloat3_DecomposeFloat3_AllComponents() {
    // TEST parameterizes ch via a local `run(ch)` lambda over ch=0,1,2; the
    // instruction sequence is identical except for DecomposeFloat3's channel
    // argument. Captured here with ch=0 (the first invocation, `run(0)`).
    const float x=1.f, y=2.f, z=3.f;
    return { pushFloat3Op(x,y,z), decomposeFloat3Op(0) };
}

inline std::vector<SdfInstruction> Make_M4d_ComposeFloat3_IsNoop() {
    return {
        posChannelOp(0), posChannelOp(1), posChannelOp(2),
        composeFloat3Op(),
        decomposeFloat3Op(2)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Add_IndependentOracle() {
    return {
        pushFloat3Op(1,2,3), pushFloat3Op(4,5,6),
        float3AddOp(),
        decomposeFloat3Op(1)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Sub_IsAsymmetric() {
    // TEST also builds a second `progRev[]` (reversed push order) to prove
    // asymmetry; this Make_ function captures the primary `prog[]`.
    return {
        pushFloat3Op(5,3,7), pushFloat3Op(1,2,4),
        float3SubOp(),
        decomposeFloat3Op(2)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3MulComponentWise_Oracle() {
    return {
        pushFloat3Op(2,3,4), pushFloat3Op(5,6,7),
        float3MulCWOp(),
        decomposeFloat3Op(0)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Min_Oracle() {
    return {
        pushFloat3Op(1,5,3), pushFloat3Op(4,2,6),
        float3MinOp(),
        decomposeFloat3Op(1)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Max_Oracle() {
    return {
        pushFloat3Op(1,5,3), pushFloat3Op(4,2,6),
        float3MaxOp(),
        decomposeFloat3Op(2)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3ScalarMul_IsAsymmetric() {
    return {
        pushFloat3Op(2,4,6),
        pushParamOp(0.5f),
        float3ScalarMulOp(),
        decomposeFloat3Op(1)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Dot_IndependentOracle() {
    // TEST also builds a second `progOrth[]` (orthogonal vectors) as a second
    // check; this Make_ function captures the primary `prog[]`.
    return {
        pushFloat3Op(1,2,3), pushFloat3Op(4,5,6),
        float3DotOp()
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Normalize_Oracle() {
    return {
        pushFloat3Op(3,0,4),
        float3NormalizeOp(),
        decomposeFloat3Op(0)
    };
}

inline std::vector<SdfInstruction> Make_M4d_Float3Normalize_ZeroVector_Safe() {
    return {
        pushFloat3Op(0,0,0),
        float3NormalizeOp(),
        decomposeFloat3Op(0)
    };
}

inline std::vector<SdfInstruction> Make_M5_CompositionAll_IndependentOracle() {
    const glm::vec3 noTrans(0.f, 0.f, 0.f);
    const glm::vec4 identRot(0.f, 0.f, 0.f, 1.f);
    const glm::vec3 invSc(0.5f, 0.5f, 1.f/3.f);
    const float dScale = 2.0f;

    return {
        twistOp(0.6f),
        transformOp(noTrans, identRot, invSc, dScale),
        boxOp(glm::vec3(0.25f, 0.25f, 0.25f)),
        restorePosOp(),
        sphere(glm::vec3(0.55f, 0.f, 0.f), 0.35f),
        smoothUnionOp(0.15f),
        restorePosOp(),
        posChannelOp(1),
        mathSinOp(3.5f, 0.f, 1.f),
        displacementOp(0.06f),
    };
}

// Recipe-Parameterization M2 Task 5b: ReadParam/ReadParamFloat3 corpus coverage. Evaluated
// with an EMPTY params span by the shared corpus consumers (evalRecipe defaults to {}), same
// as every other corpus program — bounds-check fail-safe (SdfRecipeEval.h Task 3) means an
// out-of-range read is well-defined (0.0f), not undefined behavior, so these still produce a
// deterministic CPU/GPU-comparable value even with no params supplied. The actual "params
// VARY without recompile" claim is exercised separately in Task 7's dedicated sweep test,
// which supplies real non-empty params arrays against one of these same programs.
inline std::vector<SdfInstruction> Make_M2_ReadParam_MatchesIndexedRead() {
    // A sphere whose radius is read from params[0] rather than baked — index 0 chosen to
    // land inside the fixed params[6] carrier's valid range for the Task 7 sweep test reuse.
    return { readParamOp(0), sphere(glm::vec3(0.f), 0.5f), mathMaxOp() };
}

inline std::vector<SdfInstruction> Make_M2_ReadParamFloat3_MatchesIndexedRead() {
    // Reads params[idx*3..idx*3+2] as a vec3 offset, decomposes to its Y component, and adds
    // a baked sphere distance — exercises the vec3 three-component indexed read end-to-end.
    return {
        readParamFloat3Op(0), decomposeFloat3Op(1),
        sphere(glm::vec3(0.f), 0.5f), mathAddOp()
    };
}

inline std::vector<CorpusProgram> GetAll() {
    return {
        { "SphereUnionMatchesAnalytic", Make_SphereUnionMatchesAnalytic() },
        { "MirrorXSmoothUnionBoxSphereMatchesAnalytic", Make_MirrorXSmoothUnionBoxSphereMatchesAnalytic() },
        { "M3a_Subtract_MatchesAnalytic", Make_M3a_Subtract_MatchesAnalytic() },
        { "M3a_Intersect_MatchesAnalytic", Make_M3a_Intersect_MatchesAnalytic() },
        { "M3a_Xor_MatchesAnalytic", Make_M3a_Xor_MatchesAnalytic() },
        { "M3a_SmoothSubtract_MatchesAnalytic", Make_M3a_SmoothSubtract_MatchesAnalytic() },
        { "M3a_SmoothIntersect_MatchesAnalytic", Make_M3a_SmoothIntersect_MatchesAnalytic() },
        { "M3a_SmoothMax_MatchesAnalytic", Make_M3a_SmoothMax_MatchesAnalytic() },
        { "M3a_SmoothUnionCubic_MatchesAnalytic", Make_M3a_SmoothUnionCubic_MatchesAnalytic() },
        { "M3a_SmoothSubtractCubic_MatchesAnalytic", Make_M3a_SmoothSubtractCubic_MatchesAnalytic() },
        { "M3a_SmoothIntersectCubic_MatchesAnalytic", Make_M3a_SmoothIntersectCubic_MatchesAnalytic() },
        { "M3a_Round_MatchesAnalytic", Make_M3a_Round_MatchesAnalytic() },
        { "M3a_Onion_MatchesAnalytic", Make_M3a_Onion_MatchesAnalytic() },
        { "M3b1_Capsule_MatchesAnalytic", Make_M3b1_Capsule_MatchesAnalytic() },
        { "M3b1_Cylinder_MatchesAnalytic", Make_M3b1_Cylinder_MatchesAnalytic() },
        { "M3b1_Torus_MatchesAnalytic", Make_M3b1_Torus_MatchesAnalytic() },
        { "M3b1_BoxRounded_MatchesAnalytic", Make_M3b1_BoxRounded_MatchesAnalytic() },
        { "M3b1_Plane_MatchesAnalytic", Make_M3b1_Plane_MatchesAnalytic() },
        { "M3b2_Ellipsoid_MatchesOracle", Make_M3b2_Ellipsoid_MatchesOracle() },
        { "M3b2_HollowCylinder_MatchesAnalytic", Make_M3b2_HollowCylinder_MatchesAnalytic() },
        { "M3b2_TaperedCylinder_MatchesAnalytic", Make_M3b2_TaperedCylinder_MatchesAnalytic() },
        { "M3b2_Cone_MatchesAnalytic", Make_M3b2_Cone_MatchesAnalytic() },
        { "M3b2_CappedTorus_MatchesAnalytic", Make_M3b2_CappedTorus_MatchesAnalytic() },
        { "M3b2_Link_MatchesAnalytic", Make_M3b2_Link_MatchesAnalytic() },
        { "M3b2_Panel_MatchesAnalytic", Make_M3b2_Panel_MatchesAnalytic() },
        { "M3b2_Plank_MatchesAnalytic", Make_M3b2_Plank_MatchesAnalytic() },
        { "M3b2_RoundedBox_MatchesAnalytic", Make_M3b2_RoundedBox_MatchesAnalytic() },
        { "M3b3_TriangularPrism_MatchesOracle", Make_M3b3_TriangularPrism_MatchesOracle() },
        { "M3b3_HexPrism_MatchesOracle", Make_M3b3_HexPrism_MatchesOracle() },
        { "M3b3_Pyramid_MatchesOracle", Make_M3b3_Pyramid_MatchesOracle() },
        { "M3b3_Segment_MatchesOracle", Make_M3b3_Segment_MatchesOracle() },
        { "M3b3_FakeRoundCone_MatchesOracle", Make_M3b3_FakeRoundCone_MatchesOracle() },
        { "M3b3_RoundCone_MatchesOracle", Make_M3b3_RoundCone_MatchesOracle() },
        { "M4a_MirrorY_MatchesOracle", Make_M4a_MirrorY_MatchesOracle() },
        { "M4a_MirrorZ_MatchesOracle", Make_M4a_MirrorZ_MatchesOracle() },
        { "M4a_Elongate_MatchesOracle", Make_M4a_Elongate_MatchesOracle() },
        { "M4a_Revolution_MatchesOracle", Make_M4a_Revolution_MatchesOracle() },
        { "M4b_Twist_MatchesOracle", Make_M4b_Twist_MatchesOracle() },
        { "M4b_Bend_MatchesOracle", Make_M4b_Bend_MatchesOracle() },
        { "M4b_RepeatInfinite_MatchesOracle", Make_M4b_RepeatInfinite_MatchesOracle() },
        { "M4b_RepeatLimited_MatchesOracle", Make_M4b_RepeatLimited_MatchesOracle() },
        { "M4b_Transform_MatchesOracle", Make_M4b_Transform_MatchesOracle() },
        { "M4b_DistScale_ScaledTransformScalesDistance", Make_M4b_DistScale_ScaledTransformScalesDistance() },
        { "M4b_DistScale_NestedTransformsComposeScale", Make_M4b_DistScale_NestedTransformsComposeScale() },
        { "M4a_DistScaleNoOp_MirrorXNonRegression", Make_M4a_DistScaleNoOp_MirrorXNonRegression() },
        { "M4c_PositionChannel_Y", Make_M4c_PositionChannel_Y() },
        { "M4c_PositionChannel_X_and_LenXZ", Make_M4c_PositionChannel_X_and_LenXZ() },
        { "M4c_MathSin_MatchesOracle", Make_M4c_MathSin_MatchesOracle() },
        { "M4c_MathCos_MatchesOracle", Make_M4c_MathCos_MatchesOracle() },
        { "M4c_MathSmoothstep_MatchesOracle", Make_M4c_MathSmoothstep_MatchesOracle() },
        { "M4c_MathRemap_MatchesOracle", Make_M4c_MathRemap_MatchesOracle() },
        { "M4c_MathClamp_MatchesOracle", Make_M4c_MathClamp_MatchesOracle() },
        { "M4c_MathAbs_NegativeInput", Make_M4c_MathAbs_NegativeInput() },
        { "M4c_MathFrac_MatchesOracle", Make_M4c_MathFrac_MatchesOracle() },
        { "M4c_MathPow_MatchesOracle", Make_M4c_MathPow_MatchesOracle() },
        { "M4c_MathSqrt_MatchesOracle", Make_M4c_MathSqrt_MatchesOracle() },
        { "M4c_MathNegate_MatchesOracle", Make_M4c_MathNegate_MatchesOracle() },
        { "M4c_MathStep_MatchesOracle", Make_M4c_MathStep_MatchesOracle() },
        { "M4c_MathSign_MatchesOracle", Make_M4c_MathSign_MatchesOracle() },
        { "M4c_MathSaturate_MatchesOracle", Make_M4c_MathSaturate_MatchesOracle() },
        { "M4c_MathExp_MatchesOracle", Make_M4c_MathExp_MatchesOracle() },
        { "M4c_MathLog_MatchesOracle", Make_M4c_MathLog_MatchesOracle() },
        { "M4c_MathLog2_MatchesOracle", Make_M4c_MathLog2_MatchesOracle() },
        { "M4c_MathAdd_MatchesOracle", Make_M4c_MathAdd_MatchesOracle() },
        { "M4c_MathSub_NonCommutativeAsymmetric", Make_M4c_MathSub_NonCommutativeAsymmetric() },
        { "M4c_MathMul_MatchesOracle", Make_M4c_MathMul_MatchesOracle() },
        { "M4c_MathDiv_NonCommutativeAndSafe", Make_M4c_MathDiv_NonCommutativeAndSafe() },
        { "M4c_MathMin_MatchesOracle", Make_M4c_MathMin_MatchesOracle() },
        { "M4c_MathMax_MatchesOracle", Make_M4c_MathMax_MatchesOracle() },
        { "M4c_MathLerp_Asymmetric", Make_M4c_MathLerp_Asymmetric() },
        { "M4c_Select_Asymmetric", Make_M4c_Select_Asymmetric() },
        { "M4c_Displacement_SphereWithSin", Make_M4c_Displacement_SphereWithSin() },
        { "M4c_DistanceTo_MatchesOracle", Make_M4c_DistanceTo_MatchesOracle() },
        // M4d_N1_Select_KernelProbe_AboveThresh/_BelowThresh/M4d_N1_Displacement_KernelProbe
        // deliberately omitted — see the comment above their would-be Make_ functions.
        { "M4d_PushParam_PushesDataValue", Make_M4d_PushParam_PushesDataValue() },
        { "M4d_Output_IsPassthrough", Make_M4d_Output_IsPassthrough() },
        { "M4d_Passthrough_IsNoop", Make_M4d_Passthrough_IsNoop() },
        { "M4d_PushFloat3_DecomposeFloat3_AllComponents", Make_M4d_PushFloat3_DecomposeFloat3_AllComponents() },
        { "M4d_ComposeFloat3_IsNoop", Make_M4d_ComposeFloat3_IsNoop() },
        { "M4d_Float3Add_IndependentOracle", Make_M4d_Float3Add_IndependentOracle() },
        { "M4d_Float3Sub_IsAsymmetric", Make_M4d_Float3Sub_IsAsymmetric() },
        { "M4d_Float3MulComponentWise_Oracle", Make_M4d_Float3MulComponentWise_Oracle() },
        { "M4d_Float3Min_Oracle", Make_M4d_Float3Min_Oracle() },
        { "M4d_Float3Max_Oracle", Make_M4d_Float3Max_Oracle() },
        { "M4d_Float3ScalarMul_IsAsymmetric", Make_M4d_Float3ScalarMul_IsAsymmetric() },
        { "M4d_Float3Dot_IndependentOracle", Make_M4d_Float3Dot_IndependentOracle() },
        { "M4d_Float3Normalize_Oracle", Make_M4d_Float3Normalize_Oracle() },
        { "M4d_Float3Normalize_ZeroVector_Safe", Make_M4d_Float3Normalize_ZeroVector_Safe() },
        { "M5_CompositionAll_IndependentOracle", Make_M5_CompositionAll_IndependentOracle() },
        { "M2_ReadParam_MatchesIndexedRead", Make_M2_ReadParam_MatchesIndexedRead() },
        { "M2_ReadParamFloat3_MatchesIndexedRead", Make_M2_ReadParamFloat3_MatchesIndexedRead() },
    };
}

} // namespace Vixen::SVO::Recipe::ParityCorpus
