/**
 * SVOBrickDDA.cpp - Dense Brick Voxel Grid Ray Marching
 * ==============================================================================
 * 3D DDA (Digital Differential Analyzer) traversal through dense voxel bricks.
 * Implements fine-grained ray marching within octree leaf nodes.
 *
 * REFERENCES:
 * -----------
 * [1] Amanatides, J. and Woo, A. "A Fast Voxel Traversal Algorithm for Ray Tracing"
 *     Eurographics 1987
 *     http://www.cse.yorku.ca/~amana/research/grid.pdf
 *
 * [2] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees"
 *     NVIDIA Research, I3D 2010 (Section 4.2: Bricks)
 *
 * ALGORITHM OVERVIEW:
 * -------------------
 * The DDA algorithm traverses a regular voxel grid efficiently:
 * 1. Compute entry point into brick in local [0,N]^3 coordinates
 * 2. Initialize DDA state: tDelta (step size), tNext (next boundary crossing)
 * 3. March through voxels, stepping along axis with minimum tNext
 * 4. At each voxel: query occupancy via EntityBrickView
 * 5. Return hit on first occupied voxel, or miss on brick exit
 *
 * ==============================================================================
 */

#define NOMINMAX
#include "pch.h"
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"
#include <limits>
#include <cmath>
#include <algorithm>

using namespace Vixen::GaiaVoxel;

namespace Vixen::SVO {

// ============================================================================
// Debug Utilities
// ============================================================================

#define LKOCTREE_DEBUG_TRAVERSAL 0

#if LKOCTREE_DEBUG_TRAVERSAL
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)
#endif

// ============================================================================
// Leaf Hit Handler
// ============================================================================

