// RayTracingUtils.glsl
// Common ray tracing utilities for VIXEN
// Based on "Ray Tracing in One Weekend" series by Peter Shirley et al.
//
// Usage: #include "common/RayTracingUtils.glsl"
//
// Provides:
// - Ray-sphere intersection (analytic)
// - Ray-AABB intersection
// - Random number generation (PCG hash)
// - Lambertian diffuse scattering
// - Metallic reflection
// - Dielectric refraction (Snell's law + Schlick approximation)
// - Coordinate frame utilities

#ifndef RAY_TRACING_UTILS_GLSL
#define RAY_TRACING_UTILS_GLSL

// ============================================================================
// CONSTANTS
// ============================================================================

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float INV_PI = 0.31830988618;
const float EPSILON = 1e-6;
const float INFINITY = 1e10;

// ============================================================================
// DATA STRUCTURES
// ============================================================================

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

struct Sphere {
    vec3 center;
    float radius;
    int materialIndex;
};

struct AABB {
    vec3 minimum;
    vec3 maximum;
};

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================
// PCG Hash - Based on "Hash Functions for GPU Rendering" by Jarzynski & Olano

uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Generate random float in [0, 1)
float random_float(inout uint seed) {
    seed = pcg_hash(seed);
    return float(seed) / 4294967296.0;  // 2^32
}

// Generate random float in [min, max)
float random_float_range(inout uint seed, float min_val, float max_val) {
    return min_val + (max_val - min_val) * random_float(seed);
}

// Generate random vector in unit cube
vec3 random_vec3(inout uint seed) {
    return vec3(random_float(seed), random_float(seed), random_float(seed));
}

// Generate random vector in range
vec3 random_vec3_range(inout uint seed, float min_val, float max_val) {
    return vec3(
        random_float_range(seed, min_val, max_val),
        random_float_range(seed, min_val, max_val),
        random_float_range(seed, min_val, max_val)
    );
}

// Generate random vector in unit sphere (rejection sampling)
vec3 random_in_unit_sphere(inout uint seed) {
    for (int i = 0; i < 100; ++i) {  // Limit iterations to prevent infinite loop
        vec3 p = random_vec3_range(seed, -1.0, 1.0);
        if (dot(p, p) < 1.0) {
            return p;
        }
    }
    return vec3(0.0);  // Fallback
}

// Generate random unit vector (uniform distribution on sphere)
vec3 random_unit_vector(inout uint seed) {
    return normalize(random_in_unit_sphere(seed));
}

// Generate random vector in unit disk (for depth of field)
vec2 random_in_unit_disk(inout uint seed) {
    for (int i = 0; i < 100; ++i) {
        vec2 p = vec2(
            random_float_range(seed, -1.0, 1.0),
            random_float_range(seed, -1.0, 1.0)
        );
        if (dot(p, p) < 1.0) {
            return p;
        }
    }
    return vec2(0.0);  // Fallback
}

// Generate random vector in hemisphere around normal (cosine-weighted)
vec3 random_in_hemisphere(inout uint seed, vec3 normal) {
    vec3 in_unit_sphere = random_in_unit_sphere(seed);
    if (dot(in_unit_sphere, normal) > 0.0) {
        return in_unit_sphere;  // Same hemisphere as normal
    } else {
        return -in_unit_sphere; // Flip to correct hemisphere
    }
}

// ============================================================================
// GEOMETRIC UTILITIES
// ============================================================================

// Compute ray point at parameter t
vec3 ray_at(Ray r, float t) {
    return r.origin + t * r.direction;
}

// Set face normal (ensures normal always points against ray)
void set_face_normal(inout HitRecord rec, Ray r, vec3 outward_normal) {
    rec.frontFace = dot(r.direction, outward_normal) < 0.0;
    rec.normal = rec.frontFace ? outward_normal : -outward_normal;
}

