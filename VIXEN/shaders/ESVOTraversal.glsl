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

// Per-dispatch base offset added to every node fetch.
// Default 0 reproduces the single-octree dense path (VoxelRayMarch.comp never
// sets this).  Multi-octree shaders (BodyInstanceRayMarch.comp) set this to
// configs[oi].nodeArrayBase before each traversal call.
int g_esvoNodeBase = 0;

// Fetch node descriptor from ESVO buffer
// nodeIndex: Local index within the current octree's node array.
//            g_esvoNodeBase is added to address the concatenated multi-octree buffer.
// Returns: uvec2 containing validMask, leafMask, childPointer
uvec2 fetchESVONode(uint nodeIndex) {
    return esvoNodes[uint(g_esvoNodeBase) + nodeIndex];
}

// ============================================================================
// COORDINATE-BIT DESCENT — far-field candidate cell -> ESVO node ordinal
// ============================================================================
// W-COMPOSED far-field tier (RT + DDA twins, SceneBindings.glsl/
// RayQueryTraversal.glsl) previously shaded a degenerate entry-point sample
// (sampleHitShadingChannels at the cell's ray-entry voxel) once the footprint
// cutoff fired. That's not what the ordinary ESVO LOD cutoff does
// (SceneBindings.glsl ~1195): it reads a REAL baked mip sample
// (shadeFromMipSample) at the node ordinal it stopped descending at. The far-
// field DDA/RT search resolves a brick GRID cell (ivec3, 0..bpa-1 per axis)
// rather than an ESVO node pointer -- the coarse-grid brickLookup[] is a flat
// array, not the ESVO tree, so there is no node pointer sitting around to
// reuse. This function bridges that gap using the one SVO property that
// makes it cheap: bpa == 2^(esvoMaxScale - brickESVOScale) (ShellOctreeGpu.h
// stamps bricksPerAxis from the SAME builder depth that derives
// brickESVOScale), so canonicalCell's bits, read MSB-to-LSB, ARE the
// root-to-brick octant path -- no ray-marching, no per-level t-tests, just
// depth child-pointer hops (standard SVO coordinate-bit-descent property).
//
// MIRROR-FRAME CAVEAT (the likely parity trap, read before editing): the DDA/
// RT far-field candidates hand this function a CANONICAL (unmirrored) cell --
// same convention documented at SceneBindings.glsl ~1993 ("the DDA never
// mirrors so brick/gridEntry are already the canonical coordinates"). The
// REAL ESVO traversal, by contrast, walks in MIRRORED space (state.idx/pos,
// this file) and only unmirrors at the very end for brick/voxel addressing
// (unmirrorToLocalSpace, CoordinateTransforms.glsl; the same pattern
// handleLeafHitInstancedSdf's ESVO-leaf bridge follows). So each level's
// octant bit here is computed by mirroring canonicalCell's bit per-axis via
// octantMask (identical rule to unmirrorToLocalSpace at unit scale:
// mirroredBit = bit XOR (axis mirrored ? 1 : 0), the same XOR
// initRayCoefficients uses to build octant_mask itself), THEN converted
// mirrored->local via mirroredToLocalOctant before indexing validMask/
// childPointer -- descriptors are always stored in LOCAL (unmirrored) octant
// order (checkChildValidity/executePushPhase both do this same conversion at
// every ordinary hop, ~line 214/262 below). Skipping either conversion
// silently reads the wrong child at every level below the root.
//
// canonicalCell: UNMIRRORED brick-grid coordinate (0..bpa-1 per axis, same
//   frame the DDA backend's `cell` and the RT backend's `cell` already use).
// octantMask: this ray's octant mask (see octantMaskFromDir, SVOTypes.glsl).
// depth: the brick level's distance from root (see farFieldDescentDepth,
//   SVOTypes.glsl -- NOT assumed frame-spanning; round-3 fix item 2). Caller
//   derives this from octreeConfig's scale fields directly, sidestepping the
//   old findMSB(bpa)-on-a-maybe-non-power-of-two-bpa bug; the deeper
//   shallow-rooted-tree case is a separate, NOT-fully-closed gap -- see
//   farFieldDescentDepth's own header for why.
// worldDistToBrick / brickWorldSize: the SAME quantities the caller already
//   derived for its own (brick-level) footprint test, in WORLD units.
//   ESVO LEVEL-SELECTION CRITERION (round-3 fix item 1 -- reuse verbatim,
//   not an analog): SceneBindings.glsl's non-leaf LOD gate stops descending
//   at the FIRST level whose footprint crosses (tv_max*raySizeCoef+
//   raySizeBias >= scale_exp2). Each level's node is exactly 2x the size of
//   the next (standard octree scale halving), and worldDistToBrick is a
//   good approximation of "distance to this node's content" at every level
//   on this path (the candidate cell's own entry distance -- the same
//   approximation the caller already made at the brick level). So the ratio
//   test needed is: nodeWorldSize (== brickWorldSize * 2^(depth-1-level))
//   compared against worldDistToBrick*raySizeCoef+raySizeBias, level by
//   level from the root down -- stopping at the FIRST (coarsest) level that
//   crosses, exactly mirroring the ordinary ESVO gate's early-stop.
// Returns false (leaving nodeOrdinal at the last internal node visited) if
// the path runs into a missing child, a farBit/tier-crossing descriptor
// (round-3 fix item 2 -- not interpreted here, caller falls back), or a leaf
// above the level the criterion selected -- caller falls back exactly like
// the ordinary "no mip coverage" case does.
// outLevel (batch-32 JOB 1): the LEVEL (levels-above-brick, 0 == brick
// itself) nodeOrdinal was actually resolved at -- depth-level at every
// return site below. Lets the caller record which level fed the sample it
// just shaded (recordFarFieldSampledLevel, SceneBindings.glsl), independent
// of nodeOrdinal itself.
bool descendToNodeOrdinal(ivec3 canonicalCell, int octantMask, int depth,
                          float worldDistToBrick, float brickWorldSize,
                          out uint nodeOrdinal, out uint outLevel) {
    uint parentPtr = 0u;
    nodeOrdinal = 0u;
    outLevel = uint(depth);
    float footprint = worldDistToBrick * pc.raySizeCoef + pc.raySizeBias;
#ifdef VIXEN_MIP_POLICY
    // Deep-Field Mip-Accessor Policy (design doc §regimes, regime 2 MIP HIT):
    // consult the ONE shared footprint->level function (mipPolicyLevel,
    // SVOTypes.glsl) instead of this loop's own per-hop crossing test --
    // same arithmetic (brickWorldSize*2^level), now shared with the DDA/RT
    // gate sites so every backend's level choice agrees. targetLevel is
    // levels-above-brick (0 = brick itself); the walk below still hops
    // node-by-node (descriptor fetch requires it -- no direct addressing
    // above brick level), it just stops at the POLICY's level instead of
    // re-deriving the crossing per hop.
    int policyTargetLevel = mipPolicyLevel(footprint, brickWorldSize, depth);
    recordPolicyLevel(uint(policyTargetLevel));  // batch-29 JOB 3: level histogram
#endif
    for (int level = depth - 1; level >= 0; --level) {
        // ESVO criterion, evaluated BEFORE descending into this level's
        // children: this node's (the current parentPtr's) world size is
        // brickWorldSize * 2^(level+1) (level==depth-1 => root's own child
        // cube; level==0's node, about to be descended, is the brick's
        // immediate parent, size brickWorldSize*2). Stop and return the
        // CURRENT node (coarser than brick level) the first time the
        // footprint already covers it -- matches "descend and stop at the
        // first level whose footprint crossing fires" verbatim.
#ifdef VIXEN_MIP_POLICY
        // INVARIANT (batch-30 fix; the shipped '>=' made this branch inert):
        // the loop DESCENDS (level = depth-1 .. 0), so on entry level+1 ==
        // depth and policyTargetLevel <= maxLevel == depth. With '>=' the
        // test fired on iteration 1 for EVERY input and returned the ROOT
        // (validator sweep: 9600/9600 coarsest, 65.0% mismatch vs flag-off).
        // Correct shape: keep DESCENDING while this node is still coarser
        // than the policy's answer, and stop at EQUALITY -- i.e. return the
        // node whose own level (level+1) equals policyTargetLevel, which is
        // exactly the level mipPolicyLevel() selected for this footprint.
        if (pc.raySizeCoef > 0.0 && (level + 1) <= policyTargetLevel) {
            nodeOrdinal = parentPtr;
            outLevel = uint(level + 1);
            return true;
        }
#else
        float nodeWorldSize = brickWorldSize * float(1u << uint(level + 1));
        if (pc.raySizeCoef > 0.0 && footprint >= nodeWorldSize) {
            nodeOrdinal = parentPtr;
            outLevel = uint(level + 1);
            return true;
        }
#endif

        uvec2 descriptor = fetchESVONode(parentPtr);
        uint validMask = getValidMask(descriptor);
        uint leafMask  = getLeafMask(descriptor);

        // MSB-to-LSB: bit `level` of each axis selects the octant at this
        // depth (bpa's binary expansion IS the root-to-brick path, per the
        // header derivation above). Mirror into ESVO traversal-space per
        // axis, then convert mirrored->local for descriptor indexing.
        int mirroredIdx = 0;
        if (((canonicalCell.x >> level) & 1) != 0) mirroredIdx |= 1;
        if (((canonicalCell.y >> level) & 1) != 0) mirroredIdx |= 2;
        if (((canonicalCell.z >> level) & 1) != 0) mirroredIdx |= 4;
        mirroredIdx ^= ((~octantMask) & 7);  // same rule mirroredToLocalOctant applies
        int localIdx = mirroredToLocalOctant(mirroredIdx, octantMask);

        if (!childExists(validMask, localIdx)) {
            nodeOrdinal = parentPtr;
            outLevel = uint(level + 1);
            recordFarFieldDescentFailLevel(uint(depth - level));  // round-13 probe #2
            return false;
        }
        bool isLastHop = (level == 0);
        bool childLeaf = childIsLeaf(leafMask, localIdx);
        if (childLeaf) {
            // farBit guard (round-3 fix item 2, SVOTypes.glsl:84-96): a leaf
            // with farBit set is a TIER-CROSSING reference, not a brick --
            // descriptor.y's bits mean something else entirely there. This
            // function only understands the brick-mode interpretation, so
            // fail closed rather than resolve a wrong index.
            if (getFarBit(descriptor)) {
                nodeOrdinal = parentPtr;
                outLevel = uint(level + 1);
                recordFarFieldDescentFailLevel(uint(depth - level));  // round-13 probe #2
                return false;
            }
            // Leaf reached: at the brick level this is the real target; a
            // leaf found ABOVE the brick level means a coarser LOD collapsed
            // this branch -- return its ordinal anyway (still a valid,
            // shallower mip sample) but report false so the caller knows it
            // didn't reach the requested depth.
            nodeOrdinal = resolveLeafDescriptorIndex(descriptor, validMask, leafMask, localIdx);
            outLevel = uint(level);  // level 0 == brick itself when isLastHop
            if (!isLastHop) recordFarFieldDescentFailLevel(uint(depth - level));  // round-13 probe #2 (early-leaf collapse)
            return isLastHop;
        }
        if (isLastHop) {
            // Internal node still standing exactly at the brick level
            // (shouldn't happen for a resident brick -- fail closed, this
            // node's own ordinal is still a valid coarser mip sample).
            nodeOrdinal = parentPtr;
            outLevel = uint(level + 1);
            recordFarFieldDescentFailLevel(uint(depth - level));  // round-13 probe #2
            return false;
        }

        // farBit guard on the internal hop too (round-3 fix item 2): an
        // indirect childPointer means getChildPointer's plain &0x7FFF mask
        // truncated a real offset into the far-pointer table. Not resolved
        // here -- fail closed exactly like the leaf case above.
        if (getFarBit(descriptor)) {
            nodeOrdinal = parentPtr;
            outLevel = uint(level + 1);
            recordFarFieldDescentFailLevel(uint(depth - level));  // round-13 probe #2
            return false;
        }

        // Internal hop: childPointer + count of non-leaf siblings before us
        // (identical arithmetic to executePushPhase's childLocalIndex below).
        uint childPointer = getChildPointer(descriptor);
        uint nonLeafMask = validMask & ~leafMask;
        uint maskBeforeChild = (1u << localIdx) - 1u;
        uint childLocalIndex = bitCount(nonLeafMask & maskBeforeChild);
        parentPtr = childPointer + childLocalIndex;
    }
    nodeOrdinal = parentPtr;
    outLevel = 0u;  // full descent reached the brick itself
    return true;
}

