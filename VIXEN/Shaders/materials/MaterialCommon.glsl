// MaterialCommon.glsl
// Common material structures and utilities
//
// Base definitions shared across all material types

#ifndef MATERIAL_COMMON_GLSL
#define MATERIAL_COMMON_GLSL

// Material type identifiers
const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;
const int MAT_EMISSIVE = 3;
const int MAT_ISOTROPIC = 4;  // For volumes

// Common structures
struct Ray {
    vec3 origin;
    vec3 direction;  // Should be normalized
};

struct HitRecord {
    vec3 point;           // Hit position in world space
    vec3 normal;          // Surface normal (always points outward)
    float t;              // Ray parameter at hit
    bool frontFace;       // True if ray hit from outside
    int materialIndex;    // Material identifier
    vec2 uv;              // Texture coordinates
};

// Material scatter result
struct ScatterRecord {
    bool scattered;       // True if ray was scattered (not absorbed)
    vec3 attenuation;     // Color attenuation
    Ray scattered_ray;    // Outgoing scattered ray
    vec3 emitted;         // Emitted light (for emissive materials)
};

// Set face normal (ensures normal always points against ray)
void set_face_normal(inout HitRecord rec, Ray r, vec3 outward_normal) {
    rec.frontFace = dot(r.direction, outward_normal) < 0.0;
    rec.normal = rec.frontFace ? outward_normal : -outward_normal;
}

// Check if vector is near zero in all dimensions
bool near_zero(vec3 v) {
    const float eps = 1e-8;
    return (abs(v.x) < eps) && (abs(v.y) < eps) && (abs(v.z) < eps);
}

#endif // MATERIAL_COMMON_GLSL
