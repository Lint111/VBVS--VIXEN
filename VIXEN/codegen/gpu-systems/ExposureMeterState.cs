using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// The adapted-exposure record ExposureReduce.comp writes at the head of the meter buffer
// (shaders/ExposureHistogram.glsl declares the same layout for the hand meter passes until
// they convert). Its type is the meter's schema identity in the GPU system manifest.
[GpuStruct]
public struct ExposureMeterState
{
    public float log2Exposure;
    public uint state;
    public float meteredEV100;
    public float targetLog2Exposure;
}
