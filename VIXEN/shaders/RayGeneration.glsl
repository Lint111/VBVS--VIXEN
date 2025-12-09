// ============================================================================
// RayGeneration.glsl - Unified Ray Setup and World-Space Transformations
// ============================================================================
// Common ray generation functions for compute and fragment shaders.
// Handles camera ray construction and world/local space transformations.
//
// Dependencies:
//   - octreeConfig UBO (worldToLocal, localToWorld matrices)
//   - PushConstants (cameraPos, cameraDir, cameraUp, cameraRight, fov, aspect)
// ============================================================================

#ifndef RAY_GENERATION_GLSL
#define RAY_GENERATION_GLSL

// ============================================================================
// COORDINATE SPACE TRANSFORMATIONS
// ============================================================================

// Transform world position to ESVO normalized [1,2]^3 space
vec3 worldToNormalized(vec3 worldPos) {
    vec4 localPos = octreeConfig.worldToLocal * vec4(worldPos, 1.0);
    vec3 p = localPos.xyz / localPos.w;
    return p + 1.0;  // Shift from [0,1] to [1,2]
}

// Transform ESVO normalized [1,2]^3 position back to world space
vec3 normalizedToWorld(vec3 normPos) {
    vec3 localPos = normPos - 1.0;  // Shift from [1,2] to [0,1]
    vec4 worldPos = octreeConfig.localToWorld * vec4(localPos, 1.0);
    return worldPos.xyz / worldPos.w;
}

// ============================================================================
// SCALE MAPPING FUNCTIONS
// ============================================================================

// Convert user scale (0 = finest, maxLevels-1 = root) to ESVO internal scale
// For depth 7: userScales [0-6] map to esvoScales [16-22]
int userToESVOScale(int userScale) {
    return octreeConfig.esvoMaxScale - (octreeConfig.userMaxLevels - 1 - userScale);
}

// Convert ESVO internal scale back to user scale
int esvoToUserScale(int esvoScale) {
    return esvoScale - (octreeConfig.esvoMaxScale - octreeConfig.userMaxLevels + 1);
}

// Get the ESVO scale at which nodes represent brick parents
// At this scale, each octant represents a 2x2x2 region of bricks
int getBrickESVOScale() {
    return octreeConfig.brickESVOScale;
}

// ============================================================================
// RAY GENERATION
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

// ============================================================================
// RAY-AABB INTERSECTION
// ============================================================================

// Compute ray-AABB intersection using slab method
// Returns vec2(tNear, tFar) - entry and exit distances along ray
// If tFar < 0, ray misses the box entirely
// If tNear < 0, ray starts inside the box
vec2 rayAABBIntersection(vec3 rayOrigin, vec3 rayDir, vec3 boxMin, vec3 boxMax) {
    vec3 invDir = 1.0 / rayDir;
    vec3 t0 = (boxMin - rayOrigin) * invDir;
    vec3 t1 = (boxMax - rayOrigin) * invDir;

    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);

    return vec2(tNear, tFar);
}

#endif // RAY_GENERATION_GLSL
