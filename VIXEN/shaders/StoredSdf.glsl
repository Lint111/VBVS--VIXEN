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
// Unallocated-brick SIGN-AWARE sentinel (Inc3 hole fix).
//
// _samplePoolVoxel returns a large-magnitude SIGNED sentinel for an empty brick so
// the trilinear stencil at a brick face bordering empty space stays sign-correct:
//   • exterior empty (brickLookup==0xFFFFFFFF / out-of-grid / channel-absent) → +SDF_SENTINEL
//   • interior empty (brickLookup==0xFFFFFFFE)                                → −SDF_SENTINEL
// Encoding matches ShellOctreeGpu.h kBrickUnalloc{Exterior,Interior}. marchBrickSdf
// detects |d| > SDF_SENTINEL_D as "contaminated" (a stencil that reached into empty
// space) and steps a small bounded amount instead of trusting the blown-up distance.
// ---------------------------------------------------------------------------
#define SDF_BRICK_UNALLOC_EXTERIOR 0xFFFFFFFFu  // no brick, SDF > 0
#define SDF_BRICK_UNALLOC_INTERIOR 0xFFFFFFFEu  // no brick, SDF < 0
const float SDF_SENTINEL = 1e9;

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
// Returns a SIGNED sentinel for unallocated bricks (Inc3 hole fix):
//   exterior empty / out-of-grid / channel-absent → +SDF_SENTINEL
//   interior empty (lookup == 0xFFFFFFFE)          → −SDF_SENTINEL
// so a brick-face trilinear stencil reaching into empty space stays sign-correct.
// (For non-SDF channels the +SDF_SENTINEL "absent" value is harmless — those readers
// only sample inside allocated bricks at a confirmed iso hit.)
// ---------------------------------------------------------------------------
float _samplePoolVoxel(uint channelBase, ivec3 gridCoord, int comp, int octreeIdx) {
    if (channelBase == 0xFFFFFFFFu) return SDF_SENTINEL;

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    if (bpa <= 0) return SDF_SENTINEL;

    // Grid coordinate → brick coordinate (which 8^3 brick?)
    ivec3 brickCoord   = gridCoord / 8;
    // Voxel coordinate within that brick (0..7 per axis)
    ivec3 voxelInBrick = gridCoord - brickCoord * 8;

    // Look up the brick index in the dense per-octree sub-table.
    uint flatLookup = _gridToLookupIdx(brickCoord, bpa);
    if (flatLookup == 0xFFFFFFFFu) return SDF_SENTINEL;  // out of grid → exterior

    // Each octree's sub-table is bpa^3 entries; sub-tables are appended in order.
    uint lookupBase = uint(octreeIdx) * uint(bpa) * uint(bpa) * uint(bpa);
    uint brickIdx   = brickLookup[lookupBase + flatLookup];
    // Sign-aware empty-brick sentinels: interior empty is NEGATIVE, exterior POSITIVE.
    if (brickIdx == SDF_BRICK_UNALLOC_INTERIOR) return -SDF_SENTINEL;
    if (brickIdx == SDF_BRICK_UNALLOC_EXTERIOR) return  SDF_SENTINEL;

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
//
// Inc3 hole fix: the corners are SIGN-AWARE — an empty brick yields ±SDF_SENTINEL
// (interior −, exterior +). We keep the PLAIN blend (no per-corner reconstruction):
// a stencil straddling empty space blends to a large MAGNITUDE that marchBrickSdf
// detects (|d|>SENTINEL_D) and steps through, rather than trusting it as a distance.
// (A per-corner honest-corner reconstruction was tried and REGRESSED the GPU — the
// reconstructed value is discontinuous across brick faces and the GPU's lower-precision
// blend turns that into fresh holes; the exact-CPU mirror does not reproduce it. The
// sign-correct sentinel + contamination-aware march is what holds up on real hardware.)
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

// ---------------------------------------------------------------------------
// sdfGradientStored: central-difference gradient of the trilinear SDF field.
// Returns a normalized gradient (normal pointing outward from the surface).
//
// Inc3 hole fix: a ±h tap can land in sentinel-contaminated space (a stencil that
// reached into an empty brick → ±SDF_SENTINEL). With SIGN-AWARE sentinels that tap
// still carries the CORRECT SIGN (exterior empty = +, interior empty = −), so its
// direction is meaningful — only its 1e9 MAGNITUDE is not. We therefore CLAMP each
// sample to ±CL voxels instead of dropping it: a contaminated tap then contributes a
// bounded, correctly-signed push, so the central difference yields a stable outward
// normal even at a brick CORNER/EDGE where multiple axes straddle empty space (where
// dropping taps used to collapse the gradient → a garbage fallback normal → specular
// blow-out). fallbackDir is only the degenerate last resort, never at a real iso point.
// ---------------------------------------------------------------------------
vec3 sdfGradientStored(vec3 gridPos, int octreeIdx, vec3 fallbackDir) {
    const float h  = 0.5;
    const float CL = 2.0;
    vec3 g = vec3(0.0);
    for (int ax = 0; ax < 3; ++ax) {
        vec3 e = vec3(0.0); e[ax] = h;
        float sp = clamp(sampleSdfTrilinear(gridPos + e, octreeIdx), -CL, CL);
        float sm = clamp(sampleSdfTrilinear(gridPos - e, octreeIdx), -CL, CL);
        g[ax] = (sp - sm);
    }
    float len = length(g);
    return (len > 1e-6) ? g / len : normalize(fallbackDir + vec3(0.0, 1e-6, 0.0));
}

// ---------------------------------------------------------------------------
// marchBrickSdf (Inc2 M6): sphere-trace the trilinear SDF iso-surface within ONE
// ESVO leaf brick. Called by handleLeafHitInstancedSdf once the octree traversal
// has located an allocated leaf — so this march is BOUNDED to that single 8-voxel
// brick and NEVER lunges across empty space (the octree skips empties for us).
//
// All coordinates are in TRUE GEOMETRIC grid-voxel space ([0, bpa*8]); the caller
// bridges from the ESVO [1,2]^3 frame so no octant un-mirroring is needed and
// sampleSdfTrilinear gets exactly the coordinates it expects.
//   gridEntry : ray entry point at the leaf, grid-voxel coords.
//   gridDirN  : ray direction in grid-voxel space, NORMALIZED (so the march arc-
//               length is in voxel units and the 1/√3 Lipschitz step is exact).
// On hit: hitNormal = normalized SDF gradient (grid space), sHit = arc-length from
// gridEntry to the iso-surface (voxel units). On miss the traversal ADVANCEs to
// the next leaf, so returning false here is the correct "not in this brick".
// ---------------------------------------------------------------------------
bool marchBrickSdf(int octreeIdx, vec3 gridEntry, vec3 gridDirN,
                   out vec3 hitNormal, out float sHit) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    sHit      = 0.0;

    // Identify this leaf's brick. Nudge inward along the ray so a point sitting
    // exactly on the entry face resolves to the brick we are ENTERING (correct for
    // either ray sign — the entry face is the lower face for +dir, upper for -dir).
    const float kNudge = 1e-3;
    ivec3 brick = ivec3(floor((gridEntry + gridDirN * kNudge) / 8.0));
    vec3  bMin  = vec3(brick) * 8.0;
    vec3  bMax  = bMin + vec3(8.0);

    // Brick-cube exit arc-length (slab test). gridDirN is unit-length, so the slab
    // t-values are already arc-lengths in voxel units.
    vec3 invD = vec3(
        abs(gridDirN.x) > 1e-8 ? 1.0 / gridDirN.x : 1e20,
        abs(gridDirN.y) > 1e-8 ? 1.0 / gridDirN.y : 1e20,
        abs(gridDirN.z) > 1e-8 ? 1.0 / gridDirN.z : 1e20);
    vec3  t0   = (bMin - gridEntry) * invD;
    vec3  t1   = (bMax - gridEntry) * invD;
    vec3  thi  = max(t0, t1);
    float sMax = max(min(min(thi.x, thi.y), thi.z), 0.0);   // exit; ≤ 8√3 voxels

    const int   MAX_STEPS = 96;    // one 8³ brick + sentinel probes — converges well within this
    const float EPS       = 0.01;  // iso threshold (voxel fraction)
    // |d| above this ⇒ the trilinear sample is sentinel-contaminated: at a brick face the
    // 8-corner stencil reached into an UNALLOCATED neighbour brick (_samplePoolVoxel →
    // ±SDF_SENTINEL). Real in-leaf distances are ≤ a brick diagonal (~14), so a large
    // MAGNITUDE (either sign now — interior empties are −SDF_SENTINEL) means "no honest
    // distance here". With sign-aware sentinels a contaminated sample can be large-NEGATIVE,
    // so the contamination test MUST run BEFORE the d<EPS hit test or an interior-empty
    // stencil straddle would register a FALSE hit at the brick face.
    const float SENTINEL_D    = 100.0;
    // Bounded step through contaminated space: small enough not to skip a surface crossing
    // sitting just inside the face, large enough to exit the contaminated band in a few steps.
    // (Tuned on lavapipe; 0.5 voxel closes the brick-face holes without false hits.)
    const float CONTAM_STEP   = 0.5;
    // Bounded overshoot PAST the brick exit. The iso-surface can sit a sub-voxel fraction
    // BEYOND this brick's exit face, inside the (allocated) neighbour brick — and the ESVO
    // traversal does not always then descend to that neighbour (a brick-boundary precision
    // effect), so the crossing would be missed entirely → a hole. The trilinear sampler reads
    // the GLOBAL field, so we let the march continue a SMALL margin past sMax to catch such a
    // boundary-straddling crossing. Safety: this never lunges across empty space — once the
    // sample turns CONTAMINATED in the margin (the neighbour brick is empty) we STOP, and the
    // margin is < 1 voxel so we cannot reach a distant surface.
    const float EXIT_MARGIN   = 1.0;
    const float sLimit        = sMax + EXIT_MARGIN;

    float s = 0.0;
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (s > sLimit) return false;   // left the brick (+margin) without crossing → advance
        vec3  p = gridEntry + gridDirN * s;
        float d = sampleSdfTrilinear(p, octreeIdx);

        if (abs(d) > SENTINEL_D) {
            // CONTAMINATED (sentinel straddle): NOT a hit regardless of sign.
            // In the overshoot margin a contaminated sample means the neighbour brick is
            // empty (no honest surface to catch there) → stop rather than probe into emptiness.
            if (s > sMax) return false;
            // Inside the brick: step a small bounded amount so a near-face crossing is not
            // skipped, then re-sample.
            s += CONTAM_STEP;
            continue;
        }
        if (d < EPS) {                // honest sample crossed (or reached) the iso-surface
            hitNormal = sdfGradientStored(p, octreeIdx, gridDirN);
            sHit      = s;
            return true;
        }
        // 1/√3 Lipschitz step for honest samples.
        s += max(d * 0.5773503, EPS);
    }
    return false;
}

#endif // STORED_SDF_GLSL
