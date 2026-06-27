// GENERATED from SdfCoreKernels.cs by the kernel-framework C++/HLSL emitter
// Do not edit; regenerate via the Yeroket source generator (P1 automates).

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

inline float SdfCore_Intersect(float a, float b) {
    return glm::max(a, b);
}

inline float SdfCore_Subtract(float a, float b) {
    return glm::max(a, -b);
}

inline float SdfCore_Xor(float a, float b) {
    return glm::max(glm::min(a, b), -glm::max(a, b));
}

inline float SdfCore_SmoothIntersect(float a, float b, float k) {
    float h = glm::clamp(0.5f - 0.5f * (b - a) / k, 0.0f, 1.0f);
    return glm::mix(b, a, h) + k * h * (1.0f - h);
}

inline float SdfCore_SmoothSubtract(float a, float b, float k) {
    float h = glm::clamp(0.5f - 0.5f * (b + a) / k, 0.0f, 1.0f);
    return glm::mix(a, -b, h) + k * h * (1.0f - h);
}

inline float SdfCore_SmoothMax(float a, float b, float k) {
    float h = glm::max(k - glm::abs(a - b), 0.0f) / k;
    return glm::max(a, b) + h * h * h * k * (1.0f / 6.0f);
}

inline float SdfCore_SmoothUnionCubic(float a, float b, float k) {
    float h = glm::max(k - glm::abs(a - b), 0.0f) / k;
    return glm::min(a, b) - h * h * h * k * (1.0f / 6.0f);
}

inline float SdfCore_SmoothIntersectCubic(float a, float b, float k) {
    float h = glm::max(k - glm::abs(a - b), 0.0f) / k;
    return glm::max(a, b) + h * h * h * k * (1.0f / 6.0f);
}

inline float SdfCore_SmoothSubtractCubic(float a, float b, float k) {
    float h = glm::max(k - glm::abs(-b - a), 0.0f) / k;
    return glm::max(a, -b) + h * h * h * k * (1.0f / 6.0f);
}

inline float SdfCore_Round(float d, float r) {
    return d - r;
}

inline float SdfCore_Onion(float d, float r) {
    return glm::abs(d) - r;
}

inline float SdfCore_BoxRounded(glm::vec3 p, glm::vec3 halfExtents, float roundRadius) {
    glm::vec3 q = glm::abs(p) - halfExtents + roundRadius;
    return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f) - roundRadius;
}

inline float SdfCore_Capsule(glm::vec3 p, float height, float radius) {
    glm::vec3 localP = p;
    localP.y -= glm::clamp(localP.y, -height, height);
    return glm::length(localP) - radius;
}

inline float SdfCore_Cylinder(glm::vec3 p, float height, float radius) {
    glm::vec2 d = glm::vec2(glm::length(glm::vec2(p.x, p.z)) - radius, glm::abs(p.y) - height);
    return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
}

inline float SdfCore_Plane(glm::vec3 p, glm::vec3 normal, float distance) {
    return glm::dot(p, normal) + distance;
}

inline float SdfCore_Torus(glm::vec3 p, float majorRadius, float minorRadius) {
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)) - majorRadius, p.y);
    return glm::length(q) - minorRadius;
}

} // namespace Yeroket::Sdf::Generated
