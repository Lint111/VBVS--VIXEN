#pragma once

// Derived procedural recipe chunks for the optional RT/software culling path.
// The occupancy grid is already the registration-time, conservative surface
// witness used by the recipe marcher. Reusing it here keeps the candidate set
// and the march's own empty-space proof on the same source of truth.

#include "Recipe/RecipeRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Vixen::SVO::Recipe {

struct RecipeChunkAabb {
    float minLocal[3]{};
    uint32_t recipeId = 0;
    float maxLocal[3]{};
    uint32_t flags = 0; // bit 0: coarse-parent fallback
};
static_assert(sizeof(RecipeChunkAabb) == 32, "RecipeChunkAabb must stay 32B");

inline constexpr uint32_t kRecipeChunkCoarseParent = 1u;

inline RecipeChunkAabb MakeRecipeChunkAabb(uint32_t recipeId, const glm::vec3& minLocal,
                                           const glm::vec3& maxLocal, uint32_t flags = 0u) {
    RecipeChunkAabb result{};
    result.minLocal[0] = minLocal.x;
    result.minLocal[1] = minLocal.y;
    result.minLocal[2] = minLocal.z;
    result.recipeId = recipeId;
    result.maxLocal[0] = maxLocal.x;
    result.maxLocal[1] = maxLocal.y;
    result.maxLocal[2] = maxLocal.z;
    result.flags = flags;
    return result;
}

// Build the surface shell at the occupancy-grid level. A grid cell is a
// surface witness when its conservative lower bound reaches the cell; one
// cell of dilation gives the same neighbour safety margin as ShellDerive's
// 26-neighbour shell. A recipe without a usable grid receives one coarse
// parent, so an unsupported opcode or a parameterized recipe can never be
// rejected by a missing candidate box.
inline std::vector<RecipeChunkAabb> DeriveRecipeChunkBoxes(const RecipeRegistry& registry) {
    std::vector<RecipeChunkAabb> result;
    for (const uint32_t recipeId : registry.Ids()) {
        const RecipeRegistry::RecipeEntry* entry = registry.Get(recipeId);
        if (entry == nullptr) continue;

        bool emitted = false;
        const uint32_t dim = entry->occupancyGridDim;
        const size_t expected = static_cast<size_t>(dim) * dim * dim;
        if (dim > 0u && entry->occupancyGridCellSize > 0.0f &&
            entry->occupancyGridValues.size() == expected &&
            std::isfinite(entry->occupancyGridCellSize)) {
            std::vector<uint8_t> shell(expected, 0u);
            for (uint32_t z = 0; z < dim; ++z) {
                for (uint32_t y = 0; y < dim; ++y) {
                    for (uint32_t x = 0; x < dim; ++x) {
                        const size_t index = (static_cast<size_t>(z) * dim + y) * dim + x;
                        const float lowerBound = entry->occupancyGridValues[index];
                        if (std::isfinite(lowerBound) && lowerBound <= 0.0f) {
                            // Dilate the witness by one grid cell in all axes.
                            for (int dz = -1; dz <= 1; ++dz) {
                                for (int dy = -1; dy <= 1; ++dy) {
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        const int nx = std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(dim) - 1);
                                        const int ny = std::clamp(static_cast<int>(y) + dy, 0, static_cast<int>(dim) - 1);
                                        const int nz = std::clamp(static_cast<int>(z) + dz, 0, static_cast<int>(dim) - 1);
                                        shell[(static_cast<size_t>(nz) * dim + static_cast<size_t>(ny)) * dim + static_cast<size_t>(nx)] = 1u;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (uint32_t z = 0; z < dim; ++z) {
                for (uint32_t y = 0; y < dim; ++y) {
                    for (uint32_t x = 0; x < dim; ++x) {
                        const size_t index = (static_cast<size_t>(z) * dim + y) * dim + x;
                        if (shell[index] == 0u) continue;
                        const glm::vec3 minLocal = entry->occupancyGridAabbMin +
                            glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) *
                            entry->occupancyGridCellSize;
                        result.push_back(MakeRecipeChunkAabb(recipeId, minLocal,
                            minLocal + glm::vec3(entry->occupancyGridCellSize)));
                        emitted = true;
                    }
                }
            }
        }

        if (!emitted && entry->boundRadius > 0.0f && std::isfinite(entry->boundRadius)) {
            const glm::vec3 extent(entry->boundRadius);
            result.push_back(MakeRecipeChunkAabb(recipeId, entry->boundCenter - extent,
                entry->boundCenter + extent, kRecipeChunkCoarseParent));
        }
    }
    return result;
}

} // namespace Vixen::SVO::Recipe
