#pragma once

#include "Data/CameraData.h"
#include "Generated/TileBinView.g.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Vixen::RenderGraph {

// GB1 candidate list capacity per 8x8 tile, shared by the view record and the list buffers.
// 64 bounds the per-tile serial sort in TileBinSortSystem: at 512 the 2,197-instance load
// field took 500-720 ms a frame and lost the device (base-reds #119). A tile offered more
// candidates than this keeps an incomplete list, and the camera march falls back to the
// bounded instance loop for that tile (R861.1).
inline constexpr uint32_t kTileListCapacityLog2 = 6u;

// GB1 (R857): the tile scatter's camera record, in the exact pixel convention of
// the primary rays (CameraRay.glsl getRayDir):
//   ndc.x = 2(px + 0.5)/W - 1,  ndc.y = -(2(py + 0.5)/H - 1)
//   dir   = forward + right * ndc.x * tan(fov/2) * aspect + up * ndc.y * tan(fov/2)
// with fov in DEGREES and pixel rows growing downward. Solving for the pixel of a
// view-space point (x, y, z) gives
//   px + 0.5 = W/2 + (W / (2 tan(fov/2) aspect)) * x / z
//   py + 0.5 = H/2 + (H / (2 tan(fov/2)))        * (-y) / z
// so the record's Y row is the DOWN axis and both focal lengths stay positive,
// which keeps ProjectBound's min/max rectangle construction valid.
inline Vixen::Codegen::Gpu::TileBinView MakeTileBinView(const CameraData& camera, uint32_t width,
                                                       uint32_t height, int32_t instanceCount) {
    Vixen::Codegen::Gpu::TileBinView view{};
    const glm::vec3 forward = glm::normalize(camera.cameraDir);
    const glm::vec3 right = glm::normalize(camera.cameraRight);
    const glm::vec3 down = -glm::normalize(camera.cameraUp);
    view.viewXx = right.x; view.viewXy = right.y; view.viewXz = right.z;
    view.viewXw = -glm::dot(right, camera.cameraPos);
    view.viewYx = down.x; view.viewYy = down.y; view.viewYz = down.z;
    view.viewYw = -glm::dot(down, camera.cameraPos);
    view.viewZx = forward.x; view.viewZy = forward.y; view.viewZz = forward.z;
    view.viewZw = -glm::dot(forward, camera.cameraPos);
    const float tanHalfFov = std::tan(glm::radians(0.5f * camera.fov));
    view.focalX = 0.5f * static_cast<float>(width) / (tanHalfFov * camera.aspect);
    view.focalY = 0.5f * static_cast<float>(height) / tanHalfFov;
    view.centerX = 0.5f * static_cast<float>(width);
    view.centerY = 0.5f * static_cast<float>(height);
    view.nearPlane = 0.001f;
    view.tileSize = 8.0f;
    view.tilesX = static_cast<int32_t>((width + 7u) / 8u);
    view.tilesY = static_cast<int32_t>((height + 7u) / 8u);
    view.listCapacityLog2 = static_cast<int32_t>(kTileListCapacityLog2);
    view.instanceCount = std::max(instanceCount, 0);
    return view;
}

} // namespace Vixen::RenderGraph
