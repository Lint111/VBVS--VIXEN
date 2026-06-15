// SEL-P1 — engine-native selection core types.
//
// Pure unit tests for SelectionSet.apply() modifier semantics and the
// SelectionId hash/equality contract. No Vulkan device, no graph — these are
// plain value types and a set, fully headless.

#include <gtest/gtest.h>

#include <functional>
#include <unordered_set>

#include "Selection/SelectionSet.h"
#include "Selection/SelectionId.h"

using Vixen::RenderGraph::SelectionSet;
using Vixen::RenderGraph::SelectionId;
using Vixen::RenderGraph::SelectionModifier;
using Vixen::RenderGraph::ProviderKind;
using Vixen::RenderGraph::kInvalidSelectionId;

namespace {

constexpr SelectionId kVoxel1{ ProviderKind::Voxel, 1 };
constexpr SelectionId kVoxel2{ ProviderKind::Voxel, 2 };
constexpr SelectionId kUi1{ ProviderKind::Ui, 1 };

} // namespace

// ---- SelectionId equality / hashing ---------------------------------------

TEST(SelectionId, EqualIdsCompareEqual) {
    constexpr SelectionId a{ ProviderKind::Voxel, 42 };
    constexpr SelectionId b{ ProviderKind::Voxel, 42 };
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(SelectionId, DifferentPayloadDiffers) {
    constexpr SelectionId a{ ProviderKind::Voxel, 42 };
    constexpr SelectionId b{ ProviderKind::Voxel, 43 };
    EXPECT_NE(a, b);
}

TEST(SelectionId, DifferentKindDiffers) {
    // Same payload, different domain → distinct identities.
    constexpr SelectionId voxel{ ProviderKind::Voxel, 7 };
    constexpr SelectionId ui{ ProviderKind::Ui, 7 };
    EXPECT_NE(voxel, ui);
}

TEST(SelectionId, EqualIdsHashEqual) {
    constexpr SelectionId a{ ProviderKind::Mesh, 1000 };
    constexpr SelectionId b{ ProviderKind::Mesh, 1000 };
    std::hash<SelectionId> h;
    EXPECT_EQ(h(a), h(b));
}

TEST(SelectionId, DifferentKindHashesDiffer) {
    // Not strictly required by std::hash, but our combine mixes kind in so
    // same-payload/different-kind should not collide here.
    constexpr SelectionId voxel{ ProviderKind::Voxel, 7 };
    constexpr SelectionId ui{ ProviderKind::Ui, 7 };
    std::hash<SelectionId> h;
    EXPECT_NE(h(voxel), h(ui));
}

TEST(SelectionId, UsableAsUnorderedSetKey) {
    std::unordered_set<SelectionId> s;
    s.insert(kVoxel1);
    s.insert(kVoxel1);  // duplicate
    s.insert(kUi1);
    EXPECT_EQ(s.size(), 2u);
    EXPECT_TRUE(s.count(kVoxel1) == 1);
    EXPECT_TRUE(s.count(kUi1) == 1);
}

TEST(SelectionId, InvalidIsCustomZero) {
    EXPECT_EQ(kInvalidSelectionId.kind, ProviderKind::Custom);
    EXPECT_EQ(kInvalidSelectionId.payload, 0u);
}

// ---- SelectionSet basics --------------------------------------------------

TEST(SelectionSet, StartsEmpty) {
    SelectionSet set;
    EXPECT_EQ(set.size(), 0u);
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.contains(kVoxel1));
}

TEST(SelectionSet, ClearEmptiesTheSet) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Add, kVoxel2);
    ASSERT_EQ(set.size(), 2u);

    set.clear();
    EXPECT_EQ(set.size(), 0u);
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.contains(kVoxel1));
}

// ---- apply(): Replace -----------------------------------------------------

TEST(SelectionSet, ReplaceClearsThenSets) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Add, kVoxel2);
    ASSERT_EQ(set.size(), 2u);

    // Replace wipes everything and leaves exactly {kUi1}.
    set.apply(SelectionModifier::Replace, kUi1);
    EXPECT_EQ(set.size(), 1u);
    EXPECT_TRUE(set.contains(kUi1));
    EXPECT_FALSE(set.contains(kVoxel1));
    EXPECT_FALSE(set.contains(kVoxel2));
}

// ---- apply(): Add ---------------------------------------------------------

TEST(SelectionSet, AddAccumulates) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Add, kVoxel2);
    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(kVoxel1));
    EXPECT_TRUE(set.contains(kVoxel2));
}

TEST(SelectionSet, AddDuplicateIsNoOp) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Add, kVoxel1);
    EXPECT_EQ(set.size(), 1u);
    EXPECT_TRUE(set.contains(kVoxel1));
}

// ---- apply(): Toggle (insert then remove) ---------------------------------

TEST(SelectionSet, ToggleInsertsThenRemoves) {
    SelectionSet set;

    // First toggle on an absent id inserts it.
    set.apply(SelectionModifier::Toggle, kVoxel1);
    EXPECT_TRUE(set.contains(kVoxel1));
    EXPECT_EQ(set.size(), 1u);

    // Second toggle on the present id removes it.
    set.apply(SelectionModifier::Toggle, kVoxel1);
    EXPECT_FALSE(set.contains(kVoxel1));
    EXPECT_EQ(set.size(), 0u);
}

TEST(SelectionSet, ToggleLeavesOthersUntouched) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Toggle, kVoxel2);  // add v2
    EXPECT_EQ(set.size(), 2u);

    set.apply(SelectionModifier::Toggle, kVoxel2);  // remove v2
    EXPECT_EQ(set.size(), 1u);
    EXPECT_TRUE(set.contains(kVoxel1));
    EXPECT_FALSE(set.contains(kVoxel2));
}

// ---- apply(): Range (documented = Add for now) ----------------------------

TEST(SelectionSet, RangeBehavesLikeAddForNow) {
    SelectionSet set;
    set.apply(SelectionModifier::Range, kVoxel1);
    set.apply(SelectionModifier::Range, kVoxel2);
    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(kVoxel1));
    EXPECT_TRUE(set.contains(kVoxel2));
}

// ---- ids() view reflects contents -----------------------------------------

TEST(SelectionSet, IdsViewReflectsContents) {
    SelectionSet set;
    set.apply(SelectionModifier::Add, kVoxel1);
    set.apply(SelectionModifier::Add, kUi1);

    const std::unordered_set<SelectionId>& view = set.ids();
    EXPECT_EQ(view.size(), 2u);
    EXPECT_TRUE(view.count(kVoxel1) == 1);
    EXPECT_TRUE(view.count(kUi1) == 1);
}
