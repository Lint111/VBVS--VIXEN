// OctreeTraversal-ESVO.glsl
// NVIDIA Efficient Sparse Voxel Octree (ESVO) traversal algorithm
// Ported from CUDA (Laine & Karras 2010) to GLSL
//
// Key innovations:
// 1. Implicit stack via scale parameter (no dynamic indexing)
// 2. Parametric t-value arithmetic for exact voxel boundaries
// 3. XOR-based octant mirroring (unified negative ray direction)
// 4. LOD termination via ray_size_coef
//
// References:
// - [15] Laine & Karras: "Efficient Sparse Voxel Octrees" (2010)
// - NVIDIA Optix implementation (production-proven)

#ifndef OCTREE_TRAVERSAL_ESVO_GLSL
#define OCTREE_TRAVERSAL_ESVO_GLSL

// ============================================================================
// CONSTANTS
// ============================================================================

const int S_MAX = 23;  // Maximum scale (float mantissa bits)
const float EPSILON_ESVO = exp2(-float(S_MAX));  // ~1.19e-7

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Stack entry for parent voxels (stored in local array, not dynamically indexed)
struct StackEntry {
    uint parentPtr;   // Pointer to parent node in octree buffer
    float tMax;       // Maximum t-value for this scale level
};

// Traversal result
struct HitResult {
    bool hit;         // True if ray hit solid voxel
    float t;          // Hit t-value (parametric distance)
    vec3 pos;         // Hit position in [0,1] octree space
    uint parentPtr;   // Parent node pointer (for brick access)
    uint childIdx;    // Child slot index (0-7)
    int scale;        // Hit scale (voxel size = 2^(scale - S_MAX))
};

// ============================================================================
// OCTREE NODE ACCESS (ESVO Format)
// ============================================================================

// ESVO node descriptor format (64 bits = ivec2)
// .x (32 bits):
//   bits 0-14:  child_mask (8 bits valid + 7 bits non-leaf, shifted by child index)
//   bit  16:    far_bit (if set, child pointer is indirect)
//   bits 17-31: child_offset (15 bits)
// .y (32 bits):
//   bits 0-6:   contour_mask (7 bits, optional surface detail)
//   bits 8-31:  contour_offset (24 bits, optional)

// Fetch ESVO node descriptor (ivec2 from SSBO or texture)
ivec2 fetchNodeDescriptor(uint nodePtr) {
    // Access octreeNodes buffer (binding 2)
    // ESVO uses 2 uints per node (8 bytes)
    uint baseOffset = nodePtr * 2u;

    // Note: Adjust based on your actual buffer layout
    // Current implementation uses 10 uints per node (40 bytes)
    // ESVO compact format would use 2 uints per node (8 bytes)

    ivec2 descriptor;
    descriptor.x = int(octreeNodes.data[baseOffset]);
    descriptor.y = int(octreeNodes.data[baseOffset + 1u]);
    return descriptor;
}

// Population count of lowest 8 bits (count set bits in mask)
int popc8(int mask) {
    return bitCount(mask & 0xFF);
}

// ============================================================================
// COORDINATE SPACE CONVERSION (C++ sync - Session 6Z)
// ============================================================================
// These functions match the C++ implementation in SVOTypes.h
//
// ESVO Mirrored Space Convention:
// - octant_mask starts at 7, XOR each bit for positive ray direction
// - bit=0 means axis IS mirrored (positive direction)
// - bit=1 means axis is NOT mirrored (negative direction)

// Mirror a local-space 8-bit mask to mirrored-space
// Use this for validMask/leafMask to make direct checks work with state.idx
int mirrorMask(int mask, int octant_mask) {
    // Fast path: no mirroring needed when all axes negative (octant_mask = 7)
    if (octant_mask == 7) {
        return mask;
    }

    // Flip bits where octant_mask has 0 (axis is mirrored)
    int flipMask = (~octant_mask) & 7;

    // Permute bits: for each local octant i, move its bit to mirrored position
    int result = 0;
    for (int i = 0; i < 8; i++) {
        int mirroredIdx = i ^ flipMask;
        if ((mask & (1 << i)) != 0) {
            result |= (1 << mirroredIdx);
        }
    }
    return result;
}

