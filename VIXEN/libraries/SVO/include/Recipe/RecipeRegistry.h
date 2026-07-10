#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Recipe/SdfInstruction.h"
#include "Recipe/RecipeStack.h"

namespace Vixen::SVO {

// IsValidSdfOpCode — true for every enumerator in the generated SdfOpCode enum.
// ponytail: switch covers all enumerators; default=false catches unknown bytes.
inline bool IsValidSdfOpCode(uint8_t raw) {
    using Recipe::SdfOpCode;
    switch (static_cast<SdfOpCode>(raw)) {
        case SdfOpCode::Sphere: case SdfOpCode::Box: case SdfOpCode::BoxRounded:
        case SdfOpCode::Capsule: case SdfOpCode::Cylinder: case SdfOpCode::Plane:
        case SdfOpCode::Torus: case SdfOpCode::Ellipsoid: case SdfOpCode::HollowCylinder:
        case SdfOpCode::TaperedCylinder: case SdfOpCode::Panel: case SdfOpCode::Plank:
        case SdfOpCode::RoundedBox: case SdfOpCode::CappedTorus: case SdfOpCode::Cone:
        case SdfOpCode::RoundCone: case SdfOpCode::FakeRoundCone: case SdfOpCode::Segment:
        case SdfOpCode::TriangularPrism: case SdfOpCode::Pyramid: case SdfOpCode::HexPrism:
        case SdfOpCode::Link: case SdfOpCode::Union: case SdfOpCode::SmoothUnion:
        case SdfOpCode::Subtract: case SdfOpCode::SmoothSubtract: case SdfOpCode::Intersect:
        case SdfOpCode::SmoothIntersect: case SdfOpCode::Xor: case SdfOpCode::SmoothMax:
        case SdfOpCode::SmoothUnionCubic: case SdfOpCode::SmoothSubtractCubic:
        case SdfOpCode::SmoothIntersectCubic: case SdfOpCode::Round: case SdfOpCode::Onion:
        case SdfOpCode::Transform: case SdfOpCode::Elongate: case SdfOpCode::Twist:
        case SdfOpCode::Bend: case SdfOpCode::MirrorX: case SdfOpCode::MirrorY:
        case SdfOpCode::MirrorZ: case SdfOpCode::RepeatInfinite: case SdfOpCode::RepeatLimited:
        case SdfOpCode::Revolution: case SdfOpCode::MathSin: case SdfOpCode::MathCos:
        case SdfOpCode::MathSmoothstep: case SdfOpCode::MathRemap: case SdfOpCode::MathAdd:
        case SdfOpCode::MathSub: case SdfOpCode::MathMul: case SdfOpCode::MathDiv:
        case SdfOpCode::MathMin: case SdfOpCode::MathMax: case SdfOpCode::MathClamp:
        case SdfOpCode::MathAbs: case SdfOpCode::MathFrac: case SdfOpCode::MathPow:
        case SdfOpCode::MathSqrt: case SdfOpCode::MathLerp: case SdfOpCode::MathNegate:
        case SdfOpCode::PositionChannel: case SdfOpCode::Displacement: case SdfOpCode::MathStep:
        case SdfOpCode::MathSign: case SdfOpCode::MathSaturate: case SdfOpCode::MathExp:
        case SdfOpCode::MathLog: case SdfOpCode::MathLog2: case SdfOpCode::Select:
        case SdfOpCode::DistanceTo: case SdfOpCode::Output: case SdfOpCode::PushParam:
        case SdfOpCode::RestorePos: case SdfOpCode::PushFloat3: case SdfOpCode::ComposeFloat3:
        case SdfOpCode::Passthrough: case SdfOpCode::DecomposeFloat3:
        case SdfOpCode::Float3Add: case SdfOpCode::Float3Sub:
        case SdfOpCode::Float3MulComponentWise: case SdfOpCode::Float3Min:
        case SdfOpCode::Float3Max: case SdfOpCode::Float3ScalarMul:
        case SdfOpCode::Float3Dot: case SdfOpCode::Float3Normalize:
            return true;
        default:
            return false;
    }
}

class RecipeRegistry {
public:
    static constexpr uint32_t kUnbakedSlot = 0xFFFFFFFFu;

    struct RecipeEntry {
        std::vector<Recipe::SdfInstruction> bytecode;
        uint32_t bakeResolution = 0;   // 0 = engine default
        float    bandVoxels     = 0.f; // 0 = engine default
        uint32_t brickDepth     = 0;   // 0 = engine default (3)
        uint32_t octreeSlot     = kUnbakedSlot;

