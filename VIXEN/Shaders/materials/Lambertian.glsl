// Lambertian.glsl
// Lambertian (diffuse) material implementation
//
// Perfect diffuse reflection using cosine-weighted hemisphere sampling
// Based on "Ray Tracing in One Weekend" by Peter Shirley

#ifndef LAMBERTIAN_GLSL
#define LAMBERTIAN_GLSL

#include "MaterialCommon.glsl"

// Requires random_unit_vector(inout uint seed) from RNG utilities

// ============================================================================
// LAMBERTIAN MATERIAL
// ============================================================================

// Lambertian material properties
struct LambertianMaterial {
    vec3 albedo;  // Diffuse color / reflectance
};

// Scatter ray using true Lambertian distribution
// Uses cosine-weighted hemisphere sampling
//
// Distribution: PDF = cos(θ) / π
// Implementation: normal + random_unit_vector()
//
// This creates higher probability for rays close to the normal
// and lower probability for grazing angles
bool lambertian_scatter(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Requires this function from RayTracingUtils.glsl or similar
    // Generates random unit vector for cosine-weighted distribution
    vec3 scatter_direction = rec.normal + random_unit_vector(rng_state);

    // Catch degenerate scatter direction
    // If random vector exactly cancels normal, use normal instead
    if (near_zero(scatter_direction)) {
        scatter_direction = rec.normal;
    }

    // Fill scatter record
    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(scatter_direction);
    scatter.attenuation = albedo;
    scatter.emitted = vec3(0.0);  // Lambertian doesn't emit

    return true;
}

// Alternative: True Lambertian with on-unit-sphere sampling
// Slightly different distribution but still correct
bool lambertian_scatter_sphere(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Random direction on unit sphere surface (not inside)
    // This gives uniform distribution on hemisphere
    vec3 scatter_direction = rec.normal + random_on_unit_sphere(rng_state);

    if (near_zero(scatter_direction)) {
        scatter_direction = rec.normal;
    }

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(scatter_direction);
    scatter.attenuation = albedo;
    scatter.emitted = vec3(0.0);

    return true;
}

// Alternative: Hemisphere sampling (simpler but less accurate)
// Picks random direction in hemisphere around normal
bool lambertian_scatter_hemisphere(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Requires random_in_hemisphere(rng_state, normal) function
    vec3 scatter_direction = random_in_hemisphere(rng_state, rec.normal);

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(scatter_direction);
    scatter.attenuation = albedo;
    scatter.emitted = vec3(0.0);

    return true;
}

// Get emitted light (none for Lambertian)
vec3 lambertian_emitted(LambertianMaterial mat, vec2 uv, vec3 point) {
    return vec3(0.0);
}

#endif // LAMBERTIAN_GLSL
