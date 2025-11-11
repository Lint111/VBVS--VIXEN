// GeometryUtils.glsl
// Geometric utilities for ray tracing and spatial queries
//
// Provides:
// - AABB operations (span, contains, overlaps, merge)
// - Ray-geometry intersections
// - Distance functions
// - Bounding volume utilities

#ifndef GEOMETRY_UTILS_GLSL
#define GEOMETRY_UTILS_GLSL

#ifndef EPSILON
#define EPSILON 1e-6
#endif

#ifndef INFINITY
#define INFINITY 1e10
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct AABB {
    vec3 minimum;
    vec3 maximum;
};

struct Ray {
    vec3 origin;
    vec3 direction;  // Should be normalized
};

// ============================================================================
// AABB CREATION & MANIPULATION
// ============================================================================

// Create AABB from center and half-extents
AABB aabb_from_center_extents(vec3 center, vec3 half_extents) {
    AABB box;
    box.minimum = center - half_extents;
    box.maximum = center + half_extents;
    return box;
}

// Create AABB from two points
AABB aabb_from_points(vec3 p1, vec3 p2) {
    AABB box;
    box.minimum = min(p1, p2);
    box.maximum = max(p1, p2);
    return box;
}

// Create AABB that contains a sphere
AABB aabb_from_sphere(vec3 center, float radius) {
    AABB box;
    box.minimum = center - vec3(radius);
    box.maximum = center + vec3(radius);
    return box;
}

// Get center of AABB
vec3 aabb_center(AABB box) {
    return (box.minimum + box.maximum) * 0.5;
}

// Get half-extents of AABB
vec3 aabb_half_extents(AABB box) {
    return (box.maximum - box.minimum) * 0.5;
}

// Get full extents of AABB
vec3 aabb_extents(AABB box) {
    return box.maximum - box.minimum;
}

// Get surface area of AABB (for SAH in BVH)
float aabb_surface_area(AABB box) {
    vec3 extents = box.maximum - box.minimum;
    return 2.0 * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
}

// Get volume of AABB
float aabb_volume(AABB box) {
    vec3 extents = box.maximum - box.minimum;
    return extents.x * extents.y * extents.z;
}

// Get diagonal length of AABB
float aabb_diagonal(AABB box) {
    return length(box.maximum - box.minimum);
}

// Get longest axis (0 = X, 1 = Y, 2 = Z)
int aabb_longest_axis(AABB box) {
    vec3 extents = box.maximum - box.minimum;
    if (extents.x > extents.y && extents.x > extents.z) return 0;
    if (extents.y > extents.z) return 1;
    return 2;
}

// Expand AABB by delta amount
AABB aabb_expand(AABB box, float delta) {
    AABB result;
    result.minimum = box.minimum - vec3(delta);
    result.maximum = box.maximum + vec3(delta);
    return result;
}

// Expand AABB by delta vector
AABB aabb_expand_vec(AABB box, vec3 delta) {
    AABB result;
    result.minimum = box.minimum - delta;
    result.maximum = box.maximum + delta;
    return result;
}

// ============================================================================
// AABB CONTAINMENT TESTS
// ============================================================================

// Check if AABB contains a point
bool aabb_contains_point(AABB box, vec3 point) {
    return all(greaterThanEqual(point, box.minimum)) &&
           all(lessThanEqual(point, box.maximum));
}

// Check if AABB contains a point (with epsilon tolerance)
bool aabb_contains_point_epsilon(AABB box, vec3 point, float epsilon) {
    return all(greaterThanEqual(point, box.minimum - vec3(epsilon))) &&
           all(lessThanEqual(point, box.maximum + vec3(epsilon)));
}

// Check if box1 completely contains box2
bool aabb_contains_aabb(AABB box1, AABB box2) {
    return all(greaterThanEqual(box2.minimum, box1.minimum)) &&
           all(lessThanEqual(box2.maximum, box1.maximum));
}

// ============================================================================
// AABB OVERLAP TESTS
// ============================================================================

// Check if two AABBs overlap (SAT - Separating Axis Theorem)
bool aabb_overlaps(AABB box1, AABB box2) {
    return all(lessThanEqual(box1.minimum, box2.maximum)) &&
           all(greaterThanEqual(box1.maximum, box2.minimum));
}

// Check if two AABBs overlap with epsilon tolerance
bool aabb_overlaps_epsilon(AABB box1, AABB box2, float epsilon) {
    return all(lessThanEqual(box1.minimum, box2.maximum + vec3(epsilon))) &&
           all(greaterThanEqual(box1.maximum, box2.minimum - vec3(epsilon)));
}

// Get overlap AABB (intersection of two AABBs)
// Returns AABB with min > max if no overlap
AABB aabb_overlap(AABB box1, AABB box2) {
    AABB result;
    result.minimum = max(box1.minimum, box2.minimum);
    result.maximum = min(box1.maximum, box2.maximum);
    return result;
}

// Check if overlap result is valid
bool aabb_is_valid(AABB box) {
    return all(lessThanEqual(box.minimum, box.maximum));
}

// ============================================================================
// AABB SPAN / MERGE OPERATIONS
// ============================================================================

