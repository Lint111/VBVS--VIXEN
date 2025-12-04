// ============================================================================
// ESVOTraversal.glsl - Core ESVO Octree Traversal Algorithm
// ============================================================================
// Implements the NVIDIA Efficient Sparse Voxel Octree traversal from:
//   Laine & Karras (2010): "Efficient Sparse Voxel Octrees"
//   NVIDIA Research, Section 3: Raycasting Algorithm
//
// Three-phase DFS traversal: PUSH (descend), ADVANCE (sibling), POP (ascend)
// Uses IEEE 754 float bit manipulation for efficient scale computation.
//
// Dependencies:
//   - SVOTypes.glsl (descriptor accessors, octant mirroring)
//   - ESVOCoefficients.glsl (RayCoefficients struct)
//   - octreeConfig UBO
// ============================================================================

#ifndef ESVO_TRAVERSAL_GLSL
#define ESVO_TRAVERSAL_GLSL

// ============================================================================
// CONSTANTS
// ============================================================================

const int STACK_SIZE = 23;      // Must cover full ESVO range (esvoMaxScale + 1)
const int MAX_ITERS = 512;      // Maximum traversal iterations per ray
const float EPSILON = 1e-6;     // General epsilon for floating point comparisons
const float DIR_EPSILON = 1e-5; // Epsilon for ray direction (axis-parallel detection)

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Stack entry for DFS traversal (matches CastStack in C++)
struct StackEntry {
    uint parentPtr;   // Parent node pointer
    float t_max;      // Maximum t value for this level
};

// Traversal state (matches ESVOTraversalState in C++)
struct TraversalState {
    uint parentPtr;     // Current parent node pointer
    int idx;            // Current child octant index (0-7) in MIRRORED space
    int scale;          // Current ESVO scale (16-22 for USER_MAX_LEVELS=7)
    float scale_exp2;   // 2^(scale - ESVO_MAX_SCALE)
    vec3 pos;           // Position in normalized [1,2] space
    float t_min, t_max; // Current t-span
    float h;            // Horizon value for stack management
};

// ============================================================================
// ESVO NODE FETCH
// ============================================================================

// Fetch node descriptor from ESVO buffer
// nodeIndex: Index into esvoNodes array
// Returns: uvec2 containing validMask, leafMask, childPointer
uvec2 fetchESVONode(uint nodeIndex) {
    return esvoNodes[nodeIndex];
}

// ============================================================================
// DEBUG STATE SNAPSHOT
// ============================================================================

// Snapshot current traversal state for debug visualization
void snapshotTraversalState(TraversalState state, RayCoefficients coef, inout DebugRaySample info) {
    info.scale = state.scale;
    info.stateIdx = uint(max(state.idx, 0));
    info.tMin = state.t_min;
    info.tMax = state.t_max;
    info.scaleExp2 = state.scale_exp2;
    info.posMirrored = state.pos;
    info.localNorm = computeLocalNorm(state.pos, state.scale_exp2, coef.octant_mask);
}

// ============================================================================
// TRAVERSAL STATE INITIALIZATION
// ============================================================================

