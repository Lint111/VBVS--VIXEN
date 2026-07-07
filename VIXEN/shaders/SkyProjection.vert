#version 450
// SkyProjection.vert — Tiered ESVO Observer Addressing, Inc1 M3 (Task 6).
//
// Point-sprite composite draw: one vertex invocation per sky point (gl_VertexIndex indexes
// straight into the SkyPointBuffer SSBO — no vertex buffer bound, mirrors a fullscreen-pass
// idiom already used elsewhere in this codebase (GraphicsPipelineNode's ENABLE_VERTEX_INPUT=
// false path) but with VK_PRIMITIVE_TOPOLOGY_POINT_LIST instead of a single triangle).
//
// Direction -> screen position (design decision, see plan Task 6 / SkyProjectionNode.h):
// each sky point's `direction` is a normalized WORLD-space vector (already composed by M2's
// ComposeLocalDirection — never a flattened world coordinate, just a unit direction). This
// shader treats it as an infinitely-distant point and projects through the camera's ROTATION
// only (dot products against the camera basis vectors), never translation — the standard
// "skybox never parallaxes with camera movement" technique. This mirrors shaders/
// RayGeneration.glsl's own getRayDir() convention EXACTLY (same tan(fov/2)/aspect fold),
// since that is this engine's only existing precedent for turning FOV/aspect into a screen
// basis — there is no projection MATRIX anywhere in the ray-march camera path to reuse
// (CameraData::invProjection/invView are declared but never populated — dead fields, per
// investigation), so this shader intentionally skips a projection matrix entirely, exactly
// as the ray-march path already does.
//
// getRayDir(uv) builds: rayDir = normalize(cameraDir + cameraRight*ndc.x*tanHalfFov*aspect +
//                                           cameraUp*ndc.y*tanHalfFov)
// This is inverted here: given a world direction d,
//   x = dot(d, cameraRight), y = dot(d, cameraUp), z = dot(d, cameraDir)
//   ndc.x = (x/z) / (tanHalfFov * aspect)
//   ndc.y = (y/z) / tanHalfFov
// z <= 0 means the point is behind the camera (gl_Position.w = 0 degenerates the primitive;
// the fragment shader never runs for a clipped/degenerate point).

// Mirrors SkyProjectionNode.h's SkyPointGpu C++ struct byte-for-byte. std430 packing note
// (verified by hand, not assumed — a common gotcha): a vec3 struct MEMBER has base alignment
// 16 but occupies only 12 bytes, so the scalar `magnitude` immediately following it packs
// TIGHTLY at offset 12 (NOT offset 16 — that padding only appears once, at the very end of
// the struct, rounding its overall size up to its base alignment of 16). Layout: direction
// [0..11], magnitude [12..15], appliedDelaySeconds [16..19], _pad [20..27] (unused trailing
// bytes so the struct rounds up to the 32 B std430 array stride).
struct SkyPoint {
    vec3  direction;
    float magnitude;
    float appliedDelaySeconds;
    vec2  _pad;
};

layout(std430, binding = 0) readonly buffer SkyPointBuffer {
    SkyPoint points[];
} skyPoints;

// Camera basis only — no cameraPos: the direction->screen projection below uses the camera's
// ROTATION only (an infinitely-distant point never parallaxes with camera translation), so
// position is deliberately omitted from this push-constant block (kept minimal, not carried
// as dead bytes). Mirrors SkyProjectionNode.cpp's PushConstantLayout struct's field order
// (cameraDir/fov/cameraUp/aspect/cameraRight, each vec3 padded to vec4 for std430-compatible
// packing since vec3 push-constant members otherwise leave the offset ambiguous across compilers).
layout(push_constant) uniform PushConstants {
    vec3  cameraDir;
    float fov;       // vertical FOV, degrees — matches BodyInstanceRayMarch.comp's pc.fov
    vec3  cameraUp;
    float aspect;
    vec3  cameraRight;
    float _padRight;
} pc;

layout(location = 0) out float vMagnitude;

// Sprite size in pixels, scaled by magnitude (brighter/higher magnitude => bigger point).
// Magnitude here is M2's ApparentMagnitude falloff: HIGHER value = brighter/more prominent
// (NOT inverted real-astronomy magnitude — see TierMagnitude.h's doc comment).
const float kMinPointSizePx = 2.0;
const float kMaxPointSizePx = 18.0;

void main() {
    SkyPoint p = skyPoints.points[gl_VertexIndex];

    float tanHalfFov = tan(radians(pc.fov * 0.5));

    vec3 d = normalize(p.direction);
    float x = dot(d, pc.cameraRight);
    float y = dot(d, pc.cameraUp);
    float z = dot(d, pc.cameraDir);

    if (z <= 0.0) {
        // Behind the camera: degenerate the primitive (w=0 clips it before rasterization).
        gl_Position = vec4(0.0, 0.0, 0.0, 0.0);
        gl_PointSize = 0.0;
        vMagnitude = 0.0;
        return;
    }

    float ndcX = (x / z) / (tanHalfFov * pc.aspect);
    float ndcY = (y / z) / tanHalfFov;

    gl_Position = vec4(ndcX, ndcY, 0.5, 1.0);

    // Magnitude -> point size: a simple clamped linear map is sufficient for this milestone's
    // synthetic fixture (a handful of points spanning a known, bounded magnitude range) — no
    // need for a calibrated photometric size curve (see TierMagnitude.h's own "simplicity, not
    // radiometric accuracy" bar).
    float sizeT = clamp(p.magnitude, 0.0, 1.0);
    gl_PointSize = mix(kMinPointSizePx, kMaxPointSizePx, sizeT);

    vMagnitude = p.magnitude;
}
