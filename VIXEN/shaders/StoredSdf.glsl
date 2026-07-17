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

    // Each octree's sub-table is bpa^3 entries; sub-tables are appended at an
    // EXACT PREFIX SUM stamped by the CPU concatenation (ConcatenateSdf/
    // ConcatenateSdfWithMips) into brickLookupBase — NOT octreeIdx*bpa^3, which
    // silently assumed every concatenated octree shared one uniform bpa (wrong
    // whenever bpa differs across octrees, e.g. Cornell's bpa=16 walls mixed
    // with bpa=2 bodies — see OctreeConfig.brickLookupBase's doc comment).
    uint lookupBase = configs[octreeIdx].brickLookupBase;
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
// _loadTrilinearCell: load the 8 corners of the trilinear stencil at gridPos
// for channel `base` component `comp`, taking a SINGLE-FETCH fast path when
// the whole 2x2x2 cell lies inside one brick (audit A1 / Top action #6).
//
// ~(7/8)^3 ≈ 67% of cells satisfy this: local (voxel-in-brick) coordinate on
// EVERY axis in [0,6] guarantees local+1 stays in [0,7], so the +1 neighbour
// on every axis is still inside the SAME brick — one brickLookup fetch gives
// brickIdx, and all 8 corners are then contiguous pool reads at fixed offsets
// {0,1,8,9,64,65,72,73} from the cell's base voxel (voxelIdx formula is
// z*64+y*8+x, so +1/+8/+64 step exactly one voxel along x/y/z).
//
// Falls back to the original 8 independent _samplePoolVoxel calls (each doing
// its own brickCoord/brickLookup resolution) when the cell straddles a brick
// boundary — those corners may live in different bricks (or an unallocated
// neighbour, preserved as the 1e9 sentinel per corner exactly as before).
//
// `brickIdxOut`/`localOut`/`oneBrickOut` return the resolved brick index
// (0xFFFFFFFFu if unresolved/unallocated), local voxel coordinate, and
// whether the fast path applied, so multi-component callers (color) can
// reuse the SAME brick resolution across components instead of re-running
// _gridToLookupIdx + the brickLookup fetch per component (audit A3).
// ---------------------------------------------------------------------------
void _loadTrilinearCellComp(uint base, vec3 gridPos, int comp, int octreeIdx,
                            out vec3 f, out vec4 z0, out vec4 z1,
                            out uint brickIdxOut, out ivec3 localOut, out bool oneBrickOut) {
    f = fract(gridPos);
    ivec3 i = ivec3(floor(gridPos));
    brickIdxOut = 0xFFFFFFFFu;
    localOut    = ivec3(0);
    oneBrickOut = false;

    if (base == 0xFFFFFFFFu) {
        z0 = vec4(1e9);
        z1 = vec4(1e9);
        return;
    }

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    bool inGrid = bpa > 0 &&
                  all(greaterThanEqual(i, ivec3(0))) &&
                  all(lessThan(i, ivec3(bpa * 8)));
    ivec3 brickCoord = inGrid ? (i / 8) : ivec3(-1);
    ivec3 local       = i - brickCoord * 8;
    // Entirely intra-brick iff local+1 stays in [0,7] on every axis, i.e. local<=6.
    bool oneBrick = inGrid && all(lessThanEqual(local, ivec3(6)));
    localOut    = local;
    oneBrickOut = oneBrick;

    if (oneBrick) {
        uint flatLookup = _gridToLookupIdx(brickCoord, bpa);
        uint brickIdx   = brickLookup[configs[octreeIdx].brickLookupBase + flatLookup];
        brickIdxOut = brickIdx;
        if (brickIdx == 0xFFFFFFFFu) {
            z0 = vec4(1e9);
            z1 = vec4(1e9);
            return;  // unallocated brick — same sentinel as the slow path
        }

        uint voxel000 = uint(local.z * 64 + local.y * 8 + local.x);
        uint poolBase = configs[octreeIdx].poolBrickBase +
                        brickIdx * configs[octreeIdx].brickStrideFloats +
                        base + uint(comp) * VX_VOXELS_PER_BRICK + voxel000;
        z0 = vec4(channelPool[poolBase],
                  channelPool[poolBase + 1u],
                  channelPool[poolBase + 8u],
                  channelPool[poolBase + 9u]);
        z1 = vec4(channelPool[poolBase + 64u],
                  channelPool[poolBase + 65u],
                  channelPool[poolBase + 72u],
                  channelPool[poolBase + 73u]);
    } else {
        // Brick-boundary (or out-of-grid) cell: corners may span multiple bricks —
        // keep the original per-corner resolution (each with its own sentinel check).
        z0 = vec4(_samplePoolVoxel(base, i + ivec3(0,0,0), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(1,0,0), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(0,1,0), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(1,1,0), comp, octreeIdx));
        z1 = vec4(_samplePoolVoxel(base, i + ivec3(0,0,1), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(1,0,1), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(0,1,1), comp, octreeIdx),
                  _samplePoolVoxel(base, i + ivec3(1,1,1), comp, octreeIdx));
    }
}

