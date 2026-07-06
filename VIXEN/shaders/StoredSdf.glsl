// ============================================================================
// StoredSdf.glsl — Inc3 M3: Generic multi-channel pool readers + iso-surface march.
// ============================================================================
// Included by BodyInstanceRayMarch.comp AFTER:
//   • OctreeConfig struct (binding 5), g_octreeIdx, octreeConfig macro
//   • VoxelChannelFormat.glsl:  SEM_* / FK_* defines
//   • ChannelPoolBuffer (binding 11): float channelPool[]
//   • BrickLookupBuffer (binding 12): uint  brickLookup[]
//
// Inc3 M3 replaces the per-semantic sdfData[] with a generic SoA channelPool[]:
//   channelPool[poolBrickBase + brickIdx*brickStrideFloats + channelBase + comp*512 + voxel]
// where poolBrickBase / brickStrideFloats / channels[] come from OctreeConfig.
//
// Inc2 M6: the Stored-SDF path REUSES the ESVO octree traversal
// (traverseOctreeInstanced). At each ESVO leaf brick, handleLeafHitInstancedSdf
// (in BodyInstanceRayMarch.comp) calls marchBrickSdf below to sphere-trace the
// trilinear iso-surface within that ONE brick. The standalone flat sphere-trace
// (marchStoredSdf) has been retired — the octree, not a flat march, handles
// empty-space skipping and brick→brick movement.
//
// These helpers are dead code when formatId == FORMAT_BINARY — the leaf-hit
// dispatch in traverseOctreeInstanced guards them with formatId == FORMAT_STORED_SDF.
// ============================================================================
#ifndef STORED_SDF_GLSL
#define STORED_SDF_GLSL

// ---------------------------------------------------------------------------
// Internal helper: convert a brick-grid 3D coordinate to a flat uint32 index
// into brickLookup[]. Returns 0xFFFFFFFFu for out-of-grid coords.
// bpa = bricksPerAxisSdf = octree.bricksPerAxis for the SDF grid.
// ---------------------------------------------------------------------------
uint _gridToLookupIdx(ivec3 brickCoord, int bpa) {
    if (any(lessThan(brickCoord, ivec3(0))) ||
        any(greaterThanEqual(brickCoord, ivec3(bpa)))) {
        return 0xFFFFFFFFu;
    }
    return uint(brickCoord.z * bpa * bpa + brickCoord.y * bpa + brickCoord.x);
}

