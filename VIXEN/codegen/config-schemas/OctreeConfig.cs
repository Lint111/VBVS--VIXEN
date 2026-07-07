using Yeroket.Util.KernelFramework;

// Canonical OctreeConfig — one source for C++ (Vixen::Gpu) + GLSL. 432 B std430.
// Offsets (must match ShellOctreeGpu.h): gridMin@32, gridMax@48, localToWorld@64,
// worldToLocal@128, nodeArrayBase@192, formatId@200, brickStrideFloats@216, channels@224,
// mipPoolBase@352 (Sparse-Mip ESVO LOD Inc1 M1 Task 3), brickResident@356 (Inc1 M3 Task 7),
// tierRefTableBase@360 (Tiered-ESVO Inc2 M1 Task 3).
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

    [GpuArray(17)] public uint _tailPad; // @364 (17 × 4 = 68 → ends 432)
}
