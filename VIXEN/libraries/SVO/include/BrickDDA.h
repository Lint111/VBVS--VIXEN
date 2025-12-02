#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>

namespace Vixen::SVO {

/**
 * Dense 3D-DDA traversal within a single brick.
 *
 * When ESVO tree traversal hits a leaf node (BrickReference), this algorithm
 * performs dense voxel-by-voxel ray marching through the brick's n³ grid.
 *
 * Based on: Amanatides & Woo (1987) "A Fast Voxel Traversal Algorithm"
 *           https://www.researchgate.net/publication/2611491
 *
 * Usage:
 *   BrickDDA dda(rayOrigin, rayDir, brickBounds, brickResolution);
 *   while (dda.hasNext()) {
 *       auto [x, y, z] = dda.getVoxelCoord();
 *       if (brick.get<0>(brickID, brick.getIndex(x, y, z)) > threshold) {
 *           return dda.getCurrentT(); // Hit!
 *       }
 *       dda.step();
 *   }
 */
class BrickDDA {
public:
    /**
     * Initialize brick-level DDA traversal.
     *
     * @param rayOrigin Ray origin in world space
     * @param rayDir Ray direction (normalized)
     * @param brickMin Brick AABB min corner (world space)
     * @param brickMax Brick AABB max corner (world space)
     * @param brickResolution Voxels per side (e.g., 8 for 8³ brick)
     * @param tMin Starting t parameter (entry into brick)
     * @param tMax Ending t parameter (exit from brick)
     */
    BrickDDA(const glm::vec3& rayOrigin,
             const glm::vec3& rayDir,
             const glm::vec3& brickMin,
             const glm::vec3& brickMax,
             int brickResolution,
             float tMin,
             float tMax)
        : m_rayOrigin(rayOrigin)
        , m_rayDir(rayDir)
        , m_brickMin(brickMin)
        , m_brickMax(brickMax)
        , m_resolution(brickResolution)
        , m_tMin(tMin)
        , m_tMax(tMax)
        , m_currentT(tMin)
        , m_active(true)
    {
        // Brick voxel size
        glm::vec3 brickSize = brickMax - brickMin;
        m_voxelSize = brickSize / static_cast<float>(brickResolution);

        // Entry point into brick
        glm::vec3 entryPoint = rayOrigin + rayDir * tMin;

        // Convert to brick-local voxel coordinates
        glm::vec3 localPos = (entryPoint - brickMin) / m_voxelSize;
        m_voxelX = static_cast<int>(glm::floor(localPos.x));
        m_voxelY = static_cast<int>(glm::floor(localPos.y));
        m_voxelZ = static_cast<int>(glm::floor(localPos.z));

        // Clamp to brick bounds (in case of floating point error)
        m_voxelX = glm::clamp(m_voxelX, 0, brickResolution - 1);
        m_voxelY = glm::clamp(m_voxelY, 0, brickResolution - 1);
        m_voxelZ = glm::clamp(m_voxelZ, 0, brickResolution - 1);

        // Compute step direction and t-delta for each axis
        // step: +1 or -1 depending on ray direction
        // tDelta: t-parameter distance to traverse one voxel
        // tNext: t-parameter to next voxel boundary

        for (int axis = 0; axis < 3; ++axis) {
            if (rayDir[axis] > 0.0f) {
                m_step[axis] = 1;
                float nextBoundary = (glm::floor(localPos[axis]) + 1.0f) * m_voxelSize[axis];
                m_tNext[axis] = tMin + (nextBoundary - (entryPoint[axis] - brickMin[axis])) / rayDir[axis];
                m_tDelta[axis] = m_voxelSize[axis] / rayDir[axis];
            } else if (rayDir[axis] < 0.0f) {
                m_step[axis] = -1;
                float nextBoundary = glm::floor(localPos[axis]) * m_voxelSize[axis];
                m_tNext[axis] = tMin + (nextBoundary - (entryPoint[axis] - brickMin[axis])) / rayDir[axis];
                m_tDelta[axis] = -m_voxelSize[axis] / rayDir[axis];
            } else {
                // Ray parallel to axis - will never cross this plane
                m_step[axis] = 0;
                m_tNext[axis] = std::numeric_limits<float>::infinity();
                m_tDelta[axis] = std::numeric_limits<float>::infinity();
            }
        }
    }

