// R860 RL0 witness 4: the band the generated tile prepare pass writes per instance (mirrored by
// MirrorInstanceBand over the RecipeLodBand rows, through the same generated callable) equals
// SVO's SelectLodBand on the recipe's ladder, for random ladders and q, and SelectLodBand still
// answers what the hand-written ladder walk answered before it delegated to the callable.

#include <gtest/gtest.h>

#include "Nodes/RecipeLodBandTable.h"
#include "Recipe/LodSelection.h"
#include "TileBinningMirror.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace {

using Vixen::Codegen::Gpu::InstanceBound;
using Vixen::Codegen::Gpu::RecipeLodBand;
using Vixen::Codegen::Gpu::TileBinView;
using Vixen::RenderGraph::AppendRecipeLodBandRows;
using Vixen::RenderGraph::Mirror::MirrorInstanceBand;
using Vixen::RenderGraph::Mirror::MirrorLodQ;
using Vixen::SVO::LodBand;
using Vixen::SVO::LodStrategy;

constexpr float kInf = std::numeric_limits<float>::infinity();

// SelectLodBand as written before RL0 (the ladder walk), the reference for the delegation.
std::size_t LadderWalk(const std::vector<LodBand>& ladder, float q) {
    if (ladder.empty()) return 0;
    const float safeQ = std::isnan(q) ? 0.0f : std::max(0.0f, q);
    for (std::size_t i = 0; i < ladder.size(); ++i)
        if (safeQ < ladder[i].maxQ) return i;
    return ladder.size() - 1;
}

std::vector<LodBand> RandomLadder(std::mt19937& rng, uint32_t recipeId) {
    std::uniform_int_distribution<int> bandCount(1, 4);
    std::uniform_real_distribution<float> step(0.001f, 0.5f);
    std::uniform_int_distribution<int> strategy(0, 5);
    const int count = bandCount(rng);
    std::vector<LodBand> ladder(static_cast<std::size_t>(count));
    float threshold = 0.0f;
    for (int i = 0; i < count; ++i) {
        threshold += step(rng);
        ladder[i].maxQ = i + 1 == count ? kInf : threshold;
        ladder[i].strategy = static_cast<uint8_t>(strategy(rng));
        ladder[i].variantId = recipeId + 100u + static_cast<uint32_t>(i);
    }
    EXPECT_EQ(Vixen::SVO::ValidateLodLadder(ladder), Vixen::SVO::LodValidationError::None);
    return ladder;
}

std::vector<float> ProbeQs(std::mt19937& rng, const std::vector<LodBand>& ladder) {
    std::vector<float> qs{0.0f, -1.0f, -0.0f, std::numeric_limits<float>::quiet_NaN(), kInf, 1e30f};
    for (const LodBand& band : ladder) {
        if (std::isinf(band.maxQ)) continue;
        qs.push_back(band.maxQ);  // exactly on the switch: the coarser band is active
        qs.push_back(std::nextafter(band.maxQ, 0.0f));
        qs.push_back(std::nextafter(band.maxQ, kInf));
    }
    std::uniform_real_distribution<float> any(0.0f, 2.5f);
    for (int i = 0; i < 64; ++i) qs.push_back(any(rng));
    return qs;
}

TEST(LodBandMirror, PrepareBandEqualsSelectLodBandOnRandomLadders) {
    std::mt19937 rng(860u);
    for (uint32_t recipeId = 0; recipeId < 400u; ++recipeId) {
        const std::vector<LodBand> ladder = RandomLadder(rng, recipeId);
        // Slot 0 is the shared one-band ladder; this recipe's ladder takes a later slot.
        std::vector<RecipeLodBand> table;
        AppendRecipeLodBandRows(table, {});
        const uint32_t slot = 1u + recipeId % 3u;
        for (uint32_t filler = 1; filler < slot; ++filler) AppendRecipeLodBandRows(table, RandomLadder(rng, 9u));
        AppendRecipeLodBandRows(table, ladder);
        ASSERT_EQ(table.size(), (slot + 1u) * Vixen::RenderGraph::kLodBandsPerRecipe);
        for (const float q : ProbeQs(rng, ladder)) {
            const std::size_t expected = LadderWalk(ladder, q);
            ASSERT_EQ(Vixen::SVO::SelectLodBand(ladder, q), expected) << "recipe " << recipeId << " q " << q;
            ASSERT_EQ(MirrorInstanceBand(q, recipeId, 0u, table).variantRecipeId, recipeId);
            const auto chosen = MirrorInstanceBand(q, recipeId, slot, table);
            ASSERT_EQ(chosen.band, expected) << "recipe " << recipeId << " q " << q;
            const LodBand& band = ladder[expected];
            const uint32_t field = static_cast<LodStrategy>(band.strategy) == LodStrategy::MarchVariant
                ? band.variantId : recipeId;
            ASSERT_EQ(chosen.variantRecipeId, field) << "recipe " << recipeId << " q " << q;
        }
    }
}

