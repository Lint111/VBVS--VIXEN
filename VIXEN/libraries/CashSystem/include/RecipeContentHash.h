#pragma once

#include "CacheKeyHasher.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeServices.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace CashSystem {

// R841.2: the kernel provider owns the one recipe content identity. The digest includes the
// canonical instruction stream and any path rows; recipeId is registration metadata and is not
// part of the identity.
[[nodiscard]] inline std::string ComputeRecipeContentDigest(
    const std::vector<Vixen::SVO::Recipe::SdfInstruction>& bytecode,
    std::span<const Yeroket::Sdf::Kernels::PathInstance> pathRows = {}) {
    return Vixen::SVO::Recipe::RecipeContentDigest(bytecode, pathRows);
}

// TypedCacher retains a compact integer lookup slot, but the family record carries the complete
// provider digest. This fold is an index only; it is never exposed as recipe identity.
[[nodiscard]] inline std::uint64_t ComputeRecipeContentKey(std::string_view digest) {
    CacheKeyHasher hasher;
    hasher.AddBytes(digest.data(), digest.size());
    return hasher.Finalize();
}

struct RecipeFamilyRecord {
    std::string contentDigest;
    std::uint32_t firstRecipeId = 0;
    std::vector<std::uint32_t> memberRecipeIds;
};

} // namespace CashSystem