// Scalar (comp=0) convenience wrapper — the common case (SDF, roughness, ...).
void _loadTrilinearCell(uint base, vec3 gridPos, int octreeIdx,
                        out vec3 f, out vec4 z0, out vec4 z1) {
    uint brickIdxOut; ivec3 localOut; bool oneBrickOut;
    _loadTrilinearCellComp(base, gridPos, 0, octreeIdx, f, z0, z1, brickIdxOut, localOut, oneBrickOut);
}

// z0 = (c000,c100,c010,c110), z1 = (c001,c101,c011,c111) — see _loadTrilinearCell.
float _interpolateTrilinearCell(vec3 f, vec4 z0, vec4 z1) {
    return mix(
        mix(mix(z0.x, z0.y, f.x), mix(z0.z, z0.w, f.x), f.y),
        mix(mix(z1.x, z1.y, f.x), mix(z1.z, z1.w, f.x), f.y),
        f.z);
}

// ---------------------------------------------------------------------------
// sampleChannelScalarTrilinear: trilinear interpolation of a scalar channel
// at a fractional grid position (in voxel units).
// Returns `missing` when the channel is absent.
//
// Uses the same single-brick fast path as the SDF sampler (audit A3): the
// cell's brick resolution is shared with sampleChannelVec3Trilinear's inner
// loop via _loadTrilinearCellComp, so a hit-shading call site that samples
// both roughness and color resolves the brick lookup on the FIRST call only
// when they hit the same cell (both are called at the SAME gridHit).
// ---------------------------------------------------------------------------
float sampleChannelScalarTrilinear(uint sem, vec3 gridPos, float missing) {
    uint base = channelBaseFloats(sem);
    if (base == 0xFFFFFFFFu) return missing;

    vec3 f; vec4 z0, z1; uint brickIdxOut; ivec3 localOut; bool oneBrickOut;
    _loadTrilinearCellComp(base, gridPos, 0, g_octreeIdx, f, z0, z1, brickIdxOut, localOut, oneBrickOut);
    return _interpolateTrilinearCell(f, z0, z1);
}

