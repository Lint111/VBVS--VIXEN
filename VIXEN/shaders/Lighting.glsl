// ============================================================================
// Lighting.glsl - Shading Functions
// ============================================================================
// Lighting calculations for voxel rendering: Lambert + GGX microfacet BRDF
// (see Brdf.glsl). Two families of computeLighting overloads:
//   - (color, normal, rayDir[, roughness]): hardcoded single directional
//     light, kept for VoxelRayMarch.comp / VoxelRayMarch_Compressed.comp
//     (legacy consumers that never wired LightingConfig).
//   - (color, normal, rayDir, roughness, LightingConfig): data-driven, reads
//     the light list from a LightingConfig record (Generated/LightingConfig.glsl).
//     BodyInstanceRayMarch.comp uses this overload as of Sampled Lighting
//     Inc0 M3. Default LightingConfig content (see LightingConfigNode)
//     reproduces the hardcoded overload's exact light byte-for-byte, so the
//     data path is a zero-visual-delta plumbing change.
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

// Data-driven overload: shades against every light in a LightingConfig record
// (kind 0 = directional: direction_or_position is a normalized direction away
// from the surface toward the light, matching the hardcoded overload's
// convention; kind 1 = point, unused by any content this increment). No
// shadowing (Inc1). ambientIntensity replaces the hardcoded overload's fixed
// 0.3; each light's radiance is summed, matching a single directional light's
// output exactly when lightCount == 1.
vec3 computeLighting(vec3 color, vec3 normal, vec3 rayDir, float roughness, LightingConfig lighting) {
    vec3 viewDir = normalize(-rayDir);

    vec3 Lo = lighting.ambientIntensity * color;
    for (uint i = 0u; i < lighting.lightCount; ++i) {
        Light light = lighting.lights[i];
        vec3 lightDir = normalize(light.direction_or_position);
        Lo += evalBRDF(color, roughness, normal, viewDir, lightDir) * light.radiance;
    }
    return Lo;
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
