#pragma once

/**
 * SVOLOD.h - Screen-Space LOD Parameters for Adaptive Ray Termination
 * ==============================================================================
 * Based on Laine & Karras (2010) "Efficient Sparse Voxel Octrees" Section 4.4.
 *
 * REFERENCES:
 * -----------
 * [1] Laine, S. and Karras, T. "Efficient Sparse Voxel Octrees"
 *     NVIDIA Research, I3D 2010, Section 4.4 "Level-of-detail"
 *
 * [2] NVIDIA ESVO Reference Implementation (BSD 3-Clause License)
 *     cuda/Raycast.inl line 181
 *     Copyright (c) 2009-2011, NVIDIA Corporation
 *
 * ALGORITHM:
 * ----------
 * The LOD termination uses ray cone tracing to determine when voxels project
 * to less than one pixel on screen. Given a ray cone:
 *
 *   projectedSize = distance * rayDirSize + rayOrigSize
 *
 * Where:
 *   - rayDirSize: Cone diameter growth per unit distance (tan(pixelAngle))
 *   - rayOrigSize: Cone diameter at origin (0 for pinhole camera)
 *   - distance: Distance from ray origin to current voxel (tc_max in ESVO)
 *
 * When projectedSize >= voxelSize, the voxel is smaller than a pixel, and
 * traversal can terminate at the current LOD level.
 *
 * ESVO CUDA Reference (Raycast.inl line 181):
 *   if (tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2)
 *       break;  // Terminate if voxel projects to < 1 pixel
 *
 * ==============================================================================
 */

#include <glm/glm.hpp>
#include <cmath>

namespace Vixen::SVO {

/**
 * Screen-space LOD parameters for adaptive ray termination.
 *
 * Used with castRayLOD() to enable screen-space adaptive detail.
 * Terminates traversal when voxel projects to less than one pixel.
 */
struct LODParameters {
    float rayOrigSize;  // Ray cone diameter at origin (world space pixel size)
    float rayDirSize;   // Ray cone growth per unit distance (tan(pixelAngle))

    /**
     * Default constructor - disables LOD termination (infinite detail).
     */
    LODParameters()
        : rayOrigSize(0.0f)
        , rayDirSize(0.0f)
    {}

    /**
     * Construct with explicit parameters.
     *
     * @param origSize Ray cone diameter at origin
     * @param dirSize Ray cone growth per unit distance
     */
    LODParameters(float origSize, float dirSize)
        : rayOrigSize(origSize)
        , rayDirSize(dirSize)
    {}

    /**
     * Compute ray cone parameters from camera FOV and resolution.
     *
     * For a pinhole camera model, the ray cone starts at zero diameter
     * and expands based on the pixel angular size.
     *
     * @param fovY Field of view in radians (vertical)
     * @param screenHeight Resolution in pixels (vertical)
     * @return LOD parameters for all rays from this camera
     */
    [[nodiscard]] static LODParameters fromCamera(float fovY, int screenHeight) {
        // Pixel angle = FOV / screen height
        float pixelAngle = fovY / static_cast<float>(screenHeight);

        LODParameters params;
        params.rayOrigSize = 0.0f;  // Pinhole camera: zero diameter at origin
        params.rayDirSize = 2.0f * std::tanf(pixelAngle * 0.5f);  // Cone spread

        return params;
    }

    /**
     * Compute ray cone parameters from camera FOV, resolution, and near plane.
     *
     * For cameras with a finite near plane, the ray cone has non-zero diameter
     * at the origin based on pixel size at the near plane.
     *
     * @param fovY Field of view in radians (vertical)
     * @param screenHeight Resolution in pixels (vertical)
     * @param nearPlane Distance to near plane in world units
     * @return LOD parameters for all rays from this camera
     */
    [[nodiscard]] static LODParameters fromCameraWithNearPlane(
        float fovY,
        int screenHeight,
        float nearPlane)
    {
        float pixelAngle = fovY / static_cast<float>(screenHeight);
        float dirSize = 2.0f * std::tanf(pixelAngle * 0.5f);

        LODParameters params;
        params.rayDirSize = dirSize;
        // Pixel size at near plane
        params.rayOrigSize = nearPlane * dirSize;

        return params;
    }

    /**
     * Compute projected voxel size at given distance.
     *
     * This is the core ray cone calculation from ESVO.
     *
     * @param voxelDistance Distance from ray origin to voxel center (tc_max)
     * @return Projected size of pixel at this distance (ray cone diameter)
     */
    [[nodiscard]] float getProjectedPixelSize(float voxelDistance) const {
        return voxelDistance * rayDirSize + rayOrigSize;
    }

    /**
     * Check if voxel should terminate (projects to < 1 pixel).
     *
     * Core ESVO LOD termination condition from Raycast.inl line 181:
     *   if (tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2) break;
     *
     * @param voxelDistance Distance from ray origin to voxel (tc_max)
     * @param voxelSize World-space size of voxel (scale_exp2)
     * @return true if voxel is smaller than pixel footprint (should terminate)
     */
    [[nodiscard]] bool shouldTerminate(float voxelDistance, float voxelSize) const {
        float projectedSize = getProjectedPixelSize(voxelDistance);
        return projectedSize >= voxelSize;
    }

    /**
     * Check if LOD is enabled (non-zero cone spread).
     *
     * When rayDirSize is zero, LOD termination is effectively disabled
     * and traversal will descend to maximum detail.
     *
     * @return true if LOD termination is enabled
     */
    [[nodiscard]] bool isEnabled() const {
        return rayDirSize > 0.0f;
    }

    /**
     * Compute LOD bias adjustment.
     *
     * Positive bias = coarser LOD (terminate earlier)
     * Negative bias = finer LOD (terminate later)
     *
     * @param bias LOD bias factor (1.0 = double termination threshold)
     * @return New LODParameters with adjusted threshold
     */
    [[nodiscard]] LODParameters withBias(float bias) const {
        LODParameters biased = *this;
        // Bias multiplies the cone spread - larger spread = earlier termination
        float multiplier = std::powf(2.0f, bias);
        biased.rayDirSize *= multiplier;
        biased.rayOrigSize *= multiplier;
        return biased;
    }
};

/**
 * Helper to compute world-space voxel size from ESVO scale.
 *
 * ESVO uses normalized [1,2] space with scale values 0-22.
 * scale_exp2 = 2^(scale - 23) gives voxel size in normalized space.
 *
 * @param esvoScale ESVO scale level (0-22)
 * @param worldSize World-space octree dimension
 * @return World-space voxel size at this scale
 */
[[nodiscard]] inline float esvoScaleToWorldSize(int esvoScale, float worldSize) {
    // ESVO: scale_exp2 = 2^(scale - 23) in [1,2] normalized space
    // World size = scale_exp2 * worldSize (since [1,2] maps to world bounds)
    constexpr int ESVO_MAX_SCALE = 22;
    float normalizedSize = std::ldexpf(1.0f, esvoScale - ESVO_MAX_SCALE - 1);
    return normalizedSize * worldSize;
}

/**
 * Helper to compute world-space distance from ESVO parametric t-value.
 *
 * ESVO t-values are in normalized [0,1] space relative to octree traversal.
 * Actual world distance depends on the ray length through the octree.
 *
 * @param t ESVO parametric t-value
 * @param worldRayLength Length of ray through world bounds
 * @return World-space distance corresponding to t
 */
[[nodiscard]] inline float esvoTToWorldDistance(float t, float worldRayLength) {
    return t * worldRayLength;
}

} // namespace Vixen::SVO
