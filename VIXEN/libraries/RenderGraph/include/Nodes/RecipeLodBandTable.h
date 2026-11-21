#pragma once

#include "Generated/RecipeLodBand.g.h"
#include "Recipe/LodSelection.h"
#include "Recipe/RecipeRegistry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

namespace Vixen::RenderGraph {

// R860 RL0: rows of the band table per ladder slot (TileBinPrepareSystem.LodBandsPerRecipe;
// LodSelection.h kMaxLodBands), and the row variant that means "the instance's own recipe"
// (TileBinPrepareSystem.SelfRecipe).
inline constexpr uint32_t kLodBandsPerRecipe = 4u;
inline constexpr uint32_t kLodSelfRecipe = 0xFFFFFFFFu;
static_assert(kLodBandsPerRecipe == Vixen::SVO::kMaxLodBands);

// The table the GPU binning pass reads. Recipe ids are sparse (some are four-character codes), so
// rows are indexed by a dense slot: slot 0 is the one infinite band every recipe without shells
// uses, and each recipe whose shells are registered gets the next slot.
struct RecipeLodBandTable {
    std::vector<Vixen::Codegen::Gpu::RecipeLodBand> rows;
    std::unordered_map<uint32_t, uint32_t> slotByRecipe;

    [[nodiscard]] uint32_t SlotOf(uint32_t recipeId) const {
        const auto found = slotByRecipe.find(recipeId);
        return found == slotByRecipe.end() ? 0u : found->second;
    }
};

// One ladder's rows: band b is active while q < maxQ and marches variantRecipeId. Only the variant
// of a MarchVariant band is applied on the GPU (RL0); every other strategy keeps the recipe's own
// field (kLodSelfRecipe). Rows past the ladder's end repeat its last band, so the four-row choice
// equals SelectLodBand on the ladder.
inline void AppendRecipeLodBandRows(std::vector<Vixen::Codegen::Gpu::RecipeLodBand>& rows,
                                    std::span<const Vixen::SVO::LodBand> ladder) {
    using Vixen::SVO::LodStrategy;
    Vixen::Codegen::Gpu::RecipeLodBand last{std::numeric_limits<float>::infinity(), kLodSelfRecipe};
    for (uint32_t band = 0; band < kLodBandsPerRecipe; ++band) {
        if (band < ladder.size()) {
            const Vixen::SVO::LodBand& source = ladder[band];
            last.maxQ = band + 1 == ladder.size() ? std::numeric_limits<float>::infinity() : source.maxQ;
            last.variantRecipeId = static_cast<LodStrategy>(source.strategy) == LodStrategy::MarchVariant
                ? source.variantId : kLodSelfRecipe;
        }
        rows.push_back(last);
    }
}

// R860.7 / R892.28: a recipe's ladder from its certified shells (finest first) and their registered
// keys: band 0 is the recipe itself, band k marches shell k, and band k-1 ends where shell k is
// allowed, q >= certifiedError / (2 R T toleranceScale) (ShellSwitchQ). The coarsest allowed shell
// wins because the thresholds are kept ascending (a coarser shell never switches before a finer
// one; when its own threshold is lower it waits for the finer one's). Shells beyond the keys (a
// refused registration) and beyond the four bands are left out. T <= 0: the recipe alone.
inline std::vector<Vixen::SVO::LodBand> ShellLadder(
    std::span<const Vixen::SVO::RecipeRegistry::RecipeEntry::DerivedShell> shells, float shellBoundRadius,
    std::span<const uint32_t> shellKeys, float tolerancePx) {
    std::vector<Vixen::SVO::LodBand> ladder(1);
    const std::size_t count = std::min({shells.size(), shellKeys.size(),
                                        static_cast<std::size_t>(kLodBandsPerRecipe - 1u)});
    for (std::size_t level = 0; tolerancePx > 0.0f && level < count; ++level) {
        const float switchQ = Vixen::SVO::ShellSwitchQ(shells[level].certifiedError, shellBoundRadius,
                                                       tolerancePx, shells[level].toleranceScale);
        if (!(switchQ < std::numeric_limits<float>::infinity())) break;
        ladder.back().maxQ = std::max(switchQ, level == 0 ? 0.0f
            : std::nextafter(ladder[ladder.size() - 2].maxQ, std::numeric_limits<float>::infinity()));
        Vixen::SVO::LodBand band;
        band.strategy = static_cast<uint8_t>(Vixen::SVO::LodStrategy::MarchVariant);
        band.variantId = shellKeys[level];
        ladder.push_back(band);
    }
    ladder.back().maxQ = std::numeric_limits<float>::infinity();
    return ladder;
}

// The table for every registered recipe with registered shells; T is the preset's shell tolerance.
// The exact preset (T = 0) and every recipe without shells leave all instances on slot 0.
inline RecipeLodBandTable BuildRecipeLodBandTable(const Vixen::SVO::RecipeRegistry& registry,
                                                  float shellTolerancePx) {
    RecipeLodBandTable table;
    AppendRecipeLodBandRows(table.rows, {});
    if (!(shellTolerancePx > 0.0f)) return table;
    for (const uint32_t id : registry.Ids()) {
        const auto* entry = registry.Get(id);
        const auto* keys = registry.ShellVariants(id);
        if (entry == nullptr || keys == nullptr || keys->empty()) continue;
        const std::vector<Vixen::SVO::LodBand> ladder =
            ShellLadder(entry->derivedShells, entry->derivedShellBoundRadius, *keys, shellTolerancePx);
        if (ladder.size() < 2) continue;
        table.slotByRecipe.emplace(id, static_cast<uint32_t>(table.rows.size() / kLodBandsPerRecipe));
        AppendRecipeLodBandRows(table.rows, ladder);
    }
    return table;
}

} // namespace Vixen::RenderGraph
