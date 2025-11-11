// Dielectric.glsl
// Dielectric (glass/transparent) material implementation
//
// Implements refraction using Snell's law and Fresnel equations
// Uses Schlick approximation for efficiency
// Based on "Ray Tracing in One Weekend" by Peter Shirley

#ifndef DIELECTRIC_GLSL
#define DIELECTRIC_GLSL

#include "MaterialCommon.glsl"

// Requires:
// - vec3 reflect_vector(vec3 v, vec3 n)
// - vec3 refract_vector(vec3 uv, vec3 n, float etai_over_etat)
// - float reflectance_schlick(float cosine, float ref_idx)
// - float random_float(inout uint seed)

// ============================================================================
// DIELECTRIC MATERIAL
// ============================================================================

// Dielectric material properties
struct DielectricMaterial {
    float ir;         // Index of refraction (1.0 = air, 1.5 = glass, 2.4 = diamond)
    vec3 tint;        // Optional color tint (vec3(1.0) for clear glass)
    float absorption; // Beer's law absorption coefficient
};

// Reflect vector around normal
vec3 reflect_vector(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

// Refract vector using Snell's law
// etai_over_etat = refractive_index_incident / refractive_index_transmitted
// Returns refracted ray direction
vec3 refract_vector(vec3 uv, vec3 n, float etai_over_etat) {
    float cos_theta = min(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -sqrt(abs(1.0 - dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}

// Schlick's approximation for Fresnel reflectance
// Approximates the probability of reflection vs refraction
// at different angles
//
// At normal incidence (perpendicular): uses R0
// At grazing angles (parallel): approaches 1.0 (total reflection)
float reflectance_schlick(float cosine, float ref_idx) {
    // R0 is reflectance at normal incidence
    float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
    r0 = r0 * r0;

    // Schlick's polynomial approximation
    return r0 + (1.0 - r0) * pow((1.0 - cosine), 5.0);
}

// Scatter ray through dielectric (glass) material
//
// Physics:
// 1. Snell's Law: n1 * sin(θ1) = n2 * sin(θ2)
// 2. Total Internal Reflection: When angle is too steep, no refraction occurs
// 3. Fresnel Equations: Probability of reflection vs refraction varies with angle
//
// Implementation:
// - Calculate refraction ratio based on which side of surface we're on
// - Check for total internal reflection
// - Use Schlick approximation to decide reflect vs refract stochastically
bool dielectric_scatter(
    Ray r_in,
    HitRecord rec,
    float ir,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Dielectric materials don't attenuate (clear glass)
    scatter.attenuation = vec3(1.0, 1.0, 1.0);
    scatter.emitted = vec3(0.0);

    // Determine refraction ratio based on which side we hit
    // Front face: air -> glass (1.0 / ir)
    // Back face: glass -> air (ir / 1.0)
    float refraction_ratio = rec.frontFace ? (1.0 / ir) : ir;

    vec3 unit_direction = normalize(r_in.direction);
    float cos_theta = min(dot(-unit_direction, rec.normal), 1.0);
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    // Check for total internal reflection
    bool cannot_refract = refraction_ratio * sin_theta > 1.0;

    vec3 direction;

    // Decide between reflection and refraction
    // Use Schlick approximation to model Fresnel effect
    if (cannot_refract || reflectance_schlick(cos_theta, refraction_ratio) > random_float(rng_state)) {
        // Reflect
        direction = reflect_vector(unit_direction, rec.normal);
    } else {
        // Refract
        direction = refract_vector(unit_direction, rec.normal, refraction_ratio);
    }

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(direction);

    return true;
}

// Scatter with colored glass (tinted dielectric)
bool dielectric_scatter_tinted(
    Ray r_in,
    HitRecord rec,
    float ir,
    vec3 tint,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    scatter.attenuation = tint;  // Apply color tint
    scatter.emitted = vec3(0.0);

    float refraction_ratio = rec.frontFace ? (1.0 / ir) : ir;

    vec3 unit_direction = normalize(r_in.direction);
    float cos_theta = min(dot(-unit_direction, rec.normal), 1.0);
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    bool cannot_refract = refraction_ratio * sin_theta > 1.0;
    vec3 direction;

    if (cannot_refract || reflectance_schlick(cos_theta, refraction_ratio) > random_float(rng_state)) {
        direction = reflect_vector(unit_direction, rec.normal);
    } else {
        direction = refract_vector(unit_direction, rec.normal, refraction_ratio);
    }

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(direction);

    return true;
}

// Scatter with Beer's law absorption (colored glass with depth-dependent color)
bool dielectric_scatter_absorbing(
    Ray r_in,
    HitRecord rec,
    float ir,
    vec3 absorption_color,
    float absorption_distance,
    inout uint rng_state,
    out ScatterRecord scatter
) {
    // Beer's law: I = I0 * exp(-absorption * distance)
    // Apply absorption when ray exits material (back face)
    vec3 attenuation = vec3(1.0);
    if (!rec.frontFace) {
        // Ray traveled through material, apply absorption
        vec3 absorption = -log(absorption_color) / absorption_distance;
        attenuation = exp(-absorption * rec.t);
    }

    scatter.attenuation = attenuation;
    scatter.emitted = vec3(0.0);

    float refraction_ratio = rec.frontFace ? (1.0 / ir) : ir;

    vec3 unit_direction = normalize(r_in.direction);
    float cos_theta = min(dot(-unit_direction, rec.normal), 1.0);
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    bool cannot_refract = refraction_ratio * sin_theta > 1.0;
    vec3 direction;

    if (cannot_refract || reflectance_schlick(cos_theta, refraction_ratio) > random_float(rng_state)) {
        direction = reflect_vector(unit_direction, rec.normal);
    } else {
        direction = refract_vector(unit_direction, rec.normal, refraction_ratio);
    }

    scatter.scattered = true;
    scatter.scattered_ray.origin = rec.point;
    scatter.scattered_ray.direction = normalize(direction);

    return true;
}

// Get emitted light (none for dielectric)
vec3 dielectric_emitted(DielectricMaterial mat, vec2 uv, vec3 point) {
    return vec3(0.0);
}

// ============================================================================
// COMMON DIELECTRIC PRESETS
// ============================================================================

DielectricMaterial dielectric_glass() {
    DielectricMaterial mat;
    mat.ir = 1.5;
    mat.tint = vec3(1.0);
    mat.absorption = 0.0;
    return mat;
}

DielectricMaterial dielectric_water() {
    DielectricMaterial mat;
    mat.ir = 1.333;
    mat.tint = vec3(0.9, 0.95, 1.0);
    mat.absorption = 0.0;
    return mat;
}

DielectricMaterial dielectric_diamond() {
    DielectricMaterial mat;
    mat.ir = 2.42;
    mat.tint = vec3(1.0);
    mat.absorption = 0.0;
    return mat;
}

DielectricMaterial dielectric_ice() {
    DielectricMaterial mat;
    mat.ir = 1.31;
    mat.tint = vec3(0.95, 0.98, 1.0);
    mat.absorption = 0.0;
    return mat;
}

// Refractive indices reference:
// Air: 1.0
// Water: 1.333
// Ice: 1.31
// Glass (common): 1.5 - 1.9
// Sapphire: 1.77
// Diamond: 2.42
// Cubic Zirconia: 2.15

#endif // DIELECTRIC_GLSL
