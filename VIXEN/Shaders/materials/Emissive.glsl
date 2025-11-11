// Emissive.glsl
// Emissive (light-emitting) material implementation
//
// Materials that emit light without scattering
// Used for light sources in path tracers

#ifndef EMISSIVE_GLSL
#define EMISSIVE_GLSL

#include "MaterialCommon.glsl"

// ============================================================================
// EMISSIVE MATERIAL
// ============================================================================

// Emissive material properties
struct EmissiveMaterial {
    vec3 emit_color;      // Color of emitted light
    float intensity;      // Emission intensity multiplier
};

// Emissive materials don't scatter - they absorb and emit
bool emissive_scatter(
    Ray r_in,
    HitRecord rec,
    vec3 emit_color,
    float intensity,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Ray is absorbed, not scattered
    scatter.scattered = false;
    scatter.attenuation = vec3(0.0);
    scatter.emitted = emit_color * intensity;

    return false;  // Ray terminates here
}

// Get emitted light (main purpose of this material)
vec3 emissive_emitted(EmissiveMaterial mat, vec2 uv, vec3 point) {
    return mat.emit_color * mat.intensity;
}

// Textured emissive (can vary emission based on UV)
vec3 emissive_emitted_textured(
    EmissiveMaterial mat,
    vec2 uv,
    vec3 point,
    sampler2D emission_texture
) {
    vec3 tex_color = texture(emission_texture, uv).rgb;
    return tex_color * mat.emit_color * mat.intensity;
}

// ============================================================================
// COMMON EMISSIVE PRESETS
// ============================================================================

EmissiveMaterial emissive_white_light(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(1.0);
    mat.intensity = intensity;
    return mat;
}

EmissiveMaterial emissive_warm_light(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(1.0, 0.9, 0.7);  // Warm yellowish
    mat.intensity = intensity;
    return mat;
}

EmissiveMaterial emissive_cool_light(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(0.7, 0.9, 1.0);  // Cool bluish
    mat.intensity = intensity;
    return mat;
}

EmissiveMaterial emissive_fire(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(1.0, 0.5, 0.1);  // Orange-red
    mat.intensity = intensity;
    return mat;
}

EmissiveMaterial emissive_neon_blue(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(0.1, 0.5, 1.0);
    mat.intensity = intensity;
    return mat;
}

EmissiveMaterial emissive_neon_pink(float intensity) {
    EmissiveMaterial mat;
    mat.emit_color = vec3(1.0, 0.1, 0.5);
    mat.intensity = intensity;
    return mat;
}

#endif // EMISSIVE_GLSL
