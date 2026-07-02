using Vixen.Codegen.Attributes;

/// <summary>P0 walking-skeleton config — proves the pipeline end to end.</summary>
[GpuStruct]
public struct SkeletonConfig
{
    public uint version;   // offset 0
    public int  payload;   // offset 4
}
