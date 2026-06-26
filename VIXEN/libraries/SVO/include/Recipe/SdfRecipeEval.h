#pragma once
#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/SdfCoreKernels.g.hpp"   // Yeroket::Sdf::Generated::SdfCore_*
#include <glm/glm.hpp>
#include <cstdint>

namespace Vixen::SVO::Recipe {

inline float evalRecipe(const SdfInstruction* prog, uint32_t count, glm::vec3 p) {
    float stack[64]; int sp = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                glm::vec3 c(in.data[0], in.data[1], in.data[2]);  // Data0.xyz = center
                float r = in.data[3];                              // Data0.w   = radius
                stack[sp++] = Yeroket::Sdf::Generated::SdfCore_Sphere(p, c, r);
            } break;
            case SdfOpCode::Union: {
                float b = stack[--sp]; float a = stack[--sp];
                stack[sp++] = Yeroket::Sdf::Generated::SdfCore_Union(a, b);
            } break;
        }
    }
    return stack[sp - 1];
}

} // namespace Vixen::SVO::Recipe
