#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include "Recipe/SdfInstruction.h"
#include "Recipe/RecipeRegistry.h"  // IsValidSdfOpCode

namespace Vixen::SVO::Recipe {

// DeriveConservativeBounds — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 10.
//
// Computes a conservative world-space bounding sphere (center + radius) for a recipe
// program, WITHOUT running the interval-arithmetic VM design §8.1 defers to as the real
// upgrade path. This is a cheap emit-time simulation over a strict whitelist of opcodes
// whose extent arithmetic is genuinely conservative under composition:
//   - leaf primitives (their own local extent + their data[4..6] position offset, where
//     the opcode has one — see SdfRecipeEval.h's own per-opcode data-layout comments)
//   - CSG combines (Union/SmoothUnion/Subtract/SmoothSubtract/Intersect/SmoothIntersect/
//     Xor/SmoothMax/the three Cubic variants): the combined bound is the union of the two
//     operand bounds (a conservative EXPANSION of the true result's bound in every case,
//     including Subtract/Intersect — this never CLIPS geometry, only over-estimates)
//   - Round/Onion: inflate the top-of-stack bound's radius by the opcode's rounding/
//     thickness parameter (data[0])
//
// Any program containing an opcode OUTSIDE this whitelist — Twist/Bend, RepeatInfinite/
// RepeatLimited, Displacement, Transform (has a scale term), Elongate/Revolution/MirrorX/Y/Z,
// any MathX operating on a position/value, PositionChannel, DistanceTo, or any Float3-lane
// op — returns ok=false: extent arithmetic is NOT conservative under domain warps (a
// RepeatInfinite'd sphere has unbounded true extent; a Twist/Bend can move a point
// arbitrarily far from where a naive leaf-bound would place it), and a wrong bound here
// would silently CLIP geometry that only M6's occupancy-grid gate would ever catch. The
// caller MUST fall back to an authored bound or the engine default in that case — this
// helper never guesses past the whitelist.
struct RecipeBoundsResult {
    bool      ok     = false;   // false: program uses a non-whitelisted opcode; caller must fall back
    glm::vec3 center = glm::vec3(0.0f);
    float     radius = 0.0f;
};

inline RecipeBoundsResult DeriveConservativeBounds(const SdfInstruction* prog, uint32_t count) {
    // Emit-time bound stack, one (center, radius) sphere per pushed value — mirrors the
    // value-stack VM's shape (SdfRecipeEval.h) but carries a bound instead of a distance.
    struct Bound { glm::vec3 center; float radius; };
    Bound stk[64];
    int sp = 0;

    auto pushLeaf = [&](const glm::vec3& localCenter, float localRadius) {
        stk[sp].center = localCenter;
        stk[sp].radius = localRadius;
        ++sp;
    };

    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        if (!IsValidSdfOpCode(in.opCode)) return {};
        switch (static_cast<SdfOpCode>(in.opCode)) {
            // --- Leaf primitives, no position offset (sample point used directly) ---
            case SdfOpCode::Sphere:
                // data[0..2]=center, data[3]=radius
                pushLeaf(glm::vec3(in.data[0], in.data[1], in.data[2]), in.data[3]);
                break;
            case SdfOpCode::Box:
                // data[0..2]=halfExtents about local origin
                pushLeaf(glm::vec3(0.0f), glm::length(glm::vec3(in.data[0], in.data[1], in.data[2])));
                break;
            case SdfOpCode::BoxRounded:
                // data[0..2]=halfExtents, data[3]=rounding
                pushLeaf(glm::vec3(0.0f), glm::length(glm::vec3(in.data[0], in.data[1], in.data[2])) + in.data[3]);
                break;
            case SdfOpCode::Capsule:
                // data[0]=halfHeight, data[1]=radius, axis-aligned about origin
                pushLeaf(glm::vec3(0.0f), in.data[0] + in.data[1]);
                break;
            case SdfOpCode::Cylinder:
                // data[0]=halfHeight, data[1]=radius
                pushLeaf(glm::vec3(0.0f), glm::length(glm::vec2(in.data[0], in.data[1])));
                break;
            case SdfOpCode::Torus:
                // data[0]=majorRadius, data[1]=minorRadius
                pushLeaf(glm::vec3(0.0f), in.data[0] + in.data[1]);
                break;
            case SdfOpCode::Plane:
                // Unbounded primitive — no finite conservative sphere exists.
                return {};

            // --- Leaf primitives, position-offset (data[4..6] = local center) ---
            case SdfOpCode::Ellipsoid:
                // data[0..2]=radii, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec3(in.data[0], in.data[1], in.data[2])));
                break;
            case SdfOpCode::HollowCylinder:
                // data[0]=halfLen, data[1]=outerR, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec2(in.data[0], in.data[1])));
                break;
            case SdfOpCode::TaperedCylinder:
                // data[0]=halfH, data[1]=baseR, data[2]=topR, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec2(in.data[0], glm::max(in.data[1], in.data[2]))));
                break;
            case SdfOpCode::Cone:
                // data[2]=height, data[4..6]=position — cone opens from apex; height bounds it
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]), in.data[2]);
                break;
            case SdfOpCode::CappedTorus:
                // data[2]=majorR, data[3]=minorR, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]), in.data[2] + in.data[3]);
                break;
            case SdfOpCode::Link:
                // data[0]=halfLen, data[1]=majorR, data[2]=minorR, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         in.data[0] + in.data[1] + in.data[2]);
                break;
            case SdfOpCode::Panel: case SdfOpCode::Plank: case SdfOpCode::RoundedBox:
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec3(in.data[0], in.data[1], in.data[2])) + in.data[3]);
                break;
            case SdfOpCode::RoundCone: case SdfOpCode::FakeRoundCone:
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         in.data[2] + glm::max(in.data[0], in.data[1]));
                break;
            case SdfOpCode::Segment: {
                // data[0..2]=pointA, data[3]=radius, data[4..6]=pointB — no offset (world-space endpoints)
                glm::vec3 a(in.data[0], in.data[1], in.data[2]);
                glm::vec3 b(in.data[4], in.data[5], in.data[6]);
                glm::vec3 mid = (a + b) * 0.5f;
                pushLeaf(mid, glm::length(a - mid) + in.data[3]);
                break;
            }
            case SdfOpCode::TriangularPrism:
                // data[0]=h.x, data[1]=h.y, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec2(in.data[0], in.data[1])));
                break;
            case SdfOpCode::Pyramid:
                // data[0]=height, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]), in.data[0]);
                break;
            case SdfOpCode::HexPrism:
                // data[0]=hex radius, data[1]=halfHeight, data[4..6]=position
                pushLeaf(glm::vec3(in.data[4], in.data[5], in.data[6]),
                         glm::length(glm::vec2(in.data[0], in.data[1])));
                break;

            // --- CSG combines: bound = union of the two operand bounds (conservative
            // expansion for every one of these ops, including Subtract/Intersect — the
            // TRUE result is always a subset of A's own extent, and A's own extent is
            // itself already inside the union bound below). ---
            case SdfOpCode::Union: case SdfOpCode::SmoothUnion:
            case SdfOpCode::Subtract: case SdfOpCode::SmoothSubtract:
            case SdfOpCode::Intersect: case SdfOpCode::SmoothIntersect:
            case SdfOpCode::Xor: case SdfOpCode::SmoothMax:
            case SdfOpCode::SmoothUnionCubic: case SdfOpCode::SmoothSubtractCubic:
            case SdfOpCode::SmoothIntersectCubic: {
                if (sp < 2) return {};
                Bound b = stk[--sp];
                Bound a = stk[--sp];
                // Bounding sphere of the union of two spheres.
                glm::vec3 d = b.center - a.center;
                float dist = glm::length(d);
                if (dist + b.radius <= a.radius) { stk[sp++] = a; break; }        // b fully inside a
                if (dist + a.radius <= b.radius) { stk[sp++] = b; break; }        // a fully inside b
                float newRadius = (a.radius + b.radius + dist) * 0.5f;
                glm::vec3 newCenter = (dist > 1e-8f)
                    ? a.center + d * ((newRadius - a.radius) / dist)
                    : a.center;
                stk[sp++] = Bound{newCenter, newRadius};
                break;
            }

            // --- Unary inflation: TOS radius grows by data[0] ---
            case SdfOpCode::Round: case SdfOpCode::Onion: {
                if (sp < 1) return {};
                stk[sp - 1].radius += in.data[0];
                break;
            }

            // Anything else (Twist, Bend, RepeatInfinite/Limited, Displacement, Transform,
            // Elongate, Revolution, MirrorX/Y/Z, PositionChannel, DistanceTo, MathX, Float3
            // lane, VM-control) is outside the whitelist — extent arithmetic under these is
            // not conservative (domain warps can relocate a sample point arbitrarily), so
            // fall back rather than risk a bound that CLIPS real geometry.
            default:
                return {};
        }
    }

    if (sp != 1) return {};  // program didn't reduce to a single field (shouldn't happen
                              // for a Register()-validated program, but stay defensive)
    RecipeBoundsResult out;
    out.ok     = true;
    out.center = stk[0].center;
    out.radius = stk[0].radius;
    return out;
}

