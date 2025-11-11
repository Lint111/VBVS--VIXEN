// PhotonSampling.glsl
// Two-phase progressive photon mapping with view-dependent importance sampling
//
// Industry Standards & References:
// - [1] Henrik Wann Jensen: "Realistic Image Synthesis Using Photon Mapping" (2001)
// - [2] Jensen: "Importance Driven Path Tracing using the Photon Map" (1995)
// - [3] Hachisuka & Jensen: "Stochastic Progressive Photon Mapping" (2009)
// - [4] Hachisuka et al.: "Progressive Photon Mapping" (2008)
// - [5] PBRT v3: "Light Transport III: Bidirectional Methods" (Chapter 16)
//
// Algorithm Overview:
// ====================
// PHASE 1: Photon Tracing (from light sources)
//   - Early bounces: Slack sampling for global illumination (uniform hemisphere)
//   - Later bounces: Progressive PDF bias towards camera frustum
//   - Store photons in spatial structure (kD-tree or hash grid)
//
// PHASE 2: Ray Tracing (from camera)
//   - Early bounces: Slack sampling (explore scene)
//   - Later bounces: Progressive PDF bias using photon map
//   - Gather nearby photons for radiance estimation
//
// Progressive Bias Strategy:
//   bounce_weight = lerp(1.0, frustum_weight, bounce / max_bounces)
//   PDF_biased = lerp(PDF_uniform, PDF_frustum, bounce_weight)

#ifndef PHOTON_SAMPLING_GLSL
#define PHOTON_SAMPLING_GLSL

#ifndef PI
#define PI 3.14159265359
#endif

#ifndef EPSILON
#define EPSILON 1e-6
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Photon representation (compact: 32 bytes)
struct Photon {
    vec3 position;          // 12 bytes - World space position
    vec3 incident_direction; // 12 bytes - Direction photon was traveling
    vec3 power;             // 12 bytes - RGB flux (power)
    // Optional: could add 4 bytes for flags/surface normal compression
};

// Visible point for camera ray tracing (SPPM hit points)
struct VisiblePoint {
    vec3 position;          // Hit position
    vec3 normal;            // Surface normal
    vec3 throughput;        // Path throughput (accumulated attenuation)
    float radius;           // Search radius for photon gathering
    int photon_count;       // Number of photons gathered
    vec3 accumulated_flux;  // Flux from gathered photons
};

// Frustum representation (6 planes)
struct Frustum {
    vec4 planes[6];  // Each plane: (normal.xyz, distance)
    // Plane order: left, right, bottom, top, near, far
    float thickness;  // Padding thickness (world units) for smooth edge falloff
    // Purpose: Prevents visible artifacts at screen edges by creating
    //          a smooth transition zone around the frustum
};

// Importance sampling context
struct ImportanceContext {
    vec3 target_point;      // Point to bias towards (e.g., camera position)
    vec3 target_normal;     // Normal at target (for hemisphere bias)
    float bias_strength;    // [0, 1]: 0 = uniform, 1 = fully biased
    Frustum frustum;        // Camera frustum for culling
};

// Progressive photon mapping parameters
struct PhotonMappingParams {
    uint max_bounces;           // Maximum photon/ray bounces
    uint photons_per_iteration; // Photons to emit per iteration
    float initial_radius;       // Initial gathering radius
    float alpha;                // Radius reduction factor (typically 0.7)
    float bias_start_bounce;    // Bounce to start applying bias (e.g., 2.0)
    bool use_russian_roulette;  // Enable path termination probability
};

// ============================================================================
// FRUSTUM UTILITIES
// ============================================================================

// Build frustum from inverse view-projection matrix
// No padding (exact camera view)
Frustum frustum_from_inverse_vp(mat4 inv_view_proj) {
    return frustum_from_inverse_vp_padded(inv_view_proj, 0.0);
}

