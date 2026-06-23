// ============================================================================
// StoredSdf.glsl — Inc2 M4: Stored-SDF trilinear iso-surface rendering helpers.
// ============================================================================
// Included by BodyInstanceRayMarch.comp AFTER:
//   • OctreeConfig struct (binding 5), g_octreeIdx, octreeConfig macro
//   • SdfBrickBuffer (binding 11): float sdfData[]
//   • BrickLookupBuffer (binding 12): uint  brickLookup[]
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
// Internal helper: sample the SDF for a single grid voxel at integer coord.
// gridCoord is in voxel units (0 .. bpa*8 - 1 per axis).
// Returns 1e9 (positive large) if the brick is unallocated (sentinel).
//
// sdfBrickArrayBase is in FLOAT ELEMENT units (voxels), matching ConcatenateSdf
// which sets sdfBase += brickCount * 512 (kVoxelsPerBrick) per octree.
//
// The brick-grid lookup sub-table for octree k starts at k * bpa^3 in the
// concatenated brickLookup[]. All octrees share the same bpa in practice
// (all built with the same shell depth), so this is: octreeIdx * bpa^3.
// ---------------------------------------------------------------------------
float _sampleSdfVoxel(ivec3 gridCoord, int octreeIdx) {
    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    if (bpa <= 0) return 1e9;

    // Grid coordinate → brick coordinate (which 8^3 brick?)
    ivec3 brickCoord = gridCoord / 8;
    // Voxel coordinate within that brick (0..7 per axis)
    ivec3 voxelInBrick = gridCoord - brickCoord * 8;

    // Look up the brick index in the dense per-octree sub-table.
    uint flatLookup = _gridToLookupIdx(brickCoord, bpa);
    if (flatLookup == 0xFFFFFFFFu) return 1e9;  // out of grid

    // Each octree's sub-table is bpa^3 entries; sub-tables are appended in order.
    uint lookupBase = uint(octreeIdx) * uint(bpa) * uint(bpa) * uint(bpa);
    uint brickIdx = brickLookup[lookupBase + flatLookup];
    if (brickIdx == 0xFFFFFFFFu) return 1e9;  // unallocated brick

    // sdfBrickArrayBase is the element (float) offset of this octree's SDF data
    // within the concatenated sdfData[]. Each brick holds 512 float voxels.
    uint sdfBase   = configs[octreeIdx].sdfBrickArrayBase;  // in float elements
    uint voxelIdx  = uint(voxelInBrick.z * 64 + voxelInBrick.y * 8 + voxelInBrick.x);
    return sdfData[sdfBase + brickIdx * 512u + voxelIdx];
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

// ---------------------------------------------------------------------------
// sdfGradientStored: central-difference gradient of the trilinear SDF field.
// Step h = 0.5 voxel (fine enough for the interpolated field).
// Returns a normalized gradient (normal pointing outward from the surface).
// ---------------------------------------------------------------------------
vec3 sdfGradientStored(vec3 gridPos, int octreeIdx) {
    const float h = 0.5;
    float gx = sampleSdfTrilinear(gridPos + vec3(h,0,0), octreeIdx)
             - sampleSdfTrilinear(gridPos - vec3(h,0,0), octreeIdx);
    float gy = sampleSdfTrilinear(gridPos + vec3(0,h,0), octreeIdx)
             - sampleSdfTrilinear(gridPos - vec3(0,h,0), octreeIdx);
    float gz = sampleSdfTrilinear(gridPos + vec3(0,0,h), octreeIdx)
             - sampleSdfTrilinear(gridPos - vec3(0,0,h), octreeIdx);
    vec3 g = vec3(gx, gy, gz);
    float len = length(g);
    return (len > 1e-6) ? g / len : vec3(0.0, 1.0, 0.0);
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
    // Above this, a trilinear sample is sentinel-contaminated: at a brick face the 8-corner
    // stencil reached into an UNALLOCATED neighbour brick (_sampleSdfVoxel → 1e9). Real
    // in-leaf distances are ≤ a brick diagonal (~14), so anything large means "no honest
    // distance here" — probe forward one voxel until the stencil sits fully inside populated
    // data, rather than trusting the blown-up value as a distance (which would lunge past sMax).
    const float SENTINEL_D = 100.0;

    float s = 0.0;
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (s > sMax) return false;   // left the brick without crossing → advance
        vec3  p = gridEntry + gridDirN * s;
        float d = sampleSdfTrilinear(p, octreeIdx);
        if (d < EPS) {                // crossed (or reached) the iso-surface
            hitNormal = sdfGradientStored(p, octreeIdx);
            sHit      = s;
            return true;
        }
        // 1/√3 Lipschitz step for honest samples; a bounded 1-voxel probe through
        // sentinel-contaminated (brick-face straddle) regions.
        s += (d > SENTINEL_D) ? 1.0 : max(d * 0.5773503, EPS);
    }
    return false;
}

#endif // STORED_SDF_GLSL
