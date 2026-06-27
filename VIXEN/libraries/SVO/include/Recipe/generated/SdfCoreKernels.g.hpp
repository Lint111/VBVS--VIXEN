// GENERATED from SdfCoreKernels.cs by the kernel-framework C++/HLSL emitter
// Do not edit; regenerate via the Yeroket source generator (P1 automates).
// Vendored from Yeroket-Fantasy Packages/com.utility.sdf/Runtime/GPU/Generated/ (branch feat/kernel-codegen-p1, generator emits `inline`)

#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace Yeroket::Sdf::Generated {

inline float SdfCore_Sphere(glm::vec3 p, glm::vec3 center, float radius) {
    return glm::length(p - center) - radius;
}

inline float SdfCore_Union(float a, float b) {
    return glm::min(a, b);
}

inline float SdfCore_Box(glm::vec3 p, glm::vec3 b) {
    glm::vec3 q = glm::abs(p) - b;
    return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
}

inline float SdfCore_SmoothUnion(float a, float b, float k) {
    float h = glm::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return glm::mix(b, a, h) - k * h * (1.0f - h);
}

inline glm::vec3 SdfCore_MirrorX(glm::vec3 p) {
    return glm::vec3(glm::abs(p.x), p.y, p.z);
}

} // namespace Yeroket::Sdf::Generated
