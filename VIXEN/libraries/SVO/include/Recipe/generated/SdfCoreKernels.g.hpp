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

inline glm::vec3 SdfCore_MirrorY(glm::vec3 p) {
    return glm::vec3(p.x, glm::abs(p.y), p.z);
}

inline glm::vec3 SdfCore_MirrorZ(glm::vec3 p) {
    return glm::vec3(p.x, p.y, glm::abs(p.z));
}

inline glm::vec3 SdfCore_Elongate(glm::vec3 p, glm::vec3 h) {
    return p - glm::clamp(p, -h, h);
}

inline glm::vec3 SdfCore_Revolution(glm::vec3 p, glm::vec3 center, float offset) {
    glm::vec3 pp = p - center;
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(pp.x, pp.z)) - offset, pp.y);
    return glm::vec3(q.x, q.y, 0) + center;
}

inline glm::vec3 SdfCore_Twist(glm::vec3 p, float k) {
    float c = glm::cos(k * p.y); float s = glm::sin(k * p.y);
    glm::vec2 q(c * p.x - s * p.z, s * p.x + c * p.z);
    return glm::vec3(q.x, p.y, q.y);
}

inline glm::vec3 SdfCore_Bend(glm::vec3 p, float k) {
    float c = glm::cos(k * p.x); float s = glm::sin(k * p.x);
    glm::vec2 q(c * p.x - s * p.y, s * p.x + c * p.y);
    return glm::vec3(q.x, q.y, p.z);
}

inline glm::vec3 SdfCore_RepeatInfinite(glm::vec3 p, glm::vec3 spacing) {
    return glm::mod(glm::abs(p) + spacing * 0.5f, spacing) - spacing * 0.5f;
}

inline glm::vec3 SdfCore_RepeatLimited(glm::vec3 p, float spacing, glm::vec3 limit) {
    return p - spacing * glm::clamp(glm::round(p / spacing), -limit, limit);
}

inline glm::vec3 SdfCore_Transform(glm::vec3 p, glm::vec3 translation, glm::vec4 invRotXYZW, glm::vec3 invScale) {
    glm::vec3 v = p - translation;
    glm::vec3 qv(invRotXYZW.x, invRotXYZW.y, invRotXYZW.z);
    float qw = invRotXYZW.w;
    glm::vec3 t = 2.0f * glm::cross(qv, v);
    glm::vec3 rotated = v + qw * t + glm::cross(qv, t);
    return rotated * invScale;
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

inline float SdfCore_Ellipsoid(glm::vec3 p, glm::vec3 radii) {
    glm::vec3 safeRadii = glm::max(radii, 0.0001f);
    float k0 = glm::length(p / safeRadii);
    float k1 = glm::length(p / (safeRadii * safeRadii));
    return k0 * (k0 - 1.0f) / glm::max(k1, 0.0001f);
}

inline float SdfCore_HollowCylinder(glm::vec3 p, float halfLen, float outerR, float wall) {
    glm::vec2 d = glm::vec2(glm::length(glm::vec2(p.x, p.z)) - outerR, glm::abs(p.y) - halfLen);
    float cyl = glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
    return glm::abs(cyl) - wall;
}

inline float SdfCore_TaperedCylinder(glm::vec3 p, float height, float r1, float r2) {
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y);
    glm::vec2 k1 = glm::vec2(r2, height);
    glm::vec2 k2 = glm::vec2(r2 - r1, 2.0f * height);
    glm::vec2 ca = glm::vec2(q.x - glm::min(q.x, ((q.y < 0.0f) ? r1 : r2)), glm::abs(q.y) - height);
    glm::vec2 cb = q - k1 + k2 * glm::clamp(glm::dot(k1 - q, k2) / glm::dot(k2, k2), 0.0f, 1.0f);
    float s = ((cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f);
    return s * glm::sqrt(glm::min(glm::dot(ca, ca), glm::dot(cb, cb)));
}

inline float SdfCore_Cone(glm::vec3 p, glm::vec2 angle, float height) {
    glm::vec2 q = height * glm::vec2(angle.x / angle.y, -1.0f);
    glm::vec2 w = glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y);
    glm::vec2 a = w - q * glm::clamp(glm::dot(w, q) / glm::dot(q, q), 0.0f, 1.0f);
    glm::vec2 b = w - q * glm::vec2(glm::clamp(w.x / q.x, 0.0f, 1.0f), 1.0f);
    float k = glm::sign(q.y);
    float d = glm::min(glm::dot(a, a), glm::dot(b, b));
    float s = glm::max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return glm::sqrt(d) * glm::sign(s);
}