// Build frustum with padding/thickness for smooth edge falloff
//
// QUALITY IMPROVEMENT: Prevents visible artifacts at screen edges
//
// Without padding:
//   ┌──────────────┐
//   │   Screen     │  ← Photon weight drops sharply at edge
//   └──────────────┘     Visible falloff artifacts!
//
// With padding (thickness = t):
//   ┌──────────────┐  ← Inner frustum (weight = 1.0)
//   │   Screen     │
//   ├──────────────┤  ← Padding zone (weight = 1.0 → 0.0)
//   │   Falloff    │     Smooth transition, no visible artifacts
//   └──────────────┘  ← Outer boundary (weight = 0.0)
//
// Recommended thickness:
//   - Small scenes: 0.5 - 1.0 units
//   - Medium scenes: 2.0 - 5.0 units
//   - Large scenes: 10.0 - 20.0 units
//   - Rule of thumb: 5-10% of near plane distance
Frustum frustum_from_inverse_vp_padded(mat4 inv_view_proj, float thickness) {
    Frustum f;
    f.thickness = thickness;

    // Extract frustum corners in NDC, transform to world space
    vec4 corners_ndc[8] = vec4[8](
        vec4(-1, -1, -1, 1), vec4(1, -1, -1, 1),
        vec4(-1,  1, -1, 1), vec4(1,  1, -1, 1),
        vec4(-1, -1,  1, 1), vec4(1, -1,  1, 1),
        vec4(-1,  1,  1, 1), vec4(1,  1,  1, 1)
    );

    vec3 corners_world[8];
    for (int i = 0; i < 8; ++i) {
        vec4 world = inv_view_proj * corners_ndc[i];
        corners_world[i] = world.xyz / world.w;
    }

    // Compute plane equations (cross products of edges)
    // Planes point inward (positive distance = inside)

    // Left plane: near-left-bottom, near-left-top, far-left-top
    vec3 p0 = corners_world[0];
    vec3 p1 = corners_world[2];
    vec3 p2 = corners_world[6];
    vec3 normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[0] = vec4(normal, -dot(normal, p0));

    // Right plane
    p0 = corners_world[1];
    p1 = corners_world[7];
    p2 = corners_world[3];
    normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[1] = vec4(normal, -dot(normal, p0));

    // Bottom plane
    p0 = corners_world[0];
    p1 = corners_world[5];
    p2 = corners_world[1];
    normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[2] = vec4(normal, -dot(normal, p0));

    // Top plane
    p0 = corners_world[2];
    p1 = corners_world[3];
    p2 = corners_world[7];
    normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[3] = vec4(normal, -dot(normal, p0));

    // Near plane
    p0 = corners_world[0];
    p1 = corners_world[1];
    p2 = corners_world[3];
    normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[4] = vec4(normal, -dot(normal, p0));

    // Far plane
    p0 = corners_world[4];
    p1 = corners_world[6];
    p2 = corners_world[5];
    normal = normalize(cross(p1 - p0, p2 - p0));
    f.planes[5] = vec4(normal, -dot(normal, p0));

    // Push planes outward by thickness to create padding zone
    // This expands the frustum volume by 'thickness' units in all directions
    if (thickness > EPSILON) {
        for (int i = 0; i < 6; ++i) {
            // Move plane outward along its normal
            // plane: normal·x + d = 0
            // new_plane: normal·x + (d - thickness) = 0
            f.planes[i].w -= thickness;
        }
    }

    return f;
}

// Test if point is inside frustum
bool frustum_contains_point(Frustum f, vec3 point) {
    for (int i = 0; i < 6; ++i) {
        float distance = dot(f.planes[i].xyz, point) + f.planes[i].w;
        if (distance < 0.0) {
            return false;  // Outside this plane
        }
    }
    return true;
}

// Get signed distance to frustum (negative = inside)
float frustum_signed_distance(Frustum f, vec3 point) {
    float max_distance = -1e10;
    for (int i = 0; i < 6; ++i) {
        float distance = dot(f.planes[i].xyz, point) + f.planes[i].w;
        max_distance = max(max_distance, distance);
    }
    return max_distance;
}

// Compute weight for point based on frustum proximity with thickness
// Returns [0, 1]: 1.0 = inside core frustum, smooth falloff in padding region
//
// Weight distribution:
//   distance < 0 (inside):        weight = 1.0
//   0 <= distance < thickness:    weight = smoothstep(1.0 → 0.0)
//   distance >= thickness:        weight = 0.0
//
// This creates a smooth transition zone that prevents visible artifacts
// at screen edges while maintaining full importance for visible photons
float frustum_weight(Frustum f, vec3 point) {
    float sd = frustum_signed_distance(f, point);

    // Inside core frustum: full weight
    if (sd <= 0.0) {
        return 1.0;
    }

    // Outside padding zone: zero weight
    if (f.thickness < EPSILON || sd >= f.thickness) {
        return 0.0;
    }

    // Inside padding zone: smooth falloff
    // Use smoothstep for C¹ continuity (smooth derivative)
    float t = sd / f.thickness;  // [0, 1] in padding zone
    return 1.0 - t * t * (3.0 - 2.0 * t);  // smoothstep falloff
}

