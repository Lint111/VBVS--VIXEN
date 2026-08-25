#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Vixen::SVO {

// These numeric values are part of the recipe composition contract and must remain stable.
enum class SdfCompositeRole : uint8_t {
    Field = 0,
    Interval = 1,
    SurfaceMod = 2,
    Emissive = 3,
};

constexpr std::size_t kMaxCompositionBlocks = 16;

[[nodiscard]] bool IsValidSdfCompositeRole(uint8_t raw) noexcept;

// A composition block is another registered recipe plus the role under which it is
// evaluated. `bandMask` is independent of LodBand::blockMask: a block is live only
// when both masks include the active band/block bit respectively.
struct BlockRef {
    uint8_t role = static_cast<uint8_t>(SdfCompositeRole::Field);
    uint32_t recipeId = 0;
    uint16_t bandMask = 0xFFFFu;
    float shellRadius = 0.0f;
};

// The registry returns ids rather than copying RecipeEntry values. This keeps resolution
// cheap and makes selected field/block recipes observe later baker slot updates through
// the registry's normal Get()/GetMutable() path.
struct ResolvedSdfComposite {
    uint32_t fieldRecipeId = 0;
    bool fieldEnabled = false;
    std::size_t bandIndex = 0;
    uint16_t activeBlockMask = 0;
    std::vector<const BlockRef*> blocks;

    [[nodiscard]] bool HasField() const noexcept { return fieldEnabled; }
};

} // namespace Vixen::SVO
