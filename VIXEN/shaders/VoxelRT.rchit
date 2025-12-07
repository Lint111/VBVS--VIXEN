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

// Material ID buffer - indexed by gl_PrimitiveID
layout(binding = 3, set = 0) readonly buffer MaterialIdBuffer {
    uint materialIds[];
} materialIdBuffer;

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

// Material color lookup - matches compute shader material IDs
vec3 getMaterialColor(uint matID) {
    // Same material colors as VoxelRayMarch.comp
    if (matID == 1u) return vec3(1.0, 0.0, 0.0);      // Red
    else if (matID == 2u) return vec3(0.0, 1.0, 0.0); // Green
    else if (matID == 3u) return vec3(0.9, 0.9, 0.9); // Light gray (white wall)
    else if (matID == 4u) return vec3(1.0, 0.8, 0.0); // Yellow/Gold
    else if (matID == 5u) return vec3(0.8, 0.8, 0.8); // Medium gray
    else if (matID == 6u) return vec3(0.7, 0.7, 0.7); // Darker gray
    else return vec3(float(matID) / 10.0);            // Fallback gradient
}

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

    // Get material ID from buffer using primitive ID
    uint matID = materialIdBuffer.materialIds[gl_PrimitiveID];
    vec3 baseColor = getMaterialColor(matID);

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
    } else if (pc.debugMode == 8) {
        // Material ID visualization
        hitColor = vec3(float(matID) / 10.0, float(matID % 3u) / 3.0, float(matID % 5u) / 5.0);
    }
}