/**
 * Handle leaf hit: perform brick traversal and return hit result.
 * Returns nullopt if traversal should continue (brick miss).
 *
 * BRICK LOOKUP STRATEGY:
 * - state.idx is in MIRRORED space (ray-direction dependent)
 * - leafToBrickView stores bricks by LOCAL-space octant (ray-independent)
 * - Convert mirrored->local: localOctant = state.idx ^ octant_mask
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::handleLeafHit(
    ESVOTraversalState& state,
    const ESVORayCoefficients& coef,
    const glm::vec3& origin,
    float tRayStart,
    float tEntry,
    float tExit,
    float tv_max) const
{
    DEBUG_PRINT("  handleLeafHit: idx=%d, state.t_min=%.4f, tv_max=%.4f, tRayStart=%.4f, tEntry=%.4f, tExit=%.4f\n",
                state.idx, state.t_min, tv_max, tRayStart, tEntry, tExit);

    size_t parentDescriptorIndex = state.parent - &m_octree->root->childDescriptors[0];
    glm::vec3 worldSize = m_worldMax - m_worldMin;
    int bricksPerAxis = m_octree->bricksPerAxis;
    int brickSideLength = m_octree->brickSideLength;

    // Compute brick from ESVO state position (for axes the ray is moving along)
    // and actual ray position (for stationary axes where ray doesn't move).
    constexpr float axis_epsilon = 1e-5f;

    // Get ray position from ESVO state (mirrored -> local conversion using NVIDIA formula)
    glm::vec3 localPos = state.pos;
    float octantSize = state.scale_exp2;

    // Unmirror using NVIDIA's formula: 3.0 - scale_exp2 - pos
    if ((coef.octant_mask & 1) == 0) localPos.x = 3.0f - octantSize - localPos.x;
    if ((coef.octant_mask & 2) == 0) localPos.y = 3.0f - octantSize - localPos.y;
    if ((coef.octant_mask & 4) == 0) localPos.z = 3.0f - octantSize - localPos.z;

    // Now localPos is in LOCAL [1,2] space - convert to [0,1] normalized space
    glm::vec3 localNorm = localPos - glm::vec3(1.0f);

    // Add small offset along world ray direction to get point inside the octant
    float offset = 0.001f;
    glm::vec3 offsetDir;
    offsetDir.x = (coef.rayDir.x > 0) ? offset : -offset;
    offsetDir.y = (coef.rayDir.y > 0) ? offset : -offset;
    offsetDir.z = (coef.rayDir.z > 0) ? offset : -offset;
    glm::vec3 octantInside = localNorm + offsetDir;

    // For stationary axes (ray perpendicular), use actual ray position
    glm::vec3 rayPosWorld = origin + coef.rayDir * std::max(tEntry, 0.0f);
    glm::vec3 rayPosLocal = (rayPosWorld - m_worldMin) / worldSize;
    rayPosLocal = glm::clamp(rayPosLocal, glm::vec3(0.001f), glm::vec3(0.999f));

    if (std::abs(coef.rayDir.x) < axis_epsilon) {
        octantInside.x = rayPosLocal.x;
    }
    if (std::abs(coef.rayDir.y) < axis_epsilon) {
        octantInside.y = rayPosLocal.y;
    }
    if (std::abs(coef.rayDir.z) < axis_epsilon) {
        octantInside.z = rayPosLocal.z;
    }

    octantInside = glm::clamp(octantInside, glm::vec3(0.001f), glm::vec3(0.999f));

    const EntityBrickView* brickView = nullptr;
    glm::ivec3 brickIndex;

    // Method 1: ESVO state position (correct for multi-octant traversal)
    glm::vec3 hitPosLocal = octantInside * worldSize;
    hitPosLocal = glm::clamp(hitPosLocal, glm::vec3(0.0f), worldSize - glm::vec3(0.001f));

    brickIndex = glm::ivec3(
        static_cast<int>(hitPosLocal.x / static_cast<float>(brickSideLength)),
        static_cast<int>(hitPosLocal.y / static_cast<float>(brickSideLength)),
        static_cast<int>(hitPosLocal.z / static_cast<float>(brickSideLength))
    );
    brickIndex = glm::clamp(brickIndex, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));
    brickView = m_octree->root->getBrickViewByGrid(brickIndex.x, brickIndex.y, brickIndex.z);

    // Method 2: Ray entry position (fallback for exterior rays into sparse octrees)
    if (brickView == nullptr) {
        glm::vec3 rayEntryWorld = origin + coef.rayDir * std::max(tEntry, 0.0f);
        glm::vec3 rayEntryLocal = rayEntryWorld - m_worldMin;
        rayEntryLocal += coef.rayDir * 0.01f;
        rayEntryLocal = glm::clamp(rayEntryLocal, glm::vec3(0.0f), worldSize - glm::vec3(0.001f));

        brickIndex = glm::ivec3(
            static_cast<int>(rayEntryLocal.x / static_cast<float>(brickSideLength)),
            static_cast<int>(rayEntryLocal.y / static_cast<float>(brickSideLength)),
            static_cast<int>(rayEntryLocal.z / static_cast<float>(brickSideLength))
        );
        brickIndex = glm::clamp(brickIndex, glm::ivec3(0), glm::ivec3(bricksPerAxis - 1));
        brickView = m_octree->root->getBrickViewByGrid(brickIndex.x, brickIndex.y, brickIndex.z);
    }

    // Fallback 3: ESVO octant-based lookup (legacy compatibility)
    if (brickView == nullptr) {
        int localOctant = mirroredToLocalOctant(state.idx, coef.octant_mask);
        brickView = m_octree->root->getBrickView(parentDescriptorIndex, localOctant);
    }

    DEBUG_PRINT("    parentDescriptorIndex=%zu, brickIndex=(%d,%d,%d), brickView=%p\n",
                parentDescriptorIndex, brickIndex.x, brickIndex.y, brickIndex.z, (void*)brickView);

    if (brickView != nullptr) {
        // Transform ray to volume local space using mat4
        glm::vec3 localRayOrigin = glm::vec3(m_worldToLocal * glm::vec4(origin, 1.0f));
        glm::vec3 localRayDir = glm::mat3(m_worldToLocal) * coef.rayDir;

        DEBUG_PRINT("    localRayOrigin=(%.2f,%.2f,%.2f), brickView->voxelsPerBrick=%zu\n",
                    localRayOrigin.x, localRayOrigin.y, localRayOrigin.z, brickView->getVoxelsPerBrick());
        auto hitResult = traverseBrickAndReturnHit(*brickView, localRayOrigin, localRayDir, tEntry);

        // Transform hit point back to world space using mat4
        if (hitResult.has_value()) {
            hitResult->hitPoint = glm::vec3(m_localToWorld * glm::vec4(hitResult->hitPoint, 1.0f));
        }
        return hitResult;
    }

    DEBUG_PRINT("    No brickView found, returning miss\n");
    return std::nullopt;
}

/**
 * Traverse brick and return hit result.
 * Ray is in volume local space (origin at volumeGridMin = 0,0,0).
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrickAndReturnHit(
    const EntityBrickView& brickView,
    const glm::vec3& localRayOrigin,
    const glm::vec3& rayDir,
    float tEntry) const
{
    uint8_t brickDepth = brickView.getDepth();
    size_t brickSideLength = 1u << brickDepth;
    constexpr float brickVoxelSize = 1.0f;

    // Compute brick bounds directly from LOCAL gridOrigin
    glm::vec3 brickLocalMin = glm::vec3(brickView.getLocalGridOrigin());
    glm::vec3 brickLocalMax = brickLocalMin + glm::vec3(static_cast<float>(brickSideLength) * brickVoxelSize);

    // Compute ray-brick AABB intersection in local space
    glm::vec3 invDir;
    for (int i = 0; i < 3; i++) {
        if (std::abs(rayDir[i]) < 1e-8f) {
            invDir[i] = (rayDir[i] >= 0) ? 1e8f : -1e8f;
        } else {
            invDir[i] = 1.0f / rayDir[i];
        }
    }

    glm::vec3 t0 = (brickLocalMin - localRayOrigin) * invDir;
    glm::vec3 t1 = (brickLocalMax - localRayOrigin) * invDir;
    glm::vec3 tNear = glm::min(t0, t1);
    glm::vec3 tFar = glm::max(t0, t1);

    float brickTMin = glm::max(glm::max(tNear.x, tNear.y), tNear.z);
    float brickTMax = glm::min(glm::min(tFar.x, tFar.y), tFar.z);
    brickTMin = glm::max(brickTMin, tEntry);

    DEBUG_PRINT("    traverseBrickAndReturnHit: brickLocalMin=(%.1f,%.1f,%.1f), brickLocalMax=(%.1f,%.1f,%.1f)\n",
                brickLocalMin.x, brickLocalMin.y, brickLocalMin.z, brickLocalMax.x, brickLocalMax.y, brickLocalMax.z);
    DEBUG_PRINT("    brickTMin=%.4f, brickTMax=%.4f, tEntry=%.4f\n", brickTMin, brickTMax, tEntry);

    return traverseBrickView(brickView, brickLocalMin, brickVoxelSize, localRayOrigin, rayDir, brickTMin, brickTMax);
}

// ============================================================================
// Brick DDA Traversal Implementation
// ============================================================================

/**
 * 3D DDA ray traversal through dense brick voxels.
 *
 * Based on Amanatides & Woo (1987) "A Fast Voxel Traversal Algorithm for Ray Tracing"
 * with adaptations for brick-based octree storage.
 */