// Reflect vector v around normal n
vec3 reflect_vector(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

// Refract vector using Snell's law
// etai_over_etat = refractive_index_incident / refractive_index_transmitted
// Returns refracted ray, or vec3(0) if total internal reflection
vec3 refract_vector(vec3 uv, vec3 n, float etai_over_etat) {
    float cos_theta = min(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -sqrt(abs(1.0 - dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}

// Schlick's approximation for Fresnel reflectance
// Approximates how much light is reflected vs refracted at an interface
float reflectance_schlick(float cosine, float ref_idx) {
    float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow((1.0 - cosine), 5.0);
}

// Check if vector is near zero in all dimensions
bool near_zero(vec3 v) {
    return (abs(v.x) < EPSILON) && (abs(v.y) < EPSILON) && (abs(v.z) < EPSILON);
}

// ============================================================================
// RAY-SPHERE INTERSECTION
// ============================================================================
// Based on analytic solution to quadratic equation
// Ray: P(t) = origin + t*direction
// Sphere: (P - center)·(P - center) = radius²
// Solving gives: at² + bt + c = 0
// where: a = direction·direction = 1 (if normalized)
//        b = 2*direction·(origin - center)
//        c = (origin - center)·(origin - center) - radius²

bool hit_sphere(Sphere sphere, Ray r, float t_min, float t_max, out HitRecord rec) {
    vec3 oc = r.origin - sphere.center;

    // Quadratic coefficients (direction assumed normalized, so a = 1)
    float a = dot(r.direction, r.direction);
    float half_b = dot(oc, r.direction);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;

    // Discriminant determines number of intersections
    float discriminant = half_b * half_b - a * c;

    if (discriminant < 0.0) {
        return false;  // No intersection
    }

    float sqrtd = sqrt(discriminant);

    // Find nearest root in acceptable range [t_min, t_max]
    float root = (-half_b - sqrtd) / a;
    if (root < t_min || root > t_max) {
        root = (-half_b + sqrtd) / a;
        if (root < t_min || root > t_max) {
            return false;  // Both roots outside range
        }
    }

    // Fill hit record
    rec.t = root;
    rec.point = ray_at(r, rec.t);
    vec3 outward_normal = (rec.point - sphere.center) / sphere.radius;
    set_face_normal(rec, r, outward_normal);
    rec.materialIndex = sphere.materialIndex;

    // Calculate sphere UV coordinates
    // theta: angle from -Y (down)
    // phi: angle around Y from -X
    vec3 unit_point = outward_normal;
    float theta = acos(-unit_point.y);
    float phi = atan(unit_point.z, unit_point.x) + PI;
    rec.uv = vec2(phi / TWO_PI, theta / PI);

    return true;
}

// ============================================================================
// RAY-AABB INTERSECTION
// ============================================================================
// Robust AABB intersection using slab method
// Returns true if ray intersects box in range [t_min, t_max]

bool hit_aabb(AABB box, Ray r, float t_min, float t_max) {
    for (int axis = 0; axis < 3; ++axis) {
        float origin_component = axis == 0 ? r.origin.x : (axis == 1 ? r.origin.y : r.origin.z);
        float dir_component = axis == 0 ? r.direction.x : (axis == 1 ? r.direction.y : r.direction.z);
        float box_min = axis == 0 ? box.minimum.x : (axis == 1 ? box.minimum.y : box.minimum.z);
        float box_max = axis == 0 ? box.maximum.x : (axis == 1 ? box.maximum.y : box.maximum.z);

        float inv_d = abs(dir_component) < EPSILON ? INFINITY : 1.0 / dir_component;
        float t0 = (box_min - origin_component) * inv_d;
        float t1 = (box_max - origin_component) * inv_d;

        if (inv_d < 0.0) {
            float temp = t0;
            t0 = t1;
            t1 = temp;
        }

        t_min = max(t0, t_min);
        t_max = min(t1, t_max);

        if (t_max <= t_min) {
            return false;
        }
    }

    return true;
}

// Compute AABB that bounds two AABBs
AABB surrounding_box(AABB box0, AABB box1) {
    AABB result;
    result.minimum = min(box0.minimum, box1.minimum);
    result.maximum = max(box0.maximum, box1.maximum);
    return result;
}

// ============================================================================
// MATERIAL SCATTERING FUNCTIONS
// ============================================================================

// Lambertian (diffuse) material scattering
// Returns scattered ray and attenuation color
// Uses true Lambertian distribution (cosine-weighted hemisphere)
bool scatter_lambertian(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    inout uint rng_state,
    out vec3 attenuation,
    out Ray scattered
) {
    // Generate scatter direction using random unit vector
    // This creates a Lambertian (cosine-weighted) distribution
    vec3 scatter_direction = rec.normal + random_unit_vector(rng_state);

    // Catch degenerate scatter direction
    if (near_zero(scatter_direction)) {
        scatter_direction = rec.normal;
    }

    scattered.origin = rec.point;
    scattered.direction = normalize(scatter_direction);
    attenuation = albedo;

    return true;
}

// Metallic (mirror reflection) material scattering
// fuzz: 0.0 = perfect mirror, 1.0 = very fuzzy reflection
bool scatter_metal(
    Ray r_in,
    HitRecord rec,
    vec3 albedo,
    float fuzz,
    inout uint rng_state,
    out vec3 attenuation,
    out Ray scattered
) {
    vec3 reflected = reflect_vector(normalize(r_in.direction), rec.normal);
    scattered.origin = rec.point;
    scattered.direction = normalize(reflected + fuzz * random_in_unit_sphere(rng_state));
    attenuation = albedo;

    // Absorbed if scattered below surface
    return (dot(scattered.direction, rec.normal) > 0.0);
}

// Dielectric (glass) material scattering
// ir: index of refraction (1.0 = air, 1.5 = glass, 2.4 = diamond)
bool scatter_dielectric(
    Ray r_in,
    HitRecord rec,
    float ir,
    inout uint rng_state,
    out vec3 attenuation,
    out Ray scattered
) {
    attenuation = vec3(1.0, 1.0, 1.0);  // Glass doesn't attenuate
    float refraction_ratio = rec.frontFace ? (1.0 / ir) : ir;

    vec3 unit_direction = normalize(r_in.direction);
    float cos_theta = min(dot(-unit_direction, rec.normal), 1.0);
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    bool cannot_refract = refraction_ratio * sin_theta > 1.0;
    vec3 direction;

    // Use Schlick approximation to determine reflection vs refraction
    if (cannot_refract || reflectance_schlick(cos_theta, refraction_ratio) > random_float(rng_state)) {
        direction = reflect_vector(unit_direction, rec.normal);
    } else {
        direction = refract_vector(unit_direction, rec.normal, refraction_ratio);
    }

    scattered.origin = rec.point;
    scattered.direction = normalize(direction);

    return true;
}

// ============================================================================
// COORDINATE FRAME UTILITIES
// ============================================================================

// Build orthonormal basis from a single vector (normal)
// Useful for transforming vectors to/from tangent space
void build_orthonormal_basis(vec3 n, out vec3 tangent, out vec3 bitangent) {
    // Choose arbitrary vector not parallel to n
    vec3 helper = abs(n.x) > 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangent = normalize(cross(n, helper));
    bitangent = cross(n, tangent);
}

// Transform vector from tangent space to world space
vec3 tangent_to_world(vec3 v, vec3 normal, vec3 tangent, vec3 bitangent) {
    return v.x * tangent + v.y * bitangent + v.z * normal;
}

// Transform vector from world space to tangent space
vec3 world_to_tangent(vec3 v, vec3 normal, vec3 tangent, vec3 bitangent) {
    return vec3(dot(v, tangent), dot(v, bitangent), dot(v, normal));
}

#endif // RAY_TRACING_UTILS_GLSL
