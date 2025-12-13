// ============================================================================
// Lighting.glsl - Simple Shading Functions
// ============================================================================
// Basic lighting calculations for voxel rendering.
// Can be extended with PBR or other shading models in the future.
// ============================================================================

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// ============================================================================
// SIMPLE LAMBERTIAN + AMBIENT LIGHTING
// ============================================================================

// Compute basic lighting with ambient and diffuse components
// color: Base surface color
// normal: Surface normal (normalized)
// rayDir: View ray direction (for potential specular, unused here)
vec3 computeLighting(vec3 color, vec3 normal, vec3 rayDir) {
    // Fixed directional light from upper-right-front
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // Ambient and diffuse coefficients
    float ambient = 0.3;
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.7;

    return color * (ambient + diffuse);
}

// Alternative shading with configurable light direction
vec3 computeLightingWithDir(vec3 color, vec3 normal, vec3 lightDir, float ambientStrength) {
    float ambient = ambientStrength;
    float diffuse = max(dot(normal, normalize(lightDir)), 0.0) * (1.0 - ambientStrength);
    return color * (ambient + diffuse);
}

// Simple flat shading (no lighting, just the color)
vec3 flatShading(vec3 color) {
    return color;
}

// Normal-based debug shading (maps normal components to RGB)
vec3 normalShading(vec3 normal) {
    return normal * 0.5 + 0.5;  // Map [-1,1] to [0,1]
}

#endif // LIGHTING_GLSL
