// Metal.glsl
// Metallic (mirror reflection) material implementation
//
// Specular reflection with optional fuzziness for rough metals
// Based on "Ray Tracing in One Weekend" by Peter Shirley

#ifndef METAL_GLSL
#define METAL_GLSL

#include "MaterialCommon.glsl"

// Requires:
// - vec3 reflect_vector(vec3 v, vec3 n)
// - vec3 random_in_unit_sphere(inout uint seed)

// ============================================================================
// METAL MATERIAL
// ============================================================================

// Metal material properties
struct MetalMaterial {
    vec3 albedo;   // Metal color (tint)
    float fuzz;    // Roughness: 0.0 = perfect mirror, 1.0 = very rough
};

// Reflect vector around normal
vec3 reflect_vector(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

// Scatter ray using mirror reflection with fuzziness
//
// Perfect mirror (fuzz = 0.0):
//   - Reflects exactly according to angle of incidence = angle of reflection
//   - Creates sharp, clear reflections
//
// Fuzzy metal (fuzz > 0.0):
//   - Adds random perturbation to reflection direction
//   - Simulates rough/brushed metal surfaces
//   - Higher fuzz = rougher surface, more diffuse reflection
//
// Note: If fuzzy reflection scatters below surface, ray is absorbed
bool metal_scatter(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    float fuzz,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Calculate perfect mirror reflection
    vec3 reflected = reflect_vector(normalize(r_in.direction), rec.normal);

    // Add fuzziness (clamped to [0, 1])
    fuzz = clamp(fuzz, 0.0, 1.0);
    vec3 fuzz_offset = fuzz * random_in_unit_sphere(rng_state);
    vec3 scatter_direction = reflected + fuzz_offset;

    // Check if scattered ray is below surface
    // This happens when fuzziness is high and random offset points inward
    bool above_surface = dot(scatter_direction, rec.normal) > 0.0;

    if (above_surface) {
        scatter.scattered = true;
        scatter.scattered_ray.origin = rec.point;
        scatter.scattered_ray.direction = normalize(scatter_direction);
        scatter.attenuation = albedo;
        scatter.emitted = vec3(0.0);
        return true;
    } else {
        // Ray absorbed by surface
        scatter.scattered = false;
        scatter.attenuation = vec3(0.0);
        scatter.emitted = vec3(0.0);
        return false;
    }
}

// Anisotropic metal scatter (directional roughness)
// Different fuzz amounts along tangent and bitangent
bool metal_scatter_anisotropic(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    float fuzz_u,
    float fuzz_v,
    vec3 tangent,
    vec3 bitangent,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    vec3 reflected = reflect_vector(normalize(r_in.direction), rec.normal);

    // Generate anisotropic fuzz
    float u = random_float(rng_state) * 2.0 - 1.0;
    float v = random_float(rng_state) * 2.0 - 1.0;
    float w = random_float(rng_state) * 2.0 - 1.0;

    vec3 fuzz_offset = fuzz_u * u * tangent +
                       fuzz_v * v * bitangent +
                       min(fuzz_u, fuzz_v) * w * rec.normal;

    vec3 scatter_direction = reflected + fuzz_offset;

    bool above_surface = dot(scatter_direction, rec.normal) > 0.0;

    if (above_surface) {
        scatter.scattered = true;
        scatter.scattered_ray.origin = rec.point;
        scatter.scattered_ray.direction = normalize(scatter_direction);
        scatter.attenuation = albedo;
        scatter.emitted = vec3(0.0);
        return true;
    } else {
        scatter.scattered = false;
        return false;
    }
}

// Get emitted light (none for metal)
vec3 metal_emitted(MetalMaterial mat, vec2 uv, vec3 point) {
    return vec3(0.0);
}

// ============================================================================
// COMMON METAL PRESETS
// ============================================================================

MetalMaterial metal_gold() {
    MetalMaterial mat;
    mat.albedo = vec3(1.0, 0.766, 0.336);
    mat.fuzz = 0.1;
    return mat;
}

MetalMaterial metal_silver() {
    MetalMaterial mat;
    mat.albedo = vec3(0.972, 0.960, 0.915);
    mat.fuzz = 0.05;
    return mat;
}

MetalMaterial metal_copper() {
    MetalMaterial mat;
    mat.albedo = vec3(0.955, 0.637, 0.538);
    mat.fuzz = 0.15;
    return mat;
}

MetalMaterial metal_aluminum() {
    MetalMaterial mat;
    mat.albedo = vec3(0.913, 0.921, 0.925);
    mat.fuzz = 0.2;
    return mat;
}

MetalMaterial metal_iron() {
    MetalMaterial mat;
    mat.albedo = vec3(0.560, 0.570, 0.580);
    mat.fuzz = 0.3;
    return mat;
}

#endif // METAL_GLSL
