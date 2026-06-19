#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace Vixen::SVO {

inline std::vector<glm::ivec3> ShellVoxels(int depth) {
    int n = 1 << depth;
    double inv = 2.0 / n;                       // cell size in [-1,1] units
    // A cell straddles the surface when its center is within one half-diagonal of it;
    // this band keeps the shell watertight (1 voxel thick, no gaps) at any depth.
    constexpr double kHalfDiagonal = 0.8660254037844386;  // √3 / 2
    double band = inv * kHalfDiagonal;
    std::vector<glm::ivec3> out;
    for (int z = 0; z < n; ++z)
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            glm::dvec3 p((x + 0.5) * inv - 1.0, (y + 0.5) * inv - 1.0, (z + 0.5) * inv - 1.0);
            double r = glm::length(p);
            if (r >= 1.0 - band && r <= 1.0 + band) out.push_back({x, y, z});
        }
    return out;
}

}  // namespace Vixen::SVO