// Compute AABB that bounds two AABBs (union)
AABB aabb_span(AABB box1, AABB box2) {
    AABB result;
    result.minimum = min(box1.minimum, box2.minimum);
    result.maximum = max(box1.maximum, box2.maximum);
    return result;
}

// Compute AABB that bounds AABB and point
AABB aabb_span_point(AABB box, vec3 point) {
    AABB result;
    result.minimum = min(box.minimum, point);
    result.maximum = max(box.maximum, point);
    return result;
}

// Compute AABB that bounds three AABBs
AABB aabb_span3(AABB box1, AABB box2, AABB box3) {
    return aabb_span(aabb_span(box1, box2), box3);
}

// Merge point into AABB (modifies box)
void aabb_merge_point(inout AABB box, vec3 point) {
    box.minimum = min(box.minimum, point);
    box.maximum = max(box.maximum, point);
}

// Merge AABB into AABB (modifies box1)
void aabb_merge(inout AABB box1, AABB box2) {
    box1.minimum = min(box1.minimum, box2.minimum);
    box1.maximum = max(box1.maximum, box2.maximum);
}

// ============================================================================
// RAY-AABB INTERSECTION (Robust Slab Method)
// ============================================================================

// Test ray-AABB intersection in range [t_min, t_max]
// Returns true if intersection exists
bool ray_aabb_intersect(Ray ray, AABB box, float t_min, float t_max) {
    for (int axis = 0; axis < 3; ++axis) {
        float origin_component = axis == 0 ? ray.origin.x : (axis == 1 ? ray.origin.y : ray.origin.z);
        float dir_component = axis == 0 ? ray.direction.x : (axis == 1 ? ray.direction.y : ray.direction.z);
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

// Ray-AABB intersection that returns hit distances
bool ray_aabb_intersect_dist(Ray ray, AABB box, float t_min, float t_max, out float t_near, out float t_far) {
    t_near = t_min;
    t_far = t_max;

    for (int axis = 0; axis < 3; ++axis) {
        float origin_component = axis == 0 ? ray.origin.x : (axis == 1 ? ray.origin.y : ray.origin.z);
        float dir_component = axis == 0 ? ray.direction.x : (axis == 1 ? ray.direction.y : ray.direction.z);
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

        t_near = max(t0, t_near);
        t_far = min(t1, t_far);

        if (t_far <= t_near) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// DISTANCE FUNCTIONS
// ============================================================================

// Signed distance from point to AABB (negative inside, positive outside)
float sdf_aabb(vec3 point, AABB box) {
    vec3 center = aabb_center(box);
    vec3 half_extents = aabb_half_extents(box);
    vec3 local = point - center;
    vec3 d = abs(local) - half_extents;
    return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, vec3(0.0)));
}

// Squared distance from point to AABB (faster, no sqrt)
float distance_squared_point_aabb(vec3 point, AABB box) {
    float sqDist = 0.0;

    for (int i = 0; i < 3; ++i) {
        float p = i == 0 ? point.x : (i == 1 ? point.y : point.z);
        float bmin = i == 0 ? box.minimum.x : (i == 1 ? box.minimum.y : box.minimum.z);
        float bmax = i == 0 ? box.maximum.x : (i == 1 ? box.maximum.y : box.maximum.z);

        if (p < bmin) sqDist += (bmin - p) * (bmin - p);
        if (p > bmax) sqDist += (p - bmax) * (p - bmax);
    }

    return sqDist;
}

// Distance from point to AABB
float distance_point_aabb(vec3 point, AABB box) {
    return sqrt(distance_squared_point_aabb(point, box));
}

// Closest point on AABB to given point
vec3 closest_point_aabb(vec3 point, AABB box) {
    return clamp(point, box.minimum, box.maximum);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Compute offset of point relative to AABB ([0,1] in each dimension)
vec3 aabb_offset(AABB box, vec3 point) {
    vec3 o = point - box.minimum;
    vec3 extents = box.maximum - box.minimum;
    if (extents.x > 0.0) o.x /= extents.x;
    if (extents.y > 0.0) o.y /= extents.y;
    if (extents.z > 0.0) o.z /= extents.z;
    return o;
}

// Get corner of AABB (corner index 0-7)
vec3 aabb_corner(AABB box, int corner) {
    return vec3(
        (corner & 1) != 0 ? box.maximum.x : box.minimum.x,
        (corner & 2) != 0 ? box.maximum.y : box.minimum.y,
        (corner & 4) != 0 ? box.maximum.z : box.minimum.z
    );
}

// Transform AABB by matrix (conservative bounds)
AABB aabb_transform(AABB box, mat4 transform) {
    vec3 center = aabb_center(box);
    vec3 extents = aabb_half_extents(box);

    // Transform center
    vec4 new_center = transform * vec4(center, 1.0);

    // Transform extents (take absolute values to get conservative bounds)
    mat3 abs_transform = mat3(
        abs(transform[0].xyz),
        abs(transform[1].xyz),
        abs(transform[2].xyz)
    );
    vec3 new_extents = abs_transform * extents;

    return aabb_from_center_extents(new_center.xyz, new_extents);
}

#endif // GEOMETRY_UTILS_GLSL
