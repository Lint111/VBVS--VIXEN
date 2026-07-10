// test_recipe_occupancy.cpp — Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13.
//
// TDD gate for DeriveOccupancyGrid's CORE claim: the stored (post-margin) value at every
// coarse cell is a genuine lower bound on the TRUE min-|sd| anywhere inside that cell — not
// just "it runs" or "it's non-negative." Random-sampled probe points inside each cell must
// never see a real |sd| smaller than the grid's stored value for that cell; a bug in the
// margin/downsample math would show up as a probe violating this on some cell, some seed.
#include <gtest/gtest.h>
#include "Recipe/RecipeOccupancy.h"
#include "Recipe/RecipeBounds.h"
#include <random>

using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;

namespace {

SdfInstruction sphere(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z;
    in.data[3] = r;
    return in;
}

SdfInstruction box(glm::vec3 he) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
    return in;
}

SdfInstruction combine(SdfOpCode op, float k = 0.f) {
    SdfInstruction in{};
    in.opCode = (uint8_t)op;
    in.data[2] = k;
    return in;
}

SdfInstruction twist(float k) {
    SdfInstruction in{};
    in.opCode = (uint8_t)SdfOpCode::Twist;
    in.data[0] = k;
    return in;
}

// Random-probes every coarse cell many times and asserts the grid's stored value never
// exceeds the TRUE |sd| at any probed point in that cell — the actual conservativeness
// claim, not just "some sanity check passed."
void AssertGridIsConservative(const std::vector<SdfInstruction>& prog,
                               const OccupancyGridResult& grid,
                               uint32_t probesPerCell = 6, uint32_t seed = 12345) {
    ASSERT_TRUE(grid.ok);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (uint32_t cz = 0; cz < grid.dim; ++cz) {
        for (uint32_t cy = 0; cy < grid.dim; ++cy) {
            for (uint32_t cx = 0; cx < grid.dim; ++cx) {
                const size_t idx = (size_t(cz) * grid.dim + cy) * grid.dim + cx;
                const float stored = grid.values[idx];
                const glm::vec3 cellMin = grid.aabbMin + glm::vec3(cx, cy, cz) * grid.cellSize;

                for (uint32_t p = 0; p < probesPerCell; ++p) {
                    const glm::vec3 probe = cellMin + glm::vec3(
                        unit(rng), unit(rng), unit(rng)) * grid.cellSize;
                    const float trueSd = std::fabs(
                        evalRecipe(prog.data(), uint32_t(prog.size()), probe));
                    EXPECT_LE(stored, trueSd + 1e-4f)
                        << "cell(" << cx << "," << cy << "," << cz << ") stored=" << stored
                        << " exceeds true |sd|=" << trueSd << " at probe ("
                        << probe.x << "," << probe.y << "," << probe.z << ")";
                }
            }
        }
    }
}

} // namespace

TEST(RecipeOccupancy, SingleSphereGridIsConservative) {
    std::vector<SdfInstruction> prog = { sphere({0.f, 0.f, 0.f}, 5.f) };
    auto bounds = DeriveConservativeBounds(prog.data(), uint32_t(prog.size()));
    ASSERT_TRUE(bounds.ok);

    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()),
                                     bounds.center, bounds.radius,
                                     /*denseN=*/32, /*coarseN=*/8);
    AssertGridIsConservative(prog, grid);
}

TEST(RecipeOccupancy, SmoothUnionCsgGridIsConservative) {
    std::vector<SdfInstruction> prog = {
        sphere({-2.f, 0.f, 0.f}, 3.f),
        box(glm::vec3(2.f, 2.f, 2.f)),
        combine(SdfOpCode::SmoothUnion, 0.5f),
    };
    auto bounds = DeriveConservativeBounds(prog.data(), uint32_t(prog.size()));
    ASSERT_TRUE(bounds.ok);

    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()),
                                     bounds.center, bounds.radius,
                                     /*denseN=*/32, /*coarseN=*/8);
    AssertGridIsConservative(prog, grid);
}

TEST(RecipeOccupancy, ProductionDefaultResolutionIsConservative) {
    // Same denseN/coarseN DeriveOccupancyGrid defaults to (64/16) — smaller probe count
    // to keep the test fast (64^3 dense eval is the expensive part, already paid once).
    std::vector<SdfInstruction> prog = { sphere({0.f, 0.f, 0.f}, 10.f) };
    auto bounds = DeriveConservativeBounds(prog.data(), uint32_t(prog.size()));
    ASSERT_TRUE(bounds.ok);

    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()),
                                     bounds.center, bounds.radius);
    ASSERT_TRUE(grid.ok);
    ASSERT_EQ(grid.dim, 16u);
    AssertGridIsConservative(prog, grid, /*probesPerCell=*/3);
}

TEST(RecipeOccupancy, NonWhitelistedOpcodeRefusesToGrid) {
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f), twist(1.f) };
    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()),
                                     glm::vec3(0.f), 24.f);
    EXPECT_FALSE(grid.ok);
}

TEST(RecipeOccupancy, EmptyProgramRefusesToGrid) {
    auto grid = DeriveOccupancyGrid(nullptr, 0, glm::vec3(0.f), 24.f);
    EXPECT_FALSE(grid.ok);
}

TEST(RecipeOccupancy, NonPositiveBoundRadiusRefusesToGrid) {
    std::vector<SdfInstruction> prog = { sphere({0,0,0}, 1.f) };
    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()), glm::vec3(0.f), 0.f);
    EXPECT_FALSE(grid.ok);
}

// Every stored value must be non-negative (this grid only ever claims a lower bound on
// distance, never "inside the surface").
TEST(RecipeOccupancy, AllStoredValuesAreNonNegative) {
    std::vector<SdfInstruction> prog = { sphere({0.f, 0.f, 0.f}, 5.f) };
    auto bounds = DeriveConservativeBounds(prog.data(), uint32_t(prog.size()));
    ASSERT_TRUE(bounds.ok);
    auto grid = DeriveOccupancyGrid(prog.data(), uint32_t(prog.size()),
                                     bounds.center, bounds.radius, 32, 8);
    ASSERT_TRUE(grid.ok);
    for (float v : grid.values) EXPECT_GE(v, 0.0f);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