// Initialize traversal state at octree root
// coef: Ray coefficients from initRayCoefficients()
// stack: Stack array to initialize
// rayStartsInside: True if ray origin is inside the volume
TraversalState initTraversalState(RayCoefficients coef, inout StackEntry stack[STACK_SIZE], bool rayStartsInside) {
    TraversalState state;

    // Root node t-span computation depends on whether ray starts inside or outside
    if (rayStartsInside) {
        // Interior ray: t_min = 0 (already inside), t_max = exit from [1,2]^3
        state.t_min = 0.0;
        state.t_max = min(min(coef.tx_coef - coef.tx_bias,
                             coef.ty_coef - coef.ty_bias),
                         coef.tz_coef - coef.tz_bias);
    } else {
        // Exterior ray: enters [1,2]^3 from outside
        state.t_min = max(max(2.0 * coef.tx_coef - coef.tx_bias,
                             2.0 * coef.ty_coef - coef.ty_bias),
                         2.0 * coef.tz_coef - coef.tz_bias);
        state.t_max = min(min(coef.tx_coef - coef.tx_bias,
                             coef.ty_coef - coef.ty_bias),
                         coef.tz_coef - coef.tz_bias);
    }

    state.h = state.t_max;  // CRITICAL: h must be initialized to t_max
    state.t_min = max(state.t_min, 0.0);

    // Initialize traversal at root
    state.parentPtr = 0u;
    state.scale = octreeConfig.esvoMaxScale;
    state.scale_exp2 = 0.5;  // exp2(scale - esvoMaxScale - 1)
    state.pos = vec3(1.0);

    // Initialize stack with root at all scales
    for (int s = 0; s < STACK_SIZE; s++) {
        stack[s].parentPtr = 0u;
        stack[s].t_max = state.t_max;
    }

    // Select initial child octant based on ray entry point
    state.idx = 0;
    const float boundary_epsilon = 1e-4;
    bool usePositionBased = (state.t_min < boundary_epsilon);

    // Compute mirrored origin for position-based selection
    vec3 mirroredOrigin;
    mirroredOrigin.x = ((coef.octant_mask & 1) != 0) ? coef.normOrigin.x : (3.0 - coef.normOrigin.x);
    mirroredOrigin.y = ((coef.octant_mask & 2) != 0) ? coef.normOrigin.y : (3.0 - coef.normOrigin.y);
    mirroredOrigin.z = ((coef.octant_mask & 4) != 0) ? coef.normOrigin.z : (3.0 - coef.normOrigin.z);

    // X axis selection
    if (abs(coef.rayDir.x) < DIR_EPSILON || usePositionBased) {
        if (mirroredOrigin.x >= 1.5) { state.idx |= 1; state.pos.x = 1.5; }
    } else {
        if (1.5 * coef.tx_coef - coef.tx_bias > state.t_min) { state.idx ^= 1; state.pos.x = 1.5; }
    }

    // Y axis selection
    if (abs(coef.rayDir.y) < DIR_EPSILON || usePositionBased) {
        if (mirroredOrigin.y >= 1.5) { state.idx |= 2; state.pos.y = 1.5; }
    } else {
        if (1.5 * coef.ty_coef - coef.ty_bias > state.t_min) { state.idx ^= 2; state.pos.y = 1.5; }
    }

    // Z axis selection
    if (abs(coef.rayDir.z) < DIR_EPSILON || usePositionBased) {
        if (mirroredOrigin.z >= 1.5) { state.idx |= 4; state.pos.z = 1.5; }
    } else {
        if (1.5 * coef.tz_coef - coef.tz_bias > state.t_min) { state.idx ^= 4; state.pos.z = 1.5; }
    }

    return state;
}

// ============================================================================
// VOXEL CORNER COMPUTATION
// ============================================================================

// Compute t values at voxel corners for t-span calculation
void computeVoxelCorners(vec3 pos, RayCoefficients coef,
                         out float tx_corner, out float ty_corner, out float tz_corner) {
    tx_corner = pos.x * coef.tx_coef - coef.tx_bias;
    ty_corner = pos.y * coef.ty_coef - coef.ty_bias;
    tz_corner = pos.z * coef.tz_coef - coef.tz_bias;
}

// Compute corrected tc_max for axis-parallel rays
// Filters out misleading corner values from perpendicular axes
float computeCorrectedTcMax(float tx_corner, float ty_corner, float tz_corner,
                            vec3 rayDir, float t_max) {
    const float corner_threshold = 1000.0;

    bool useXCorner = (abs(rayDir.x) >= DIR_EPSILON);
    bool useYCorner = (abs(rayDir.y) >= DIR_EPSILON);
    bool useZCorner = (abs(rayDir.z) >= DIR_EPSILON);

    float tx_valid = (useXCorner && abs(tx_corner) < corner_threshold) ? tx_corner : t_max;
    float ty_valid = (useYCorner && abs(ty_corner) < corner_threshold) ? ty_corner : t_max;
    float tz_valid = (useZCorner && abs(tz_corner) < corner_threshold) ? tz_corner : t_max;

    return min(min(tx_valid, ty_valid), tz_valid);
}

