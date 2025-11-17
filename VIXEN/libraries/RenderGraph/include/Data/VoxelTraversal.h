#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace VIXEN {
namespace RenderGraph {

// ============================================================================
// RAY-AABB INTERSECTION UTILITIES
// ============================================================================
//
// Based on:
// - "An Efficient and Robust Ray–Box Intersection Algorithm" (Williams et al., 2005)
// - "A Fast Voxel Traversal Algorithm for Ray Tracing" (Amanatides & Woo, 1987)
//
// Used for octree traversal and voxel ray marching.
// ============================================================================

/**
 * @brief Simple ray structure for traversal
 */
struct Ray {
    glm::vec3 origin;     // Ray origin point
    glm::vec3 direction;  // Ray direction (should be normalized)

    Ray() : origin(0.0f), direction(0.0f, 0.0f, 1.0f) {}
    Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(d) {}

    /**
     * @brief Get point along ray at parametric distance t
     * @param t Distance along ray
     * @return Point at ray.origin + t * ray.direction
     */
    glm::vec3 At(float t) const {
        return origin + t * direction;
    }
};

/**
 * @brief Axis-Aligned Bounding Box (AABB)
 */
struct AABB {
    glm::vec3 min;  // Minimum corner
    glm::vec3 max;  // Maximum corner

    AABB() : min(0.0f), max(0.0f) {}
    AABB(const glm::vec3& minPt, const glm::vec3& maxPt) : min(minPt), max(maxPt) {}

    /**
     * @brief Get center point of AABB
     */
    glm::vec3 Center() const {
        return (min + max) * 0.5f;
    }

    /**
     * @brief Get extents (half-size) of AABB
     */
    glm::vec3 Extents() const {
        return (max - min) * 0.5f;
    }

    /**
     * @brief Get full size of AABB
     */
    glm::vec3 Size() const {
        return max - min;
    }

    /**
     * @brief Check if point is inside AABB
     */
    bool Contains(const glm::vec3& point) const {
        return (point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y &&
                point.z >= min.z && point.z <= max.z);
    }
};

/**
 * @brief Result of ray-AABB intersection test
 */
struct RayAABBHit {
    bool hit;        // True if ray intersects AABB
    float tEnter;    // Parametric distance where ray enters AABB
    float tExit;     // Parametric distance where ray exits AABB

    RayAABBHit() : hit(false), tEnter(0.0f), tExit(0.0f) {}
};

/**
 * @brief Test ray-AABB intersection using slab method
 *
 * Based on Williams et al. 2005 algorithm:
 * - Compute intersection intervals for each axis (X, Y, Z)
 * - Find overlap of all three intervals
 * - If overlap exists, ray hits AABB
 *
 * @param ray Ray to test
 * @param aabb AABB to test against
 * @return RayAABBHit with hit flag and entry/exit distances
 */
inline RayAABBHit IntersectRayAABB(const Ray& ray, const AABB& aabb) {
    RayAABBHit result;

    // Precompute inverse direction (avoid division in loop)
    const float EPSILON = 1e-8f;
    glm::vec3 invDir = glm::vec3(
        (std::abs(ray.direction.x) < EPSILON) ? 1e8f : 1.0f / ray.direction.x,
        (std::abs(ray.direction.y) < EPSILON) ? 1e8f : 1.0f / ray.direction.y,
        (std::abs(ray.direction.z) < EPSILON) ? 1e8f : 1.0f / ray.direction.z
    );

    // Compute intersection distances for each axis
    glm::vec3 t0 = (aabb.min - ray.origin) * invDir;
    glm::vec3 t1 = (aabb.max - ray.origin) * invDir;

    // Find min/max per axis (account for negative direction)
    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);

    // Find largest tmin (entry point) and smallest tmax (exit point)
    result.tEnter = std::max(std::max(tmin.x, tmin.y), tmin.z);
    result.tExit = std::min(std::min(tmax.x, tmax.y), tmax.z);

    // Check if ray intersects AABB
    // Conditions:
    // 1. tExit >= 0 (AABB is in front of ray origin)
    // 2. tEnter <= tExit (ray passes through AABB)
    result.hit = (result.tExit >= 0.0f) && (result.tEnter <= result.tExit);

    return result;
}

/**
 * @brief Fast ray-AABB intersection test (hit/miss only)
 *
 * Optimized version that only returns true/false without computing distances.
 * Use this when you only need to know IF intersection occurs, not WHERE.
 *
 * @param ray Ray to test
 * @param aabb AABB to test against
 * @return true if ray hits AABB, false otherwise
 */
inline bool IntersectsRayAABB(const Ray& ray, const AABB& aabb) {
    const float EPSILON = 1e-8f;

    // Precompute inverse direction
    glm::vec3 invDir = glm::vec3(
        (std::abs(ray.direction.x) < EPSILON) ? 1e8f : 1.0f / ray.direction.x,
        (std::abs(ray.direction.y) < EPSILON) ? 1e8f : 1.0f / ray.direction.y,
        (std::abs(ray.direction.z) < EPSILON) ? 1e8f : 1.0f / ray.direction.z
    );

    // Compute intersection distances
    glm::vec3 t0 = (aabb.min - ray.origin) * invDir;
    glm::vec3 t1 = (aabb.max - ray.origin) * invDir;

    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);

    float tEnter = std::max(std::max(tmin.x, tmin.y), tmin.z);
    float tExit = std::min(std::min(tmax.x, tmax.y), tmax.z);

    return (tExit >= 0.0f) && (tEnter <= tExit);
}

