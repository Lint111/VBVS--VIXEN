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
    using namespace Yeroket::Sdf::Generated;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                glm::vec3 c(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = center
                float r = in.data[3];                              // Data0.w   = radius
                stack[sp++] = SdfCore_Sphere(pos, c, r);           // pos (not p)
            } break;
            case SdfOpCode::Box: {
                glm::vec3 b(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = halfExtents
                stack[sp++] = SdfCore_Box(pos, b);
            } break;
            case SdfOpCode::Union: {
                assert(sp >= 2 && "Union: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = SdfCore_Union(a, b);
            } break;
            case SdfOpCode::SmoothUnion: {
                assert(sp >= 2 && "SmoothUnion: value stack underflow");
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = SdfCore_SmoothUnion(a, b, in.data[2]); // k = Data0.z
            } break;
            case SdfOpCode::MirrorX: {
                assert(psp < 64 && "MirrorX: position stack overflow");
                posStack[psp++] = pos; pos = SdfCore_MirrorX(pos);
            } break;
            case SdfOpCode::RestorePos: {
                assert(psp > 0 && "RestorePos: position stack underflow");
                pos = posStack[--psp];
            } break;
        }
    }
    assert(sp == 1 && "evalRecipe: expected exactly one value on stack at return");
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
