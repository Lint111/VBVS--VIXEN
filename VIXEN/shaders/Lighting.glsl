// ============================================================================
// Lighting.glsl - Shading Functions
// ============================================================================
// Lighting calculations for voxel rendering: Lambert + GGX microfacet BRDF
// (see Brdf.glsl) with a single hardcoded directional light. Light-data
// wiring (LightingConfig / light array) lands in a later Sampled Lighting
// Inc0 milestone; the light here stays hardcoded by design for this one.
// ============================================================================

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include "Brdf.glsl"

// ============================================================================
// LAMBERT + GGX PHYSICALLY-BASED LIGHTING
// ============================================================================

// Compute lighting with ambient term + one hardcoded directional light shaded
// via evalBRDF (Lambert diffuse + GGX specular, dielectric F0 = 0.04).
// color    : Base surface color (albedo)
// normal   : Surface normal (normalized)
// rayDir   : View ray direction (normalized, points AWAY from surface toward camera)
// roughness: PBR-style roughness in [0,1]; 0 = mirror-like, 1 = fully diffuse.
vec3 computeLighting(vec3 color, vec3 normal, vec3 rayDir, float roughness) {
    // Fixed directional light from upper-right-front
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // View direction: rayDir points from camera toward surface; negate for lighting math.
    vec3 viewDir = normalize(-rayDir);

    float ambient = 0.3;
    vec3 lightRadiance = vec3(1.0);

    vec3 Lo = ambient * color + evalBRDF(color, roughness, normal, viewDir, lightDir) * lightRadiance;
    return Lo;
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
