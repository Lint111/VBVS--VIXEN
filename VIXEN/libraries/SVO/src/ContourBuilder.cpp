#include "pch.h"
#include "SVOBuilder.h"
#include <algorithm>
#include <limits>

namespace SVO {

/**
 * Constructs optimal contour for a voxel using greedy algorithm.
 * Based on Laine & Karras 2010 Section 7.2.
 *
 * Algorithm:
 * 1. Generate candidate normal directions (surface normals + boundary perpendiculars)
 * 2. For each direction, compute tight parallel planes
 * 3. Evaluate overestimation (volume outside surface but inside planes)
 * 4. Select direction with minimum overestimation
 */
std::optional<Contour> ContourBuilder::construct(
    const glm::vec3& voxelPos,
    float voxelSize,
    const std::vector<glm::vec3>& surfacePoints,
    const std::vector<glm::vec3>& surfaceNormals,
    const std::vector<Contour>& ancestorContours,
    float errorThreshold) {

    if (surfacePoints.empty()) {
        return std::nullopt;
    }

    // Generate candidate directions
    std::vector<glm::vec3> candidates;

    // 1. Add surface normals
    for (const auto& normal : surfaceNormals) {
        candidates.push_back(normal);
    }

    // 2. Add axis-aligned directions (perpendicular to voxel faces)
    candidates.push_back(glm::vec3(1, 0, 0));
    candidates.push_back(glm::vec3(0, 1, 0));
    candidates.push_back(glm::vec3(0, 0, 1));

    // Remove duplicates and ensure normalized
    auto comparator = [](const glm::vec3& a, const glm::vec3& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    };
    std::sort(candidates.begin(), candidates.end(), comparator);
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
        [](const glm::vec3& a, const glm::vec3& b) {
            return glm::length(a - b) < 0.001f;
        }), candidates.end());

    // Evaluate each candidate direction
    float bestOverestimation = std::numeric_limits<float>::max();
    glm::vec3 bestNormal(0, 1, 0);
    float bestThickness = 1.0f;
    float bestPosition = 0.0f;

    for (const auto& normal : candidates) {
        float overestimation = evaluateOverestimation(
            normal, voxelPos, voxelSize, surfacePoints, ancestorContours);

        // Project surface points onto direction
        std::vector<float> projections;
        projections.reserve(surfacePoints.size());

        for (const auto& point : surfacePoints) {
            float proj = glm::dot(point - voxelPos, normal);
            projections.push_back(proj);
        }

        if (projections.empty()) continue;

        // Find min/max projections
        float minProj = *std::min_element(projections.begin(), projections.end());
        float maxProj = *std::max_element(projections.begin(), projections.end());

        // Contour thickness and position (normalized to voxel space)
        float thickness = (maxProj - minProj) / voxelSize;
        float position = ((minProj + maxProj) * 0.5f) / voxelSize;

        // Update best candidate
        if (overestimation < bestOverestimation) {
            bestOverestimation = overestimation;
            bestNormal = normal;
            bestThickness = thickness;
            bestPosition = position;
        }
    }

    // Check if contour provides sufficient improvement
    if (bestOverestimation > errorThreshold) {
        return std::nullopt;  // Cube is sufficient
    }

    // Clamp thickness and position to valid ranges
    bestThickness = glm::clamp(bestThickness, 0.0f, 1.0f);
    bestPosition = glm::clamp(bestPosition, -0.5f, 0.5f);

    // Encode contour
    return makeContour(bestNormal, bestThickness, bestPosition);
}

/**
 * Evaluates overestimation for a given direction.
 * Returns volume fraction outside surface but inside contour planes.
 */
float ContourBuilder::evaluateOverestimation(
    const glm::vec3& direction,
    const glm::vec3& voxelPos,
    float voxelSize,
    const std::vector<glm::vec3>& surfacePoints,
    const std::vector<Contour>& ancestorContours) {

    if (surfacePoints.empty()) {
        return 1.0f;  // Maximum overestimation
    }

    // Project surface points onto direction
    std::vector<float> projections;
    projections.reserve(surfacePoints.size());

    for (const auto& point : surfacePoints) {
        float proj = glm::dot(point - voxelPos, direction);
        projections.push_back(proj);
    }

    // Find min/max projections
    float minProj = *std::min_element(projections.begin(), projections.end());
    float maxProj = *std::max_element(projections.begin(), projections.end());

    // Compute thickness and position
    float thickness = (maxProj - minProj) / voxelSize;
    float position = ((minProj + maxProj) * 0.5f) / voxelSize;
    float halfThickness = thickness * 0.5f;

    // Estimate overestimation as thickness (tighter = better)
    float overestimation = thickness;

    // Penalize if contour doesn't fit well with ancestors
    for (const auto& ancestor : ancestorContours) {
        glm::vec3 ancestorNormal = decodeContourNormal(ancestor);

        // Check if directions are similar
        float alignment = glm::dot(direction, ancestorNormal);

        if (std::abs(alignment) > 0.9f) {
            // Similar directions - penalize if thickness is larger
            float ancestorThickness = decodeContourThickness(ancestor);

            if (thickness > ancestorThickness) {
                overestimation += (thickness - ancestorThickness) * 0.5f;
            }
        }
    }

    // Penalize off-center positions
    overestimation += std::abs(position) * 0.1f;

    return overestimation;
}

} // namespace SVO