std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrick(
    const BrickReference& brickRef,
    const glm::vec3& brickWorldMin,
    float brickVoxelSize,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    float tMin,
    float tMax) const
{
    const int brickN = brickRef.getSideLength();

    // 1. Compute ray entry point into brick
    const glm::vec3 entryPoint = rayOrigin + rayDir * tMin;

    // 2. Transform entry point to brick-local [0, N]^3 space
    const glm::vec3 localEntry = (entryPoint - brickWorldMin) / brickVoxelSize;

    // 3. Initialize current voxel (integer coordinates)
    glm::ivec3 currentVoxel{
        static_cast<int>(std::floor(localEntry.x)),
        static_cast<int>(std::floor(localEntry.y)),
        static_cast<int>(std::floor(localEntry.z))
    };

    // Clamp to brick bounds [0, N-1]
    currentVoxel = glm::clamp(currentVoxel, glm::ivec3(0), glm::ivec3(brickN - 1));

    // 4. Compute DDA step directions and tDelta
    glm::ivec3 step;
    glm::vec3 tDelta;
    glm::vec3 tNext;

    constexpr float epsilon = 1e-8f;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(rayDir[axis]) < epsilon) {
            step[axis] = 0;
            tDelta[axis] = std::numeric_limits<float>::max();
            tNext[axis] = std::numeric_limits<float>::max();
        } else {
            step[axis] = (rayDir[axis] > 0.0f) ? 1 : -1;
            tDelta[axis] = brickVoxelSize / std::abs(rayDir[axis]);

            if (rayDir[axis] > 0.0f) {
                const float nextBoundaryWorld = brickWorldMin[axis] + (currentVoxel[axis] + 1) * brickVoxelSize;
                const float distToNextBoundary = nextBoundaryWorld - entryPoint[axis];
                tNext[axis] = tMin + distToNextBoundary / rayDir[axis];
            } else {
                const float nextBoundaryWorld = brickWorldMin[axis] + currentVoxel[axis] * brickVoxelSize;
                const float distToNextBoundary = entryPoint[axis] - nextBoundaryWorld;
                tNext[axis] = tMin + distToNextBoundary / std::abs(rayDir[axis]);
            }
        }
    }

    // 5. DDA march through brick voxels
    int maxSteps = brickN * 3;
    int stepCount = 0;

    while (stepCount < maxSteps) {
        ++stepCount;

        if (currentVoxel.x < 0 || currentVoxel.x >= brickN ||
            currentVoxel.y < 0 || currentVoxel.y >= brickN ||
            currentVoxel.z < 0 || currentVoxel.z >= brickN) {
            return std::nullopt;
        }

        // 6. Sample brick voxel for occupancy using key predicate
        bool voxelOccupied = true;

        if (m_registry) {
            BrickView brick = m_registry->getBrick(brickRef.brickID);
            const int brickNLocal = brickRef.getSideLength();
            const size_t localIdx = static_cast<size_t>(currentVoxel.x + currentVoxel.y * brickNLocal + currentVoxel.z * brickNLocal * brickNLocal);

            const std::any& keyAttributeValue = brick.getKeyAttributePointer()[localIdx];

            if(!keyAttributeValue.has_value()) {
                voxelOccupied = false;
                return std::nullopt;
            }

            voxelOccupied = m_registry->evaluateKey(keyAttributeValue);
        }

        if (voxelOccupied) {
            glm::vec3 voxelWorldMin = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;
            glm::vec3 voxelWorldMax = voxelWorldMin + glm::vec3(brickVoxelSize);

            glm::vec3 t0, t1;
            for (int i = 0; i < 3; i++) {
                if (std::abs(rayDir[i]) < 1e-8f) {
                    if (rayOrigin[i] < voxelWorldMin[i] || rayOrigin[i] > voxelWorldMax[i]) {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    } else {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    }
                } else {
                    t0[i] = (voxelWorldMin[i] - rayOrigin[i]) / rayDir[i];
                    t1[i] = (voxelWorldMax[i] - rayOrigin[i]) / rayDir[i];
                }
            }
            glm::vec3 tNearHit = glm::min(t0, t1);

            float hitT = glm::max(glm::max(tNearHit.x, tNearHit.y), tNearHit.z);

            glm::vec3 entryNormal;
            if (tNearHit.x >= tNearHit.y && tNearHit.x >= tNearHit.z) {
                entryNormal = glm::vec3(rayDir.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
            } else if (tNearHit.y >= tNearHit.z) {
                entryNormal = glm::vec3(0.0f, rayDir.y > 0.0f ? -1.0f : 1.0f, 0.0f);
            } else {
                entryNormal = glm::vec3(0.0f, 0.0f, rayDir.z > 0.0f ? -1.0f : 1.0f);
            }

            ISVOStructure::RayHit hit;
            hit.hit = true;
            hit.tMin = hitT;
            hit.tMax = hitT + brickVoxelSize;
            hit.hitPoint = rayOrigin + rayDir * hitT;
            hit.scale = m_maxLevels - 1;
            hit.normal = entryNormal;
            hit.entity = gaia::ecs::Entity();

            return hit;
        }

        // 7. Advance to next voxel
        if (tNext.x < tNext.y && tNext.x < tNext.z) {
            if (tNext.x > tMax) return std::nullopt;
            currentVoxel.x += step.x;
            tNext.x += tDelta.x;
        } else if (tNext.y < tNext.z) {
            if (tNext.y > tMax) return std::nullopt;
            currentVoxel.y += step.y;
            tNext.y += tDelta.y;
        } else {
            if (tNext.z > tMax) return std::nullopt;
            currentVoxel.z += step.z;
            tNext.z += tDelta.z;
        }
    }

    return std::nullopt;
}

