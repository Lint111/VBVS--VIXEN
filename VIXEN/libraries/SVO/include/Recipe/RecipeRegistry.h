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
        case SdfOpCode::ReadParam: case SdfOpCode::ReadParamFloat3:
        case SdfOpCode::DeclarePosition: case SdfOpCode::InvokeRecipe:
            return true;
        default:
            return false;
    }
}

class RecipeRegistry {
public:
    static constexpr uint32_t kUnbakedSlot = 0xFFFFFFFFu;

    // Recipe-Nested-Invocation M1: fixed, small max nesting depth for InvokeRecipe chains.
    // Chosen per the direction doc's own "2-4 levels is plausible" suggestion — enough to
    // prove the mechanism and give a future A/B test (M2) something to sweep, without
    // entertaining unbounded recursion. Depth 1 = a recipe that itself invokes zero further
    // nested recipes; a chain A->B->C->D->E (4 InvokeRecipe hops) is the deepest chain this
    // constant permits.
    static constexpr uint32_t kMaxRecipeNestingDepth = 4;

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

        // Recipe Load-Tier Contract M1 (gating tier) — same "0 = not opted in" convention
        // as boundRadius/stepRelaxation above. A positive value is a minimum screen-space
        // footprint (same units/formula as TraceWorld.glsl's
        // `footprint = distance * raySizeCoef + raySizeBias`); RecipeInstanceBucketing.comp
        // drops an instance of this recipe from bucketing/promotion for a frame where its
        // computed footprint falls below this threshold. Non-participating recipes (the
        // default, 0.0) are entirely unaffected — see Recipe-Load-Tier-Contract-Direction-
        // 2026-07.md.
        float     gateFootprintThreshold = 0.f;

