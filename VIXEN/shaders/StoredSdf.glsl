// ============================================================================
// StoredSdf.glsl — Inc2 M4: Stored-SDF trilinear iso-surface rendering helpers.
// ============================================================================
// Included by BodyInstanceRayMarch.comp AFTER:
//   • OctreeConfig struct (binding 5), g_octreeIdx, octreeConfig macro
//   • SdfBrickBuffer (binding 11): float sdfData[]
//   • BrickLookupBuffer (binding 12): uint  brickLookup[]
//   • SdfRecipes.glsl (reuses evalSdf for out-of-AABB sentinel clamping)
//   • Lighting.glsl (reuses computeLighting in marchStoredSdf)
//
// Parallel to traceProceduralBody (SdfRecipes.glsl) for the Stored path.
// These helpers are dead code when formatId == FORMAT_BINARY — the dispatch
// in main() guards them with configs[oi].formatId == FORMAT_STORED_SDF.
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
// marchStoredSdf: sphere-trace the trilinear SDF field for a single Stored-SDF
// body instance. ro/rd are in octree GRID-VOXEL space (de-instanced by caller).
//
// The body's grid AABB is [0, bpa*8]^3 in voxel units. We intersect ro+rd with
// that box first, then sphere-trace inside.
//
// On hit: hitNormal is the normalized SDF gradient (world normal = transforms of
// gradient can be deferred to M5; for now gradient is in grid space, consistent
// with the Procedural path which also returns the analytic local normal).
// hitT is the ray parameter (same units as ro/rd, grid-voxel distance).
// ---------------------------------------------------------------------------
bool marchStoredSdf(int octreeIdx, vec3 ro, vec3 rd,
                    out vec3 hitNormal, out float hitT) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    hitT      = 0.0;

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    if (bpa <= 0) return false;

    float gridSize = float(bpa * 8);
    vec3  aabbMin  = vec3(0.0);
    vec3  aabbMax  = vec3(gridSize);

    // Ray–AABB intersection in grid space.
    vec3 invDir = vec3(
        abs(rd.x) > 1e-8 ? 1.0 / rd.x : 1e20 * sign(rd.x + 1e-30),
        abs(rd.y) > 1e-8 ? 1.0 / rd.y : 1e20 * sign(rd.y + 1e-30),
        abs(rd.z) > 1e-8 ? 1.0 / rd.z : 1e20 * sign(rd.z + 1e-30));
    vec3 t0 = (aabbMin - ro) * invDir;
    vec3 t1 = (aabbMax - ro) * invDir;
    vec3 tNear3 = min(t0, t1);
    vec3 tFar3  = max(t0, t1);
    float tNear = max(max(tNear3.x, tNear3.y), tNear3.z);
    float tFar  = min(min(tFar3.x,  tFar3.y),  tFar3.z);
    if (tFar < 0.0 || tNear > tFar) return false;

    float t = max(tNear, 0.0);

    const int   MAX_STEPS = 128;
    const float EPS       = 0.01;  // hit threshold in voxel units
    const float MAX_T     = tFar;

    for (int i = 0; i < MAX_STEPS; ++i) {
        if (t > MAX_T) return false;
        vec3  p = ro + rd * t;
        float d = sampleSdfTrilinear(p, octreeIdx);
        if (d < EPS) {
            hitNormal = sdfGradientStored(p, octreeIdx);
            hitT      = t;
            return true;
        }
        // Sphere-trace step: advance by the SDF value (clamped to a minimum to avoid stalling).
        t += max(d, 0.1);
    }
    return false;
}

#endif // STORED_SDF_GLSL
