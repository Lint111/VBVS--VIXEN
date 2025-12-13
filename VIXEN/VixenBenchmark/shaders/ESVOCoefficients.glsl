// ============================================================================
// ESVOCoefficients.glsl - Ray Parametric Coefficient Initialization
// ============================================================================
// Implements the NVIDIA ESVO ray coefficient setup from:
//   Laine & Karras (2010): "Efficient Sparse Voxel Octrees"
//   Section 3.2: Ray Parametrization
//
// The ray is represented parametrically as: p(t) = origin + t * direction
// Coefficients tx_coef, ty_coef, tz_coef encode the t-span boundaries.
// The octant_mask handles ray direction mirroring for efficient traversal.
//
// Dependencies:
//   - octreeConfig UBO (worldToLocal, esvoMaxScale)
//   - RayGeneration.glsl (worldToNormalized)
// ============================================================================

#ifndef ESVO_COEFFICIENTS_GLSL
#define ESVO_COEFFICIENTS_GLSL

// ============================================================================
// RAY COEFFICIENTS STRUCTURE
// ============================================================================

// Ray coefficients for parametric traversal (matches ESVORayCoefficients in C++)
struct RayCoefficients {
    float tx_coef, ty_coef, tz_coef;  // Parametric ray coefficients
    float tx_bias, ty_bias, tz_bias;  // Parametric ray biases
    int octant_mask;                   // Ray direction octant (0-7)
    vec3 rayDir;                       // Original ray direction (world space)
    vec3 normOrigin;                   // Normalized origin in [1,2] space
};

// ============================================================================
// RAY COEFFICIENT INITIALIZATION
// ============================================================================

// Initialize ray coefficients for ESVO traversal
// rayDir: Normalized ray direction in world space
// rayStartWorld: Ray origin in world space (entry point if external)
//
// This implements the NVIDIA ESVO coordinate transform:
// 1. Transform ray to ESVO [1,2]^3 normalized space
// 2. Mirror ray direction into positive octant (octant_mask tracks original)
// 3. Compute parametric coefficients for t-span calculations
//
// The mirroring trick allows the traversal to always process octants
// in a consistent order (idx 0,1,2...7) regardless of ray direction.
RayCoefficients initRayCoefficients(vec3 rayDir, vec3 rayStartWorld) {
    RayCoefficients coef;
    coef.rayDir = rayDir;

    // Transform origin to ESVO normalized [1,2] space
    vec3 p = worldToNormalized(rayStartWorld);
    coef.normOrigin = p;

    // Transform ray direction to local space (rotation only, no translation)
    vec3 d = mat3(octreeConfig.worldToLocal) * rayDir;

    // Handle near-zero direction components to avoid division issues
    // This prevents NaN/Inf in t-span calculations for axis-parallel rays
    float epsilon_esvo = exp2(-float(octreeConfig.esvoMaxScale));
    float sx = d.x >= 0.0 ? 1.0 : -1.0;
    float sy = d.y >= 0.0 ? 1.0 : -1.0;
    float sz = d.z >= 0.0 ? 1.0 : -1.0;
    if (abs(d.x) < epsilon_esvo) d.x = sx * epsilon_esvo;
    if (abs(d.y) < epsilon_esvo) d.y = sy * epsilon_esvo;
    if (abs(d.z) < epsilon_esvo) d.z = sz * epsilon_esvo;

    // Compute parametric coefficients
    // t = coef * pos - bias gives the t value at position pos
    // The negative sign handles the ESVO mirroring convention
    coef.tx_coef = 1.0 / -abs(d.x);
    coef.ty_coef = 1.0 / -abs(d.y);
    coef.tz_coef = 1.0 / -abs(d.z);
    coef.tx_bias = coef.tx_coef * p.x;
    coef.ty_bias = coef.ty_coef * p.y;
    coef.tz_bias = coef.tz_coef * p.z;

    // Compute octant mask based on ray direction
    // octant_mask encodes which octants the ray enters first
    // XOR with 1,2,4 flips the corresponding bit when direction is positive
    coef.octant_mask = 7;
    if (d.x > 0.0) { coef.octant_mask ^= 1; coef.tx_bias = 3.0 * coef.tx_coef - coef.tx_bias; }
    if (d.y > 0.0) { coef.octant_mask ^= 2; coef.ty_bias = 3.0 * coef.ty_coef - coef.ty_bias; }
    if (d.z > 0.0) { coef.octant_mask ^= 4; coef.tz_bias = 3.0 * coef.tz_coef - coef.tz_bias; }

    return coef;
}

#endif // ESVO_COEFFICIENTS_GLSL