// Convert mirrored-space octant index to local-space for brick/descriptor lookup
int mirroredToLocalOctant(int mirroredIdx, int octant_mask) {
    return mirroredIdx ^ ((~octant_mask) & 7);
}

// ============================================================================
// ESVO RAY CASTING (Main Algorithm)
// ============================================================================

HitResult castRayESVO(
    vec3 rayOrigin,         // Ray origin in world space
    vec3 rayDir,            // Ray direction (normalized)
    vec3 gridMin,           // Octree AABB min (world space)
    vec3 gridMax,           // Octree AABB max (world space)
    float raySizeCoef,      // LOD coefficient (larger = coarser LOD)
    float raySizeBias       // LOD bias (increase along ray)
) {
    HitResult result;
    result.hit = false;
    result.t = 2.0;  // Miss value

    // ========================================================================
    // SETUP: Transform ray to octree space [1, 2]
    // ========================================================================

    // Transform ray to [0, 1] normalized space
    vec3 gridSize = gridMax - gridMin;
    vec3 p = (rayOrigin - gridMin) / gridSize;
    vec3 d = rayDir;  // Direction stays normalized

    // ESVO assumes octree in [1, 2] space - scale and offset
    p = p + 1.0;  // [0,1] → [1,2]

    // Get rid of small direction components (avoid division by zero)
    if (abs(d.x) < EPSILON_ESVO) d.x = sign(d.x) * EPSILON_ESVO;
    if (abs(d.y) < EPSILON_ESVO) d.y = sign(d.y) * EPSILON_ESVO;
    if (abs(d.z) < EPSILON_ESVO) d.z = sign(d.z) * EPSILON_ESVO;

    // ========================================================================
    // PRECOMPUTE: Parametric plane coefficients
    // ========================================================================

    // Coefficients for tx(x) = (x - px) / dx
    // Transformed to: tx(x) = x * tx_coef - tx_bias
    float tx_coef = 1.0 / -abs(d.x);
    float ty_coef = 1.0 / -abs(d.y);
    float tz_coef = 1.0 / -abs(d.z);

    float tx_bias = tx_coef * p.x;
    float ty_bias = ty_coef * p.y;
    float tz_bias = tz_coef * p.z;

    // ========================================================================
    // OCTANT MIRRORING: Unify ray direction to always negative
    // ========================================================================

    // XOR mask to flip octant indices based on ray direction
    int octant_mask = 7;
    if (d.x > 0.0) { octant_mask ^= 1; tx_bias = 3.0 * tx_coef - tx_bias; }
    if (d.y > 0.0) { octant_mask ^= 2; ty_bias = 3.0 * ty_coef - ty_bias; }
    if (d.z > 0.0) { octant_mask ^= 4; tz_bias = 3.0 * tz_coef - tz_bias; }

    // ========================================================================
    // RAY-OCTREE INTERSECTION: Initialize active t-span
    // ========================================================================

    // Entry point: t where ray enters [1,2] cube
    float t_min = max(max(2.0 * tx_coef - tx_bias,
                          2.0 * ty_coef - ty_bias),
                          2.0 * tz_coef - tz_bias);

    // Exit point: t where ray exits [1,2] cube
    float t_max = min(min(tx_coef - tx_bias,
                          ty_coef - ty_bias),
                          tz_coef - tz_bias);

    float h = t_max;
    t_min = max(t_min, 0.0);
    t_max = min(t_max, 1.0);

    // Early exit if ray misses octree
    if (t_min >= t_max) {
        return result;  // Miss
    }

    // ========================================================================
    // TRAVERSAL STATE: Initialize at root
    // ========================================================================

    StackEntry stack[S_MAX + 1];  // Implicit stack (local memory)

    uint parentPtr = 0u;          // Root node pointer
    ivec2 child_descriptor = ivec2(0, 0);  // Invalid until fetched
    int idx = 0;                  // Child slot index (0-7)
    vec3 pos = vec3(1.0);         // Current voxel position
    int scale = S_MAX - 1;        // Current scale level
    float scale_exp2 = 0.5;       // 2^(scale - S_MAX)

    // Select initial child octant based on ray entry point
    if (1.5 * tx_coef - tx_bias > t_min) { idx ^= 1; pos.x = 1.5; }
    if (1.5 * ty_coef - ty_bias > t_min) { idx ^= 2; pos.y = 1.5; }
    if (1.5 * tz_coef - tz_bias > t_min) { idx ^= 4; pos.z = 1.5; }

    // ========================================================================
    // MAIN TRAVERSAL LOOP: PUSH/POP/ADVANCE until hit or miss
    // ========================================================================

    while (scale < S_MAX) {
        // --------------------------------------------------------------------
        // FETCH: Load child descriptor if not cached
        // --------------------------------------------------------------------

        if (child_descriptor.x == 0) {
            child_descriptor = fetchNodeDescriptor(parentPtr);
        }

        // --------------------------------------------------------------------
        // INTERSECT: Compute voxel boundaries
        // --------------------------------------------------------------------

        // Evaluate tx(x), ty(y), tz(z) at cube corner
        float tx_corner = pos.x * tx_coef - tx_bias;
        float ty_corner = pos.y * ty_coef - ty_bias;
        float tz_corner = pos.z * tz_coef - tz_bias;
        float tc_max = min(min(tx_corner, ty_corner), tz_corner);

        // --------------------------------------------------------------------
        // CHILD CHECK: Process voxel if valid and active
        // --------------------------------------------------------------------

        int child_shift = idx ^ octant_mask;  // Permute based on mirroring
        int child_masks = child_descriptor.x << child_shift;

        // Check if child exists (valid bit set) and t-span non-empty
        if ((child_masks & 0x8000) != 0 && t_min <= t_max) {
            // ----------------------------------------------------------------
            // LOD TERMINATION: Stop if voxel small enough
            // ----------------------------------------------------------------

            if (tc_max * raySizeCoef + raySizeBias >= scale_exp2) {
                // Hit! Voxel is sufficiently detailed
                result.hit = true;
                result.t = t_min;
                result.parentPtr = parentPtr;
                result.childIdx = uint(idx ^ octant_mask ^ 7);
                result.scale = scale;
                break;
            }

            // ----------------------------------------------------------------
            // INTERSECT: Narrow t-span to voxel interior
            // ----------------------------------------------------------------

            float tv_max = min(t_max, tc_max);
            float half = scale_exp2 * 0.5;

            // Evaluate tx/ty/tz at voxel center
            float tx_center = half * tx_coef + tx_corner;
            float ty_center = half * ty_coef + ty_corner;
            float tz_center = half * tz_coef + tz_corner;

            // Note: Contour mask handling omitted (surface detail optimization)
            // Can be added later for high-quality surface representation

            // ----------------------------------------------------------------
            // PUSH: Descend to child if non-empty t-span
            // ----------------------------------------------------------------

            if (t_min <= tv_max) {
                // Check if node is non-leaf (has children)
                if ((child_masks & 0x0080) == 0) {
                    // Leaf node! This is a hit
                    result.hit = true;
                    result.t = t_min;
                    result.parentPtr = parentPtr;
                    result.childIdx = uint(idx ^ octant_mask ^ 7);
                    result.scale = scale;
                    break;
                }

                // PUSH: Store parent on stack
                if (tc_max < h) {
                    stack[scale].parentPtr = parentPtr;
                    stack[scale].tMax = t_max;
                }
                h = tc_max;

                // Find child pointer
                int ofs = (child_descriptor.x >> 17);  // Child offset (15 bits)
                if ((child_descriptor.x & 0x10000) != 0) {
                    // Far pointer (indirect)
                    ofs = int(octreeNodes.data[parentPtr * 2u + uint(ofs) * 2u]);
                }
                ofs += popc8(child_masks & 0x7F);
                parentPtr += uint(ofs) * 2u;

                // Select child voxel that ray enters first
                idx = 0;
                scale--;
                scale_exp2 = half;

                if (tx_center > t_min) { idx ^= 1; pos.x += scale_exp2; }
                if (ty_center > t_min) { idx ^= 2; pos.y += scale_exp2; }
                if (tz_center > t_min) { idx ^= 4; pos.z += scale_exp2; }

                // Update t-span and invalidate cache
                t_max = tv_max;
                child_descriptor.x = 0;
                continue;
            }
        }

        // --------------------------------------------------------------------
        // ADVANCE: Step to next voxel at current scale
        // --------------------------------------------------------------------

        int step_mask = 0;
        if (tx_corner <= tc_max) { step_mask ^= 1; pos.x -= scale_exp2; }
        if (ty_corner <= tc_max) { step_mask ^= 2; pos.y -= scale_exp2; }
        if (tz_corner <= tc_max) { step_mask ^= 4; pos.z -= scale_exp2; }

        t_min = tc_max;
        idx ^= step_mask;

        // --------------------------------------------------------------------
        // POP: Ascend if we exited parent bounds
        // --------------------------------------------------------------------

        if ((idx & step_mask) != 0) {
            // Find highest differing bit (determines scale to pop to)
            uint differing_bits = 0u;
            if ((step_mask & 1) != 0) {
                differing_bits |= floatBitsToUint(pos.x) ^ floatBitsToUint(pos.x + scale_exp2);
            }
            if ((step_mask & 2) != 0) {
                differing_bits |= floatBitsToUint(pos.y) ^ floatBitsToUint(pos.y + scale_exp2);
            }
            if ((step_mask & 4) != 0) {
                differing_bits |= floatBitsToUint(pos.z) ^ floatBitsToUint(pos.z + scale_exp2);
            }

            // Extract scale from exponent bits
            scale = int(floatBitsToUint(float(differing_bits)) >> 23) - 127;
            scale_exp2 = uintBitsToFloat((uint(scale - S_MAX + 127) << 23));

            // Restore parent from stack
            parentPtr = stack[scale].parentPtr;
            t_max = stack[scale].tMax;

            // Round position and extract child index
            int shx = int(floatBitsToUint(pos.x)) >> scale;
            int shy = int(floatBitsToUint(pos.y)) >> scale;
            int shz = int(floatBitsToUint(pos.z)) >> scale;

            pos.x = uintBitsToFloat(uint(shx) << scale);
            pos.y = uintBitsToFloat(uint(shy) << scale);
            pos.z = uintBitsToFloat(uint(shz) << scale);

            idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

            h = 0.0;
            child_descriptor.x = 0;
        }
    }

    // ========================================================================
    // OUTPUT: Undo mirroring and compute final hit position
    // ========================================================================

    if (scale >= S_MAX) {
        result.t = 2.0;  // Miss
        return result;
    }

    // Undo octant mirroring
    if ((octant_mask & 1) == 0) pos.x = 3.0 - scale_exp2 - pos.x;
    if ((octant_mask & 2) == 0) pos.y = 3.0 - scale_exp2 - pos.y;
    if ((octant_mask & 4) == 0) pos.z = 3.0 - scale_exp2 - pos.z;

    // Clamp hit position to voxel bounds
    vec3 hit_pos_oct;
    hit_pos_oct.x = clamp(p.x + result.t * d.x, pos.x + EPSILON_ESVO, pos.x + scale_exp2 - EPSILON_ESVO);
    hit_pos_oct.y = clamp(p.y + result.t * d.y, pos.y + EPSILON_ESVO, pos.y + scale_exp2 - EPSILON_ESVO);
    hit_pos_oct.z = clamp(p.z + result.t * d.z, pos.z + EPSILON_ESVO, pos.z + scale_exp2 - EPSILON_ESVO);

    // Transform back to [0,1] space
    result.pos = hit_pos_oct - 1.0;

    return result;
}

#endif // OCTREE_TRAVERSAL_ESVO_GLSL
