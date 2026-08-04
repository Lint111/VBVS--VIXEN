#ifndef CAMERA_RAY_GLSL
#define CAMERA_RAY_GLSL
// ============================================================================
// CameraRay.glsl - camera ray construction from the PushConstants block
// ============================================================================
// W1b extraction (wavefront epoch): getRayDir moved VERBATIM out of
// RayGeneration.glsl so non-traversal consumers (SpatialReuseShade's default
// variant via SceneCommon.glsl) can build camera rays without dragging in the
// octreeConfig-dependent coordinate-transform helpers that the rest of
// RayGeneration.glsl carries. RayGeneration.glsl #includes this file, so the
// function still has exactly ONE source. Depends ONLY on the `pc` push block
// (cameraDir/Right/Up, fov, aspect) — no octreeConfig, no scene SSBOs.
// ============================================================================

// Generate ray direction from UV coordinates using perspective projection
// uv: normalized screen coordinates [0,1]^2
// Note: Vulkan has (0,0) at top-left, so we flip Y to match camera convention
vec3 getRayDir(vec2 uv) {
    float tanHalfFov = tan(radians(pc.fov * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;  // Convert to [-1,1]
    ndc.y = -ndc.y;  // Vulkan Y-flip: (0,0) is top-left, we want bottom-left convention

    vec3 rayDir = pc.cameraDir +
                  pc.cameraRight * ndc.x * tanHalfFov * pc.aspect +
                  pc.cameraUp * ndc.y * tanHalfFov;

    return normalize(rayDir);
}

#endif // CAMERA_RAY_GLSL
