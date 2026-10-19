// Type declarations only, for the recipe SIMD codegen compilation: the kernel's SdfCoreKernels.cs
// names Unity.Mathematics vectors that the standalone CodegenTool compilation does not reference.
namespace Unity.Mathematics
{
    public struct float2 { public float x, y; public float2(float x, float y) { this.x = x; this.y = y; } }
    public struct float3 { public float x, y, z; public float3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; } }
    public struct float4 { public float x, y, z, w; public float4(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; } }
}
