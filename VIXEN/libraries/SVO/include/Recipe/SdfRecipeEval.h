#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/SdfCoreKernels.g.hpp"   // Yeroket::Sdf::Generated::SdfCore_*
#include <glm/glm.hpp>
#include <cassert>
#include <cstdint>

namespace Vixen::SVO::Recipe {

inline float evalRecipe(const SdfInstruction* prog, uint32_t count, glm::vec3 p) {
    float stack[64]; int sp = 0;
    glm::vec3 pos = p;                   // current sample point (mirrors C# VM ctx.Pos)
    glm::vec3 posStack[64]; int psp = 0; // domain-transform save stack (C# VM ctx.PosStack)
    float distScaleStack[64];            // distance-scale per saved frame (pushed 1.0f for non-scaling transforms; M4b Transform uses data[11])
    using namespace Yeroket::Sdf::Generated;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        assert(in.paramMask == 0 && "ParamMask!=0 deferred to P4");
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                assert(sp < 64 && "value stack overflow");
                glm::vec3 c(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = center
                float r = in.data[3];                              // Data0.w   = radius
                stack[sp++] = SdfCore_Sphere(pos, c, r);           // pos (not p)
            } break;
            case SdfOpCode::Box: {
                assert(sp < 64 && "value stack overflow");
                glm::vec3 b(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = halfExtents
                stack[sp++] = SdfCore_Box(pos, b);
            } break;
            case SdfOpCode::Union: {
                assert(sp >= 2 && "Union: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_Union(a, b);
            } break;
            case SdfOpCode::SmoothUnion: {
                assert(sp >= 2 && "SmoothUnion: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothUnion(a, b, in.data[2]); // k = Data0.z
            } break;
            // --- Binary CSG (non-smooth): b=top (cutter/B), a=deeper (base/A) ---
            case SdfOpCode::Subtract: {           // non-commutative: A minus B
                assert(sp >= 2 && "Subtract: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_Subtract(a, b);
            } break;
            case SdfOpCode::Intersect: {
                assert(sp >= 2 && "Intersect: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_Intersect(a, b);
            } break;
            case SdfOpCode::Xor: {
                assert(sp >= 2 && "Xor: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_Xor(a, b);
            } break;
            // --- Binary CSG (smooth linear): k = data[2] (Data0.z) ---
            case SdfOpCode::SmoothSubtract: {     // non-commutative
                assert(sp >= 2 && "SmoothSubtract: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothSubtract(a, b, in.data[2]);
            } break;
            case SdfOpCode::SmoothIntersect: {
                assert(sp >= 2 && "SmoothIntersect: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothIntersect(a, b, in.data[2]);
            } break;
            case SdfOpCode::SmoothMax: {
                assert(sp >= 2 && "SmoothMax: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothMax(a, b, in.data[2]);
            } break;
            // --- Binary CSG (smooth cubic): k = data[2] (Data0.z) ---
            case SdfOpCode::SmoothUnionCubic: {
                assert(sp >= 2 && "SmoothUnionCubic: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothUnionCubic(a, b, in.data[2]);
            } break;
            case SdfOpCode::SmoothSubtractCubic: { // non-commutative
                assert(sp >= 2 && "SmoothSubtractCubic: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothSubtractCubic(a, b, in.data[2]);
            } break;
            case SdfOpCode::SmoothIntersectCubic: {
                assert(sp >= 2 && "SmoothIntersectCubic: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                assert(sp < 64 && "value stack overflow");
                stack[sp++] = SdfCore_SmoothIntersectCubic(a, b, in.data[2]);
            } break;
            // --- Unary modifiers: TOS-modify, net stack delta 0; radius/thickness = data[0] ---
            case SdfOpCode::Round: {
                assert(sp >= 1 && "Round: value stack underflow");
                stack[sp - 1] = SdfCore_Round(stack[sp - 1], in.data[0]);  // radius = Data0.x
            } break;
            case SdfOpCode::Onion: {
                assert(sp >= 1 && "Onion: value stack underflow");
                stack[sp - 1] = SdfCore_Onion(stack[sp - 1], in.data[0]);  // thickness = Data0.x
            } break;
            // --- Leaf primitives (no-position, pos-off=NO) — P2.4 M3b-1 ---
            case SdfOpCode::Capsule: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=halfHeight, data[1]=radius
                stack[sp++] = SdfCore_Capsule(pos, in.data[0], in.data[1]);
            } break;
            case SdfOpCode::Cylinder: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=halfHeight, data[1]=radius
                stack[sp++] = SdfCore_Cylinder(pos, in.data[0], in.data[1]);
            } break;
            case SdfOpCode::Torus: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=majorRadius, data[1]=minorRadius
                stack[sp++] = SdfCore_Torus(pos, in.data[0], in.data[1]);
            } break;
            case SdfOpCode::BoxRounded: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=halfExtents, data[3]=rounding
                glm::vec3 he(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_BoxRounded(pos, he, in.data[3]);
            } break;
            case SdfOpCode::Plane: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=normal, data[3]=distance
                glm::vec3 n(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_Plane(pos, n, in.data[3]);
            } break;
            // --- Leaf primitives (position-offset, pos-off=YES) — P2.4 M3b-2 ---
            // Sample point: q = pos - vec3(data[4..6])
            case SdfOpCode::Ellipsoid: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=radii, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                glm::vec3 radii(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_Ellipsoid(q, radii);
            } break;
            case SdfOpCode::HollowCylinder: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=halfLen, data[1]=outerR, data[2]=wall, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_HollowCylinder(q, in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::TaperedCylinder: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=halfH (height), data[1]=baseR (r1), data[2]=topR (r2), data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_TaperedCylinder(q, in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::Cone: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=sinAngle, data[1]=cosAngle, data[2]=height, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_Cone(q, glm::vec2(in.data[0], in.data[1]), in.data[2]);
            } break;
            case SdfOpCode::CappedTorus: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=sinA, data[1]=cosA, data[2]=majorR, data[3]=minorR, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_CappedTorus(q, glm::vec2(in.data[0], in.data[1]), in.data[2], in.data[3]);
            } break;
            case SdfOpCode::Link: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=halfLen, data[1]=majorR, data[2]=minorR, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_Link(q, in.data[0], in.data[1], in.data[2]);
            } break;
            // Panel/Plank/RoundedBox: positioned BoxRounded (same math as BoxRounded=2, opcode differs)
            case SdfOpCode::Panel: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                glm::vec3 he(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_BoxRounded(q, he, in.data[3]);
            } break;
            case SdfOpCode::Plank: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                glm::vec3 he(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_BoxRounded(q, he, in.data[3]);
            } break;
            case SdfOpCode::RoundedBox: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                glm::vec3 he(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = SdfCore_BoxRounded(q, he, in.data[3]);
            } break;
            // --- Leaf primitives — P2.4 M3b-3: prism + cone family ---
            case SdfOpCode::RoundCone: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_RoundCone(q, in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::FakeRoundCone: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_FakeRoundCone(q, in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::Segment: {
                assert(sp < 64 && "value stack overflow");
                // data[0..2]=pointA, data[3]=radius, data[4..6]=pointB — no pos-offset; samples pos directly
                glm::vec3 a(in.data[0], in.data[1], in.data[2]);
                glm::vec3 b(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_Segment(pos, a, b, in.data[3]);
            } break;
            case SdfOpCode::TriangularPrism: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=h.x (tri half-size), data[1]=h.y (depth half-size), data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_TriangularPrism(q, glm::vec2(in.data[0], in.data[1]));
            } break;
            case SdfOpCode::Pyramid: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=height, data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_Pyramid(q, in.data[0]);
            } break;
            case SdfOpCode::HexPrism: {
                assert(sp < 64 && "value stack overflow");
                // data[0]=h.x (hex radius), data[1]=h.y (half-height), data[4..6]=position
                glm::vec3 q = pos - glm::vec3(in.data[4], in.data[5], in.data[6]);
                stack[sp++] = SdfCore_HexPrism(q, glm::vec2(in.data[0], in.data[1]));
            } break;
            case SdfOpCode::MirrorX: {
                assert(psp < 64 && "MirrorX: position stack overflow");
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_MirrorX(pos);
            } break;
            case SdfOpCode::MirrorY: {
                assert(psp < 64 && "MirrorY: position stack overflow");
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_MirrorY(pos);
            } break;
            case SdfOpCode::MirrorZ: {
                assert(psp < 64 && "MirrorZ: position stack overflow");
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_MirrorZ(pos);
            } break;
            case SdfOpCode::Elongate: {
                assert(psp < 64 && "Elongate: position stack overflow");
                // data[0..2] = elongation h
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_Elongate(pos, glm::vec3(in.data[0], in.data[1], in.data[2]));
            } break;
            case SdfOpCode::Revolution: {
                assert(psp < 64 && "Revolution: position stack overflow");
                // data[0]=offset, data[4..6]=center
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                glm::vec3 center(in.data[4], in.data[5], in.data[6]);
                pos = SdfCore_Revolution(pos, center, in.data[0]);
            } break;
            case SdfOpCode::Transform: {
                assert(psp < 64 && "Transform: position stack overflow");
                // data[0..2]=translation (Data0.xyz), data[4..7]=invRot xyzw (Data1),
                // data[8..10]=invScale (Data2.xyz), data[11]=distScale (Data2.w)
                glm::vec3 trans(in.data[0], in.data[1], in.data[2]);
                glm::vec4 invRot(in.data[4], in.data[5], in.data[6], in.data[7]);  // xyzw
                glm::vec3 invScale(in.data[8], in.data[9], in.data[10]);
                float distScale = in.data[11];  // math.cmin(abs(nodeScale))
                posStack[psp] = pos; distScaleStack[psp] = distScale; psp++;
                pos = SdfCore_Transform(pos, trans, invRot, invScale);
            } break;
            case SdfOpCode::Twist: {
                assert(psp < 64 && "Twist: position stack overflow");
                // data[0]=k (radians/unit Y)
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_Twist(pos, in.data[0]);
            } break;
            case SdfOpCode::Bend: {
                assert(psp < 64 && "Bend: position stack overflow");
                // data[0]=k bend amount
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_Bend(pos, in.data[0]);
            } break;
            case SdfOpCode::RepeatInfinite: {
                assert(psp < 64 && "RepeatInfinite: position stack overflow");
                // data[0..2]=spacing xyz (Data0.xyz)
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_RepeatInfinite(pos, glm::vec3(in.data[0], in.data[1], in.data[2]));
            } break;
            case SdfOpCode::RepeatLimited: {
                assert(psp < 64 && "RepeatLimited: position stack overflow");
                // data[0]=spacing scalar (Data0.x), data[1..3]=limit xyz (Data0.yzw)
                posStack[psp] = pos; distScaleStack[psp] = 1.0f; psp++;
                pos = SdfCore_RepeatLimited(pos, in.data[0], glm::vec3(in.data[1], in.data[2], in.data[3]));
            } break;
            case SdfOpCode::RestorePos: {
                assert(psp > 0 && "RestorePos: position stack underflow");
                psp--;
                pos = posStack[psp];
                stack[sp-1] *= distScaleStack[psp];  // apply distScale (1.0f for M4a; M4b Transform uses actual scale)
            } break;

            // ── M4c: value-math lane ───────────────────────────────────────────────
            // Unary: stack[sp-1] = SdfCore_MathX(stack[sp-1], data…)
            case SdfOpCode::MathSin: {
                assert(sp >= 1 && "MathSin: value stack underflow");
                // data[0]=frequency, data[1]=phase, data[2]=amplitude (ParamMask=0, baked)
                stack[sp-1] = SdfCore_MathSin(stack[sp-1], in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::MathCos: {
                assert(sp >= 1 && "MathCos: value stack underflow");
                stack[sp-1] = SdfCore_MathCos(stack[sp-1], in.data[0], in.data[1], in.data[2]);
            } break;
            case SdfOpCode::MathSmoothstep: {
                assert(sp >= 1 && "MathSmoothstep: value stack underflow");
                // data[0]=edge0, data[1]=edge1
                stack[sp-1] = SdfCore_MathSmoothstep(stack[sp-1], in.data[0], in.data[1]);
            } break;
            case SdfOpCode::MathRemap: {
                assert(sp >= 1 && "MathRemap: value stack underflow");
                // data[0]=inMin, data[1]=inMax, data[2]=outMin, data[3]=outMax
                stack[sp-1] = SdfCore_MathRemap(stack[sp-1], in.data[0], in.data[1], in.data[2], in.data[3]);
            } break;
            case SdfOpCode::MathClamp: {
                assert(sp >= 1 && "MathClamp: value stack underflow");
                // data[0]=lo, data[1]=hi
                stack[sp-1] = SdfCore_MathClamp(stack[sp-1], in.data[0], in.data[1]);
            } break;
            case SdfOpCode::MathAbs: {
                assert(sp >= 1 && "MathAbs: value stack underflow");
                stack[sp-1] = SdfCore_MathAbs(stack[sp-1]);
            } break;
            case SdfOpCode::MathFrac: {
                assert(sp >= 1 && "MathFrac: value stack underflow");
                stack[sp-1] = SdfCore_MathFrac(stack[sp-1]);
            } break;
            case SdfOpCode::MathPow: {
                assert(sp >= 1 && "MathPow: value stack underflow");
                // data[0]=power
                stack[sp-1] = SdfCore_MathPow(stack[sp-1], in.data[0]);
            } break;
            case SdfOpCode::MathSqrt: {
                assert(sp >= 1 && "MathSqrt: value stack underflow");
                stack[sp-1] = SdfCore_MathSqrt(stack[sp-1]);
            } break;
            case SdfOpCode::MathNegate: {
                assert(sp >= 1 && "MathNegate: value stack underflow");
                stack[sp-1] = SdfCore_MathNegate(stack[sp-1]);
            } break;
            case SdfOpCode::MathStep: {
                assert(sp >= 1 && "MathStep: value stack underflow");
                // data[0]=edge
                stack[sp-1] = SdfCore_MathStep(stack[sp-1], in.data[0]);
            } break;
            case SdfOpCode::MathSign: {
                assert(sp >= 1 && "MathSign: value stack underflow");
                stack[sp-1] = SdfCore_MathSign(stack[sp-1]);
            } break;
            case SdfOpCode::MathSaturate: {
                assert(sp >= 1 && "MathSaturate: value stack underflow");
                stack[sp-1] = SdfCore_MathSaturate(stack[sp-1]);
            } break;
            case SdfOpCode::MathExp: {
                assert(sp >= 1 && "MathExp: value stack underflow");
                stack[sp-1] = SdfCore_MathExp(stack[sp-1]);
            } break;
            case SdfOpCode::MathLog: {
                assert(sp >= 1 && "MathLog: value stack underflow");
                stack[sp-1] = SdfCore_MathLog(stack[sp-1]);
            } break;
            case SdfOpCode::MathLog2: {
                assert(sp >= 1 && "MathLog2: value stack underflow");
                stack[sp-1] = SdfCore_MathLog2(stack[sp-1]);
            } break;
            // Binary: float b=stack[--sp]; stack[sp-1]=SdfCore_MathX(stack[sp-1]/*a*/, b)
            case SdfOpCode::MathAdd: {
                assert(sp >= 2 && "MathAdd: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathAdd(stack[sp-1], b);
            } break;
            case SdfOpCode::MathSub: {            // non-commutative: a - b
                assert(sp >= 2 && "MathSub: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathSub(stack[sp-1], b);
            } break;
            case SdfOpCode::MathMul: {
                assert(sp >= 2 && "MathMul: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathMul(stack[sp-1], b);
            } break;
            case SdfOpCode::MathDiv: {            // safe: 0 when b==0
                assert(sp >= 2 && "MathDiv: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathDiv(stack[sp-1], b);
            } break;
            case SdfOpCode::MathMin: {
                assert(sp >= 2 && "MathMin: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathMin(stack[sp-1], b);
            } break;
            case SdfOpCode::MathMax: {
                assert(sp >= 2 && "MathMax: value stack underflow");
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathMax(stack[sp-1], b);
            } break;
            // Ternary MathLerp: t=top, b=middle, a=stack[sp-1] → lerp(a,b,t)
            case SdfOpCode::MathLerp: {
                assert(sp >= 3 && "MathLerp: value stack underflow");
                float t_val = stack[--sp];
                float b = stack[--sp];
                stack[sp-1] = SdfCore_MathLerp(stack[sp-1], b, t_val);
            } break;
            // Ternary Select: b=top, a=middle, cond=stack[sp-1] → cond>data[0]?a:b
            // N1: delegated to generated kernel (single-source math)
            case SdfOpCode::Select: {
                assert(sp >= 3 && "Select: value stack underflow");
                float b = stack[--sp];
                float a = stack[--sp];
                float cond = stack[sp-1];
                stack[sp-1] = SdfCore_Select(cond, a, b, in.data[0]);
            } break;
            // Leaf/peek: push a value derived from pos
            case SdfOpCode::PositionChannel: {
                assert(sp < 64 && "PositionChannel: value stack overflow");
                int ch = (int)in.data[0];  // 0=x, 1=y, 2=z, 3=length(xz)
                float val;
                switch (ch) {
                    case 0: val = pos.x; break;
                    case 1: val = pos.y; break;
                    case 2: val = pos.z; break;
                    case 3: val = glm::length(glm::vec2(pos.x, pos.z)); break;
                    default: val = pos.y; break;
                }
                stack[sp++] = val;
            } break;
            case SdfOpCode::Displacement: {       // pop disp; stack[sp-1] = sdf + disp * scale
                // N1: delegated to generated kernel (single-source math)
                assert(sp >= 2 && "Displacement: value stack underflow");
                float disp = stack[--sp];
                stack[sp-1] = SdfCore_Displacement(stack[sp-1], disp, in.data[0]);
            } break;
            case SdfOpCode::DistanceTo: {          // push length(pos - center)
                assert(sp < 64 && "DistanceTo: value stack overflow");
                glm::vec3 center(in.data[0], in.data[1], in.data[2]);
                stack[sp++] = glm::length(pos - center);
            } break;
            // VM-control ops (hand-dispatched — no [SdfCoreKernel] equivalent)
            case SdfOpCode::Output: {              // passthrough: marks recipe end
                // no-op for eval (stack unchanged)
            } break;
            case SdfOpCode::PushParam: {           // push baked parameter value
                assert(sp < 64 && "PushParam: value stack overflow");
                stack[sp++] = in.data[0];
            } break;
            case SdfOpCode::PushFloat3: {          // push data[0..2] as 3 floats (x then y then z)
                assert(sp < 62 && "PushFloat3: value stack overflow");
                stack[sp++] = in.data[0]; // x (deepest)
                stack[sp++] = in.data[1]; // y
                stack[sp++] = in.data[2]; // z (top)
            } break;
            case SdfOpCode::ComposeFloat3: {       // no-op: 3 scalars on stack already form float3
            } break;
            case SdfOpCode::Passthrough: {         // pop 1, push 1 unchanged (reroute node)
                // no-op for eval (stack[sp-1] unchanged)
            } break;
            case SdfOpCode::DecomposeFloat3: {     // pop float3, push one component
                assert(sp >= 3 && "DecomposeFloat3: value stack underflow");
                float vz = stack[--sp], vy = stack[--sp], vx = stack[--sp];
                int ch = (int)in.data[0]; // 0=x, 1=y, 2=z
                stack[sp++] = (ch == 0) ? vx : (ch == 1) ? vy : vz;
            } break;
            // Float3 arithmetic (float3 = 3 consecutive scalars; x=deepest, z=top)
            // Binary ops: pop b(bz,by,bx), pop a(az,ay,ax), push result(x,y,z)
            case SdfOpCode::Float3Add: {
                assert(sp >= 6 && "Float3Add: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                glm::vec3 r = SdfCore_Float3Add({ax,ay,az}, {bx,by,bz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            case SdfOpCode::Float3Sub: {           // non-commutative: a - b
                assert(sp >= 6 && "Float3Sub: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                glm::vec3 r = SdfCore_Float3Sub({ax,ay,az}, {bx,by,bz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            case SdfOpCode::Float3MulComponentWise: {
                assert(sp >= 6 && "Float3MulComponentWise: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                glm::vec3 r = SdfCore_Float3MulComponentWise({ax,ay,az}, {bx,by,bz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            case SdfOpCode::Float3Min: {
                assert(sp >= 6 && "Float3Min: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                glm::vec3 r = SdfCore_Float3Min({ax,ay,az}, {bx,by,bz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            case SdfOpCode::Float3Max: {
                assert(sp >= 6 && "Float3Max: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                glm::vec3 r = SdfCore_Float3Max({ax,ay,az}, {bx,by,bz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            // Float3ScalarMul: scalar=top, then vz,vy,vx → push result
            case SdfOpCode::Float3ScalarMul: {
                assert(sp >= 4 && "Float3ScalarMul: value stack underflow");
                float s  = stack[--sp];
                float vz = stack[--sp], vy = stack[--sp], vx = stack[--sp];
                glm::vec3 r = SdfCore_Float3ScalarMul({vx,vy,vz}, s);
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
            // Float3Dot: pop b then a → push scalar dot product
            case SdfOpCode::Float3Dot: {
                assert(sp >= 6 && "Float3Dot: value stack underflow");
                float bz = stack[--sp], by = stack[--sp], bx = stack[--sp];
                float az = stack[--sp], ay = stack[--sp], ax = stack[--sp];
                stack[sp++] = SdfCore_Float3Dot({ax,ay,az}, {bx,by,bz});
            } break;
            // Float3Normalize: pop vz,vy,vx → push normalized float3
            case SdfOpCode::Float3Normalize: {
                assert(sp >= 3 && "Float3Normalize: value stack underflow");
                float vz = stack[--sp], vy = stack[--sp], vx = stack[--sp];
                glm::vec3 r = SdfCore_Float3Normalize({vx,vy,vz});
                stack[sp++] = r.x; stack[sp++] = r.y; stack[sp++] = r.z;
            } break;
        }
    }
    assert(sp == 1 && "evalRecipe: expected exactly one value on stack at return");
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
