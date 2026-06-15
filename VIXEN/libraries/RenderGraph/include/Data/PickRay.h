#pragma once

#include <glm/glm.hpp>
#include "Data/CameraData.h"

namespace Vixen::RenderGraph {

/**
 * @brief A world-space picking ray (origin + normalized direction).
 *
 * Produced by ComputePickRay() by unprojecting a screen pixel through a
 * camera's inverse projection/view matrices.
 */
struct PickRay {
    glm::vec3 origin;     // World-space ray origin (the camera position)
    glm::vec3 direction;  // Normalized world-space ray direction
};

/**
 * @brief Unproject a screen pixel into a world-space picking ray.
 *
 * PURE FUNCTION — no Vulkan, no device, no global state. Depends only on the
 * matrices already baked into @p cam (computed once per frame by CameraNode).
 *
 * CONVENTIONS (must match how CameraNode builds the camera — see
 * CameraNode::UpdateCameraData):
 *   - Pixel coordinates are window pixels with a TOP-LEFT origin (the convention
 *     GLFW reports and InputState.mousePosition carries): pixelX grows right,
 *     pixelY grows DOWN.
 *   - The projection matrix CameraNode feeds into invProjection is
 *     glm::perspective(...) with an additional Vulkan Y-flip (projection[1][1] *= -1).
 *     After that flip the engine's clip/NDC +Y axis points DOWN (screen-space).
 *     Therefore the correct screen->NDC mapping is:
 *         ndc.x = 2*pixelX/viewportW - 1     (left pixel  -> -1, right pixel -> +1)
 *         ndc.y = 2*pixelY/viewportH - 1     (top  pixel  -> -1, bottom     -> +1)
 *     i.e. NDC.y uses the SAME sign as the pixel axis precisely because the
 *     projection already flipped Y. (A naive `1 - 2*pixelY/H` would point the
 *     ray vertically backwards in this engine.)
 *   - Depth uses GLM_FORCE_DEPTH_ZERO_TO_ONE (Vulkan [0,1]): the near plane is
 *     ndc.z = 0 and the far plane is ndc.z = 1.
 *
 * The ray origin is the camera position; the direction is the normalized vector
 * from the unprojected near point to the unprojected far point.
 *
 * @param cam        Camera data with valid invProjection, invView and cameraPos.
 * @param pixelX     Cursor X in window pixels (top-left origin).
 * @param pixelY     Cursor Y in window pixels (top-left origin).
 * @param viewportW  Viewport width in pixels (> 0).
 * @param viewportH  Viewport height in pixels (> 0).
 * @return PickRay   World-space origin + normalized direction.
 */
inline PickRay ComputePickRay(const CameraData& cam,
                              float pixelX, float pixelY,
                              float viewportW, float viewportH) {
    // Screen pixel -> normalized device coordinates.
    // NDC.y intentionally shares the sign of the pixel Y axis: the projection
    // baked into cam.invProjection already applied the Vulkan Y-flip, so a
    // top-left-origin pixel maps directly (top -> ndc.y == -1).
    const float ndcX = 2.0f * pixelX / viewportW - 1.0f;
    const float ndcY = 2.0f * pixelY / viewportH - 1.0f;

    // Unproject a near point (z=0) and a far point (z=1) from clip space back to
    // view space via inverse projection (perspective divide), then to world space
    // via inverse view.
    const glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);

    glm::vec4 nearView = cam.invProjection * nearClip;
    glm::vec4 farView = cam.invProjection * farClip;

    // Perspective divide (guard against a zero w just in case).
    nearView /= (nearView.w != 0.0f ? nearView.w : 1.0f);
    farView /= (farView.w != 0.0f ? farView.w : 1.0f);

    const glm::vec3 nearWorld = glm::vec3(cam.invView * nearView);
    const glm::vec3 farWorld = glm::vec3(cam.invView * farView);

    PickRay ray;
    ray.origin = cam.cameraPos;
    ray.direction = glm::normalize(farWorld - nearWorld);
    return ray;
}

} // namespace Vixen::RenderGraph
