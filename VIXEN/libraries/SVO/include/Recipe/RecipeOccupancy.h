#pragma once
/**
 * @file RecipeOccupancy.h
 * @brief Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13 — per-recipe coarse occupancy grid.
 *
 * A virtual (never-baked) recipe body has no octree, so the shader's uber-recipe march
 * (traceUberRecipeBody, SdfRecipes.glsl) has nothing to empty-space-skip against except the
 * bound-sphere entry test (Task 12). This header derives, ONCE per recipe registration, a
 * small dense-then-downsampled grid of conservative min-|distance| values covering the
 * recipe's bound-sphere AABB, so the shader can skip cells the field provably never crosses
 * zero in, and shade far-away hits flat once the whole bound sphere sub-resolves a cell.
 *
 * ---------------------------------------------------------------------------------------
 * CONSERVATIVENESS PROOF (why this grid never causes a false empty-space skip)
 * ---------------------------------------------------------------------------------------
 * Step 1 — dense evaluation: evalRecipe (the same CPU stack-VM the bake path and the M4
 * numerical-parity gate both trust) is sampled at every one of denseN^3 grid-cell CENTERS
 * covering the AABB [boundCenter - boundRadius, boundCenter + boundRadius]. This is exactly
 * the bake-resolution default (64^3, DENSE_N below) — this grid's conservativeness is
 * relative to that sampling resolution, no stronger, mirroring the guarantee today's bake
 * occupancy pass already has (same finite-sample-grid caveat, not a novel weakness).
 *
 * Step 2 — coarse downsample: the coarse grid (COARSE_N^3, COARSE_N < DENSE_N) partitions
 * the same AABB into larger cells; each coarse cell's value is the MIN |sd| over every dense
 * sample whose center falls inside it.
 *
 * Step 3 — Lipschitz margin: a raw min-of-samples is not yet a sound "the field never
 * crosses zero inside this coarse cell" bound, because the true surface could pass between
 * two sampled dense-cell centers without either sample recording a value near zero. The
 * fix: subtract a margin equal to HALF THE DENSE CELL'S DIAGONAL (dense cell size *
 * sqrt(3)/2) from the raw min before storing. This is sound whenever the field is
 * 1-Lipschitz in the sampled region (an exact SDF's defining property: |f(a)-f(b)| <=
 * |a-b|) — the true minimum |sd| anywhere within a dense cell can differ from the value at
 * its center by at most the distance from that center to the cell's farthest corner, i.e.
 * half the cell diagonal. A CSG union/intersect of 1-Lipschitz fields (Union=min, its
 * partners) stays 1-Lipschitz; Round/Onion (inflate by a constant) also preserve it. This
 * mirrors DeriveConservativeBounds' (RecipeBounds.h) own whitelist reasoning almost exactly
 * — same set of ops trusted, same "domain warps break the guarantee" caveat — but the two
 * whitelists are DELIBERATELY NOT unified into one symbol: RecipeBounds' whitelist governs
 * an emit-time (compile-time-shaped) bound-stack simulation with per-opcode closed-form
 * radius arithmetic, while this grid is a numeric dense-sample-and-downsample pass with no
 * per-opcode arithmetic at all — the ONLY property this file needs from the opcode set is
 * "the composed field stays 1-Lipschitz," which happens to hold for the identical set
 * RecipeBounds already vets for a different reason. If a future opcode is 1-Lipschitz but
 * fails RecipeBounds' bound-arithmetic (or vice versa), the two whitelists could legitimately
 * diverge — keeping them separate avoids conflating two different soundness arguments.
 * A program using any non-whitelisted opcode (domain warp, RepeatInfinite, etc.) skips grid
 * derivation entirely (ok=false) and the caller must treat the instance as ungapped (no
 * empty-space skip) rather than risk an unsound grid that silently clips real geometry.
 * ---------------------------------------------------------------------------------------
 *
 * Consumers:
 *   - BodyOctreeSceneNode concatenates every registered recipe's coarse grid into ONE SSBO
 *     (binding 16), each recipe's slice located by a per-recipe (gridOffset, gridDims) pair
 *     spliced as GLSL constants into getRecipeOccupancyGrid(...) — same "baked as literals,
 *     no runtime metadata SSBO" convention Task 11's getRecipeBoundSphere already uses for
 *     boundCenter/boundRadius/relaxation (registration already forces a shader recompile).
 *   - The shader (SdfRecipes.glsl) samples the grid during traceUberRecipeBody's march to
 *     skip a coarse cell whose stored (already-margined) value exceeds the cell's own
 *     diagonal-based worst-case remaining step, and to shade flat once the WHOLE bound
 *     sphere sub-resolves to under one screen pixel (the far early-out) — see M6 plan Task
 *     13's "far color fidelity is out of scope" note; flat shade is intentionally coarse.
 */
