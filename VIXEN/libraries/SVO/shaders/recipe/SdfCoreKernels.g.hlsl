// GENERATED from SdfCoreKernels.cs by the kernel-framework C++/HLSL emitter
// Do not edit; regenerate via the Yeroket source generator (P1 automates).
// Vendored from Yeroket-Fantasy Packages/com.utility.sdf/Runtime/GPU/Generated/ (branch feat/kernel-cpp-emitter)

#ifndef SDF_CORE_KERNELS_G_HLSL
#define SDF_CORE_KERNELS_G_HLSL

float SdfCore_Sphere(float3 p, float3 center, float radius) {
    return length(p - center) - radius;
}

float SdfCore_Union(float a, float b) {
    return min(a, b);
}

float SdfCore_Box(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
}

float SdfCore_SmoothUnion(float a, float b, float k) {
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1 - h);
}

float3 SdfCore_MirrorX(float3 p) {
    return float3(abs(p.x), p.y, p.z);
}


#endif // SDF_CORE_KERNELS_G_HLSL
