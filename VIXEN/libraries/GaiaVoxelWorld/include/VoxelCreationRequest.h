#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace GaiaVoxel {

/**
 * VoxelCreationRequest - Lightweight voxel creation parameters.
 *
 * Used by VoxelInjectionQueue to batch voxel creation requests.
 * Replaces DynamicVoxelScalar copies in queue (64+ bytes → 32 bytes).
 *
 * Design:
 * - Fixed attribute set (density, color, normal) for common case
 * - Position stored separately as MortonKey in queue
 * - Total size: 32 bytes (4 floats × 3 vec3 + 1 float)
 */
struct VoxelCreationRequest {
    float density = 1.0f;
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);

    // Optional: Material ID (uint32_t) - defaults to 0
    uint32_t materialID = 0;

    // Default constructor
    VoxelCreationRequest() = default;

    // Convenience constructor
    VoxelCreationRequest(float d, const glm::vec3& c, const glm::vec3& n, uint32_t mat = 0)
        : density(d), color(c), normal(n), materialID(mat) {}
};

/**
 * Extended voxel creation with emission.
 * Total size: 48 bytes (adds vec3 + float for emission)
 */
struct VoxelCreationRequestExtended : VoxelCreationRequest {
    glm::vec3 emissionColor = glm::vec3(0.0f);
    float emissionIntensity = 0.0f;

    VoxelCreationRequestExtended() = default;

    VoxelCreationRequestExtended(
        float d,
        const glm::vec3& c,
        const glm::vec3& n,
        const glm::vec3& emissColor,
        float emissIntensity,
        uint32_t mat = 0)
        : VoxelCreationRequest(d, c, n, mat)
        , emissionColor(emissColor)
        , emissionIntensity(emissIntensity) {}
};

} // namespace GaiaVoxel
