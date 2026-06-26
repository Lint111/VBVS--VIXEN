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


#endif // SDF_CORE_KERNELS_G_HLSL
