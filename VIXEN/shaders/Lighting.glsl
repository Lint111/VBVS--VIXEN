// ============================================================================
// Lighting.glsl - Simple Shading Functions
// ============================================================================
// Basic lighting calculations for voxel rendering.
// Can be extended with PBR or other shading models in the future.
// ============================================================================

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// ============================================================================
// SIMPLE LAMBERTIAN + BLINN-PHONG SPECULAR LIGHTING
// ============================================================================

// Compute lighting with ambient, diffuse, and roughness-modulated specular.
// color    : Base surface color
// normal   : Surface normal (normalized)
// rayDir   : View ray direction (normalized, points AWAY from surface toward camera)
// roughness: PBR-style roughness in [0,1]; 0 = mirror-like, 1 = fully diffuse.
//            Blinn-Phong exponent: mix(64.0, 4.0, roughness)
//            Specular scale:       1.0 - roughness
vec3 computeLighting(vec3 color, vec3 normal, vec3 rayDir, float roughness) {
    // Fixed directional light from upper-right-front
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // View direction: rayDir points from camera toward surface; negate for lighting math.
    vec3 viewDir  = normalize(-rayDir);
    vec3 halfVec  = normalize(lightDir + viewDir);

    // Ambient and diffuse
    float ambient = 0.3;
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.7;

    // Blinn-Phong specular: exponent and scale both decrease with roughness.
    float shininess    = mix(64.0, 4.0, roughness);
    float specScale    = (1.0 - roughness) * 0.4;
    float specular     = pow(max(dot(normal, halfVec), 0.0), shininess) * specScale;

    return color * (ambient + diffuse) + vec3(specular);
}

// Backward-compat overload for binary/procedural paths that have no roughness.
// Passes the default roughness (0.5) to the full function so look is unchanged.
vec3 computeLighting(vec3 color, vec3 normal, vec3 rayDir) {
    return computeLighting(color, normal, rayDir, 0.5);
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