        // Recipe Load-Tier Contract M2 (precision tier) — same "0 = not opted in" convention as
        // gateFootprintThreshold above, and the SAME footprint signal/formula (distance *
        // raySizeCoef + raySizeBias): a positive value is the screen-space footprint BELOW which
        // this recipe's instances upload/evaluate render params (RecipeParams, see
        // libraries/SVO/include/Recipe/generated/RecipeParams.g.h) at half precision
        // (RecipeParamsHalf, packHalf2x16) instead of full float. Independent of
        // gateFootprintThreshold — a recipe can opt into gating, precision, both, or neither (the
        // direction doc's §1: these are not mutually exclusive). Non-participating recipes
        // (default 0.0) always evaluate at full precision, byte-identical to before this
        // milestone — see GPU-Struct-Precision-Tiering-Direction-2026-07.md §3 and
        // Recipe-Load-Tier-Contract-Direction-2026-07.md's Milestone Map M2 entry.
        float     precisionFootprintThreshold = 0.f;

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
        ParamMaskUnsupported, // paramMask != 0 on an opcode that does NOT accept dynamic params
        ParamMaskRequired,    // paramMask == 0 on ReadParam/ReadParamFloat3 (P4 requires it)
        StackOverflow,     // static stack-depth exceeds 64 or underflows
        BadBoundRadius,    // boundRadius set (nonzero) but not > 0
        BadStepRelaxation, // stepRelaxation set (nonzero) but not in (0,1]
        BadGateFootprintThreshold, // gateFootprintThreshold set (nonzero) but not > 0
        BadPrecisionFootprintThreshold, // precisionFootprintThreshold set (nonzero) but not > 0
        UnknownCalleeRecipe,   // InvokeRecipe references a recipeId not yet Register()-ed
        RecursiveInvocation,   // InvokeRecipe graph contains a cycle (direct or indirect)
        NestingTooDeep,        // InvokeRecipe chain exceeds kMaxRecipeNestingDepth
    };

    RegisterResult Register(uint32_t recipeId, const RecipeEntry& entry) {
        if (entries_.count(recipeId)) return RegisterResult::DuplicateId;
        if (entry.bytecode.empty())   return RegisterResult::EmptyProgram;
        // "0 = engine default" — anything nonzero must be a valid conservative value.
        if (entry.boundRadius != 0.f && !(entry.boundRadius > 0.f))
            return RegisterResult::BadBoundRadius;
        if (entry.stepRelaxation != 0.f && !(entry.stepRelaxation > 0.f && entry.stepRelaxation <= 1.f))
            return RegisterResult::BadStepRelaxation;
        if (entry.gateFootprintThreshold != 0.f && !(entry.gateFootprintThreshold > 0.f))
            return RegisterResult::BadGateFootprintThreshold;
        if (entry.precisionFootprintThreshold != 0.f && !(entry.precisionFootprintThreshold > 0.f))
            return RegisterResult::BadPrecisionFootprintThreshold;

        int sp = 0, psp = 0;
        for (const auto& in : entry.bytecode) {
            if (!IsValidSdfOpCode(in.opCode)) return RegisterResult::BadOpCode;

            // paramMask gate: ReadParam/ReadParamFloat3 (P4) REQUIRE an explicit non-zero marker
            // (convention: paramMask must equal 1, meaning "data[0] is a runtime param-array
            // index"); every other opcode still hard-rejects a nonzero paramMask unchanged.
            const auto op = static_cast<Recipe::SdfOpCode>(in.opCode);
            const bool isParamOp = (op == Recipe::SdfOpCode::ReadParam) ||
                                    (op == Recipe::SdfOpCode::ReadParamFloat3);
            if (isParamOp) {
                if (in.paramMask == 0) return RegisterResult::ParamMaskRequired;
            } else {
                if (in.paramMask != 0) return RegisterResult::ParamMaskUnsupported;
            }

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

        // Recipe-Nested-Invocation M1: InvokeRecipe cycle/depth guard, done at Register()
        // time (never runtime-only) so an unguarded cycle can never reach evalRecipe or the
        // GLSL emitter. Every InvokeRecipe callee referenced by `entry` must already be a
        // registered entry (recipes register in dependency order — a callee must exist before
        // a caller referencing it can be registered), so the ENTIRE transitive callee graph is
        // already present in entries_ except for `entry` itself (about to be inserted as
        // `recipeId`). This walk therefore only needs to look at entries_ plus `entry`.
        {
            RegisterResult guardResult = ValidateNestingGuard(recipeId, entry);
            if (guardResult != RegisterResult::Ok) return guardResult;
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
    // Recipe-Nested-Invocation M1: walks `entry`'s InvokeRecipe callees (recursively, via
    // already-registered entries_) to reject a cycle (direct self-invocation or an indirect
    // cycle through other recipes) or a chain exceeding kMaxRecipeNestingDepth. `path` tracks
    // the current call-chain (by recipeId, starting with the not-yet-inserted `recipeId` being
    // registered) so a repeat anywhere in `path` is detected as a cycle; `depth` counts
    // InvokeRecipe hops from the entry point.
    RegisterResult ValidateNestingGuard(uint32_t recipeId, const RecipeEntry& entry) const {
        return WalkNestingGuard(recipeId, entry, /*depth=*/0, /*path=*/{recipeId});
    }

    RegisterResult WalkNestingGuard(uint32_t /*ownerId*/, const RecipeEntry& entry,
                                     uint32_t depth, std::vector<uint32_t> path) const {
        for (const auto& in : entry.bytecode) {
            if (static_cast<Recipe::SdfOpCode>(in.opCode) != Recipe::SdfOpCode::InvokeRecipe)
                continue;

            const uint32_t calleeId = static_cast<uint32_t>(in.data[0]);

            // Cycle check: callee already appears earlier in the current call chain (covers
            // both direct self-invocation, calleeId == path.front() with an empty chain so
            // far, and an indirect A->B->A-style cycle).
            for (uint32_t seen : path) {
                if (seen == calleeId) return RegisterResult::RecursiveInvocation;
            }

            if (depth + 1 > kMaxRecipeNestingDepth) return RegisterResult::NestingTooDeep;

            const RecipeEntry* callee = Get(calleeId);
            if (!callee) return RegisterResult::UnknownCalleeRecipe;

            std::vector<uint32_t> nextPath = path;
            nextPath.push_back(calleeId);
            RegisterResult sub = WalkNestingGuard(calleeId, *callee, depth + 1, nextPath);
            if (sub != RegisterResult::Ok) return sub;
        }
        return RegisterResult::Ok;
    }

    std::map<uint32_t, RecipeEntry> entries_;
};

} // namespace Vixen::SVO
