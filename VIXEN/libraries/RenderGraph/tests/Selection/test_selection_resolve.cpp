// SEL-P2 (providers are nodes) — candidate fan-in resolution.
//
// The voxel pick is now a graph NODE (VoxelSelectionProviderNode); its GPU
// ID-buffer readback needs a device/queue/ID-image and is exercised LIVE in the
// running app (the user's manual click test). What IS pure and unit-testable is
// the cross-provider resolution rule the SelectionCoordinatorNode applies to the
// candidates gathered through its MultiConnect slot — pickBestCandidate(). These
// tests pin that rule (max priority, tie-break min depth, ignore non-hits) and
// the SelectionCandidate contract, exercising the SAME function the coordinator
// calls (no logic copy).

#include <gtest/gtest.h>

#include "Selection/SelectionCandidate.h"
#include "Selection/SelectionResolve.h"
#include "Selection/SelectionId.h"

#include <vector>

using Vixen::RenderGraph::SelectionCandidate;
using Vixen::RenderGraph::pickBestCandidate;
using Vixen::RenderGraph::ProviderKind;
using Vixen::RenderGraph::SelectionId;

namespace {

SelectionCandidate MakeHit(ProviderKind kind, uint64_t payload, float depth, int priority) {
    SelectionCandidate c{};
    c.hit      = true;
    c.id       = SelectionId{ kind, payload };
    c.depth    = depth;
    c.priority = priority;
    c.worldPos = glm::vec3(0.0f);
    return c;
}

SelectionCandidate MakeMiss(int priority = 0) {
    SelectionCandidate c{};
    c.hit      = false;
    c.id       = Vixen::RenderGraph::kInvalidSelectionId;
    c.depth    = 0.0f;
    c.priority = priority;
    c.worldPos = glm::vec3(0.0f);
    return c;
}

} // namespace

TEST(SelectionResolve, EmptyIsNoWinner) {
    std::vector<SelectionCandidate> candidates;
    EXPECT_EQ(pickBestCandidate(candidates), nullptr);
}

TEST(SelectionResolve, AllMissesIsNoWinner) {
    // Providers emit {hit=false} off the click edge / on a miss — those must be ignored.
    std::vector<SelectionCandidate> candidates{ MakeMiss(0), MakeMiss(100), MakeMiss(5) };
    EXPECT_EQ(pickBestCandidate(candidates), nullptr);
}

TEST(SelectionResolve, SingleHitWins) {
    std::vector<SelectionCandidate> candidates{ MakeHit(ProviderKind::Voxel, 42, 0.0f, 0) };
    const SelectionCandidate* best = pickBestCandidate(candidates);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->id.kind, ProviderKind::Voxel);
    EXPECT_EQ(best->id.payload, 42u);
}

TEST(SelectionResolve, HigherPriorityOccludes) {
    // UI (priority 10) must occlude the world voxel (priority 0) even though the voxel is nearer.
    std::vector<SelectionCandidate> candidates{
        MakeHit(ProviderKind::Voxel, 7, /*depth=*/0.1f, /*priority=*/0),
        MakeHit(ProviderKind::Ui,    3, /*depth=*/0.9f, /*priority=*/10),
    };
    const SelectionCandidate* best = pickBestCandidate(candidates);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->id.kind, ProviderKind::Ui);
    EXPECT_EQ(best->priority, 10);
}

TEST(SelectionResolve, EqualPriorityNearerDepthWins) {
    // Same layer → the nearer (smaller depth) candidate wins.
    std::vector<SelectionCandidate> candidates{
        MakeHit(ProviderKind::Mesh, 1, /*depth=*/5.0f, /*priority=*/2),
        MakeHit(ProviderKind::Mesh, 2, /*depth=*/2.0f, /*priority=*/2),
        MakeHit(ProviderKind::Mesh, 3, /*depth=*/8.0f, /*priority=*/2),
    };
    const SelectionCandidate* best = pickBestCandidate(candidates);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->id.payload, 2u);  // depth 2.0 is nearest
}

TEST(SelectionResolve, HitsAndMissesMixed) {
    // Misses interleaved with hits must not affect the winner.
    std::vector<SelectionCandidate> candidates{
        MakeMiss(99),
        MakeHit(ProviderKind::Voxel, 11, /*depth=*/0.5f, /*priority=*/0),
        MakeMiss(0),
        MakeHit(ProviderKind::Ui, 22, /*depth=*/0.5f, /*priority=*/5),
        MakeMiss(50),
    };
    const SelectionCandidate* best = pickBestCandidate(candidates);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->id.kind, ProviderKind::Ui);
    EXPECT_EQ(best->id.payload, 22u);
}
