#include "Recipe/RecipeComposition.h"

#include "Recipe/RecipeRegistry.h"

namespace Vixen::SVO {

bool IsValidSdfCompositeRole(uint8_t raw) noexcept {
    return raw <= static_cast<uint8_t>(SdfCompositeRole::Emissive);
}

std::optional<ResolvedSdfComposite> RecipeRegistry::ResolveSdfComposite(
    uint32_t recipeId, std::size_t bandIndex) const {
    const RecipeEntry* entry = Get(recipeId);
    if (!entry || bandIndex >= entry->lodLadder.size()) return std::nullopt;

    const LodBand& band = entry->lodLadder[bandIndex];
    ResolvedSdfComposite result;
    result.bandIndex = bandIndex;

    const LodStrategy strategy = static_cast<LodStrategy>(band.strategy);
    if (strategy == LodStrategy::MarchFull) {
        result.fieldRecipeId = recipeId;
        result.fieldEnabled = true;
    } else if (strategy == LodStrategy::MarchVariant) {
        result.fieldRecipeId = band.variantId;
        result.fieldEnabled = true;
    }

    // LodBand::blockMask selects a block-list slot; BlockRef::bandMask selects
    // the LOD bands in which that slot is authored live. Intersecting both masks
    // makes either authoring surface able to disable a block without rewriting
    // the other one.
    if (strategy != LodStrategy::Skip) {
        const uint16_t bandBit = static_cast<uint16_t>(1u << bandIndex);
        for (std::size_t blockIndex = 0; blockIndex < entry->blocks.size(); ++blockIndex) {
            const uint16_t blockBit = static_cast<uint16_t>(1u << blockIndex);
            const BlockRef& block = entry->blocks[blockIndex];
            if ((band.blockMask & blockBit) == 0 || (block.bandMask & bandBit) == 0)
                continue;
            result.activeBlockMask = static_cast<uint16_t>(result.activeBlockMask | blockBit);
            result.blocks.push_back(&block);
        }
    }
    return result;
}

} // namespace Vixen::SVO