// ---------------------------------------------------------------------------
// sampleChannelVec3Trilinear: trilinear interpolation of a 3-component channel
// (e.g. SEM_COLOR). Returns `missing` when the channel is absent.
//
// Resolves the cell's brick/local coordinate ONCE (component 0), then reuses
// that SAME brick index + local-voxel addressing for components 1 and 2 —
// the old version re-ran the full corner resolution (brickCoord math +
// brickLookup fetch) independently per component; 16 of 24 corner chains were
// pure duplicate work since only the `comp*512` pool offset differs (audit A3).
// ---------------------------------------------------------------------------
vec3 sampleChannelVec3Trilinear(uint sem, vec3 gridPos, vec3 missing) {
    uint base = channelBaseFloats(sem);
    if (base == 0xFFFFFFFFu) return missing;

    vec3 f; vec4 z0, z1; uint brickIdx; ivec3 local; bool oneBrick;
    _loadTrilinearCellComp(base, gridPos, 0, g_octreeIdx, f, z0, z1, brickIdx, local, oneBrick);

    vec3 result;
    result.x = _interpolateTrilinearCell(f, z0, z1);

    if (oneBrick) {
        // brickIdx/local already resolved above — remaining components are pure
        // contiguous pool reads at the same voxel offsets, no re-lookup.
        if (brickIdx == 0xFFFFFFFFu) return vec3(1e9);  // unallocated brick (matches per-corner path)
        uint voxel000 = uint(local.z * 64 + local.y * 8 + local.x);
        for (int comp = 1; comp < 3; ++comp) {
            uint poolBase = configs[g_octreeIdx].poolBrickBase +
                            brickIdx * configs[g_octreeIdx].brickStrideFloats +
                            base + uint(comp) * VX_VOXELS_PER_BRICK + voxel000;
            vec4 cz0 = vec4(channelPool[poolBase],
                            channelPool[poolBase + 1u],
                            channelPool[poolBase + 8u],
                            channelPool[poolBase + 9u]);
            vec4 cz1 = vec4(channelPool[poolBase + 64u],
                            channelPool[poolBase + 65u],
                            channelPool[poolBase + 72u],
                            channelPool[poolBase + 73u]);
            result[comp] = _interpolateTrilinearCell(f, cz0, cz1);
        }
    } else {
        // Brick-boundary cell: fall back to the original independent per-component
        // resolution (corners may span multiple bricks per component lookup).
        for (int comp = 1; comp < 3; ++comp) {
            vec3 cf; vec4 cz0, cz1; uint cBrickIdx; ivec3 cLocal; bool cOneBrick;
            _loadTrilinearCellComp(base, gridPos, comp, g_octreeIdx, cf, cz0, cz1, cBrickIdx, cLocal, cOneBrick);
            result[comp] = _interpolateTrilinearCell(cf, cz0, cz1);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// sampleHitShadingChannels: resolve color (SEM_COLOR, 3 comp) AND roughness
// (SEM_ROUGHNESS, 1 comp) at the SAME hit grid position, sharing ONE brick-
// address resolution across both channels (audit A3: the per-channel entry
// points above each independently resolve brickCoord/flatLookup/brickIdx for
// the identical gridPos; brick addressing depends only on gridPos + bpa, not
// on which channel's data is being read, so the hit-shading call site can
// resolve it once and read all 4 components — color's 3 + roughness's 1 —
// from that single resolved brick).
// ---------------------------------------------------------------------------
void sampleHitShadingChannels(vec3 gridPos, vec3 missingColor, float missingRoughness,
                              out vec3 outColor, out float outRoughness) {
    uint colorBase = channelBaseFloats(SEM_COLOR);
    uint roughBase = channelBaseFloats(SEM_ROUGHNESS);
    outColor     = missingColor;
    outRoughness = missingRoughness;
    if (colorBase == 0xFFFFFFFFu && roughBase == 0xFFFFFFFFu) return;

    // Resolve the shared brick address ONCE using whichever channel is present
    // (brick addressing itself doesn't depend on `base`); component 0 of that
    // channel comes along for free from the same fetch.
    uint primaryBase = (colorBase != 0xFFFFFFFFu) ? colorBase : roughBase;
    vec3 f; vec4 z0, z1; uint brickIdx; ivec3 local; bool oneBrick;
    _loadTrilinearCellComp(primaryBase, gridPos, 0, g_octreeIdx, f, z0, z1, brickIdx, local, oneBrick);

    if (!oneBrick) {
        // Brick-boundary cell: no shared addressing to reuse — fall back to the
        // independent per-channel resolution (each channel's corners may span
        // different neighbour bricks).
        if (colorBase != 0xFFFFFFFFu) outColor = sampleChannelVec3Trilinear(SEM_COLOR, gridPos, missingColor);
        if (roughBase != 0xFFFFFFFFu) outRoughness = sampleChannelScalarTrilinear(SEM_ROUGHNESS, gridPos, missingRoughness);
        return;
    }
    if (brickIdx == 0xFFFFFFFFu) {
        // Unallocated brick: matches the per-corner path's 1e9 sentinel behavior.
        if (colorBase != 0xFFFFFFFFu) outColor = vec3(1e9);
        if (roughBase != 0xFFFFFFFFu) outRoughness = 1e9;
        return;
    }

    uint voxel000 = uint(local.z * 64 + local.y * 8 + local.x);
    uint stride    = configs[g_octreeIdx].brickStrideFloats;
    uint poolBrick = configs[g_octreeIdx].poolBrickBase + brickIdx * stride;

    if (colorBase != 0xFFFFFFFFu) {
        vec3 c;
        // Component 0 already loaded above iff colorBase was the primary channel;
        // otherwise (roughness was primary) fetch color's component 0 too.
        if (primaryBase == colorBase) {
            c.x = _interpolateTrilinearCell(f, z0, z1);
        } else {
            uint pb0 = poolBrick + colorBase + voxel000;
            vec4 cz0 = vec4(channelPool[pb0], channelPool[pb0+1u], channelPool[pb0+8u], channelPool[pb0+9u]);
            vec4 cz1 = vec4(channelPool[pb0+64u], channelPool[pb0+65u], channelPool[pb0+72u], channelPool[pb0+73u]);
            c.x = _interpolateTrilinearCell(f, cz0, cz1);
        }
        for (int comp = 1; comp < 3; ++comp) {
            uint pb = poolBrick + colorBase + uint(comp) * VX_VOXELS_PER_BRICK + voxel000;
            vec4 cz0 = vec4(channelPool[pb], channelPool[pb+1u], channelPool[pb+8u], channelPool[pb+9u]);
            vec4 cz1 = vec4(channelPool[pb+64u], channelPool[pb+65u], channelPool[pb+72u], channelPool[pb+73u]);
            c[comp] = _interpolateTrilinearCell(f, cz0, cz1);
        }
        outColor = c;
    }
    if (roughBase != 0xFFFFFFFFu) {
        if (primaryBase == roughBase) {
            outRoughness = _interpolateTrilinearCell(f, z0, z1);
        } else {
            uint pb = poolBrick + roughBase + voxel000;
            vec4 rz0 = vec4(channelPool[pb], channelPool[pb+1u], channelPool[pb+8u], channelPool[pb+9u]);
            vec4 rz1 = vec4(channelPool[pb+64u], channelPool[pb+65u], channelPool[pb+72u], channelPool[pb+73u]);
            outRoughness = _interpolateTrilinearCell(f, rz0, rz1);
        }
    }
}

// ---------------------------------------------------------------------------
// sampleSdfTrilinearAtBase: trilinear interpolation of the SDF at a fractional
// grid position, given an ALREADY-RESOLVED channel base (hoisted out of the
// per-step march loop — audit A1's channelBaseFloats(SEM_SDF) linear scan).
// ---------------------------------------------------------------------------
float sampleSdfTrilinearAtBase(vec3 gridPos, int octreeIdx, uint sdfBase) {
    vec3 f;
    vec4 z0, z1;
    _loadTrilinearCell(sdfBase, gridPos, octreeIdx, f, z0, z1);
    return _interpolateTrilinearCell(f, z0, z1);
}

// ---------------------------------------------------------------------------
// sampleSdfTrilinear: trilinear interpolation of the SDF at a fractional grid
// position (in voxel units). gridPos is in octree grid-voxel coordinates
// (0..bpa*8 per axis). Resolves the SEM_SDF channel base itself — callers on
// the hot march-step path should prefer sampleSdfTrilinearAtBase with a
// hoisted base instead.
// ---------------------------------------------------------------------------
float sampleSdfTrilinear(vec3 gridPos, int octreeIdx) {
    return sampleSdfTrilinearAtBase(gridPos, octreeIdx, channelBaseFloats(SEM_SDF));
}

// Same sentinel threshold as marchBrickSdf's SENTINEL_D: a trilinear sample this large means
// the stencil straddled into an unallocated neighbour brick (_samplePoolVoxel -> 1e9), not an
// honest distance.
const float SDF_GRAD_SENTINEL_D = 100.0;

vec3 _normalizeSdfGradient(vec3 g) {
    float len = length(g);
    return (len > 1e-6) ? g / len : vec3(0.0, 1.0, 0.0);
}

// ---------------------------------------------------------------------------
// _sdfGradientFiniteDifference: the ORIGINAL central-difference gradient (6
// extra trilinear samples), kept as the fallback for the rare cell whose own
// 8-corner stencil is sentinel-contaminated (straddles an unallocated
// neighbour brick) -- the analytic cell gradient below has no well-defined
// derivative there, so this preserves the exact old degenerate-cell behavior.
// ---------------------------------------------------------------------------
vec3 _sdfGradientFiniteDifference(vec3 gridPos, int octreeIdx, uint sdfBase) {
    const float h = 0.5;
    float d0 = sampleSdfTrilinearAtBase(gridPos, octreeIdx, sdfBase);

    float dxPlus  = sampleSdfTrilinearAtBase(gridPos + vec3(h,0,0), octreeIdx, sdfBase);
    float dxMinus = sampleSdfTrilinearAtBase(gridPos - vec3(h,0,0), octreeIdx, sdfBase);
    float gx = (abs(dxPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dxMinus) * 2.0
             : (abs(dxMinus) > SDF_GRAD_SENTINEL_D) ? (dxPlus - d0) * 2.0
             : (dxPlus - dxMinus);

    float dyPlus  = sampleSdfTrilinearAtBase(gridPos + vec3(0,h,0), octreeIdx, sdfBase);
    float dyMinus = sampleSdfTrilinearAtBase(gridPos - vec3(0,h,0), octreeIdx, sdfBase);
    float gy = (abs(dyPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dyMinus) * 2.0
             : (abs(dyMinus) > SDF_GRAD_SENTINEL_D) ? (dyPlus - d0) * 2.0
             : (dyPlus - dyMinus);

    float dzPlus  = sampleSdfTrilinearAtBase(gridPos + vec3(0,0,h), octreeIdx, sdfBase);
    float dzMinus = sampleSdfTrilinearAtBase(gridPos - vec3(0,0,h), octreeIdx, sdfBase);
    float gz = (abs(dzPlus) > SDF_GRAD_SENTINEL_D) ? (d0 - dzMinus) * 2.0
             : (abs(dzMinus) > SDF_GRAD_SENTINEL_D) ? (dzPlus - d0) * 2.0
             : (dzPlus - dzMinus);

    return _normalizeSdfGradient(vec3(gx, gy, gz));
}

// ---------------------------------------------------------------------------
// _sdfCellGradient: exact analytic gradient of the trilinear interpolant
// represented by the cell's 8 already-loaded corners (z0/z1, see
// _loadTrilinearCell) -- the partial derivatives of
//   mix(mix(mix(z0.x,z0.y,fx),mix(z0.z,z0.w,fx),fy), mix(mix(z1.x,z1.y,fx),mix(z1.z,z1.w,fx),fy), fz)
// w.r.t. fx/fy/fz respectively (audit A2: replaces 7 full trilinear samples,
// including a redundant re-sample of d0, with zero additional pool reads --
// the hit sample already loaded these exact corners).
// ---------------------------------------------------------------------------
vec3 _sdfCellGradient(vec3 f, vec4 z0, vec4 z1) {
    float gx = mix(mix(z0.y - z0.x, z0.w - z0.z, f.y),
                   mix(z1.y - z1.x, z1.w - z1.z, f.y), f.z);
    float gy = mix(mix(z0.z - z0.x, z0.w - z0.y, f.x),
                   mix(z1.z - z1.x, z1.w - z1.y, f.x), f.z);
    float gz = mix(mix(z1.x - z0.x, z1.y - z0.y, f.x),
                   mix(z1.z - z0.z, z1.w - z0.w, f.x), f.y);
    return _normalizeSdfGradient(vec3(gx, gy, gz));
}

bool _sdfCellContaminated(vec4 z0, vec4 z1) {
    return any(greaterThan(abs(z0), vec4(SDF_GRAD_SENTINEL_D))) ||
           any(greaterThan(abs(z1), vec4(SDF_GRAD_SENTINEL_D)));
}

// ---------------------------------------------------------------------------
// sdfGradientStoredFromCell: gradient of the trilinear SDF field at gridPos,
// given the cell (f, z0, z1) the caller ALREADY loaded for the hit sample --
// no extra pool reads on the honest-cell path. Falls back to the original
// sentinel-aware central-difference (_sdfGradientFiniteDifference) exactly
// when any of the 8 corners is sentinel-contaminated, preserving the old
// degenerate-cell (brick-seam) behavior described below.
//
// Sentinel-aware per axis (fallback path only): a central-difference sample straddling a brick
// boundary can read an unallocated neighbour (1e9 sentinel, see _samplePoolVoxel), which would
// otherwise blow up that axis's component and corrupt the normal -- visible as speckled shading
// noise right along brick seams (worst near a smooth-union fillet, where the iso-surface sits
// close to a brick face). marchBrickSdf's own iso-search loop already guards against this
// (SENTINEL_D); the fallback mirrors that guard by falling back to a one-sided difference against
// the known-good on-surface sample (d0, near-zero since gridPos is the just-found hit point)
// whenever a side is contaminated.
// ---------------------------------------------------------------------------
vec3 sdfGradientStoredFromCell(vec3 gridPos, int octreeIdx, vec3 f, vec4 z0, vec4 z1) {
    return _sdfCellContaminated(z0, z1)
        ? _sdfGradientFiniteDifference(gridPos, octreeIdx, channelBaseFloats(SEM_SDF))
        : _sdfCellGradient(f, z0, z1);
}

// ---------------------------------------------------------------------------
// sdfGradientStored: gradient of the trilinear SDF field at gridPos, re-loading
// the cell itself. Callers that already have the hit cell's (f, z0, z1) on hand
// (the march loop) should use sdfGradientStoredFromCell instead to avoid the
// redundant reload.
// ---------------------------------------------------------------------------
vec3 sdfGradientStored(vec3 gridPos, int octreeIdx) {
    uint sdfBase = channelBaseFloats(SEM_SDF);
    vec3 f;
    vec4 z0, z1;
    _loadTrilinearCell(sdfBase, gridPos, octreeIdx, f, z0, z1);
    return sdfGradientStoredFromCell(gridPos, octreeIdx, f, z0, z1);
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
    // See _samplePoolVoxel's matching comment: exact-prefix brickLookupBase,
    // not a uniform-bpa octreeIdx*bpa^3 assumption.
    uint lookupBase = configs[octreeIdx].brickLookupBase;
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
    // Hoisted out of the per-step/per-corner path (audit A1): channelBaseFloats scans
    // octreeConfig.channels[] linearly, so resolving it once per march (not once per
    // trilinear sample) removes a redundant scan from every one of up to MAX_STEPS*
    // MAX_BRICK_HOPS step evaluations.
    uint sdfBase = channelBaseFloats(SEM_SDF);

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
            // Load the cell ONCE per step: the single-brick fast path (audit A1/#6) resolves
            // it in 1 brickLookup fetch + 8 contiguous pool loads for ~67% of cells; a hit
            // reuses these SAME corners for the analytic gradient below (audit A2) instead of
            // re-sampling 7 more trilinear points.
            vec3 cellF;
            vec4 cellZ0, cellZ1;
            _loadTrilinearCell(sdfBase, p, octreeIdx, cellF, cellZ0, cellZ1);
            float d = _interpolateTrilinearCell(cellF, cellZ0, cellZ1);
            if (d < EPS) {                    // crossed (or reached) the iso-surface
                hitNormal = sdfGradientStoredFromCell(p, octreeIdx, cellF, cellZ0, cellZ1);
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

// ---------------------------------------------------------------------------
// marchBrickSdfAnyHit (Baked-Perf M4 Task 4.2 / audit C1-C2 / Top #7): any-hit
// occlusion variant of marchBrickSdf above -- identical brick-hop + sphere-trace
// stepping (same crossing test, same step formula, same brick-to-brick
// continuation via _advanceToNextSdfLeaf), but:
//   (a) never computes the analytic gradient (sdfGradientStoredFromCell) --
//       a shadow/probe-occlusion ray only needs "did the ray cross the
//       iso-surface," never the surface normal;
//   (b) never calls sampleHitShadingChannels (color/roughness trilinear, ~32
//       pool reads for the two channels combined) -- the caller (TraceWorldShadow)
//       discards everything but the boolean;
//   (c) is handed sMaxLimit, the remaining arc-length budget to the caller's
//       tmax (light distance), converted to this SAME grid-arc-length unit by
//       the caller -- once curEntry's own arc-length exceeds it, no further
//       brick can contain an occluder within [tmin,tmax], so the hop loop
//       exits early instead of continuing to march/hop past the light.
// Returns true the instant ANY crossing is found within [0, sMaxLimit] (never
// mind which brick); sHit is returned for the caller's tmax re-check (the
// per-brick sMax test only bounds "did we walk far enough to give up", the
// actual sHit vs tmax comparison still happens once at the true crossing --
// same two-tier discipline TraceWorldShadow already applies to hitT below).
// ---------------------------------------------------------------------------
bool marchBrickSdfAnyHit(int octreeIdx, ivec3 brick, vec3 gridEntry, vec3 gridDirN,
                         float sMaxLimit,
                         TraversalState state, RayCoefficients coef, StackEntry stack[STACK_SIZE],
                         out float sHit) {
    sHit = 0.0;

    int bpa = int(configs[octreeIdx].bricksPerAxisSdf);
    uint sdfBase = channelBaseFloats(SEM_SDF);

    const int   MAX_STEPS   = 96;
    const float EPS         = 0.01;
    const float SENTINEL_D  = 100.0;
    const int   MAX_BRICK_HOPS = 2048;

    float sBase = 0.0;
    ivec3 curBrick = brick;

    for (int hop = 0; hop < MAX_BRICK_HOPS; ++hop) {
        if (sBase > sMaxLimit) return false;  // walked past the light distance -- no occluder can matter beyond here

        vec3 curEntry = gridEntry + gridDirN * sBase;
        vec3 bMin = vec3(curBrick) * 8.0;
        vec3 bMax = bMin + vec3(8.0);

        const float kAxisParallelSlabDist = 64.0;
        vec3 invD = vec3(
            abs(gridDirN.x) > 1e-8 ? 1.0 / gridDirN.x : 0.0,
            abs(gridDirN.y) > 1e-8 ? 1.0 / gridDirN.y : 0.0,
            abs(gridDirN.z) > 1e-8 ? 1.0 / gridDirN.z : 0.0);
        bvec3 axisParallel = lessThanEqual(abs(gridDirN), vec3(1e-8));
        vec3  t0   = mix((bMin - curEntry) * invD, vec3(-kAxisParallelSlabDist), axisParallel);
        vec3  t1   = mix((bMax - curEntry) * invD, vec3( kAxisParallelSlabDist), axisParallel);
        vec3  thi  = max(t0, t1);
        float sMax = max(min(min(thi.x, thi.y), thi.z), 0.0);
        float sMaxClamped = min(sMax, sMaxLimit - sBase);  // don't step past the light within this brick either

        float s = 0.0;
        for (int i = 0; i < MAX_STEPS; ++i) {
            if (s > sMaxClamped) break;
            vec3  p = curEntry + gridDirN * s;
            vec3 cellF;
            vec4 cellZ0, cellZ1;
            _loadTrilinearCell(sdfBase, p, octreeIdx, cellF, cellZ0, cellZ1);
            float d = _interpolateTrilinearCell(cellF, cellZ0, cellZ1);
            if (d < EPS) {
                sHit = sBase + s;
                return true;
            }
            s += (d > SENTINEL_D) ? 1.0 : max(d * 0.5773503, EPS);
        }
        if (sBase + s > sMaxLimit) return false;  // exited the clamped span without a crossing -- no occluder in range

        ivec3 nextBrick;
        if (!_advanceToNextSdfLeaf(state, coef, stack, octreeIdx, bpa, nextBrick)) {
            return false;
        }

        vec3 nMin = vec3(nextBrick) * 8.0;
        vec3 nMax = nMin + vec3(8.0);
        vec3 nt0  = mix((nMin - gridEntry) * invD, vec3(-kAxisParallelSlabDist), axisParallel);
        vec3 nt1  = mix((nMax - gridEntry) * invD, vec3( kAxisParallelSlabDist), axisParallel);
        vec3 ntlo = min(nt0, nt1);
        float sEnter = max(max(max(ntlo.x, ntlo.y), ntlo.z), sBase);
        sBase    = sEnter;
        curBrick = nextBrick;
    }
    return false;
}

#endif // STORED_SDF_GLSL
