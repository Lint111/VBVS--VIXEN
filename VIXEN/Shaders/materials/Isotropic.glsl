// Isotropic.glsl
// Isotropic (volumetric scattering) material implementation
//
// Used for volumes like fog, smoke, clouds
// Scatters light uniformly in all directions
// From "Ray Tracing: The Next Week" by Peter Shirley

#ifndef ISOTROPIC_GLSL
#define ISOTROPIC_GLSL

#include "MaterialCommon.glsl"

// Requires:
// - vec3 random_unit_vector(inout uint seed)

// ============================================================================
// ISOTROPIC MATERIAL
// ============================================================================

// Isotropic material properties
struct IsotropicMaterial {
    vec3 albedo;      // Scattering color
    float density;    // Volume density (for Beer's law)
};

// Scatter ray isotropically (uniform in all directions)
// Used for volumetric effects like fog, smoke, subsurface scattering
//
// Unlike Lambertian (which prefers normal direction),
// isotropic scattering has equal probability in all directions
bool isotropic_scatter(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Scatter in completely random direction
    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = random_unit_vector(rng_state);
    scatter.attenuation = albedo;
    scatter.emitted = vec3(0.0);

    return true;
}

// Scatter with phase function (anisotropic scattering)
// phase_g: Henyey-Greenstein parameter
//   g = 0: isotropic (equal all directions)
//   g > 0: forward scattering (continues in similar direction)
//   g < 0: backward scattering (bounces back)
bool isotropic_scatter_phase(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    float phase_g,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Henyey-Greenstein phase function sampling
    float g = clamp(phase_g, -0.99, 0.99);
    float cos_theta;

    if (abs(g) < 0.001) {
        // Isotropic case
        cos_theta = 2.0 * random_float(rng_state) - 1.0;
    } else {
        // Anisotropic case
        float sqr_term = (1.0 - g * g) / (1.0 - g + 2.0 * g * random_float(rng_state));
        cos_theta = (1.0 + g * g - sqr_term * sqr_term) / (2.0 * g);
    }

    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    float phi = 2.0 * 3.14159265359 * random_float(rng_state);

    // Build orthonormal basis
    vec3 w = normalize(r_in.direction);
    vec3 u = normalize(cross(abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0), w));
    vec3 v = cross(w, u);

    // Scatter direction in local coordinates
    vec3 scatter_dir = sin_theta * cos(phi) * u +
                       sin_theta * sin(phi) * v +
                       cos_theta * w;

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(scatter_dir);
    scatter.attenuation = albedo;
    scatter.emitted = vec3(0.0);

    return true;
}

// Get emitted light (none for isotropic)
vec3 isotropic_emitted(IsotropicMaterial mat, vec2 uv, vec3 point) {
    return vec3(0.0);
}

// ============================================================================
// VOLUME UTILITIES
// ============================================================================

// Sample volume density for ray marching
// Uses Beer's law: transmission = exp(-density * distance)
bool volume_hit(
    Ray ray,
    float t_min,
    float t_max,
    float density,
    inout uint rng_state,
    out float hit_distance
) {
    // Probabilistic hit based on density
    // Higher density = more likely to scatter

    // Sample distance using inverse transform sampling
    // PDF(t) = density * exp(-density * t)
    float random_val = random_float(rng_state);
    hit_distance = -log(random_val) / density;

    // Check if hit is within ray segment
    if (hit_distance < t_min || hit_distance > t_max) {
        return false;
    }

    return true;
}

// Calculate volume transmittance (Beer's law)
// How much light passes through without scattering
vec3 volume_transmittance(vec3 albedo, float density, float distance) {
    vec3 optical_depth = density * distance * (vec3(1.0) - albedo);
    return exp(-optical_depth);
}

// ============================================================================
// COMMON ISOTROPIC PRESETS
// ============================================================================

IsotropicMaterial isotropic_fog(vec3 color, float density) {
    IsotropicMaterial mat;
    mat.albedo = color;
    mat.density = density;
    return mat;
}

IsotropicMaterial isotropic_smoke() {
    IsotropicMaterial mat;
    mat.albedo = vec3(0.5);  // Gray
    mat.density = 0.1;
    return mat;
}

IsotropicMaterial isotropic_white_cloud() {
    IsotropicMaterial mat;
    mat.albedo = vec3(0.95);  // Nearly white
    mat.density = 0.05;
    return mat;
}

#endif // ISOTROPIC_GLSL
