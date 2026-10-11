using Yeroket.Util.KernelFramework;

// Canonical SparseEmitterEntry — one source for C++ (Vixen::Gpu) + GLSL.
// Count-agnostic lighting (R502.2, lightdesign §2.3 step 1): one row per STORED body
// instance whose octree carries a non-zero sparse power lane, with an inclusive running
// power CDF (cdf of the last row == EmitterTotals.phiSparse). areaScale converts the lane's
// octree-normalized area to world area: (kWorldGridSize * instanceScale)^2. 32 B std430.
[GpuStruct]
public struct SparseEmitterEntry
{
    public uint instIdx;    // @0
    public uint octreeIdx;  // @4
    public float phi;       // @8   instance power, W
    public float cdf;       // @12  inclusive cumulative power

    public float areaScale; // @16
    public uint _pad0;      // @20
    public uint _pad1;      // @24
    public uint _pad2;      // @28
}
