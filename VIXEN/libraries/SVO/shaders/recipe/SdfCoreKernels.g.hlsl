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
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float SdfCore_SmoothUnion(float a, float b, float k) {
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

float3 SdfCore_MirrorX(float3 p) {
    return float3(abs(p.x), p.y, p.z);
}

float SdfCore_Intersect(float a, float b) {
    return max(a, b);
}

float SdfCore_Subtract(float a, float b) {
    return max(a, -b);
}

float SdfCore_Xor(float a, float b) {
    return max(min(a, b), -max(a, b));
}

float SdfCore_SmoothIntersect(float a, float b, float k) {
    float h = saturate(0.5 - 0.5 * (b - a) / k);
    return lerp(b, a, h) + k * h * (1.0 - h);
}

float SdfCore_SmoothSubtract(float a, float b, float k) {
    float h = saturate(0.5 - 0.5 * (b + a) / k);
    return lerp(a, -b, h) + k * h * (1.0 - h);
}

float SdfCore_SmoothMax(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothUnionCubic(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothIntersectCubic(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_SmoothSubtractCubic(float a, float b, float k) {
    float h = max(k - abs(-b - a), 0.0) / k;
    return max(a, -b) + h * h * h * k * (1.0 / 6.0);
}

float SdfCore_Round(float d, float r) {
    return d - r;
}

float SdfCore_Onion(float d, float r) {
    return abs(d) - r;
}


#endif // SDF_CORE_KERNELS_G_HLSL