// ============================================================================
// CHILD VALIDITY CHECK
// ============================================================================

// Check if current child is valid and compute t-span intersection
// Returns: true if child should be processed, false to skip
bool checkChildValidity(TraversalState state, RayCoefficients coef,
                        uint validMask, uint leafMask,
                        out bool isLeaf, out float tv_max,
                        out float tx_center, out float ty_center, out float tz_center) {
    // Convert mirrored-space idx to local-space for descriptor lookup
    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);

    // Check if child exists using local-space validMask
    bool child_valid = childExists(validMask, localChildIdx);
    isLeaf = childIsLeaf(leafMask, localChildIdx);

    if (!child_valid || state.t_min > state.t_max + EPSILON) {
        return false;
    }

    // Compute corner values
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    // Use corrected tc_max for axis-parallel rays
    float tc_max = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);
    tv_max = min(state.t_max, tc_max);

    // Compute center values for octant selection after PUSH
    float halfScale = state.scale_exp2 * 0.5;
    tx_center = halfScale * coef.tx_coef + tx_corner;
    ty_center = halfScale * coef.ty_coef + ty_corner;
    tz_center = halfScale * coef.tz_coef + tz_corner;

    return state.t_min <= tv_max + EPSILON;
}

// ============================================================================
// PUSH PHASE - Descend to child node
// ============================================================================

void executePushPhase(inout TraversalState state, RayCoefficients coef,
                      inout StackEntry stack[STACK_SIZE],
                      uint validMask, uint leafMask, uint childPointer,
                      float tv_max, float tx_center, float ty_center, float tz_center) {
    // Compute tc_max for stack management
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);
    float tc_max = min(min(tx_corner, ty_corner), tz_corner);

    // Push current state to stack
    if (state.scale >= 0 && state.scale < STACK_SIZE) {
        stack[state.scale].parentPtr = state.parentPtr;
        stack[state.scale].t_max = state.t_max;
    }
    state.h = tc_max;

    // Convert mirrored idx to local space for child offset calculation
    int worldIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);

    uint nonLeafMask = validMask & ~leafMask;
    uint mask_before_child = (1u << worldIdx) - 1u;
    uint childLocalIndex = bitCount(nonLeafMask & mask_before_child);

    state.parentPtr = childPointer + childLocalIndex;

    // Descend to next level
    state.idx = 0;
    state.scale--;
    float halfScale = state.scale_exp2 * 0.5;
    state.scale_exp2 = halfScale;

    // Select child octant using parent's center values
    if (tx_center > state.t_min) { state.idx ^= 1; state.pos.x += state.scale_exp2; }
    if (ty_center > state.t_min) { state.idx ^= 2; state.pos.y += state.scale_exp2; }
    if (tz_center > state.t_min) { state.idx ^= 4; state.pos.z += state.scale_exp2; }

    // Update t-span
    state.t_max = tv_max;
}

// ============================================================================
// ADVANCE PHASE - Move to sibling octant
// ============================================================================

// Returns: 0 = CONTINUE, 1 = POP_NEEDED
int executeAdvancePhase(inout TraversalState state, RayCoefficients coef, out int step_mask) {
    // Compute corner values
    float tx_corner, ty_corner, tz_corner;
    computeVoxelCorners(state.pos, coef, tx_corner, ty_corner, tz_corner);

    // Determine which axes can step (non-parallel)
    bool canStepX = (abs(coef.rayDir.x) >= DIR_EPSILON);
    bool canStepY = (abs(coef.rayDir.y) >= DIR_EPSILON);
    bool canStepZ = (abs(coef.rayDir.z) >= DIR_EPSILON);

    // Compute corrected tc_max
    float tc_max = computeCorrectedTcMax(tx_corner, ty_corner, tz_corner, coef.rayDir, state.t_max);

    // Fallback for fully axis-parallel rays
    if (tc_max >= 1e10) {
        float fallbackX = canStepX ? tx_corner : -1e10;
        float fallbackY = canStepY ? ty_corner : -1e10;
        float fallbackZ = canStepZ ? tz_corner : -1e10;
        tc_max = max(max(fallbackX, fallbackY), fallbackZ);
    }

    // Step along axes at their exit boundary (in mirrored space, pos decreases)
    step_mask = 0;
    if (canStepX && tx_corner <= tc_max) { step_mask ^= 1; state.pos.x -= state.scale_exp2; }
    if (canStepY && ty_corner <= tc_max) { step_mask ^= 2; state.pos.y -= state.scale_exp2; }
    if (canStepZ && tz_corner <= tc_max) { step_mask ^= 4; state.pos.z -= state.scale_exp2; }

    state.t_min = max(tc_max, 0.0);
    state.idx ^= step_mask;

    // Check if we need to POP (bit flips disagree with ray direction)
    if ((state.idx & step_mask) != 0) {
        return 1;  // POP_NEEDED
    }

    return 0;  // CONTINUE
}