// ---------------------------------------------------------------------------
// channelBaseFloats: return the channelBaseFloats for a semantic in the active
// OctreeConfig, or 0xFFFFFFFFu if the semantic is not present.
// Scans octreeConfig.channels[0..channelCount-1] (.x = semanticId, .z = channelBaseFloats).
// ---------------------------------------------------------------------------
uint channelBaseFloats(uint sem) {
    for (uint i = 0u; i < octreeConfig.channelCount; ++i) {
        if (octreeConfig.channels[i].x == sem) return octreeConfig.channels[i].z;
    }
    return 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Internal helper: sample a single float from the channel pool at a given
// brick and voxel slot.
//   channelBase : channelBaseFloats for the desired channel (from channelBaseFloats())
//   gridCoord   : grid voxel coordinate (x,y,z in [0, bpa*8-1] per axis)
//   comp        : component index within the channel's elemCount (0 for scalars)
//   octreeIdx   : index into configs[]
// Returns 1e9 for unallocated bricks (sentinel, mirrors old _sampleSdfVoxel).
// Returns 1e9 if channelBase == 0xFFFFFFFFu (channel absent).
// ---------------------------------------------------------------------------
float _samplePoolVoxel(uint channelBase, ivec3 gridCoord, int comp, int octreeIdx) {
    if (channelBase == 0xFFFFFFFFu) return 1e9;

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    if (bpa <= 0) return 1e9;

    // Grid coordinate → brick coordinate (which 8^3 brick?)
    ivec3 brickCoord   = gridCoord / 8;
    // Voxel coordinate within that brick (0..7 per axis)
    ivec3 voxelInBrick = gridCoord - brickCoord * 8;

    // Look up the brick index in the dense per-octree sub-table.
    uint flatLookup = _gridToLookupIdx(brickCoord, bpa);
    if (flatLookup == 0xFFFFFFFFu) return 1e9;  // out of grid

    // Each octree's sub-table is bpa^3 entries; sub-tables are appended in order.
    uint lookupBase = uint(octreeIdx) * uint(bpa) * uint(bpa) * uint(bpa);
    uint brickIdx   = brickLookup[lookupBase + flatLookup];
    if (brickIdx == 0xFFFFFFFFu) return 1e9;  // unallocated brick

    // Pool addressing:
    //   channelPool[poolBrickBase + brickIdx*brickStrideFloats + channelBase + comp*512 + voxelIdx]
    uint poolBase   = configs[octreeIdx].poolBrickBase;   // float-element offset of octree's data
    uint stride     = configs[octreeIdx].brickStrideFloats;
    uint voxelIdx   = uint(voxelInBrick.z * 64 + voxelInBrick.y * 8 + voxelInBrick.x);
    return channelPool[poolBase + brickIdx * stride + channelBase + uint(comp) * VX_VOXELS_PER_BRICK + voxelIdx];
}

// ---------------------------------------------------------------------------
// _sampleSdfVoxel: backward-compat wrapper — reads the SDF channel (SEM_SDF).
// gridCoord is in voxel units (0 .. bpa*8-1 per axis).
// ---------------------------------------------------------------------------
float _sampleSdfVoxel(ivec3 gridCoord, int octreeIdx) {
    return _samplePoolVoxel(channelBaseFloats(SEM_SDF), gridCoord, 0, octreeIdx);
}

// ---------------------------------------------------------------------------
// sampleChannelScalarTrilinear: trilinear interpolation of a scalar channel
// at a fractional grid position (in voxel units).
// Returns `missing` when the channel is absent.
// ---------------------------------------------------------------------------
float sampleChannelScalarTrilinear(uint sem, vec3 gridPos, float missing) {
    uint base = channelBaseFloats(sem);
    if (base == 0xFFFFFFFFu) return missing;

    vec3  f = fract(gridPos);
    ivec3 i = ivec3(floor(gridPos));

    float c000 = _samplePoolVoxel(base, i + ivec3(0,0,0), 0, g_octreeIdx);
    float c100 = _samplePoolVoxel(base, i + ivec3(1,0,0), 0, g_octreeIdx);
    float c010 = _samplePoolVoxel(base, i + ivec3(0,1,0), 0, g_octreeIdx);
    float c110 = _samplePoolVoxel(base, i + ivec3(1,1,0), 0, g_octreeIdx);
    float c001 = _samplePoolVoxel(base, i + ivec3(0,0,1), 0, g_octreeIdx);
    float c101 = _samplePoolVoxel(base, i + ivec3(1,0,1), 0, g_octreeIdx);
    float c011 = _samplePoolVoxel(base, i + ivec3(0,1,1), 0, g_octreeIdx);
    float c111 = _samplePoolVoxel(base, i + ivec3(1,1,1), 0, g_octreeIdx);

    return mix(
        mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
        mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
        f.z);
}

// ---------------------------------------------------------------------------
// sampleChannelVec3Trilinear: trilinear interpolation of a 3-component channel
// (e.g. SEM_COLOR). Returns `missing` when the channel is absent.
// ---------------------------------------------------------------------------
vec3 sampleChannelVec3Trilinear(uint sem, vec3 gridPos, vec3 missing) {
    uint base = channelBaseFloats(sem);
    if (base == 0xFFFFFFFFu) return missing;

    vec3  f = fract(gridPos);
    ivec3 i = ivec3(floor(gridPos));

    vec3 result;
    for (int comp = 0; comp < 3; ++comp) {
        float c000 = _samplePoolVoxel(base, i + ivec3(0,0,0), comp, g_octreeIdx);
        float c100 = _samplePoolVoxel(base, i + ivec3(1,0,0), comp, g_octreeIdx);
        float c010 = _samplePoolVoxel(base, i + ivec3(0,1,0), comp, g_octreeIdx);
        float c110 = _samplePoolVoxel(base, i + ivec3(1,1,0), comp, g_octreeIdx);
        float c001 = _samplePoolVoxel(base, i + ivec3(0,0,1), comp, g_octreeIdx);
        float c101 = _samplePoolVoxel(base, i + ivec3(1,0,1), comp, g_octreeIdx);
        float c011 = _samplePoolVoxel(base, i + ivec3(0,1,1), comp, g_octreeIdx);
        float c111 = _samplePoolVoxel(base, i + ivec3(1,1,1), comp, g_octreeIdx);

        result[comp] = mix(
            mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
            mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
            f.z);
    }
    return result;
}

// ---------------------------------------------------------------------------
// sampleSdfTrilinear: trilinear interpolation of the SDF at a fractional grid
// position (in voxel units). The 8 corners are fetched via _sampleSdfVoxel.
// gridPos is in octree grid-voxel coordinates (0..bpa*8 per axis).
// ---------------------------------------------------------------------------
float sampleSdfTrilinear(vec3 gridPos, int octreeIdx) {
    vec3  f = fract(gridPos);
    ivec3 i = ivec3(floor(gridPos));

    float c000 = _sampleSdfVoxel(i + ivec3(0,0,0), octreeIdx);
    float c100 = _sampleSdfVoxel(i + ivec3(1,0,0), octreeIdx);
    float c010 = _sampleSdfVoxel(i + ivec3(0,1,0), octreeIdx);
    float c110 = _sampleSdfVoxel(i + ivec3(1,1,0), octreeIdx);
    float c001 = _sampleSdfVoxel(i + ivec3(0,0,1), octreeIdx);
    float c101 = _sampleSdfVoxel(i + ivec3(1,0,1), octreeIdx);
    float c011 = _sampleSdfVoxel(i + ivec3(0,1,1), octreeIdx);
    float c111 = _sampleSdfVoxel(i + ivec3(1,1,1), octreeIdx);

    return mix(
        mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
        mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
        f.z);
}

// Same sentinel threshold as marchBrickSdf's SENTINEL_D: a trilinear sample this large means
// the stencil straddled into an unallocated neighbour brick (_samplePoolVoxel -> 1e9), not an
// honest distance.
const float SDF_GRAD_SENTINEL_D = 100.0;

// ---------------------------------------------------------------------------
// sdfGradientStored: central-difference gradient of the trilinear SDF field.
// Step h = 0.5 voxel (fine enough for the interpolated field).
// Returns a normalized gradient (normal pointing outward from the surface).
//
// Sentinel-aware per axis: a central-difference sample straddling a brick boundary can read an
// unallocated neighbour (1e9 sentinel, see _samplePoolVoxel), which would otherwise blow up that
// axis's component and corrupt the normal -- visible as speckled shading noise right along brick
// seams (worst near a smooth-union fillet, where the iso-surface sits close to a brick face).
// marchBrickSdf's own iso-search loop already guards against this (SENTINEL_D); this mirrors that
// guard by falling back to a one-sided difference against the known-good on-surface sample
// (d0, near-zero since gridPos is the just-found hit point) whenever a side is contaminated.
// ---------------------------------------------------------------------------
vec3 sdfGradientStored(vec3 gridPos, int octreeIdx) {
    const float h = 0.5;
    float d0 = sampleSdfTrilinear(gridPos, octreeIdx);

    float dxPlus  = sampleSdfTrilinear(gridPos + vec3(h,0,0), octreeIdx);
    float dxMinus = sampleSdfTrilinear(gridPos - vec3(h,0,0), octreeIdx);
    float gx = (abs(dxPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dxMinus) * 2.0
             : (abs(dxMinus) > SDF_GRAD_SENTINEL_D) ? (dxPlus - d0) * 2.0
             : (dxPlus - dxMinus);

    float dyPlus  = sampleSdfTrilinear(gridPos + vec3(0,h,0), octreeIdx);
    float dyMinus = sampleSdfTrilinear(gridPos - vec3(0,h,0), octreeIdx);
    float gy = (abs(dyPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dyMinus) * 2.0
             : (abs(dyMinus) > SDF_GRAD_SENTINEL_D) ? (dyPlus - d0) * 2.0
             : (dyPlus - dyMinus);

    float dzPlus  = sampleSdfTrilinear(gridPos + vec3(0,0,h), octreeIdx);
    float dzMinus = sampleSdfTrilinear(gridPos - vec3(0,0,h), octreeIdx);
    float gz = (abs(dzPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dzMinus) * 2.0
             : (abs(dzMinus) > SDF_GRAD_SENTINEL_D) ? (dzPlus - d0) * 2.0
             : (dzPlus - dzMinus);

    vec3 g = vec3(gx, gy, gz);
    float len = length(g);
    return (len > 1e-6) ? g / len : vec3(0.0, 1.0, 0.0);
}

// ---------------------------------------------------------------------------
// _sdfBrickAllocated: does this brick-grid coordinate have an ALLOCATED SDF brick
// in the pool (true), or is it out-of-grid / an unallocated hole (false)? Mirrors
// the brickLookup addressing in _samplePoolVoxel so the "which brick is next"
// decision uses exactly the same allocation table the samples do.
// ---------------------------------------------------------------------------
bool _sdfBrickAllocated(ivec3 brickCoord, int octreeIdx) {
    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    if (bpa <= 0) return false;
    uint flatLookup = _gridToLookupIdx(brickCoord, bpa);
    if (flatLookup == 0xFFFFFFFFu) return false;  // out of grid
    uint lookupBase = uint(octreeIdx) * uint(bpa) * uint(bpa) * uint(bpa);
    return brickLookup[lookupBase + flatLookup] != 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// _stateToSdfBrick: convert an ESVO traversal state (position in mirrored [1,2]^3
// space + current node size) into the true integer brick-grid coordinate of the
// leaf it sits at. This is the SAME state.pos-authoritative octant-unmirror math
// handleLeafHitInstancedSdf uses (NOT a gridEntry-nudge re-derivation, which is
// ambiguous right on a brick face); factored here so the brick-hop continuation
// below derives each next brick identically.
// ---------------------------------------------------------------------------
ivec3 _stateToSdfBrick(vec3 statePos, float scaleExp2, int octantMask, int bpa) {
    vec3 brickOriginMirrored = statePos;
    if ((octantMask & 1) == 0) brickOriginMirrored.x = 3.0 - scaleExp2 - brickOriginMirrored.x;
    if ((octantMask & 2) == 0) brickOriginMirrored.y = 3.0 - scaleExp2 - brickOriginMirrored.y;
    if ((octantMask & 4) == 0) brickOriginMirrored.z = 3.0 - scaleExp2 - brickOriginMirrored.z;
    ivec3 b = ivec3(round((brickOriginMirrored - vec3(1.0)) * float(bpa)));
    return clamp(b, ivec3(0), ivec3(bpa - 1));
}

// ---------------------------------------------------------------------------
// _advanceToNextSdfLeaf: run the REAL ESVO traversal machinery (the same
// executeAdvancePhase / executePushPhase / executePopPhase the outer
// traverseOctreeInstanced loop uses) on a LOCAL COPY of the traversal state +
// stack, starting from the current leaf, to find the NEXT allocated leaf the ray
// enters. On success returns true and outputs that leaf's authoritative brick
// coordinate (nextBrick) plus the ADVANCEd state (so a further hop can continue).
// Returns false when the ray leaves the octree (or exhausts the iteration bound)
// without reaching another allocated leaf — a genuine miss.
//
// This mutates ONLY its by-value `state`/`stack` copies; the caller's outer-loop
// TraversalState is untouched, so if the whole march ultimately misses, the outer
// loop resumes from its own pristine state exactly as before.
// ---------------------------------------------------------------------------
bool _advanceToNextSdfLeaf(inout TraversalState state, RayCoefficients coef,
                           inout StackEntry stack[STACK_SIZE], int octreeIdx, int bpa,
                           out ivec3 nextBrick) {
    nextBrick = ivec3(0);
    // Step t past the current leaf so the first ADVANCE moves to the sibling, mirroring
    // the outer loop's `state.t_min = tv_max` on a leaf miss. tv_max is the CLAMPED leaf
    // exit (min(t_max, tc_max)); recompute it exactly as the outer loop does via
    // checkChildValidity rather than approximating it by state.t_max, so the ADVANCE steps
    // to the correct sibling at the true face-crossing t (state.t_max alone can overshoot
    // when the child's corner exit tc_max is nearer than the node t_max).
    {
        bool  isLeaf0;
        float tv_max0, txc0, tyc0, tzc0;
        uvec2 parent0    = fetchESVONode(state.parentPtr);
        uint  validMask0 = getValidMask(parent0);
        uint  leafMask0  = getLeafMask(parent0);
        if (checkChildValidity(state, coef, validMask0, leafMask0,
                               isLeaf0, tv_max0, txc0, tyc0, tzc0)) {
            state.t_min = tv_max0;
        } else {
            state.t_min = state.t_max;
        }
    }

    // Bounded mini-traversal. MAX_ITERS is shared with the outer loop's bound; a leaf
    // that is reached is returned immediately, so this only spins over empty siblings /
    // internal PUSHes between two allocated leaves.
    for (int it = 0; it < MAX_ITERS; ++it) {
        // ADVANCE to the next sibling (may request a POP).
        int step_mask;
        int advanceResult = executeAdvancePhase(state, coef, step_mask);
        if (advanceResult == 0) {
            if (state.scale < octreeConfig.esvoMaxScale) {
                state.t_max = stack[state.scale + 1].t_max;
            }
        } else { // POP_NEEDED
            int popResult = executePopPhase(state, coef, stack, step_mask);
            if (popResult == 1) return false;  // exited octree → genuine miss
        }
        if (state.scale > octreeConfig.esvoMaxScale) return false;

        // Descend from wherever ADVANCE/POP left us until we either reach an allocated
        // leaf or run out of ray. Re-fetch the parent each descent step.
        for (int desc = 0; desc < STACK_SIZE + 1; ++desc) {
            uvec2 parent = fetchESVONode(state.parentPtr);
            uint validMask   = getValidMask(parent);
            uint leafMask    = getLeafMask(parent);
            uint childPointer = getChildPointer(parent);

            bool isLeaf;
            float tv_max, tx_center, ty_center, tz_center;
            if (!checkChildValidity(state, coef, validMask, leafMask,
                                    isLeaf, tv_max, tx_center, ty_center, tz_center)) {
                break;  // this child is empty / behind us → back to the outer ADVANCE
            }

            if (isLeaf) {
                // Reached a leaf. It is allocated iff its SDF brick exists in the pool
                // (the ESVO leafMask can flag a leaf whose SDF brick was never baked —
                // a cut feature's field can bleed into occupancy the pool didn't store).
                ivec3 b = _stateToSdfBrick(state.pos, state.scale_exp2, coef.octant_mask, bpa);
                if (_sdfBrickAllocated(b, octreeIdx)) {
                    nextBrick = b;
                    return true;
                }
                // Allocated-less leaf: advance past it and keep hunting.
                state.t_min = tv_max;
                break;
            }

            // Internal node: PUSH one level and keep descending.
            executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                             tv_max, tx_center, ty_center, tz_center);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// marchBrickSdf (Inc2 M6; brick-hop continuation added for the concave-seam
// "notch" fix): sphere-trace the trilinear SDF iso-surface starting at an ESVO
// leaf brick and CONTINUING across real adjacent leaf bricks when the ray exits
// one brick without crossing.
//
// All coordinates are in TRUE GEOMETRIC grid-voxel space ([0, bpa*8]); the caller
// bridges from the ESVO [1,2]^3 frame so no octant un-mirroring is needed and
// sampleSdfTrilinear gets exactly the coordinates it expects.
//   gridEntry : ray entry point at the FIRST leaf, grid-voxel coords.
//   gridDirN  : ray direction in grid-voxel space, NORMALIZED (so the march arc-
//               length is in voxel units and the 1/√3 Lipschitz step is exact).
//   state/coef/stack : LOCAL COPIES of the outer traversal state, used to walk the
//               REAL ESVO machinery to the next allocated leaf on a brick exit.
// On hit: hitNormal = normalized SDF gradient (grid space), sHit = arc-length from
// the ORIGINAL gridEntry to the iso-surface (voxel units), measured along gridDirN
// across however many bricks were traversed. On miss (no crossing in any reachable
// brick, or the ray left the octree) returns false, exactly as before.
//
// WHY THE OLD kFaceOvershoot HACK IS GONE: it peeked one voxel PAST this brick's
// exit using the CURRENT brick's frame — an arithmetic guess that only resolved a
// crossing landing a hair past the shared face. It could not follow the ray into
// the NEXT leaf's own brick data, so a grazing ray through a concave CSG-subtract
// seam (whose true crossing spans several adjacent bricks — Subtract=max(a,-b) is
// only a distance BOUND, not a true SDF, at a reentrant seam) still false-missed.
// The continuation below enters each next brick correctly-addressed via the real
// traversal, so no arithmetic overshoot is needed. Tolerance-based "close enough
// to zero → hit" fudges were tried and reverted (they false-hit the cavity's
// concave inner walls, where legitimate rays pass near but do not cross); this
// is the architectural fix instead.
// ---------------------------------------------------------------------------
bool marchBrickSdf(int octreeIdx, ivec3 brick, vec3 gridEntry, vec3 gridDirN,
                   TraversalState state, RayCoefficients coef, StackEntry stack[STACK_SIZE],
                   out vec3 hitNormal, out float sHit, out ivec3 hitBrick) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    sHit      = 0.0;
    hitBrick  = brick;   // brick the crossing was actually found in (for the pick/ID buffer)

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);

    const int   MAX_STEPS = 96;    // one 8³ brick + sentinel probes — converges well within this
    const float EPS       = 0.01;  // iso threshold (voxel fraction)
    // Above this, a trilinear sample is sentinel-contaminated: at a brick face the 8-corner
    // stencil reached into an UNALLOCATED neighbour brick (_sampleSdfVoxel → 1e9). Real
    // in-leaf distances are ≤ a brick diagonal (~14), so anything large means "no honest
    // distance here" — probe forward one voxel until the stencil sits fully inside populated
    // data, rather than trusting the blown-up value as a distance (which would lunge past sMax).
    const float SENTINEL_D = 100.0;

    // Runaway guard on brick-to-brick hops. The genuine terminator is NOT this counter but the
    // octree-exit test inside _advanceToNextSdfLeaf (executePopPhase → returns false → the hop
    // loop `return false`s below), which fires the instant the ray leaves the octree — normal
    // rays exit long before this bound. The old cap of 8 was a CONTENT limit: a body wider than
    // ~8 leaf bricks along the ray truncated mid-solid, so occupancy-filled interiors (which now
    // span the whole body, not a thin shell) would false-miss on deep rays. 2048 makes this a
    // pure runaway guard, not a content limit. MAX_STEPS (96) and the inner _advanceToNextSdfLeaf
    // bound (MAX_ITERS, ≤512) are UNCHANGED — only the outer hop cap moves — so grazing rays stay
    // correct while the octree-exit break terminates every normal ray far below 2048 (no TDR risk:
    // the loop cannot iterate 2048× unless the ray is genuinely traversing 2048 real bricks).
    const int MAX_BRICK_HOPS = 2048;

    // `sBase` is the arc-length (from the ORIGINAL gridEntry, along gridDirN) at which the
    // CURRENT brick's entry point sits. Because every brick shares the same grid frame and the
    // same unit gridDirN, the entry point of the k-th brick lies on the ray at
    // gridEntry + gridDirN*sBase, so a crossing found `s` voxels into the current brick is at
    // total arc-length sBase + s — a single consistent parametrization for sHit and the
    // downstream color/roughness gather (gridEntry + gridDirN*sHit).
    float sBase = 0.0;
    ivec3 curBrick = brick;

    for (int hop = 0; hop < MAX_BRICK_HOPS; ++hop) {
        // Brick-cube slab test in the CURRENT brick's frame, from the current entry point.
        vec3 curEntry = gridEntry + gridDirN * sBase;
        vec3 bMin = vec3(curBrick) * 8.0;
        vec3 bMax = bMin + vec3(8.0);

        // gridEntry (for hop 0) / curEntry (for later hops) is guaranteed to lie inside
        // [bMin, bMax] (hop 0: authoritative caller brick; later hops: the leaf the real
        // traversal just entered). A near-zero direction component means "this axis's slab
        // bound is effectively at infinity" — use a bounded large distance rather than 1e20.
        const float kAxisParallelSlabDist = 64.0;  // >> 8*sqrt(3), the largest real in-brick distance
        vec3 invD = vec3(
            abs(gridDirN.x) > 1e-8 ? 1.0 / gridDirN.x : 0.0,
            abs(gridDirN.y) > 1e-8 ? 1.0 / gridDirN.y : 0.0,
            abs(gridDirN.z) > 1e-8 ? 1.0 / gridDirN.z : 0.0);
        bvec3 axisParallel = lessThanEqual(abs(gridDirN), vec3(1e-8));
        vec3  t0   = mix((bMin - curEntry) * invD, vec3(-kAxisParallelSlabDist), axisParallel);
        vec3  t1   = mix((bMax - curEntry) * invD, vec3( kAxisParallelSlabDist), axisParallel);
        vec3  thi  = max(t0, t1);
        float sMax = max(min(min(thi.x, thi.y), thi.z), 0.0);   // exit; ≤ 8√3 voxels

        // Sphere-trace within this brick's [0, sMax] span (relative to curEntry).
        float s = 0.0;
        for (int i = 0; i < MAX_STEPS; ++i) {
            if (s > sMax) break;              // left the brick without crossing → try to continue
            vec3  p = curEntry + gridDirN * s;
            float d = sampleSdfTrilinear(p, octreeIdx);
            if (d < EPS) {                    // crossed (or reached) the iso-surface
                hitNormal = sdfGradientStored(p, octreeIdx);
                sHit      = sBase + s;
                hitBrick  = curBrick;
                return true;
            }
            // 1/√3 Lipschitz step for honest samples; a bounded 1-voxel probe through
            // sentinel-contaminated (brick-face straddle) regions.
            s += (d > SENTINEL_D) ? 1.0 : max(d * 0.5773503, EPS);
        }

        // No crossing in this brick. Ask the REAL ESVO traversal which allocated leaf the ray
        // enters next; if there is none (empty space / octree exit), this is a genuine miss.
        ivec3 nextBrick;
        if (!_advanceToNextSdfLeaf(state, coef, stack, octreeIdx, bpa, nextBrick)) {
            return false;
        }

        // Continue in the next brick. Its authoritative entry point on the ray is the slab
        // entry of that brick's cube; advance sBase to there so the parametrization stays
        // global. (Guard against a non-advancing degenerate hop.)
        vec3 nMin = vec3(nextBrick) * 8.0;
        vec3 nMax = nMin + vec3(8.0);
        vec3 nt0  = mix((nMin - gridEntry) * invD, vec3(-kAxisParallelSlabDist), axisParallel);
        vec3 nt1  = mix((nMax - gridEntry) * invD, vec3( kAxisParallelSlabDist), axisParallel);
        vec3 ntlo = min(nt0, nt1);
        float sEnter = max(max(max(ntlo.x, ntlo.y), ntlo.z), sBase);  // entry arc-length ≥ current
        sBase   = sEnter;
        curBrick = nextBrick;
    }
    return false;   // exhausted the hop budget without crossing → miss
}

#endif // STORED_SDF_GLSL