#include "Recipe/RecipeRegistry.h"  // IsValidSdfOpCode
#include "Recipe/SdfRecipeEval.h"   // evalRecipe
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <vector>

namespace Vixen::SVO::Recipe {

// Same 1-Lipschitz-under-composition whitelist DeriveConservativeBounds (RecipeBounds.h)
// vets, for the reason explained in this file's header comment: this is a SEPARATE symbol
// deliberately, not a shared one — the two guarantees are independent even though the
// opcode set happens to coincide today.
inline bool IsLipschitzSafeForOccupancyGrid(const SdfInstruction* prog, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (!IsValidSdfOpCode(prog[i].opCode)) return false;
        switch (static_cast<SdfOpCode>(prog[i].opCode)) {
            case SdfOpCode::Sphere: case SdfOpCode::Box: case SdfOpCode::BoxRounded:
            case SdfOpCode::Capsule: case SdfOpCode::Cylinder: case SdfOpCode::Torus:
            case SdfOpCode::Ellipsoid: case SdfOpCode::HollowCylinder:
            case SdfOpCode::TaperedCylinder: case SdfOpCode::Cone: case SdfOpCode::CappedTorus:
            case SdfOpCode::Link: case SdfOpCode::Panel: case SdfOpCode::Plank:
            case SdfOpCode::RoundedBox: case SdfOpCode::RoundCone: case SdfOpCode::FakeRoundCone:
            case SdfOpCode::Segment: case SdfOpCode::TriangularPrism: case SdfOpCode::Pyramid:
            case SdfOpCode::HexPrism:
            case SdfOpCode::Union: case SdfOpCode::SmoothUnion:
            case SdfOpCode::Subtract: case SdfOpCode::SmoothSubtract:
            case SdfOpCode::Intersect: case SdfOpCode::SmoothIntersect:
            case SdfOpCode::Xor: case SdfOpCode::SmoothMax:
            case SdfOpCode::SmoothUnionCubic: case SdfOpCode::SmoothSubtractCubic:
            case SdfOpCode::SmoothIntersectCubic:
            case SdfOpCode::Round: case SdfOpCode::Onion:
                continue;
            // Note: Plane is Lipschitz-safe (it's the |dot(p,n)-d| distance-to-plane form)
            // but RecipeBounds excludes it only because it's UNBOUNDED (no finite bound
            // sphere) — that exclusion is about bound-radius arithmetic, not Lipschitz
            // safety, and is moot here since a program reaching this function already has
            // a finite boundRadius from a DIFFERENT source (authored or engine-default) by
            // construction (see DeriveOccupancyGrid's caller contract below). Kept OUT of
            // this whitelist anyway for the simpler, conservative reason: it isn't in
            // IsValidSdfOpCode's caller-vetted whitelist set here without re-deriving its
            // own bound arithmetic, and doing so buys nothing (a Plane recipe already has
            // no derivable bound center/radius, so DeriveOccupancyGrid never gets a finite
            // AABB to grid in the first place).
            default:
                return false;
        }
    }
    return true;
}

struct OccupancyGridResult {
    bool ok = false;              // false: non-whitelisted opcode, or count==0 — caller must skip gridding
    uint32_t dim = 0;              // grid is dim^3 cells (COARSE_N)
    glm::vec3 aabbMin = glm::vec3(0.0f);  // world-space min corner (== boundCenter - boundRadius)
    float cellSize = 0.0f;         // world-space cell edge length == (2*boundRadius)/dim
    std::vector<float> values;     // dim^3 conservative min-|sd| values, x-fastest (matches shader indexing)
};

