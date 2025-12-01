#pragma once

#include <glm/glm.hpp>

namespace Vixen::RenderGraph {

/**
 * @brief Camera data structure for both push constants and uniform buffers
 *
 * Contains camera-related fields that can be used for push constants or UBOs:
 * - Camera position and orientation vectors
 * - Projection parameters (fov, aspect)
 * - Matrix fields for uniform buffers (invProjection, invView)
 *
 * CRITICAL: Field order MUST match shader PushConstants struct in VoxelRayMarch.comp!
 */
struct CameraData {
    // Camera fields (for ray generation push constants)
    // MUST match shader layout exactly!
    glm::vec3 cameraPos;         // Offset 0, 12 bytes
    float time;                  // Offset 12, 4 bytes (was: fov - WRONG ORDER!)
    glm::vec3 cameraDir;         // Offset 16, 12 bytes
    float fov;                   // Offset 28, 4 bytes (was: aspect - WRONG ORDER!)
    glm::vec3 cameraUp;          // Offset 32, 12 bytes
    float aspect;                // Offset 44, 4 bytes (was: lodBias - WRONG ORDER!)
    glm::vec3 cameraRight;       // Offset 48, 12 bytes
    int32_t debugMode;           // Offset 60, 4 bytes (was: gridResolution - WRONG ORDER!)

    // Matrix fields (for uniform buffers - not used in push constants)
    glm::mat4 invProjection;     // Offset 64, 64 bytes
    glm::mat4 invView;           // Offset 128, 64 bytes
};

} // namespace Vixen::RenderGraph
