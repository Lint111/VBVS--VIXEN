#ifndef SCENE_COMMON_GLSL
#define SCENE_COMMON_GLSL
// ============================================================================
// SceneCommon.glsl - the non-traversal core of the scene interface
// ============================================================================
// W1b extraction (wavefront epoch): the PushConstants block (+ its debug-mode
// vocabulary) and camera-ray construction, moved VERBATIM out of
// SceneBindings.glsl so shading passes that no longer trace (SpatialReuseShade's
// default variant) can consume the camera/push interface WITHOUT the traversal
// machinery (scene SSBOs, TraceWorld include chain). SceneBindings.glsl
// #includes this file at the exact position the block used to occupy, so every
// traversal consumer is unchanged and the block still has ONE source.
// ============================================================================

// Original 48-byte block: cameraPos(12) + time(4) + cameraDir(12) + fov(4) +
//                         cameraUp(12)  + aspect(4) + cameraRight(12) + debugMode(4)
// Added: raySizeCoef(4) + raySizeBias(4) + instanceCount(4) = 60 B total.
// Vulkan minimum push-constant range is 128 B, so we have ample headroom.
//
// raySizeCoef: cone spread per unit distance = 2*tan(fov / screenHeight / 2)
//              Set to 0.0 to disable LOD (full-detail traversal).
// raySizeBias: cone diameter at origin (0.0 for pinhole camera).
// instanceCount: number of valid entries in bodyInstances[].

#define DEBUG_MODE_NORMAL 0
#define DEBUG_MODE_OCTANT 1
#define DEBUG_MODE_DEPTH  2
#define DEBUG_MODE_ITERATIONS 3
#define DEBUG_MODE_T_SPAN 4
#define DEBUG_MODE_NORMALS 5
#define DEBUG_MODE_POSITION 6
#define DEBUG_MODE_BRICKS 7
#define DEBUG_MODE_MATERIALS 8

layout(push_constant) uniform PushConstants {
    vec3  cameraPos;
    float time;
    vec3  cameraDir;
    float fov;
    vec3  cameraUp;
    float aspect;
    vec3  cameraRight;
    int   debugMode;
    float raySizeCoef;    // LOD cone spread (bytes 48-51)
    float raySizeBias;    // LOD cone origin size (bytes 52-55)
    int   instanceCount;  // active body instances  (bytes 56-59)
    ivec2 debugTargetPixel;  // TEMP DEBUG: pixel to force-capture in the ray-trace buffer
                             // regardless of DEBUG_GRID_SPACING (-1,-1 disables); lets the
                             // trace follow the actual click/cursor position instead of a
                             // fixed viewport-center crosshair (bytes 60-67)
    uint  accumFrameCount;  // Sampled Lighting Inc2 M2: consecutive STATIC-camera frame count,
                             // 1-based, reset to 1 by AccumulationConfigNode the instant the
                             // camera moves; drives the accumulate seam's converging-1/N alpha
                             // below (bytes 68-71)
    // Regime-3 (cosmic accumulation) first slice, deep-field-mip-policy design doc: the
    // K threshold ("footprint >= K*cell") that promotes a ray from a single mip-hit
    // commit to a transmittance-accumulation walk. Push constant (not baked) so tests
    // can sweep it later; VIXEN_REGIME3-gated on the shader side, so this field is inert
    // (read but never branched on) when the flag is off. Default wired to 4.0 by the
    // CPU side when the flag is unset -- irrelevant since the flag guards every use.
    float cosmicK;          // regime-3 threshold multiplier (bytes 72-75)
} pc;

// Camera-ray construction (pc-only, no octreeConfig — see the file's header).
#include "CameraRay.glsl"

#endif // SCENE_COMMON_GLSL
