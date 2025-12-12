#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

// ============================================================================
// VoxelRT.rchit - Closest Hit Shader (Phase K: Hardware Ray Tracing)
// ============================================================================
// Called when a ray hits the closest voxel AABB.
// Computes shading based on hit normal and material.
//
// MATERIAL ACCESS: Uses materialIdBuffer indexed by gl_PrimitiveID.
// This is equivalent to compute shader's brickData access - both ultimately
// retrieve the same material ID, just through different data structures.
// ============================================================================

#include "Materials.glsl"

// Hit attributes from intersection shader
hitAttributeEXT vec3 hitNormal;

// Ray payload - color output
layout(location = 0) rayPayloadInEXT vec3 hitColor;

// Material ID buffer - indexed by gl_PrimitiveID
// Mirrors VoxelGrid material data in AABB order
layout(binding = 3, set = 0) readonly buffer MaterialIdBuffer {
    uint materialIds[];
} materialIdBuffer;

// OctreeConfigUBO - matches compute shader binding 5
// Used for resolution info and debug modes
layout(std140, binding = 5) uniform OctreeConfigUBO {
    int esvoMaxScale;
    int userMaxLevels;
    int brickDepthLevels;
    int brickSize;
    int minESVOScale;
    int brickESVOScale;
    int bricksPerAxis;
    int _padding1;
    vec3 gridMin;
    float _padding2;
    vec3 gridMax;
    float _padding3;
    mat4 localToWorld;
    mat4 worldToLocal;
} octreeConfig;

// Push constants (same as compute shader)
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
    vec3 cameraDir;
    float fov;
    vec3 cameraUp;
    float aspect;
    vec3 cameraRight;
    int debugMode;
} pc;

// getMaterialColor() is now provided by Materials.glsl

// Lighting computation - matches compute shader (Lighting.glsl)
vec3 computeLighting(vec3 baseColor, vec3 normal, vec3 rayDir) {
    // Simple directional light from upper-right (same as compute)
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // Basic diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Ambient term (matches compute shader)
    float ambient = 0.3;

    // Final color with lighting
    float lighting = ambient + (1.0 - ambient) * NdotL;
    return baseColor * lighting;
}

void main() {
    // Get ray direction (in local voxel space, matches rgen)
    vec3 rayDir = gl_WorldRayDirectionEXT;

    // Normalize the hit normal
    vec3 N = normalize(hitNormal);

    // Get material ID from buffer using primitive ID
    uint matID = materialIdBuffer.materialIds[gl_PrimitiveID];
    vec3 baseColor = getMaterialColor(matID);

    // Apply lighting (matches compute shader)
    hitColor = computeLighting(baseColor, N, rayDir);

    // Debug modes (aligned with compute shader debug modes)
    if (pc.debugMode > 0) {
        vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

        switch (pc.debugMode) {
            case 2:  // DEBUG_MODE_DEPTH
                {
                    float depth = gl_HitTEXT / 100.0;
                    hitColor = vec3(depth, depth * 0.5, 1.0 - depth);
                }
                break;

            case 5:  // DEBUG_MODE_NORMALS
                hitColor = N * 0.5 + 0.5;
                break;

            case 6:  // DEBUG_MODE_POSITION
                hitColor = fract(hitPos);
                break;

            case 8:  // DEBUG_MODE_MATERIALS
                hitColor = vec3(float(matID) / 10.0, float(matID % 3u) / 3.0, float(matID % 5u) / 5.0);
                break;
        }
    }
}
