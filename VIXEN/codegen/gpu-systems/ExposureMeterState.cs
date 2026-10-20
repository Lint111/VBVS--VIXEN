namespace Vixen.Codegen.Gpu;

// The adapted-exposure record ExposureReduce.comp writes at the head of the meter buffer
// (shaders/ExposureHistogram.glsl declares the same layout for the hand meter passes until
// they convert). The vixen:exposure-meter frame kind is its manifest identity;
// the generated tonemap consumer therefore needs no parallel [GpuStruct] identity.
public struct ExposureMeterState
{
    public float log2Exposure;
    public uint state;
    public float meteredEV100;
    public float targetLog2Exposure;
}
