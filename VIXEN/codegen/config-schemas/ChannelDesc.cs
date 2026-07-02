using Yeroket.Util.KernelFramework;

// Canonical per-channel descriptor — mirrors VoxelChannelFormat.h ChannelDesc (1 uvec4).
[GpuStruct]
public struct ChannelDesc
{
    public uint semanticId;
    public uint elemCount;
    public uint channelBaseFloats;
    public uint fieldKind;
}