        // Lazy-Procedural-Delta-Baseline Inc0 M5 Task 10 — zero-bake dispatch metadata.
        // Same "0 = engine default" convention as bakeResolution/bandVoxels/brickDepth above.
        // V1 sourcing: authored directly by the caller, OR filled in by
        // RecipeRegistry::DeriveAndApplyBounds (Recipe/RecipeBounds.h) restricted to an
        // explicit opcode whitelist — see that file's header for why a program outside
        // the whitelist MUST fall back to authored/engine-default rather than guess (a
        // wrong bound silently clips geometry). This does NOT resolve design §8.1 (the
        // interval-VM upgrade path remains open).
        glm::vec3 boundCenter    = glm::vec3(0.0f);  // world-space bound-sphere center
        float     boundRadius    = 0.f;              // 0 = engine default (kResidencyBoundingRadius-style)
        float     stepRelaxation = 0.f;               // 0 = engine default; else must be in (0,1]

        // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13 — coarse occupancy grid metadata.
        // Filled in by Recipe::DeriveOccupancyGrid (RecipeOccupancy.h) at the SAME
        // registration call site as boundCenter/boundRadius above (RegisterProceduralRecipe).
        // occupancyGridValues stays empty when the program uses a non-Lipschitz-whitelisted
        // opcode (DeriveOccupancyGrid's ok=false) — the shader's splice-generated lookup
        // switch treats a zero-dim entry as "no grid, no empty-space skip for this recipe,"
        // never a hard error (an ungridded recipe still renders correctly, just without the
        // Task 13 skip optimization).
        std::vector<float> occupancyGridValues;                 // dim^3 conservative min-|sd|, x-fastest
        uint32_t            occupancyGridDim       = 0;         // 0 = no grid derived
        glm::vec3           occupancyGridAabbMin   = glm::vec3(0.0f);
        float               occupancyGridCellSize  = 0.f;
    };

    enum class RegisterResult {
        Ok,
        DuplicateId,
        EmptyProgram,
        BadOpCode,
        ParamMaskUnsupported,
        StackOverflow,     // static stack-depth exceeds 64 or underflows
        BadBoundRadius,    // boundRadius set (nonzero) but not > 0
        BadStepRelaxation, // stepRelaxation set (nonzero) but not in (0,1]
    };

    RegisterResult Register(uint32_t recipeId, const RecipeEntry& entry) {
        if (entries_.count(recipeId)) return RegisterResult::DuplicateId;
        if (entry.bytecode.empty())   return RegisterResult::EmptyProgram;
        // "0 = engine default" — anything nonzero must be a valid conservative value.
        if (entry.boundRadius != 0.f && !(entry.boundRadius > 0.f))
            return RegisterResult::BadBoundRadius;
        if (entry.stepRelaxation != 0.f && !(entry.stepRelaxation > 0.f && entry.stepRelaxation <= 1.f))
            return RegisterResult::BadStepRelaxation;

        int sp = 0, psp = 0;
        for (const auto& in : entry.bytecode) {
            if (!IsValidSdfOpCode(in.opCode)) return RegisterResult::BadOpCode;
            if (in.paramMask != 0)            return RegisterResult::ParamMaskUnsupported;

            auto a = Recipe::RecipeStackArity(static_cast<Recipe::SdfOpCode>(in.opCode));
            // underflow checks
            if (sp < a.vPop)  return RegisterResult::StackOverflow;
            if (psp < a.pPop) return RegisterResult::StackOverflow;
            // apply delta
            sp  = sp  - a.vPop  + a.vPush;
            psp = psp - a.pPop  + a.pPush;
            // overflow checks (64-slot stacks)
            if (sp  > 64) return RegisterResult::StackOverflow;
            if (psp > 64) return RegisterResult::StackOverflow;
        }

        entries_.emplace(recipeId, entry);
        return RegisterResult::Ok;
    }

    const RecipeEntry* Get(uint32_t recipeId) const {
        auto it = entries_.find(recipeId);
        return it == entries_.end() ? nullptr : &it->second;
    }

    // Mutable access for the baker (I3) to stamp octreeSlot.
    RecipeEntry* GetMutable(uint32_t recipeId) {
        auto it = entries_.find(recipeId);
        return it == entries_.end() ? nullptr : &it->second;
    }

    std::vector<uint32_t> Ids() const {
        std::vector<uint32_t> out; out.reserve(entries_.size());
        for (const auto& kv : entries_) out.push_back(kv.first); // std::map → ascending
        return out;
    }

private:
    std::map<uint32_t, RecipeEntry> entries_;
};

} // namespace Vixen::SVO
