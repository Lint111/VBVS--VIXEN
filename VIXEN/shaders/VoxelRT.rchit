#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// VoxelRT.rchit - Closest Hit Shader (Phase K: Hardware Ray Tracing)
// ============================================================================
// Called when a ray hits the closest voxel AABB.
// Computes shading based on hit normal and material.
// ============================================================================

// Hit attributes from intersection shader
hitAttributeEXT vec3 hitNormal;

// Ray payload - color output
layout(location = 0) rayPayloadInEXT vec3 hitColor;

// Push constants
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

void main() {
    // Get ray direction for lighting calculations
    vec3 rayDir = gl_WorldRayDirectionEXT;

    // Normalize the hit normal (in case it wasn't normalized)
    vec3 N = normalize(hitNormal);

    // Simple directional light from upper-right
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // Basic diffuse lighting
    float NdotL = max(dot(N, lightDir), 0.0);

    // Ambient term
    float ambient = 0.3;

    // Base voxel color - use primitive ID for variation
    // gl_PrimitiveID gives us the AABB index
    uint primID = gl_PrimitiveID;

    // Generate color from primitive ID (simple hash for variety)
    vec3 baseColor;
    uint colorSeed = primID * 2654435761u;  // Golden ratio hash

    // Create distinct colors for different voxels
    float r = float((colorSeed >> 0) & 0xFF) / 255.0;
    float g = float((colorSeed >> 8) & 0xFF) / 255.0;
    float b = float((colorSeed >> 16) & 0xFF) / 255.0;

    // Boost saturation and brightness
    baseColor = mix(vec3(0.5), vec3(r, g, b), 0.7);
    baseColor = baseColor * 0.8 + 0.2;

    // Final color with lighting
    float lighting = ambient + (1.0 - ambient) * NdotL;
    hitColor = baseColor * lighting;

    // Debug modes
    if (pc.debugMode == 5) {
        // Normal visualization
        hitColor = N * 0.5 + 0.5;
    } else if (pc.debugMode == 6) {
        // Position visualization
        vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
        hitColor = fract(hitPos);
    } else if (pc.debugMode == 2) {
        // Depth visualization
        float depth = gl_HitTEXT / 100.0;
        hitColor = vec3(depth, depth * 0.5, 1.0 - depth);
    }
}
