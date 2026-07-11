using Yeroket.Util.KernelFramework;

// Canonical LightTreeGpuNode / LightTreeBuffer — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc3 M4: the GPU-side mirror of LightTree.h's CPU LightTreeNode, so
// RIS candidate generation can sample the SAME mip-cut light-tree the M3 brute-force
// reference validates against, without duplicating the cut math in two independently-
// drifting places (only the UPLOAD is new; BuildLightTreeCut, LightTree.h, still owns
// the cut algorithm).
//
// Fixed-capacity array (UBO-friendly, same shape as LightingConfig.lights[]) rather than
// a variable-length SSBO array — kMaxLightTreeNodes bounds a single body's cut to a size
// that comfortably fits a per-frame upload; BuildLightTreeCut's own powerThreshold/
// minExtentThreshold params are the caller's lever to keep a real cut under this cap
// (LightTreeBufferNode.cpp truncates + logs if a cut somehow exceeds it, rather than
// silently overrunning the buffer).
//
// Field order/units mirror LightTreeNode (LightTree.h) exactly:
//   worldPos    -- node's cube center, grid/world space (SdfBake.h's [0,n) convention)
//   worldExtent -- cube side length
//   intensity   -- mean emissive intensity beneath this node (MipSample::value)
//   coverage    -- fraction of the node's footprint that is actually emissive (0..1]
[GpuStruct]
public struct LightTreeGpuNode
{
    public Float3 worldPos;
    public float worldExtent;
    public float intensity;
    public float coverage;
}

public static class LightTreeBufferLimits
{
    // A coarse mip-cut ("million glowing voxels -> handful of nodes") comfortably fits
    // well under this; generous headroom over the gate scene's own node counts (single-
    // to low-double-digit at the tested power thresholds -- see test_light_tree.cpp).
    public const int kMaxLightTreeNodes = 64;
}

[GpuStruct]
public struct LightTreeBuffer
{
    public uint nodeCount;

    [GpuArray(LightTreeBufferLimits.kMaxLightTreeNodes)] public LightTreeGpuNode nodes;
}