// ============================================================================
// DDA VOXEL TRAVERSAL UTILITIES
// ============================================================================
//
// Based on:
// - "A Fast Voxel Traversal Algorithm for Ray Tracing" (Amanatides & Woo, 1987)
//
// DDA (Digital Differential Analyzer) efficiently steps through voxels along
// a ray by tracking the parametric distance to the next voxel boundary on each
// axis (tMax), then advancing along the axis with the smallest tMax value.
// ============================================================================

/**
 * @brief DDA traversal state for stepping through voxel grid
 *
 * Maintains all state needed for Amanatides & Woo DDA algorithm.
 */
struct DDAState {
    glm::ivec3 voxelPos;   // Current voxel position (integer grid coords)
    glm::ivec3 step;       // Step direction per axis (+1 or -1)
    glm::vec3 tMax;        // Parametric distance to next voxel boundary per axis
    glm::vec3 tDelta;      // Distance between voxel boundaries per axis
    glm::vec3 rayPos;      // Current ray position (world space)

    /**
     * @brief Step to next voxel along smallest tMax axis
     *
     * This is the core DDA step: advance to the next voxel boundary by
     * stepping along whichever axis has the smallest tMax value.
     */
    void StepToNextVoxel() {
        // Find axis with smallest tMax (closest boundary)
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                // Step along X axis
                voxelPos.x += step.x;
                tMax.x += tDelta.x;
            } else {
                // Step along Z axis
                voxelPos.z += step.z;
                tMax.z += tDelta.z;
            }
        } else {
            if (tMax.y < tMax.z) {
                // Step along Y axis
                voxelPos.y += step.y;
                tMax.y += tDelta.y;
            } else {
                // Step along Z axis
                voxelPos.z += step.z;
                tMax.z += tDelta.z;
            }
        }
    }

    /**
     * @brief Get parametric distance to current voxel entry point
     *
     * Returns the smallest tMax value, which corresponds to the distance
     * along the ray where we entered the current voxel.
     */
    float GetCurrentT() const {
        return std::min(std::min(tMax.x - tDelta.x, tMax.y - tDelta.y), tMax.z - tDelta.z);
    }
};

/**
 * @brief Initialize DDA traversal state for ray through voxel grid
 *
 * Sets up all state needed to step through voxels using DDA algorithm.
 *
 * @param ray Ray to traverse (origin must be inside grid or at entry point)
 * @param gridSize Size of voxel grid (assumes grid spans [0, gridSize] in each axis)
 * @return DDAState initialized for traversal
 */
inline DDAState InitializeDDA(const Ray& ray, uint32_t gridSize) {
    DDAState state;

    const float EPSILON = 1e-6f;

    // Compute ray direction signs and inverse
    glm::vec3 raySign = glm::sign(ray.direction);
    glm::vec3 rayInvDir = glm::vec3(
        (std::abs(ray.direction.x) < EPSILON) ? 1e8f : 1.0f / ray.direction.x,
        (std::abs(ray.direction.y) < EPSILON) ? 1e8f : 1.0f / ray.direction.y,
        (std::abs(ray.direction.z) < EPSILON) ? 1e8f : 1.0f / ray.direction.z
    );

    // Starting voxel (floor of ray origin in grid space)
    state.voxelPos = glm::ivec3(glm::floor(ray.origin));

    // Step direction per axis (+1 or -1)
    state.step = glm::ivec3(raySign);

    // tMax: parametric distance along ray to next voxel boundary per axis
    // For positive ray direction: next boundary is ceil(rayPos)
    // For negative ray direction: next boundary is floor(rayPos)
    glm::vec3 voxelBoundary = glm::vec3(state.voxelPos) + glm::max(glm::vec3(state.step), glm::vec3(0.0f));
    state.tMax = (voxelBoundary - ray.origin) * rayInvDir;

    // Handle zero direction components: set tMax to infinity for axes with no movement
    // This prevents stepping along axes where the ray is parallel to the plane
    if (std::abs(ray.direction.x) < EPSILON) state.tMax.x = std::numeric_limits<float>::max();
    if (std::abs(ray.direction.y) < EPSILON) state.tMax.y = std::numeric_limits<float>::max();
    if (std::abs(ray.direction.z) < EPSILON) state.tMax.z = std::numeric_limits<float>::max();

    // tDelta: distance along ray between voxel boundaries per axis
    state.tDelta = glm::abs(rayInvDir);

    state.rayPos = ray.origin;

    return state;
}

/**
 * @brief Check if voxel position is within grid bounds
 *
 * @param voxelPos Voxel position to test
 * @param gridSize Grid size (assumes grid spans [0, gridSize) in each axis)
 * @return true if voxel is within bounds, false otherwise
 */
inline bool IsVoxelInBounds(const glm::ivec3& voxelPos, uint32_t gridSize) {
    return (voxelPos.x >= 0 && voxelPos.x < static_cast<int>(gridSize) &&
            voxelPos.y >= 0 && voxelPos.y < static_cast<int>(gridSize) &&
            voxelPos.z >= 0 && voxelPos.z < static_cast<int>(gridSize));
}

} // namespace RenderGraph
} // namespace VIXEN
