// AR#35 — CPU picking: pure unit tests for ComputePickRay (screen pixel -> world ray).
//
// These tests are PURE: no Vulkan device, no render graph. They build a CameraData
// exactly the way CameraNode::UpdateCameraData does (glm::perspective + Vulkan Y-flip,
// glm::lookAt, then glm::inverse) and assert that ComputePickRay unprojects pixels into
// the correct world-space directions.
//
// The load-bearing detail under test is the NDC Y convention: because CameraNode applies
// an extra `projection[1][1] *= -1` Vulkan Y-flip before inverting, the screen->NDC map
// must be `ndc.y = 2*pixelY/H - 1` (top-left-origin pixels map straight through). A naive
// `1 - 2*pixelY/H` would send the vertical ray the wrong way; the up/down sign tests below
// would fail and catch that regression.

// CRITICAL: match the engine's GLM configuration BEFORE including glm, so the glm::perspective
// we build here is bit-for-bit the matrix CameraNode produces (Vulkan [0,1] depth, radians).
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Data/PickRay.h"
#include "Data/CameraData.h"

using Vixen::RenderGraph::CameraData;
using Vixen::RenderGraph::ComputePickRay;
using Vixen::RenderGraph::PickRay;

namespace {

// Build a CameraData the SAME way CameraNode::UpdateCameraData does:
//   projection = glm::perspective(radians(fov), aspect, near, far); projection[1][1] *= -1;
//   view       = glm::lookAt(eye, target, up);
//   invProjection = inverse(projection); invView = inverse(view);
// Forward = normalize(target - eye); right/up derived the same way as the node.
CameraData MakeCamera(const glm::vec3& eye,
                      const glm::vec3& target,
                      float fovDeg = 45.0f,
                      float aspect = 16.0f / 9.0f,
                      float nearP = 0.1f,
                      float farP = 1000.0f) {
    glm::mat4 projection = glm::perspective(glm::radians(fovDeg), aspect, nearP, farP);
    projection[1][1] *= -1.0f;  // Vulkan Y-flip (mirrors CameraNode)

    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 forward = glm::normalize(target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    CameraData cam{};
    cam.cameraPos = eye;
    cam.cameraDir = forward;
    cam.cameraUp = up;
    cam.cameraRight = right;
    cam.fov = fovDeg;
    cam.aspect = aspect;
    cam.invProjection = glm::inverse(projection);
    cam.invView = glm::inverse(view);
    return cam;
}

constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;

} // namespace

// ---- Center pixel shoots straight down the camera's forward axis -------------

TEST(PickRay, CenterPixelMatchesCameraDir) {
    // Camera at origin looking down -Z (the canonical GL/Vulkan forward).
    CameraData cam = MakeCamera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    PickRay ray = ComputePickRay(cam, kW * 0.5f, kH * 0.5f, kW, kH);

    // Origin is exactly the camera position.
    EXPECT_NEAR(ray.origin.x, cam.cameraPos.x, 1e-5f);
    EXPECT_NEAR(ray.origin.y, cam.cameraPos.y, 1e-5f);
    EXPECT_NEAR(ray.origin.z, cam.cameraPos.z, 1e-5f);

    // Direction is (essentially) the camera forward.
    const float d = glm::dot(ray.direction, cam.cameraDir);
    EXPECT_NEAR(d, 1.0f, 1e-4f);
}

TEST(PickRay, CenterPixelMatchesCameraDirOffsetEyeAndAngle) {
    // A non-trivial pose: off-origin eye looking at a non-axis-aligned target.
    CameraData cam = MakeCamera(glm::vec3(5.0f, 8.0f, 12.0f), glm::vec3(0.0f, 0.0f, 0.0f));

    PickRay ray = ComputePickRay(cam, kW * 0.5f, kH * 0.5f, kW, kH);

    EXPECT_NEAR(ray.origin.x, cam.cameraPos.x, 1e-4f);
    EXPECT_NEAR(ray.origin.y, cam.cameraPos.y, 1e-4f);
    EXPECT_NEAR(ray.origin.z, cam.cameraPos.z, 1e-4f);

    const float d = glm::dot(ray.direction, cam.cameraDir);
    EXPECT_NEAR(d, 1.0f, 1e-4f);
}

// ---- Horizontal offset: pixel to the right leans toward +cameraRight ---------

TEST(PickRay, RightPixelLeansRight) {
    CameraData cam = MakeCamera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    PickRay center = ComputePickRay(cam, kW * 0.5f, kH * 0.5f, kW, kH);
    PickRay rightP = ComputePickRay(cam, kW * 0.75f, kH * 0.5f, kW, kH);
    PickRay leftP = ComputePickRay(cam, kW * 0.25f, kH * 0.5f, kW, kH);

    // A pixel right of center must project more along +cameraRight than the center ray,
    // and the left pixel less (more along -cameraRight).
    EXPECT_GT(glm::dot(rightP.direction, cam.cameraRight), glm::dot(center.direction, cam.cameraRight));
    EXPECT_LT(glm::dot(leftP.direction, cam.cameraRight), glm::dot(center.direction, cam.cameraRight));

    // Right and left are mirror images about center on the horizontal axis.
    EXPECT_GT(glm::dot(rightP.direction, cam.cameraRight), 0.0f);
    EXPECT_LT(glm::dot(leftP.direction, cam.cameraRight), 0.0f);
}

// ---- Vertical offset: this is the Y-sign regression guard --------------------

TEST(PickRay, TopPixelLeansUp) {
    CameraData cam = MakeCamera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

    PickRay center = ComputePickRay(cam, kW * 0.5f, kH * 0.5f, kW, kH);
    // Top of screen = small pixelY (top-left origin).
    PickRay topP = ComputePickRay(cam, kW * 0.5f, kH * 0.25f, kW, kH);
    // Bottom of screen = large pixelY.
    PickRay bottomP = ComputePickRay(cam, kW * 0.5f, kH * 0.75f, kW, kH);

    // A pixel near the TOP of the screen must project UPWARD (more along +cameraUp).
    // If the NDC.y sign were wrong, this is the assertion that fails.
    EXPECT_GT(glm::dot(topP.direction, cam.cameraUp), glm::dot(center.direction, cam.cameraUp));
    EXPECT_LT(glm::dot(bottomP.direction, cam.cameraUp), glm::dot(center.direction, cam.cameraUp));

    EXPECT_GT(glm::dot(topP.direction, cam.cameraUp), 0.0f);
    EXPECT_LT(glm::dot(bottomP.direction, cam.cameraUp), 0.0f);
}

// ---- All rays are unit length ------------------------------------------------

TEST(PickRay, DirectionIsNormalized) {
    CameraData cam = MakeCamera(glm::vec3(2.0f, 3.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f));

    for (float px : {0.0f, kW * 0.5f, kW}) {
        for (float py : {0.0f, kH * 0.5f, kH}) {
            PickRay ray = ComputePickRay(cam, px, py, kW, kH);
            EXPECT_NEAR(glm::length(ray.direction), 1.0f, 1e-4f) << "px=" << px << " py=" << py;
        }
    }
}