// ============================================================================
// EntityBrickView-based DDA Traversal
// ============================================================================

std::optional<ISVOStructure::RayHit> LaineKarrasOctree::traverseBrickView(
    const EntityBrickView& brickView,
    const glm::vec3& brickWorldMin,
    float brickVoxelSize,
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDir,
    float tMin,
    float tMax) const
{
    const int brickN = 1 << brickView.getDepth();

    // 1. Compute ray entry point
    const glm::vec3 entryPoint = rayOrigin + rayDir * tMin;

    // 2. Transform to brick-local [0, N]^3 space
    const glm::vec3 localEntry = (entryPoint - brickWorldMin) / brickVoxelSize;

    // 3. Initialize current voxel (integer coordinates)
    glm::ivec3 currentVoxel{
        static_cast<int>(std::floor(localEntry.x)),
        static_cast<int>(std::floor(localEntry.y)),
        static_cast<int>(std::floor(localEntry.z))
    };

    currentVoxel = glm::clamp(currentVoxel, glm::ivec3(0), glm::ivec3(brickN - 1));

    // 4. Compute DDA step directions and tDelta
    glm::ivec3 step;
    glm::vec3 tDelta;
    glm::vec3 tNext;

    constexpr float epsilon = 1e-8f;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(rayDir[axis]) < epsilon) {
            step[axis] = 0;
            tDelta[axis] = std::numeric_limits<float>::max();
            tNext[axis] = std::numeric_limits<float>::max();
        } else {
            step[axis] = (rayDir[axis] > 0.0f) ? 1 : -1;
            tDelta[axis] = brickVoxelSize / std::abs(rayDir[axis]);

            if (rayDir[axis] > 0.0f) {
                const float nextBoundaryWorld = brickWorldMin[axis] + (currentVoxel[axis] + 1) * brickVoxelSize;
                const float distToNextBoundary = nextBoundaryWorld - entryPoint[axis];
                tNext[axis] = tMin + distToNextBoundary / rayDir[axis];
            } else {
                const float nextBoundaryWorld = brickWorldMin[axis] + currentVoxel[axis] * brickVoxelSize;
                const float distToNextBoundary = entryPoint[axis] - nextBoundaryWorld;
                tNext[axis] = tMin + distToNextBoundary / std::abs(rayDir[axis]);
            }
        }
    }

    // 5. DDA march through brick voxels
    int maxSteps = brickN * 3;
    int stepCount = 0;

    while (stepCount < maxSteps) {
        ++stepCount;

        if (currentVoxel.x < 0 || currentVoxel.x >= brickN ||
            currentVoxel.y < 0 || currentVoxel.y >= brickN ||
            currentVoxel.z < 0 || currentVoxel.z >= brickN) {
            return std::nullopt;
        }

        // 6. Query entity at voxel position via EntityBrickView
        gaia::ecs::Entity entity = brickView.getEntity(currentVoxel.x, currentVoxel.y, currentVoxel.z);
        glm::vec3 voxelWorldPos = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;

        bool voxelOccupied = false;
        if (m_voxelWorld != nullptr) {
            using namespace GaiaVoxel;
            auto density = m_voxelWorld->getComponentValue<Density>(entity);
            if (density.has_value() && *density > 0.0f) {
                voxelOccupied = true;
            }
        }

        if (voxelOccupied) {
            glm::vec3 voxelWorldMin = brickWorldMin + glm::vec3(currentVoxel) * brickVoxelSize;
            glm::vec3 voxelWorldMax = voxelWorldMin + glm::vec3(brickVoxelSize);

            glm::vec3 t0, t1;
            for (int i = 0; i < 3; i++) {
                if (std::abs(rayDir[i]) < 1e-8f) {
                    if (rayOrigin[i] < voxelWorldMin[i] || rayOrigin[i] > voxelWorldMax[i]) {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    } else {
                        t0[i] = -std::numeric_limits<float>::infinity();
                        t1[i] = std::numeric_limits<float>::infinity();
                    }
                } else {
                    t0[i] = (voxelWorldMin[i] - rayOrigin[i]) / rayDir[i];
                    t1[i] = (voxelWorldMax[i] - rayOrigin[i]) / rayDir[i];
                }
            }
            glm::vec3 tNearHit = glm::min(t0, t1);
            glm::vec3 tFarHit = glm::max(t0, t1);

            float hitT = glm::max(glm::max(tNearHit.x, tNearHit.y), tNearHit.z);
            hitT = std::max(hitT, 0.0f);

            glm::vec3 entryNormal;
            if (tNearHit.x >= tNearHit.y && tNearHit.x >= tNearHit.z) {
                entryNormal = glm::vec3(rayDir.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
            } else if (tNearHit.y >= tNearHit.z) {
                entryNormal = glm::vec3(0.0f, rayDir.y > 0.0f ? -1.0f : 1.0f, 0.0f);
            } else {
                entryNormal = glm::vec3(0.0f, 0.0f, rayDir.z > 0.0f ? -1.0f : 1.0f);
            }

            ISVOStructure::RayHit hit;
            hit.hit = true;
            hit.tMin = hitT;
            hit.tMax = hitT + brickVoxelSize;
            hit.hitPoint = rayOrigin + rayDir * hitT;
            hit.scale = m_maxLevels - 1;
            hit.normal = entryNormal;
            hit.entity = entity;

            return hit;
        }

        // 7. Advance to next voxel
        if (tNext.x < tNext.y && tNext.x < tNext.z) {
            if (tNext.x > tMax) return std::nullopt;
            currentVoxel.x += step.x;
            tNext.x += tDelta.x;
        } else if (tNext.y < tNext.z) {
            if (tNext.y > tMax) return std::nullopt;
            currentVoxel.y += step.y;
            tNext.y += tDelta.y;
        } else {
            if (tNext.z > tMax) return std::nullopt;
            currentVoxel.z += step.z;
            tNext.z += tDelta.z;
        }
    }

    return std::nullopt;
}

} // namespace Vixen::SVO