// Legacy version with custom falloff distance
// Deprecated: Use frustum_weight(f, point) with padded frustum instead
float frustum_weight_custom_falloff(Frustum f, vec3 point, float falloff_distance) {
    float sd = frustum_signed_distance(f, point);
    if (sd <= 0.0) return 1.0;  // Inside
    if (falloff_distance < EPSILON) return 0.0;  // No falloff

    // Exponential falloff
    return exp(-sd / falloff_distance);
}

// ============================================================================
// PROGRESSIVE PDF CONSTRUCTION
// ============================================================================

// Calculate progressive bias weight for current bounce
// Early bounces: weight ≈ 0 (slack, uniform sampling)
// Later bounces: weight ≈ 1 (biased towards importance region)
float calculate_bounce_bias_weight(
    uint current_bounce,
    uint max_bounces,
    float bias_start_bounce
) {
    if (float(current_bounce) < bias_start_bounce) {
        return 0.0;  // No bias for early bounces (global illumination)
    }

    float normalized_bounce = (float(current_bounce) - bias_start_bounce) /
                              (float(max_bounces) - bias_start_bounce);

    // Smooth cubic ease-in for gradual transition
    return normalized_bounce * normalized_bounce * (3.0 - 2.0 * normalized_bounce);
}

// PDF for uniform hemisphere sampling (Lambertian-style)
float pdf_uniform_hemisphere() {
    return 1.0 / (2.0 * PI);
}

// PDF for cosine-weighted hemisphere (true Lambertian)
float pdf_cosine_hemisphere(float cos_theta) {
    return cos_theta / PI;
}

// PDF for direction biased towards target point
// Uses von Mises-Fisher distribution on sphere
float pdf_towards_target(vec3 direction, vec3 target_direction, float concentration) {
    // concentration (kappa): 0 = uniform, large = concentrated
    float cos_angle = dot(direction, target_direction);

    if (concentration < EPSILON) {
        return 1.0 / (4.0 * PI);  // Uniform sphere
    }

    // von Mises-Fisher PDF: (k / (4π sinh(k))) * exp(k * cos(θ))
    float sinh_k = (exp(concentration) - exp(-concentration)) * 0.5;
    return (concentration / (4.0 * PI * sinh_k)) * exp(concentration * cos_angle);
}

// Sample direction with progressive bias towards target
// Requires: random_unit_vector(inout uint seed)
vec3 sample_direction_progressive_bias(
    vec3 surface_normal,
    vec3 target_point,
    vec3 current_position,
    float bias_weight,
    inout uint rng_state
) {
    // Direction towards target
    vec3 to_target = normalize(target_point - current_position);

    // Early bounces (bias_weight ≈ 0): uniform hemisphere
    // Later bounces (bias_weight ≈ 1): biased towards target

    if (bias_weight < EPSILON) {
        // Uniform hemisphere sampling
        vec3 random_dir = random_unit_vector(rng_state);
        if (dot(random_dir, surface_normal) < 0.0) {
            random_dir = -random_dir;
        }
        return random_dir;
    }

    // Blend between uniform and biased sampling
    // Use rejection sampling with importance weighting

    // Concentration parameter (higher = more concentrated)
    float concentration = bias_weight * 10.0;  // Max concentration = 10

    // Sample from von Mises-Fisher distribution
    // Simplified: blend random hemisphere with direction to target
    vec3 random_dir = random_unit_vector(rng_state);
    if (dot(random_dir, surface_normal) < 0.0) {
        random_dir = -random_dir;
    }

    // Slerp between random and target direction
    float cos_angle = dot(random_dir, to_target);
    float angle = acos(clamp(cos_angle, -1.0, 1.0));
    float blend_angle = angle * (1.0 - bias_weight);

    // Rotate random_dir towards to_target
    vec3 axis = normalize(cross(random_dir, to_target));
    if (length(axis) < EPSILON) {
        return random_dir;  // Already aligned
    }

    float s = sin(blend_angle);
    float c = cos(blend_angle);
    mat3 rotation = mat3(
        c + axis.x * axis.x * (1.0 - c),
        axis.x * axis.y * (1.0 - c) - axis.z * s,
        axis.x * axis.z * (1.0 - c) + axis.y * s,

        axis.y * axis.x * (1.0 - c) + axis.z * s,
        c + axis.y * axis.y * (1.0 - c),
        axis.y * axis.z * (1.0 - c) - axis.x * s,

        axis.z * axis.x * (1.0 - c) - axis.y * s,
        axis.z * axis.y * (1.0 - c) + axis.x * s,
        c + axis.z * axis.z * (1.0 - c)
    );

    return normalize(rotation * random_dir);
}

