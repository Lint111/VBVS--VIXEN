using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// GPU-only frame state shared by the hand meter/reduce passes and generated tonemap.
// The fixed histogram width is mirrored by the vixen:exposure-meter schema member.
[GpuStruct]
public struct ExposureMeterState
{
    public float log2Exposure;
    public uint state;
    public float meteredEV100;
    public float targetLog2Exposure;
    [GpuArray(128)] public uint histogram;
}