// DeriveOccupancyGrid — dense-evaluate + conservatively downsample, per this file's proof.
// boundCenter/boundRadius MUST already be a valid finite bound (from RecipeBounds.h's
// DeriveConservativeBounds or an authored value) — this function does not derive its own
// bound, it only grids the AABB the caller already trusts.
inline OccupancyGridResult DeriveOccupancyGrid(
    const SdfInstruction* prog, uint32_t count,
    const glm::vec3& boundCenter, float boundRadius,
    uint32_t denseN = 64, uint32_t coarseN = 16)
{
    OccupancyGridResult out;
    if (count == 0 || boundRadius <= 0.0f) return out;
    if (!IsLipschitzSafeForOccupancyGrid(prog, count)) return out;
    if (coarseN == 0 || denseN < coarseN) return out;  // degenerate config — refuse rather than guess

    const float aabbSize   = 2.0f * boundRadius;
    const glm::vec3 aabbMin = boundCenter - glm::vec3(boundRadius);
    const float denseCell   = aabbSize / static_cast<float>(denseN);
    const float coarseCell  = aabbSize / static_cast<float>(coarseN);
    // Half the DENSE cell's diagonal — the conservativeness margin from Step 3 of the
    // header proof. sqrt(3) because the dense grid is 3D (worst case: true surface point
    // sits at a dense cell's far corner relative to the sampled center).
    const float margin = denseCell * 0.8660254f;  // sqrt(3)/2

    std::vector<float> coarseMin(static_cast<size_t>(coarseN) * coarseN * coarseN,
                                  std::numeric_limits<float>::infinity());

    // Dense pass: sample every fine-cell CENTER, fold each sample into its owning coarse
    // cell via a MIN reduction (Step 2). denseN is chosen so each coarse cell receives
    // (denseN/coarseN)^3 dense samples when denseN is an exact multiple — not required to
    // be exact (the coarse-index computation below clamps), but the default 64/16 is.
    for (uint32_t dz = 0; dz < denseN; ++dz) {
        const float wz = aabbMin.z + (static_cast<float>(dz) + 0.5f) * denseCell;
        for (uint32_t dy = 0; dy < denseN; ++dy) {
            const float wy = aabbMin.y + (static_cast<float>(dy) + 0.5f) * denseCell;
            for (uint32_t dx = 0; dx < denseN; ++dx) {
                const float wx = aabbMin.x + (static_cast<float>(dx) + 0.5f) * denseCell;
                const float sd = std::fabs(evalRecipe(prog, count, glm::vec3(wx, wy, wz)));

                uint32_t cx = static_cast<uint32_t>((wx - aabbMin.x) / coarseCell);
                uint32_t cy = static_cast<uint32_t>((wy - aabbMin.y) / coarseCell);
                uint32_t cz = static_cast<uint32_t>((wz - aabbMin.z) / coarseCell);
                cx = glm::min(cx, coarseN - 1); cy = glm::min(cy, coarseN - 1); cz = glm::min(cz, coarseN - 1);

                const size_t idx = (static_cast<size_t>(cz) * coarseN + cy) * coarseN + cx;
                coarseMin[idx] = glm::min(coarseMin[idx], sd);
            }
        }
    }

    out.ok        = true;
    out.dim       = coarseN;
    out.aabbMin   = aabbMin;
    out.cellSize  = coarseCell;
    out.values.resize(coarseMin.size());
    for (size_t i = 0; i < coarseMin.size(); ++i) {
        // A cell no dense sample ever landed in (shouldn't happen for denseN>=coarseN,
        // defensive only) stays +inf pre-margin; clamp the margined result at 0 so a
        // degenerate near-zero raw min never goes negative (a negative stored value would
        // read as "definitely inside," which this grid never claims — it only ever
        // claims "definitely at least THIS far from the surface").
        out.values[i] = glm::max(0.0f, coarseMin[i] - margin);
    }
    return out;
}

} // namespace Vixen::SVO::Recipe