// Sample direction with frustum bias (for photon tracing)
// Early bounces: explore scene uniformly
// Later bounces: concentrate photons towards visible regions
//
// Uses frustum thickness for smooth falloff - no hard edges!
vec3 sample_direction_frustum_bias(
    vec3 surface_normal,
    vec3 current_position,
    Frustum frustum,
    float bias_weight,
    inout uint rng_state
) {
    if (bias_weight < EPSILON) {
        // Uniform hemisphere sampling
        vec3 random_dir = random_unit_vector(rng_state);
        if (dot(random_dir, surface_normal) < 0.0) {
            random_dir = -random_dir;
        }
        return random_dir;
    }

    // Sample multiple directions, pick one with highest frustum weight
    // Number of candidates increases with bias strength
    int num_candidates = int(1.0 + bias_weight * 7.0);  // 1 to 8 candidates

    vec3 best_dir = vec3(0.0);
    float best_weight = -1.0;

    for (int i = 0; i < num_candidates; ++i) {
        vec3 candidate_dir = random_unit_vector(rng_state);
        if (dot(candidate_dir, surface_normal) < 0.0) {
            candidate_dir = -candidate_dir;
        }

        // Test point along ray
        // Use adaptive test distance based on scene scale
        float test_distance = max(1.0, frustum.thickness * 2.0);
        vec3 test_point = current_position + candidate_dir * test_distance;

        // Use built-in thickness for smooth falloff
        float weight = frustum_weight(frustum, test_point);

        if (weight > best_weight) {
            best_weight = weight;
            best_dir = candidate_dir;
        }
    }

    return normalize(best_dir);
}

// ============================================================================
// PHOTON OPERATIONS
// ============================================================================

// Create photon
Photon create_photon(vec3 position, vec3 direction, vec3 power) {
    Photon p;
    p.position = position;
    p.incident_direction = normalize(direction);
    p.power = power;
    return p;
}

// Estimate radiance from photon (cone filter)
// Jensen's original estimator: L = (1 / (π * r²)) * Σ(Φ_p * BRDF)
vec3 photon_radiance_estimate(
    vec3 surface_position,
    vec3 surface_normal,
    vec3 view_direction,
    Photon photon,
    float search_radius
) {
    // Distance from surface to photon
    float dist = length(photon.position - surface_position);

    if (dist > search_radius) {
        return vec3(0.0);  // Outside search radius
    }

    // Check if photon hit front face (similar normal direction)
    // Photon travels in direction of incident_direction
    vec3 photon_to_surface = -photon.incident_direction;
    float normal_alignment = dot(photon_to_surface, surface_normal);

    if (normal_alignment < 0.1) {
        return vec3(0.0);  // Wrong side of surface
    }

    // Cone filter (Jensen): weight = 1 - (d / (k * r))
    // k = 1.0 for basic cone, k > 1.0 for gentler falloff
    float k = 1.0;
    float weight = max(0.0, 1.0 - dist / (k * search_radius));

    // Radiance contribution
    // Φ / (π * r²) gives irradiance, multiply by BRDF
    float area = PI * search_radius * search_radius;
    vec3 irradiance = photon.power / area;

    return irradiance * weight;
}

// ============================================================================
// PROGRESSIVE RADIUS REDUCTION (SPPM)
// ============================================================================

// Update search radius for progressive photon mapping
// Hachisuka's formula: r_new = r_old * sqrt((N + α * M) / (N + M))
// Where: N = photon count from previous iteration
//        M = new photons added
//        α = reduction factor (typically 0.7)
float update_search_radius(
    float current_radius,
    int previous_photon_count,
    int new_photon_count,
    float alpha
) {
    if (previous_photon_count == 0) {
        return current_radius;
    }

    float N = float(previous_photon_count);
    float M = float(new_photon_count);

    float ratio = (N + alpha * M) / (N + M);
    return current_radius * sqrt(max(ratio, 0.01));  // Prevent collapse to zero
}

// Calculate progressive weight for flux accumulation
// Ensures unbiased convergence in SPPM
float calculate_flux_weight(
    int total_photon_count,
    int iteration_photons,
    float alpha
) {
    float N = float(total_photon_count);
    float M = float(iteration_photons);

    if (N < EPSILON) {
        return 1.0;
    }

    return (N + alpha * M) / (N + M);
}

