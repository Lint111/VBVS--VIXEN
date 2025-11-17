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
 */
struct CameraData {
    // Camera fields (for ray generation push constants)
    glm::vec3 cameraPos;         // Offset 0, 12 bytes
    float fov;                   // Offset 12, 4 bytes
    glm::vec3 cameraDir;         // Offset 16, 12 bytes
    float aspect;                // Offset 28, 4 bytes
    glm::vec3 cameraUp;          // Offset 32, 12 bytes
    float lodBias;               // Offset 44, 4 bytes
    glm::vec3 cameraRight;       // Offset 48, 12 bytes
    uint32_t gridResolution;     // Offset 60, 4 bytes

    // Matrix fields (for uniform buffers)
    glm::mat4 invProjection;     // Offset 64, 64 bytes
    glm::mat4 invView;           // Offset 128, 64 bytes
};

} // namespace Vixen::RenderGraph