// How RecipeEntry's boundCenter/boundRadius/stepRelaxation ended up populated — the
// registration-time log line Task 10 requires is the caller's responsibility (this header
// has no Logger access), but the caller needs to know WHICH of the three sources fired to
// write a meaningful line. Kept as an explicit enum rather than a bool so a future 4th
// source (e.g. a pack-authored bound) doesn't silently collapse into "derived".
enum class RecipeBoundsSource {
    Authored,        // entry already had a nonzero boundRadius/stepRelaxation — untouched
    Derived,         // DeriveConservativeBounds succeeded (whitelist-only program)
    EngineDefault,   // program used a non-whitelisted opcode — fell back to defaults
};

struct ApplyBoundsResult {
    RecipeBoundsSource boundSource;
    RecipeBoundsSource relaxationSource;
};

// ApplyRecipeBoundsDefaults — fills in entry.boundCenter/boundRadius/stepRelaxation IN
// PLACE for any field the caller left at its "0 = engine default" sentinel, preferring
// DeriveConservativeBounds's whitelist-restricted result when it succeeds and otherwise
// falling back to (defaultRadius, defaultRelaxation). Does NOT touch a field the caller
// already authored (nonzero) — authored values always win over derivation.
inline ApplyBoundsResult ApplyRecipeBoundsDefaults(
    RecipeRegistry::RecipeEntry& entry,
    float defaultBoundRadius,      // e.g. the kResidencyBoundingRadius-style engine default
    float defaultStepRelaxation)   // must itself be in (0,1]
{
    ApplyBoundsResult result{};

    const bool authoredBound = entry.boundRadius != 0.f;
    result.boundSource = authoredBound ? RecipeBoundsSource::Authored : RecipeBoundsSource::EngineDefault;

    if (!authoredBound) {
        RecipeBoundsResult derived =
            DeriveConservativeBounds(entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()));
        if (derived.ok) {
            entry.boundCenter = derived.center;
            entry.boundRadius = derived.radius;
            result.boundSource = RecipeBoundsSource::Derived;
        } else {
            entry.boundRadius = defaultBoundRadius;
            // boundCenter stays at its RecipeEntry default (origin) — the whitelist bailed,
            // so no center estimate is trustworthy either; engine-default radius about the
            // instance's own placement is the caller's existing (pre-M5) behavior.
        }
    }

    const bool authoredRelaxation = entry.stepRelaxation != 0.f;
    result.relaxationSource = authoredRelaxation ? RecipeBoundsSource::Authored : RecipeBoundsSource::EngineDefault;
    if (!authoredRelaxation) {
        entry.stepRelaxation = defaultStepRelaxation;
    }

    return result;
}

} // namespace Vixen::SVO::Recipe
