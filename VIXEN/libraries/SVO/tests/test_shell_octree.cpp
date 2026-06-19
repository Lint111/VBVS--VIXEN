#include "ShellVoxelizer.h"
#include <gtest/gtest.h>
#include <algorithm>

TEST(ShellVoxelizer, IsHollowSurfaceShell) {
    int d = 5;                          // 32^3 lattice
    auto cells = Vixen::SVO::ShellVoxels(d);   // vector<glm::ivec3> in [0, 2^d)
    ASSERT_FALSE(cells.empty());
    double N = double(1 << d);
    for (auto c : cells) {              // every emitted cell straddles the sphere surface
        glm::dvec3 p = (glm::dvec3(c) + 0.5) / N * 2.0 - 1.0;   // cell center in [-1,1]
        double r = glm::length(p);
        EXPECT_NEAR(r, 1.0, 2.0 / N);   // within ~one voxel of the unit surface
    }
    // hollow: the exact center cell is NOT in the set
    // (use glm::all(glm::equal(...)) — glm's operator== is component-wise on some versions)
    glm::ivec3 center(1 << (d - 1));
    bool centerPresent = std::any_of(cells.begin(), cells.end(),
        [&](const glm::ivec3& c) { return glm::all(glm::equal(c, center)); });
    EXPECT_FALSE(centerPresent);
}

TEST(ShellVoxelizer, CountScalesAsSurface) {
    auto c4 = Vixen::SVO::ShellVoxels(4).size();
    auto c5 = Vixen::SVO::ShellVoxels(5).size();
    EXPECT_GT(c5, c4 * 3);   // surface ~O(2^2d): doubling N ~4x the shell, not ~8x (solid) nor ~2x
    EXPECT_LT(c5, c4 * 6);
}