// ============================================================================
// RUSSIAN ROULETTE PATH TERMINATION
// ============================================================================

// Decide whether to continue path (unbiased path termination)
// Returns: (continue, throughput_multiplier)
bool russian_roulette(
    vec3 current_throughput,
    uint bounce,
    uint min_bounces,
    inout uint rng_state,
    out float throughput_multiplier
) {
    if (bounce < min_bounces) {
        throughput_multiplier = 1.0;
        return true;  // Always continue for first few bounces
    }

    // Survival probability based on path throughput (luminance)
    float luminance = dot(current_throughput, vec3(0.2126, 0.7152, 0.0722));
    float survival_prob = clamp(luminance, 0.05, 0.95);

    float random_val = random_float(rng_state);
    if (random_val < survival_prob) {
        // Continue path, boost throughput to remain unbiased
        throughput_multiplier = 1.0 / survival_prob;
        return true;
    } else {
        // Terminate path
        throughput_multiplier = 0.0;
        return false;
    }
}

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/*
// PHASE 1: Photon Tracing from Light Source
void trace_photon_progressive(
    vec3 light_position,
    vec3 light_power,
    Frustum camera_frustum,
    PhotonMappingParams params,
    inout uint rng_state
) {
    vec3 photon_power = light_power / float(params.photons_per_iteration);
    vec3 position = light_position;
    vec3 throughput = vec3(1.0);

    // Initial direction (uniform from light)
    vec3 direction = random_unit_vector(rng_state);

    for (uint bounce = 0; bounce < params.max_bounces; ++bounce) {
        // Intersect with scene
        HitRecord hit;
        if (!intersect_scene(position, direction, hit)) {
            break;  // Escaped to infinity
        }

        // Calculate progressive bias weight
        float bias_weight = calculate_bounce_bias_weight(
            bounce, params.max_bounces, params.bias_start_bounce
        );

        // Store photon if on diffuse surface
        if (is_diffuse(hit.material)) {
            Photon photon = create_photon(
                hit.position,
                direction,
                photon_power * throughput
            );
            store_photon(photon);  // Add to photon map
        }

        // Scatter photon with progressive frustum bias
        direction = sample_direction_frustum_bias(
            hit.normal,
            hit.position,
            camera_frustum,
            bias_weight,
            rng_state
        );

        // Update throughput with BRDF
        throughput *= evaluate_brdf(hit.material, -direction, hit.normal, direction);

        // Russian roulette termination
        if (params.use_russian_roulette) {
            float multiplier;
            if (!russian_roulette(throughput, bounce, 3, rng_state, multiplier)) {
                break;
            }
            throughput *= multiplier;
        }

        position = hit.position + direction * EPSILON;
    }
}

// PHASE 2: Ray Tracing from Camera with Photon Gathering
vec3 trace_camera_ray_progressive(
    vec3 ray_origin,
    vec3 ray_direction,
    vec3 camera_position,
    PhotonMappingParams params,
    float current_search_radius,
    inout uint rng_state
) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 position = ray_origin;
    vec3 direction = ray_direction;

    for (uint bounce = 0; bounce < params.max_bounces; ++bounce) {
        HitRecord hit;
        if (!intersect_scene(position, direction, hit)) {
            radiance += throughput * sample_environment(direction);
            break;
        }

        // Add emissive contribution
        radiance += throughput * evaluate_emission(hit.material);

        // Calculate progressive bias weight
        float bias_weight = calculate_bounce_bias_weight(
            bounce, params.max_bounces, params.bias_start_bounce
        );

        // On diffuse surfaces, gather photons
        if (is_diffuse(hit.material)) {
            vec3 photon_radiance = gather_photons(
                hit.position,
                hit.normal,
                -direction,
                current_search_radius
            );
            radiance += throughput * photon_radiance;
        }

        // Scatter ray with progressive bias towards photon map
        direction = sample_direction_progressive_bias(
            hit.normal,
            camera_position,
            hit.position,
            bias_weight,
            rng_state
        );

        // Update throughput
        throughput *= evaluate_brdf(hit.material, -direction, hit.normal, direction);

        // Russian roulette
        if (params.use_russian_roulette) {
            float multiplier;
            if (!russian_roulette(throughput, bounce, 3, rng_state, multiplier)) {
                break;
            }
            throughput *= multiplier;
        }

        position = hit.position + direction * EPSILON;
    }

    return radiance;
}
*/

#endif // PHOTON_SAMPLING_GLSL
