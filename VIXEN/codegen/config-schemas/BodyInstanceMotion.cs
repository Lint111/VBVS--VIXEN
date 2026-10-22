using Yeroket.Util.KernelFramework;

namespace Vixen.Gpu;

// R494: previous rigid transform resolved by the producer from its generated
// schema Ref and store generation. No correspondence is implied by an index.
[GpuStruct]
public struct BodyInstanceMotion
{
    public Mat4 previousLocalToWorld;
    public uint valid;
    public uint _pad0;
    public uint _pad1;
    public uint _pad2;
}