// ============================================================================
// POP PHASE - Ascend to ancestor node
// ============================================================================

// Returns: 0 = CONTINUE, 1 = EXIT_OCTREE
// Uses IEEE 754 float bit manipulation for efficient scale computation
// Reference: NVIDIA ESVO Raycast.inl lines 294-327
int executePopPhase(inout TraversalState state, RayCoefficients coef,
                    inout StackEntry stack[STACK_SIZE], int step_mask) {
    // For root scale, check for octree exit
    if (state.scale >= octreeConfig.esvoMaxScale) {
        if (state.t_min > state.t_max ||
            state.pos.x < 1.0 || state.pos.x >= 2.0 ||
            state.pos.y < 1.0 || state.pos.y >= 2.0 ||
            state.pos.z < 1.0 || state.pos.z >= 2.0) {
            return 1;  // EXIT_OCTREE
        }
        return 0;  // CONTINUE at root
    }

    // IEEE 754 bit manipulation: Find highest differing bit
    uint differing_bits = 0u;
    if ((step_mask & 1) != 0)
        differing_bits |= floatBitsToUint(state.pos.x) ^ floatBitsToUint(state.pos.x + state.scale_exp2);
    if ((step_mask & 2) != 0)
        differing_bits |= floatBitsToUint(state.pos.y) ^ floatBitsToUint(state.pos.y + state.scale_exp2);
    if ((step_mask & 4) != 0)
        differing_bits |= floatBitsToUint(state.pos.z) ^ floatBitsToUint(state.pos.z + state.scale_exp2);

    if (differing_bits == 0u) {
        return 1;  // EXIT_OCTREE
    }

    // Extract scale from highest bit using IEEE 754 exponent extraction
    state.scale = int((floatBitsToUint(float(differing_bits)) >> 23u) - 127u);

    // Compute scale_exp2 from scale
    state.scale_exp2 = uintBitsToFloat(uint(state.scale - octreeConfig.esvoMaxScale - 1 + 127) << 23u);

    // Validate scale range
    int minESVOScale = octreeConfig.minESVOScale;
    if (state.scale < minESVOScale || state.scale > octreeConfig.esvoMaxScale) {
        return 1;  // EXIT_OCTREE
    }

    // Restore from stack
    state.parentPtr = stack[state.scale].parentPtr;
    state.t_max = stack[state.scale].t_max;

    // Round position by shifting float bits (quantize to voxel boundary)
    uint shx = floatBitsToUint(state.pos.x) >> uint(state.scale);
    uint shy = floatBitsToUint(state.pos.y) >> uint(state.scale);
    uint shz = floatBitsToUint(state.pos.z) >> uint(state.scale);
    state.pos.x = uintBitsToFloat(shx << uint(state.scale));
    state.pos.y = uintBitsToFloat(shy << uint(state.scale));
    state.pos.z = uintBitsToFloat(shz << uint(state.scale));

    // Extract child index from shifted position bits
    state.idx = int(shx & 1u) | (int(shy & 1u) << 1) | (int(shz & 1u) << 2);

    // Prevent same parent from being stored again
    state.h = 0.0;

    return 0;  // CONTINUE
}

#endif // ESVO_TRAVERSAL_GLSL