inline float SdfCore_CappedTorus(glm::vec3 p, glm::vec2 sc, float majorRadius, float minorRadius) {
    glm::vec3 localP = p;
    localP.x = glm::abs(localP.x);
    float k = ((sc.y * localP.x > sc.x * localP.z) ? glm::dot(glm::vec2(localP.x, localP.z), sc) : glm::length(glm::vec2(localP.x, localP.z)));
    return glm::sqrt(glm::dot(localP, localP) + majorRadius * majorRadius - 2.0f * majorRadius * k) - minorRadius;
}

inline float SdfCore_Link(glm::vec3 p, float halfLength, float majorRadius, float minorRadius) {
    glm::vec3 q = glm::vec3(p.x, glm::max(glm::abs(p.y) - halfLength, 0.0f), p.z);
    return glm::length(glm::vec2(glm::length(glm::vec2(q.x, q.y)) - majorRadius, q.z)) - minorRadius;
}

inline float SdfCore_TriangularPrism(glm::vec3 p, glm::vec2 h) {
    glm::vec3 q = glm::abs(p);
    return glm::max(q.z - h.y, glm::max(q.x * 0.866025f + p.y * 0.5f, -p.y) - h.x * 0.5f);
}

inline float SdfCore_HexPrism(glm::vec3 p, glm::vec2 h) {
    float k0 = 0.8660254f;
    float kz = 0.57735f;
    glm::vec3 q = glm::abs(p);
    float dotVal = glm::min(glm::dot(glm::vec2(-k0, 0.5f), glm::vec2(q.x, q.z)), 0.0f);
    q.x -= 2.0f * dotVal * (-k0);
    q.z -= 2.0f * dotVal * 0.5f;
    glm::vec2 d = glm::vec2(glm::length(glm::vec2(q.x, q.z) - glm::vec2(glm::clamp(q.x, -kz * h.x, kz * h.x), h.x)) * glm::sign(q.z - h.x), q.y - h.y);
    return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
}

inline float SdfCore_Pyramid(glm::vec3 p, float height) {
    float m2 = height * height + 0.25f;
    glm::vec3 q = glm::vec3(glm::abs(p.x), p.y, glm::abs(p.z));
    q = ((q.z > q.x) ? glm::vec3(q.z, q.y, q.x) : q);
    q.x -= 0.5f;
    q.z -= 0.5f;
    glm::vec3 a = glm::vec3(q.z, height * q.y - 0.5f * q.x, height * q.x + 0.5f * q.y);
    float s = glm::max(-a.x, 0.0f);
    float t = glm::clamp((a.y - 0.5f * q.z) / (m2 + 0.25f), 0.0f, 1.0f);
    float da = m2 * (a.x + s) * (a.x + s) + a.y * a.y;
    float db = m2 * (a.x + 0.5f * t) * (a.x + 0.5f * t) + (a.y - m2 * t) * (a.y - m2 * t);
    float d2 = ((glm::min(a.y, -a.x * m2 - a.y * 0.5f) > 0.0f) ? 0.0f : glm::min(da, db));
    return glm::sqrt((d2 + a.z * a.z) / m2) * glm::sign(glm::max(a.z, -q.y));
}

inline float SdfCore_Segment(glm::vec3 p, glm::vec3 a, glm::vec3 b, float radius) {
    glm::vec3 pa = p - a;
    glm::vec3 ba = b - a;
    float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - radius;
}

inline float SdfCore_FakeRoundCone(glm::vec3 p, float r1, float r2, float height) {
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y);
    float h = glm::clamp(q.y / height, 0.0f, 1.0f);
    float r = glm::mix(r1, r2, h);
    return glm::length(glm::vec2(q.x, q.y - height * h)) - r;
}

inline float SdfCore_RoundCone(glm::vec3 p, float r1, float r2, float height) {
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y);
    float b = (r1 - r2) / height;
    float a = glm::sqrt(1.0f - b * b);
    float k = glm::dot(q, glm::vec2(-b, a));
    float regionA = glm::length(q) - r1;
    float regionB = glm::length(q - glm::vec2(0.0f, height)) - r2;
    float regionC = glm::dot(q, glm::vec2(a, b)) - r1;
    float d = ((k < 0.0f) ? regionA : regionC);
    d = ((k > a * height) ? regionB : d);
    return d;
}

} // namespace Yeroket::Sdf::Generated