TEST(LodBandMirror, ExactPresetAndImplicitLaddersMarchTheRecipeItself) {
    // A one-band ladder (every recipe on the exact preset today) and an implicit gate/precision
    // ladder (Skip, Half: CPU-side strategies) never change the recipe the GPU evaluates.
    std::vector<LodBand> gated(3);
    gated[0].maxQ = 0.25f;
    gated[1].maxQ = 0.5f;
    gated[1].paramTier = static_cast<uint8_t>(Vixen::SVO::LodParamTier::Half);
    gated[2].maxQ = kInf;
    gated[2].strategy = static_cast<uint8_t>(LodStrategy::Skip);
    gated[2].uploadSet = 0;
    for (const auto& ladder : {std::vector<LodBand>{LodBand{kInf}}, gated}) {
        std::vector<RecipeLodBand> table;
        AppendRecipeLodBandRows(table, {});
        AppendRecipeLodBandRows(table, ladder);
        for (const float q : {0.0f, 0.3f, 0.6f, 100.0f}) {
            EXPECT_EQ(MirrorInstanceBand(q, 0x524f4f4du, 1u, table).variantRecipeId, 0x524f4f4du) << q;
            EXPECT_EQ(MirrorInstanceBand(q, 7u, 0u, table).variantRecipeId, 7u) << q;
        }
    }
}

TEST(LodBandMirror, ShellLadderFollowsCertifiedErrorToleranceAndScale) {
    // R860.7 / R892.28: shell k is allowed once q >= e(k) / (2 R T s(k)), rounded up; band k-1 ends
    // there; the coarsest allowed shell wins; T = 0 (the exact preset) is the recipe alone.
    using DerivedShell = Vixen::SVO::RecipeRegistry::RecipeEntry::DerivedShell;
    std::vector<DerivedShell> shells(2);
    shells[0].certifiedError = 0.03f;
    shells[1].certifiedError = 0.2f;
    shells[1].toleranceScale = 0.4f;  // a shell that dropped a text row (R892.28)
    const float radius = 1.05f;
    const std::vector<uint32_t> keys{0xFF000000u, 0xFF000001u};
    for (const float tolerance : {0.125f, 0.5f, 1.0f}) {
        const auto ladder = Vixen::RenderGraph::ShellLadder(shells, radius, keys, tolerance);
        ASSERT_EQ(ladder.size(), 3u);
        EXPECT_EQ(Vixen::SVO::ValidateLodLadder(ladder), Vixen::SVO::LodValidationError::None);
        // The exact thresholds of the binary32 inputs, in double (ShellSwitchQ rounds these up).
        const double first = static_cast<double>(0.03f) / (2.0 * static_cast<double>(radius) * tolerance);
        const double second = static_cast<double>(0.2f) /
            (2.0 * static_cast<double>(radius) * tolerance * static_cast<double>(0.4f));
        EXPECT_GE(static_cast<double>(ladder[0].maxQ), first);
        EXPECT_LT(static_cast<double>(std::nextafter(ladder[0].maxQ, 0.0f)), first);
        EXPECT_GE(static_cast<double>(ladder[1].maxQ), second);
        EXPECT_EQ(ladder[1].variantId, keys[0]);
        EXPECT_EQ(ladder[2].variantId, keys[1]);
        EXPECT_TRUE(std::isinf(ladder[2].maxQ));
        // The table rows choose the same band as the ladder, through the prepare mirror.
        std::vector<RecipeLodBand> table;
        AppendRecipeLodBandRows(table, {});
        AppendRecipeLodBandRows(table, ladder);
        EXPECT_EQ(MirrorInstanceBand(ladder[0].maxQ * 0.999f, 7u, 1u, table).variantRecipeId, 7u);
        EXPECT_EQ(MirrorInstanceBand(ladder[0].maxQ, 7u, 1u, table).variantRecipeId, keys[0]);
        EXPECT_EQ(MirrorInstanceBand(ladder[1].maxQ, 7u, 1u, table).variantRecipeId, keys[1]);
    }
    EXPECT_EQ(Vixen::RenderGraph::ShellLadder(shells, radius, keys, 0.0f).size(), 1u);
    // A coarser shell with a lower threshold waits for the finer one (ascending thresholds).
    shells[1].certifiedError = 0.001f;
    shells[1].toleranceScale = 1.0f;
    const auto ordered = Vixen::RenderGraph::ShellLadder(shells, radius, keys, 1.0f);
    ASSERT_EQ(ordered.size(), 3u);
    EXPECT_GT(ordered[1].maxQ, ordered[0].maxQ);
    // A refused registration (fewer keys) ends the ladder there.
    EXPECT_EQ(Vixen::RenderGraph::ShellLadder(shells, radius, std::vector<uint32_t>{keys[0]}, 1.0f).size(), 2u);
}

TEST(LodBandMirror, PrepareQIsPixelFootprintOverBoundDiameter) {
    // A camera at the origin looking down +z with focalY 500 px per unit depth: a bound of radius
    // 0.5 whose centre sits 10.5 units away is 10 units from its nearest point, where one pixel
    // spans 10 / 500 = 0.02 units, over a diameter of 1.
    TileBinView view{};
    view.viewXx = 1.0f;
    view.viewYy = 1.0f;
    view.viewZz = 1.0f;
    view.focalX = 500.0f;
    view.focalY = 500.0f;
    InstanceBound bound{0.0f, 0.0f, 10.5f, 0.5f};
    EXPECT_NEAR(MirrorLodQ(bound, view), 0.02f, 1e-6f);
    // Beside or behind the camera the euclidean distance decides, not the view depth.
    bound.centerZ = -10.5f;
    EXPECT_NEAR(MirrorLodQ(bound, view), 0.02f, 1e-6f);
    // Inside the bound, and a degenerate bound, select the finest band.
    bound.centerZ = 0.25f;
    EXPECT_EQ(MirrorLodQ(bound, view), 0.0f);
    bound.radius = 0.0f;
    EXPECT_EQ(MirrorLodQ(bound, view), 0.0f);
}

} // namespace
