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
    float distScaleStack[64];            // distance-scale per saved frame (pushed 1.0f for non-scaling transforms; M4b Transform uses data[7])
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
            case SdfOpCode::RestorePos: {
                assert(psp > 0 && "RestorePos: position stack underflow");
                psp--;
                pos = posStack[psp];
                stack[sp-1] *= distScaleStack[psp];  // apply distScale (1.0f for M4a; M4b Transform uses actual scale)
            } break;
        }
    }
    assert(sp == 1 && "evalRecipe: expected exactly one value on stack at return");
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
