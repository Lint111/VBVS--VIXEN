/**
 * @file test_view_selection_provider.cpp
 * @brief Inc-C (View-Model-Binding-Inc-C-Plan-2026-07.md) Task 4 -- the key new proof: multi-
 * instance selection is real, not scaffolding. Creates 3 Gaia entities each carrying a LayerMask
 * component (reused from Inc-B -- see the plan's Task 4 "reuse LayerMask or introduce a second
 * trivial component, your call"; LayerMask is sufficient here since Inc-C tests selection
 * RESOLUTION, not a new noun), commits a genuine SUBSET (entities 0 and 2 of 3, not all/none) to
 * `Selected`, and asserts:
 *   1. IViewSelectionProvider::ids() yields EXACTLY that subset, in stable order.
 *   2. at(0)/at(1) resolve to the RIGHT entities (not just "some entity").
 *   3. Resolving an index through SelectionResolvingViewDataProvider to the EXISTING (Inc-B,
 *      unmodified) GaiaLayerViewDataProvider machinery reads/writes the CORRECT entity's value --
 *      each selected entity is seeded with a DIFFERENT LayerMask value, so a wrong-entity bug
 *      would surface as a wrong VALUE, not just a wrong bool (the plan's own non-vacuousness bar).
 *
 * Pure headless test (Task 1's decision): unlike Inc-B's own Task 4 proof, this needs NO RmlUi --
 * selection is a pure Gaia/AppFlow-seam concern, so there is no ODR hazard to isolate and no
 * bridge-split TU is required; this single TU includes gaia.h (via GaiaVoxelWorld.h/
 * GaiaViewSelectionProvider.h/SelectionResolvingViewDataProvider.h) and nothing RmlUi-related.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "SelectionComponents.h"
#include "GaiaViewSelectionProvider.h"
#include "SelectionResolvingViewDataProvider.h"
#include "IViewSelectionProvider.h"
#include "DirectListViewSelectionProvider.h"

using Vixen::AppFlow::SelectionEntityID;
using Vixen::AppFlow::ViewNounId;
using Vixen::AppFlow::ViewNounKey;

TEST(ViewSelectionProvider, GaiaBackedSelectionOfSubsetIsExactAndOrdered) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    // 3 bare entities (mirrors GaiaLayerViewDataProvider's MakeGaiaLayerEntity -- no spatial
    // identity, just a LayerMask each), seeded to DIFFERENT values so a wrong-entity resolution
    // bug would show up as a wrong VALUE, not just a wrong bool (plan's non-vacuousness bar).
    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0x1u);
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0x2u);
    auto e2 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e2, 0x3u);

    // Commit a genuine SUBSET: entities 0 and 2, NOT entity 1, NOT all three.
    world.getWorld().add<Vixen::App::Selected>(e0);
    world.getWorld().add<Vixen::App::Selected>(e2);

    Vixen::App::GaiaViewSelectionProvider selection(world);

    std::vector<SelectionEntityID> ids;
    const size_t count = selection.ids(ids);
    ASSERT_EQ(count, 2u) << "expected exactly the committed subset (2 of 3), not all/none";
    EXPECT_EQ(ids.size(), 2u);

    const auto e0Id = Vixen::App::GaiaViewSelectionProvider::EntityToSelectionId(e0);
    const auto e1Id = Vixen::App::GaiaViewSelectionProvider::EntityToSelectionId(e1);
    const auto e2Id = Vixen::App::GaiaViewSelectionProvider::EntityToSelectionId(e2);

    // The unselected entity must NOT appear anywhere in the set (proves filtering, not "everything
    // is selected").
    EXPECT_EQ(std::find(ids.begin(), ids.end(), e1Id), ids.end())
        << "unselected entity e1 leaked into the selection set";

    // Both committed entities must appear, in the query's own stable (creation) order --
    // GaiaViewSelectionProvider.h's header comment documents exactly why this order is guaranteed
    // for an append-only Selected set.
    ASSERT_EQ(ids[0], e0Id) << "at(0) resolved to the wrong entity -- stable order violated";
    ASSERT_EQ(ids[1], e2Id) << "at(1) resolved to the wrong entity -- stable order violated";

    // at() must agree with ids() index-for-index.
    SelectionEntityID at0, at1;
    ASSERT_TRUE(selection.at(0, at0));
    ASSERT_TRUE(selection.at(1, at1));
    EXPECT_EQ(at0, e0Id);
    EXPECT_EQ(at1, e2Id);

    // Out-of-range index is fallible (false), not a silent wrong answer.
    SelectionEntityID outOfRange;
    EXPECT_FALSE(selection.at(2, outOfRange));
}

TEST(ViewSelectionProvider, SelectionResolvedReadWriteHitsCorrectEntityNotEntityZero) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;

    // Same 3-entity/2-selected setup, but this time assert through the FULL Task-3 wiring:
    // selection index -> IViewSelectionProvider::at() -> SelectionResolvingViewDataProvider ->
    // the EXISTING GaiaLayerViewDataProvider machinery (Inc-B, unmodified).
    auto e0 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e0, 0xAAu);  // distinct value per entity
    auto e1 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e1, 0xBBu);
    auto e2 = world.getWorld().add();
    world.setComponent<Vixen::GaiaVoxel::LayerMask>(e2, 0xCCu);

    // Selected subset = {e1, e2} this time (deliberately NOT starting at entity 0, so "index 0
    // happens to be entity 0" can't hide a bug -- selection index 0 must resolve to e1, not e0).
    world.getWorld().add<Vixen::App::Selected>(e1);
    world.getWorld().add<Vixen::App::Selected>(e2);

    Vixen::App::GaiaViewSelectionProvider selection(world);
    Vixen::App::SelectionResolvingViewDataProvider provider(world, selection);

    // Selection index 0 must read e1's value (0xBB), NOT e0's (0xAA) and NOT e2's (0xCC).
    uint32_t valueAtIndex0 = 0;
    ASSERT_TRUE(provider.ReadU32(ViewNounKey{ViewNounId::LayerMask, 0}, valueAtIndex0));
    EXPECT_EQ(valueAtIndex0, 0xBBu) << "selection index 0 resolved to the wrong entity's value";

    // Selection index 1 must read e2's value (0xCC).
    uint32_t valueAtIndex1 = 0;
    ASSERT_TRUE(provider.ReadU32(ViewNounKey{ViewNounId::LayerMask, 1}, valueAtIndex1));
    EXPECT_EQ(valueAtIndex1, 0xCCu) << "selection index 1 resolved to the wrong entity's value";

    // WRITE through selection index 1 (e2) must land on e2, and must NOT perturb e1 or e0 --
    // varying which entity gets written proves this isn't coincidentally correct.
    provider.WriteU32(ViewNounKey{ViewNounId::LayerMask, 1}, 0xDDu);

    auto e2Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e2);
    ASSERT_TRUE(e2Value.has_value());
    EXPECT_EQ(*e2Value, 0xDDu) << "write through selection index 1 did not land on e2";

    auto e1Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e1);
    ASSERT_TRUE(e1Value.has_value());
    EXPECT_EQ(*e1Value, 0xBBu) << "write through selection index 1 incorrectly perturbed e1";

    auto e0Value = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0Value.has_value());
    EXPECT_EQ(*e0Value, 0xAAu) << "write through selection index 1 incorrectly perturbed the unselected e0";

    // Out-of-range selection index must fail closed (false / no-op), not fall back to entity 0.
    uint32_t outOfRangeRead = 0;
    EXPECT_FALSE(provider.ReadU32(ViewNounKey{ViewNounId::LayerMask, 2}, outOfRangeRead));
    provider.WriteU32(ViewNounKey{ViewNounId::LayerMask, 2}, 0xFFu);  // must no-op, not touch e0
    auto e0ValueAfter = world.getComponentValue<Vixen::GaiaVoxel::LayerMask>(e0);
    ASSERT_TRUE(e0ValueAfter.has_value());
    EXPECT_EQ(*e0ValueAfter, 0xAAu) << "out-of-range write incorrectly fell back to entity 0";
}

TEST(ViewSelectionProvider, DirectListImplementationMirrorsGaiaBackedContract) {
    // The direct-list provider (Task 2's non-Gaia implementation) must satisfy the exact same
    // seam contract -- non-vacuous subset + stable order + fallible out-of-range.
    Vixen::AppFlow::DirectListViewSelectionProvider provider({SelectionEntityID(100), SelectionEntityID(300)});

    std::vector<SelectionEntityID> ids;
    ASSERT_EQ(provider.ids(ids), 2u);
    EXPECT_EQ(ids[0], SelectionEntityID(100));
    EXPECT_EQ(ids[1], SelectionEntityID(300));

    SelectionEntityID at0, at1, outOfRange;
    ASSERT_TRUE(provider.at(0, at0));
    ASSERT_TRUE(provider.at(1, at1));
    EXPECT_EQ(at0, SelectionEntityID(100));
    EXPECT_EQ(at1, SelectionEntityID(300));
    EXPECT_FALSE(provider.at(2, outOfRange));
}
