using Yeroket.Util.KernelFramework;

// Canonical OctreeConfig — one source for C++ (Vixen::Gpu) + GLSL. 432 B std430.
// Offsets (must match ShellOctreeGpu.h): traceBoundsMin@32, traceBoundsMax@48, localToWorld@64,
// worldToLocal@128, nodeArrayBase@192, formatId@200, brickStrideFloats@216, channels@224,
// mipPoolBase@352 (Sparse-Mip ESVO LOD Inc1 M1 Task 3), brickResident@356 (Inc1 M3 Task 7),
// tierRefTableBase@360 (Tiered-ESVO Inc2 M1 Task 3), brickLookupBase@364 (Baked-Perf M1
// Task 1.1).
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

    // Baked-Perf M5 Task 5.1: conservative allocated-brick AABB in this octree's OWN
    // [0,1]^3 local grid space (NOT world space, despite the "grid" naming this field
    // replaces) — the tight bound over every brick actually populated by SerializeSdf's
    // grid->brick lookup loop, as opposed to the octree's full root extent. REPLACES the
    // dead gridMin/gridMax fields (uploaded but read by no dispatched shader — Dormant-
    // Work-Inventory #10; the only real consumer of the octree's world AABB is the
    // localToWorld/worldToLocal matrix pair, which every instanced shader path already
    // uses instead). Net-zero schema growth: same two Float3 slots, same offsets.
    // Zero-default (both fields left at their memset(0) default, i.e. min==max==(0,0,0))
    // is BACKWARD-SAFE: old cached SerializedOctree data / any non-SDF serializer that
    // never sets these fields reproduces the historical "no tighter bound than the full
    // [0,1]^3 root" behavior — see TraceWorld.glsl's getOctreeTraceBounds validity check.
    public Float3 traceBoundsMin;   // @32 (align16; implicit 4B pad before traceBoundsMax)
    public Float3 traceBoundsMax;   // @48

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

    // Baked-Perf M1 Task 1.1: element offset (in uint32 units, i.e. brickLookup[]
    // table-entry units — NOT bytes) of this octree's own slice of the shared/
    // concatenated brickLookup grid-lookup table. Mirrors mipPoolBase's/
    // tierRefTableBase's convention exactly, but is NOT redundant with them: the
    // brickLookup sub-table size is bpa^3 (bricksPerAxisSdf, a per-octree GRID
    // dimension), which is unrelated to nodeCount/brickCount/tierRefCount, so it
    // cannot be derived from nodeArrayBase/brickArrayBase/tierRefTableBase.
    //
    // WHY THIS FIELD EXISTS (do not delete/revert): before this field, the shader
    // computed lookupBase as `octreeIdx * bpa^3` using the CURRENT octree's own
    // bpa (StoredSdf.glsl _samplePoolVoxel/_sdfBrickAllocated) — silently assuming
    // every concatenated octree shares the same bricksPerAxisSdf. The CPU-side
    // concatenation (ConcatenateSdf/ConcatenateSdfWithMips) appends each octree's
    // bpa^3-sized sub-table at a variable-size EXACT PREFIX SUM (mirrored
    // correctly already in ShellDerive.h's DeriveShellPool loop). Cornell mixes
    // bpa=16 walls with bpa=2 light/sphere/box bodies: octrees 5-7 read the
    // WRONG base (uniform-bpa formula) and the shader silently walked garbage
    // brickLookup entries -> those bodies rendered as unallocated/miss (vanished
    // from the [CornellDiag] instIdx map) while walls (whose bpa matched the
    // uniform assumption at index 0) stayed correct. Zero-default (existing
    // scenes with a single uniform bpa, or octreeIdx==0) reproduces the old
    // value exactly, since the old formula's octreeIdx==0 term is always 0 too.
    public uint brickLookupBase; // @364

    [GpuArray(16)] public uint _tailPad; // @368 (16 × 4 = 64 → ends 432)
}