    /**
     * Check if traversal has more voxels to visit.
     */
    bool hasNext() const {
        return m_active &&
               m_voxelX >= 0 && m_voxelX < m_resolution &&
               m_voxelY >= 0 && m_voxelY < m_resolution &&
               m_voxelZ >= 0 && m_voxelZ < m_resolution &&
               m_currentT < m_tMax;
    }

    /**
     * Advance to next voxel along ray.
     */
    void step() {
        // Find axis with minimum tNext (closest voxel boundary)
        int axis = 0;
        if (m_tNext.y < m_tNext.x) axis = 1;
        if (m_tNext.z < m_tNext[axis]) axis = 2;

        // Step to next voxel on that axis
        if (axis == 0) {
            m_voxelX += m_step.x;
        } else if (axis == 1) {
            m_voxelY += m_step.y;
        } else {
            m_voxelZ += m_step.z;
        }

        // Update t parameter
        m_currentT = m_tNext[axis];
        m_tNext[axis] += m_tDelta[axis];

        // Check if exited brick
        if (m_voxelX < 0 || m_voxelX >= m_resolution ||
            m_voxelY < 0 || m_voxelY >= m_resolution ||
            m_voxelZ < 0 || m_voxelZ >= m_resolution ||
            m_currentT >= m_tMax) {
            m_active = false;
        }
    }

    /**
     * Get current voxel coordinates (0-based, within brick).
     */
    glm::ivec3 getVoxelCoord() const {
        return glm::ivec3(m_voxelX, m_voxelY, m_voxelZ);
    }

    /**
     * Get current t-parameter along ray.
     */
    float getCurrentT() const {
        return m_currentT;
    }

    /**
     * Get hit position in world space.
     */
    glm::vec3 getHitPosition() const {
        return m_rayOrigin + m_rayDir * m_currentT;
    }

    /**
     * Get normal of last crossed voxel face.
     * Returns the normal of the face that was crossed to enter current voxel.
     */
    glm::vec3 getLastCrossedFaceNormal() const {
        // Determine which axis was crossed most recently
        int axis = 0;
        float minT = m_tNext.x - m_tDelta.x;
        if (m_tNext.y - m_tDelta.y < minT) {
            axis = 1;
            minT = m_tNext.y - m_tDelta.y;
        }
        if (m_tNext.z - m_tDelta.z < minT) {
            axis = 2;
        }

        glm::vec3 normal(0.0f);
        normal[axis] = -static_cast<float>(m_step[axis]);
        return normal;
    }

    /**
     * Skip to specific t-parameter within brick.
     * Useful for continuing traversal after processing a sub-voxel feature.
     */
    void skipTo(float t) {
        if (t <= m_currentT || t >= m_tMax) {
            m_active = false;
            return;
        }

        m_currentT = t;

        // Recompute voxel position at new t
        glm::vec3 pos = m_rayOrigin + m_rayDir * t;
        glm::vec3 localPos = (pos - m_brickMin) / m_voxelSize;

        m_voxelX = static_cast<int>(glm::floor(localPos.x));
        m_voxelY = static_cast<int>(glm::floor(localPos.y));
        m_voxelZ = static_cast<int>(glm::floor(localPos.z));

        // Clamp to brick bounds
        m_voxelX = glm::clamp(m_voxelX, 0, m_resolution - 1);
        m_voxelY = glm::clamp(m_voxelY, 0, m_resolution - 1);
        m_voxelZ = glm::clamp(m_voxelZ, 0, m_resolution - 1);

        // Check if still inside brick
        if (m_voxelX < 0 || m_voxelX >= m_resolution ||
            m_voxelY < 0 || m_voxelY >= m_resolution ||
            m_voxelZ < 0 || m_voxelZ >= m_resolution) {
            m_active = false;
        }
    }

private:
    glm::vec3 m_rayOrigin;
    glm::vec3 m_rayDir;
    glm::vec3 m_brickMin;
    glm::vec3 m_brickMax;
    glm::vec3 m_voxelSize;

    int m_resolution;
    float m_tMin;
    float m_tMax;
    float m_currentT;
    bool m_active;

    // Current voxel coordinates
    int m_voxelX;
    int m_voxelY;
    int m_voxelZ;

    // DDA state
    glm::ivec3 m_step;      // Step direction (+1 or -1) per axis
    glm::vec3 m_tNext;      // t-parameter to next voxel boundary
    glm::vec3 m_tDelta;     // t-parameter increment per voxel
};

} // namespace Vixen::SVO