// ============================================================================
// DEBUG STATE SNAPSHOT
// ============================================================================

// Snapshot current traversal state for debug visualization. Called up to 8x per
// traversal loop iteration (see traverseOctreeInstancedOnce in SceneBindings.glsl) --
// none of its output fields (scale/stateIdx/tMin/tMax/scaleExp2/posMirrored/localNorm)
// are read by the live shading path (baked-perf-pipeline M2, audit D1); only
// debugInfo.iterationCount (set directly by the call sites, NOT by this function) and
// debugInfo.hitFlag/exitCode/instIdx-adjacent bookkeeping stay live, since
// instanceIterCount[] readback is a real test dependency (Inc1 M4b occlusion-reject +
// tier-crossing tests). Gated on VIXEN_GPU_TRACE_HOOKS, same mechanism as
// TraceRecording.glsl -- the no-op stub below skips computeLocalNorm's branch+clamp
// work and the 6 struct-field writes entirely.
#ifdef VIXEN_GPU_TRACE_HOOKS
void snapshotTraversalState(TraversalState state, RayCoefficients coef, inout DebugRaySample info) {
    info.scale = state.scale;
    info.stateIdx = uint(max(state.idx, 0));
    info.tMin = state.t_min;
    info.tMax = state.t_max;
    info.scaleExp2 = state.scale_exp2;
    info.posMirrored = state.pos;
    info.localNorm = computeLocalNorm(state.pos, state.scale_exp2, coef.octant_mask);
}
#else
void snapshotTraversalState(TraversalState state, RayCoefficients coef, inout DebugRaySample info) {}
#endif // VIXEN_GPU_TRACE_HOOKS

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
