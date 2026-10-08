using Yeroket.Util.KernelFramework;

// A small, fixed-capacity set of additive mining beams. The schema is the
// shared C++/GLSL contract for both the list header and each beam record.
public static class MiningBeamBufferLimits
{
    public const int kMaxBeams = 16;
}

[GpuStruct]
public struct MiningBeamGpu
{
    public uint sourceInstanceIndex;
    public uint targetInstanceIndex;
    public float radius;
    public float luminosity;

    public Float3 sourceLocalOffset;
    public float purposeScale;

    public Float3 targetLocalOffset;
    public float _pad0;

    public Float3 color;
    public float _pad1;
}

[GpuStruct]
public struct MiningBeamBuffer
{
    public uint beamCount;
    public uint enabled;
    public uint _pad0;
    public uint _pad1;

    [GpuArray(MiningBeamBufferLimits.kMaxBeams)]
    public MiningBeamGpu beams;
}
