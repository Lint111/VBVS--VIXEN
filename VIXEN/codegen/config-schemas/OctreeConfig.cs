using Yeroket.Util.KernelFramework;

// Canonical OctreeConfig — one source for C++ (Vixen::Gpu) + GLSL. 432 B std430.
// Offsets (must match ShellOctreeGpu.h): gridMin@32, gridMax@48, localToWorld@64,
// worldToLocal@128, nodeArrayBase@192, formatId@200, brickStrideFloats@216, channels@224,
// mipPoolBase@352 (Sparse-Mip ESVO LOD Inc1 M1 Task 3).
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

    [GpuArray(19)] public uint _tailPad; // @356 (19 × 4 = 76 → ends 432)
}
