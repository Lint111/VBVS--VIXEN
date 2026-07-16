using Yeroket.Util.KernelFramework;

// Canonical OctreeConfig — one source for C++ (Vixen::Gpu) + GLSL. 432 B std430.
// Offsets (must match ShellOctreeGpu.h): gridMin@32, gridMax@48, localToWorld@64,
// worldToLocal@128, nodeArrayBase@192, formatId@200, brickStrideFloats@216, channels@224,
// mipPoolBase@352 (Sparse-Mip ESVO LOD Inc1 M1 Task 3), brickResident@356 (Inc1 M3 Task 7),
// tierRefTableBase@360 (Tiered-ESVO Inc2 M1 Task 3), brickLookupBase@364
// (Stored-SDF heterogeneous lookup-table prefix), traceBoundsMin@368,
// traceBoundsMax@384 (conservative occupied-brick cull bounds in root-local [0,1]).
[GpuStruct]
public struct OctreeConfig
{
    public int esvoMaxScale;
    public int userMaxLevels;
    public int brickDepthLevels;
    public int brickSize;

    public int minESVOScale;
    public int brickESVOScale;
    public int bricksPerAxis;
    public int _padding1;

    public Float3 gridMin;   // @32 (align16; implicit 4B pad before gridMax)
    public Float3 gridMax;   // @48

    public Mat4 localToWorld; // @64
    public Mat4 worldToLocal; // @128

    public int nodeArrayBase; // @192
    public int brickArrayBase; // @196

    public uint formatId;         // @200
    public uint bricksPerAxisSdf; // @204
    public uint poolBrickBase;    // @208
    public uint channelCount;     // @212

    public uint brickStrideFloats; // @216
    public uint _padChannels;      // @220 — load-bearing: 16-aligns channels[] for the GLSL uvec4 array

    [GpuArray(8)]  public ChannelDesc channels; // @224 (8 × 16 B = 128 → ends 352)

    // Sparse-Mip ESVO LOD Inc1 M1 Task 3: element offset (in MipSample units)
    // of this octree's mip pool in the shared/concatenated mip pool buffer —
    // mirrors poolBrickBase's convention for the channel pool.
    public uint mipPoolBase; // @352

    // Sparse-Mip ESVO LOD Inc1 M3 Task 7: per-tree binary brick residency
    // (§0 scope). 0 = bricksBuffer_ region for this octree is allocated but
    // NOT populated (mip-only tree, M2's RequestBrickResidency(false)); 1 =
    // fully uploaded. The shader's leaf-hit existence check reads THIS field,
    // not hasBrick()/contourPointer — the descriptor's brick pointer stays
    // valid regardless of residency (M2 Task 4), so it cannot itself signal
    // "allocated but not yet uploaded."
    public uint brickResident; // @356

    // Tiered-ESVO Inc2 M1 Task 3: element offset (in TierRef units) of this
    // octree's own slice of the shared/concatenated TierRefTable — mirrors
    // mipPoolBase's/poolBrickBase's convention exactly. 0 == "no tier-ref
    // entries before this offset" for every tree until M2's construction
    // path (farBit==1 leaves) starts registering real entries.
    public uint tierRefTableBase; // @360

    // Element offset (in uint32 entries) of this octree's dense brick-grid
    // lookup table within the shared concatenated brickLookup buffer.
    // Unlike octreeIdx*bpa^3, this remains valid when adjacent octrees use
    // different grid resolutions.
    public uint brickLookupBase; // @364

    // Conservative root-local AABB enclosing every allocated stored-SDF brick.
    // The shader uses this only as a reject/ordering bound before traversing the
    // unchanged [0,1]^3 ESVO root, so brick-level quantization cannot remove hits.
    // Legacy/uninitialized zero bounds deliberately fall back to the full root.
    public Float3 traceBoundsMin; // @368 (16-byte std430 slot)
    public Float3 traceBoundsMax; // @384 (16-byte std430 slot)

    // Keep the long-standing 432-byte array stride. traceBoundsMax's three scalar
    // lanes end at byte 396 in C++; nine uints fill the record exactly to byte 432.
    // GLSL's std430 vec3 slot remains 16-byte aligned at its byte-384 start.
    [GpuArray(9)] public uint _tailPad; // @396 (9 × 4 = 36 → ends 432)
}
