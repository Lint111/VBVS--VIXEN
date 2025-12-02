#include "pch.h"
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

    if (triangles.empty()) {
        // Default attributes for empty voxels (gray, up normal)
        return makeAttributes(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0, 1, 0));
    }

    // Integrate color
    glm::vec3 color = integrateColor(voxelPos, voxelSize, triangles);

    // Integrate normal
    glm::vec3 normal = integrateNormal(voxelPos, voxelSize, triangles);

    // Use helper function to encode
    return makeAttributes(color, normal);
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

} // namespace SVO
