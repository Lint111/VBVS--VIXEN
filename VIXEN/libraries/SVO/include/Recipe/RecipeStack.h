#pragma once
#include <cstdint>
#include "Recipe/generated/SdfOpCodes.g.h"

// RecipeStackArity — shared arity table for the value-stack VM.
// Source of truth derived from evalRecipe (SdfRecipeEval.h) so the registry
// validator and the emitter agree without duplicating the table.
// ponytail: psp (position stack) tracked too; both share the 64-slot limit.
namespace Vixen::SVO::Recipe {

struct StackArity {
    int8_t vPop;   // pops from value stack (underflow if sp < vPop)
    int8_t vPush;  // pushes to value stack (overflow if sp-vPop+vPush > 64)
    int8_t pPop;   // pops from position stack
    int8_t pPush;  // pushes to position stack
};

// Returns the arity for the given opcode.
// Returns {0,0,0,0} for unknown/no-op opcodes (Output, ComposeFloat3).
inline StackArity RecipeStackArity(SdfOpCode op) {
    switch (op) {
        // --- Leaf primitives: push 1 value ---
        case SdfOpCode::Sphere:
        case SdfOpCode::Box:
        case SdfOpCode::BoxRounded:
        case SdfOpCode::Capsule:
        case SdfOpCode::Cylinder:
        case SdfOpCode::Plane:
        case SdfOpCode::Torus:
        case SdfOpCode::Ellipsoid:
        case SdfOpCode::HollowCylinder:
        case SdfOpCode::TaperedCylinder:
        case SdfOpCode::Panel:
        case SdfOpCode::Plank:
        case SdfOpCode::RoundedBox:
        case SdfOpCode::CappedTorus:
        case SdfOpCode::Cone:
        case SdfOpCode::RoundCone:
        case SdfOpCode::FakeRoundCone:
        case SdfOpCode::Segment:
        case SdfOpCode::TriangularPrism:
        case SdfOpCode::Pyramid:
        case SdfOpCode::HexPrism:
        case SdfOpCode::PositionChannel:
        case SdfOpCode::DistanceTo:
        case SdfOpCode::PushParam:
            return {0, 1, 0, 0};

        // PushFloat3: push 3 values
        case SdfOpCode::PushFloat3:
            return {0, 3, 0, 0};

        // --- Unary value ops: pop 1, push 1 (net 0) ---
        case SdfOpCode::Round:
        case SdfOpCode::Onion:
        case SdfOpCode::MathSin:
        case SdfOpCode::MathCos:
        case SdfOpCode::MathSmoothstep:
        case SdfOpCode::MathRemap:
        case SdfOpCode::MathClamp:
        case SdfOpCode::MathAbs:
        case SdfOpCode::MathFrac:
        case SdfOpCode::MathPow:
        case SdfOpCode::MathSqrt:
        case SdfOpCode::MathNegate:
        case SdfOpCode::MathStep:
        case SdfOpCode::MathSign:
        case SdfOpCode::MathSaturate:
        case SdfOpCode::MathExp:
        case SdfOpCode::MathLog:
        case SdfOpCode::MathLog2:
        case SdfOpCode::Passthrough:
            return {1, 1, 0, 0};

        // RestorePos: reads+multiplies stack[sp-1] in place (modelled as pop+push),
        // and pops the position save stack.
        case SdfOpCode::RestorePos:
            return {1, 1, 1, 0};

        // Float3Normalize: pop 3, push 3 (net 0)
        case SdfOpCode::Float3Normalize:
            return {3, 3, 0, 0};

        // --- Binary CSG and math: pop 2, push 1 (net -1) ---
        case SdfOpCode::Union:
        case SdfOpCode::SmoothUnion:
        case SdfOpCode::Subtract:
        case SdfOpCode::Intersect:
        case SdfOpCode::Xor:
        case SdfOpCode::SmoothSubtract:
        case SdfOpCode::SmoothIntersect:
        case SdfOpCode::SmoothMax:
        case SdfOpCode::SmoothUnionCubic:
        case SdfOpCode::SmoothSubtractCubic:
        case SdfOpCode::SmoothIntersectCubic:
        case SdfOpCode::MathAdd:
        case SdfOpCode::MathSub:
        case SdfOpCode::MathMul:
        case SdfOpCode::MathDiv:
        case SdfOpCode::MathMin:
        case SdfOpCode::MathMax:
        case SdfOpCode::Displacement:
            return {2, 1, 0, 0};

        // Float3ScalarMul: pop 4 (float3 + scalar), push 3 (net -1)
        case SdfOpCode::Float3ScalarMul:
            return {4, 3, 0, 0};

        // --- Ternary: pop 3, push 1 (net -2) ---
        case SdfOpCode::MathLerp:
        case SdfOpCode::Select:
        case SdfOpCode::DecomposeFloat3:
            return {3, 1, 0, 0};

        // Float3 binary: pop 6, push 3 (net -3)
        case SdfOpCode::Float3Add:
        case SdfOpCode::Float3Sub:
        case SdfOpCode::Float3MulComponentWise:
        case SdfOpCode::Float3Min:
        case SdfOpCode::Float3Max:
            return {6, 3, 0, 0};

        // Float3Dot: pop 6, push 1 (net -5)
        case SdfOpCode::Float3Dot:
            return {6, 1, 0, 0};

        // --- Domain-transform ops: push to position save stack ---
        case SdfOpCode::MirrorX:
        case SdfOpCode::MirrorY:
        case SdfOpCode::MirrorZ:
        case SdfOpCode::Elongate:
        case SdfOpCode::Revolution:
        case SdfOpCode::Transform:
        case SdfOpCode::Twist:
        case SdfOpCode::Bend:
        case SdfOpCode::RepeatInfinite:
        case SdfOpCode::RepeatLimited:
            return {0, 0, 0, 1};

        // --- No-ops / control ---
        case SdfOpCode::Output:
        case SdfOpCode::ComposeFloat3:
        default:
            return {0, 0, 0, 0};
    }
}

} // namespace Vixen::SVO::Recipe
