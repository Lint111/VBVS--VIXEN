#pragma once

#include "CacheKeyHasher.h"
#include "Recipe/RecipeRegistry.h"

#include <cstdint>

namespace CashSystem {

/**
 * @brief Content-hash a recipe's PRE-EMIT bytecode for exact-duplicate detection.
 *
 * Hashes `Vixen::SVO::RecipeRegistry::RecipeEntry::bytecode` — NOT the post-emit GLSL/SPIR-V.
 * `SdfInstruction` is a fixed 132-byte POD (RecipeContainer.g.h), so the whole vector is fed to
 * `CacheKeyHasher` as one raw-byte span; this is deterministic and requires no glslang/GPU step.
 *
 * Equivalence caveat (verified, not assumed): `EmitProceduralFieldFunctionGlsl` bakes `recipeId`
 * into the emitted function name (`sdfRecipe_<recipeId>`), so identical bytecode under two
 * different recipeIds emits two textually-different GLSL strings. That does NOT break this
 * hash's purpose — the hash identifies "same bytecode logic" (the thing a later increment can
 * share one specialized pipeline over), not "byte-identical emitted GLSL". `recipeId` is
 * therefore deliberately excluded from the hash input: it is per-registration identity, not part
 * of the bytecode content.
 *
 * Deliberate correctness property (tested in the M2 suite): the hash is computed ONLY from
 * `RecipeEntry::bytecode` bytes. `ReadParam`/`ReadParamFloat3` slot indices live in `data[0]` of
 * their instruction (not in `paramMask`, which is only a validity gate) — they are part of the
 * bytecode and correctly distinguish "reads params[0]" from "reads params[1]". Runtime
 * `recipeParams[]` VALUES live in `BodyInstanceGpu`, a separate per-instance structure that
 * `RecipeEntry` never references — there is nothing runtime-valued for this hash to accidentally
 * pick up.
 */
[[nodiscard]] inline uint64_t ComputeRecipeBytecodeHash(
    const std::vector<Vixen::SVO::Recipe::SdfInstruction>& bytecode) {
    CacheKeyHasher hasher;
    hasher.AddBytes(bytecode.data(), bytecode.size() * sizeof(Vixen::SVO::Recipe::SdfInstruction));
    return hasher.Finalize();
}

/**
 * @brief Cache-entry shape for Increment 1 of the recipe pipeline cache.
 *
 * Deliberately NOT `PipelineWrapper`-shaped: no per-recipe `VkPipeline` exists yet (that's
 * Increment 2's job — see Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07.md §7). This
 * increment's cache answers one question only: "which registered recipeIds share identical
 * bytecode content?" — a lightweight identity/membership record, populated at registration time
 * and queried by nothing downstream yet (Increment 2 is the first consumer).
 */
struct RecipeFamilyRecord {
    uint64_t contentHash = 0;
    uint32_t firstRecipeId = 0;
    std::vector<uint32_t> memberRecipeIds;
};

} // namespace CashSystem
