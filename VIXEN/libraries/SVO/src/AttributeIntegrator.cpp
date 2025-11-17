#include "SVOBuilder.h"
#include <algorithm>
#include <numeric>

namespace SVO {

/**
 * Integrates color and normal attributes from triangles within a voxel.
 * Implements weighted box filtering based on triangle area coverage.
 */
UncompressedAttributes AttributeIntegrator::integrate(
    const glm::vec3& voxelPos,
    float voxelSize,
    const std::vector<InputTriangle>& triangles) {

    UncompressedAttributes result{};

    if (triangles.empty()) {
        // Default attributes for empty voxels
        result.color = 0x80808080;  // Gray (ABGR format)
        result.normal = encodeNormal(glm::vec3(0, 1, 0));
        return result;
    }

    // Integrate color
    glm::vec3 color = integrateColor(voxelPos, voxelSize, triangles);

    // Integrate normal
    glm::vec3 normal = integrateNormal(voxelPos, voxelSize, triangles);

    // Encode to packed format
    result.color = encodeColor(color);
    result.normal = encodeNormal(normal);

    return result;
}

/**
 * Integrates color from triangles using weighted average.
 */
glm::vec3 AttributeIntegrator::integrateColor(
    const glm::vec3& voxelPos,
    float voxelSize,
    const std::vector<InputTriangle>& triangles) {

    glm::vec3 colorSum(0.0f);
    float weightSum = 0.0f;

    for (const auto& tri : triangles) {
        // Triangle vertices
        const glm::vec3& v0 = tri.vertices[0];
        const glm::vec3& v1 = tri.vertices[1];
        const glm::vec3& v2 = tri.vertices[2];

        // Triangle center and area
        glm::vec3 triCenter = (v0 + v1 + v2) / 3.0f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        float triArea = glm::length(glm::cross(edge1, edge2)) * 0.5f;

        // Weight based on distance to voxel center and triangle area
        float dist = glm::length(triCenter - voxelPos);
        float weight = triArea / std::max(dist * dist, voxelSize * voxelSize * 0.01f);

        // Average vertex colors
        glm::vec3 triColor = (tri.colors[0] + tri.colors[1] + tri.colors[2]) / 3.0f;
        colorSum += triColor * weight;

        weightSum += weight;
    }

    if (weightSum > 0.0f) {
        return glm::clamp(colorSum / weightSum, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    return glm::vec3(0.5f);  // Fallback
}

/**
 * Integrates normal from triangles using weighted average.
 */
glm::vec3 AttributeIntegrator::integrateNormal(
    const glm::vec3& voxelPos,
    float voxelSize,
    const std::vector<InputTriangle>& triangles) {

    glm::vec3 normalSum(0.0f);
    float weightSum = 0.0f;

    for (const auto& tri : triangles) {
        // Triangle vertices
        const glm::vec3& v0 = tri.vertices[0];
        const glm::vec3& v1 = tri.vertices[1];
        const glm::vec3& v2 = tri.vertices[2];

        // Triangle center and area
        glm::vec3 triCenter = (v0 + v1 + v2) / 3.0f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        float triArea = glm::length(glm::cross(edge1, edge2)) * 0.5f;

        // Weight based on distance to voxel center and triangle area
        float dist = glm::length(triCenter - voxelPos);
        float weight = triArea / std::max(dist * dist, voxelSize * voxelSize * 0.01f);

        // Use face normal
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
        normalSum += faceNormal * weight;

        weightSum += weight;
    }

    if (weightSum > 0.0f && glm::length(normalSum) > 0.001f) {
        return glm::normalize(normalSum);
    }

    return glm::vec3(0, 1, 0);  // Fallback
}

/**
 * Encode RGB color to ABGR8 format.
 */
uint32_t AttributeIntegrator::encodeColor(const glm::vec3& color) {
    uint8_t r = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = 255;

    return (a << 24) | (b << 16) | (g << 8) | r;
}

/**
 * Encode normal using point-on-cube encoding (32 bits).
 */
uint32_t AttributeIntegrator::encodeNormal(const glm::vec3& normal) {
    // Find dominant axis
    glm::vec3 absNormal = glm::abs(normal);
    int dominantAxis = 0;

    if (absNormal.y > absNormal.x && absNormal.y > absNormal.z) {
        dominantAxis = 1;  // Y axis
    } else if (absNormal.z > absNormal.x && absNormal.z > absNormal.y) {
        dominantAxis = 2;  // Z axis
    }

    // Project onto cube face
    glm::vec2 uv;
    float sign = 1.0f;

    switch (dominantAxis) {
        case 0:  // X dominant
            sign = (normal.x >= 0.0f) ? 1.0f : -1.0f;
            uv = glm::vec2(normal.y / absNormal.x, normal.z / absNormal.x);
            break;
        case 1:  // Y dominant
            sign = (normal.y >= 0.0f) ? 1.0f : -1.0f;
            uv = glm::vec2(normal.x / absNormal.y, normal.z / absNormal.y);
            break;
        case 2:  // Z dominant
            sign = (normal.z >= 0.0f) ? 1.0f : -1.0f;
            uv = glm::vec2(normal.x / absNormal.z, normal.y / absNormal.z);
            break;
    }

    // Map to [0,1] range
    uv = (uv + 1.0f) * 0.5f;
    uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));

    // Pack: 2 bits axis, 1 bit sign, 14+15 bits UV
    uint32_t encoded = 0;
    encoded |= (dominantAxis & 0x3) << 30;        // 2 bits axis
    encoded |= ((sign >= 0.0f ? 1 : 0) & 0x1) << 29;  // 1 bit sign
    encoded |= (static_cast<uint32_t>(uv.x * 16383.0f) & 0x3FFF) << 15;  // 14 bits U
    encoded |= (static_cast<uint32_t>(uv.y * 32767.0f) & 0x7FFF);         // 15 bits V

    return encoded;
}

} // namespace SVO
