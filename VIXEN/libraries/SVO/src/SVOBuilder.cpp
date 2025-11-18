#include "SVOBuilder.h"
#include <algorithm>
#include <execution>
#include <numeric>
#include <chrono>
#include <queue>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>

namespace SVO {

// ============================================================================
// SVOBuilder Implementation
// ============================================================================
// Note: BuildContext is now defined in SVOBuilder.h

SVOBuilder::SVOBuilder(const BuildParams& params)
    : m_params(params)
    , m_context(std::make_unique<BuildContext>()) {
}

SVOBuilder::~SVOBuilder() = default;

void SVOBuilder::setProgressCallback(std::function<void(float)> callback) {
    m_progressCallback = std::move(callback);
}

std::unique_ptr<Octree> SVOBuilder::build(const InputMesh& mesh) {
    // Convert mesh to triangles
    std::vector<InputTriangle> triangles;
    triangles.reserve(mesh.indices.size() / 3);

    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        InputTriangle tri;
        for (int j = 0; j < 3; ++j) {
            uint32_t idx = mesh.indices[i + j];
            tri.vertices[j] = mesh.vertices[idx];
            tri.normals[j] = idx < mesh.normals.size() ? mesh.normals[idx] : glm::vec3(0, 1, 0);
            tri.colors[j] = idx < mesh.colors.size() ? mesh.colors[idx] : glm::vec3(1, 1, 1);
            tri.uvs[j] = idx < mesh.uvs.size() ? mesh.uvs[idx] : glm::vec2(0, 0);
        }
        triangles.push_back(tri);
    }

    return build(triangles, mesh.minBounds, mesh.maxBounds);
}

std::unique_ptr<Octree> SVOBuilder::build(
    const std::vector<InputTriangle>& triangles,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax) {

    auto startTime = std::chrono::high_resolution_clock::now();

    // Initialize build context
    m_context->triangles = triangles;
    m_context->worldMin = worldMin;
    m_context->worldMax = worldMax;
    m_context->params = m_params;
    m_context->progressCallback = m_progressCallback;
    m_context->octree = std::make_unique<Octree>();

    // Estimate total nodes for progress tracking
    m_context->totalEstimatedNodes = estimateNodeCount();

    // Create root node
    m_context->rootNode = std::make_unique<BuildContext::VoxelNode>();
    m_context->rootNode->position = glm::vec3(0.0f);
    m_context->rootNode->size = 1.0f;
    m_context->rootNode->level = 0;

    // All triangles potentially intersect root
    m_context->rootNode->triangleIndices.resize(triangles.size());
    std::iota(m_context->rootNode->triangleIndices.begin(),
              m_context->rootNode->triangleIndices.end(), 0);

    // Recursively build octree
    subdivideNode(m_context->rootNode.get());

    // Finalize octree structure
    finalizeOctree();

    auto endTime = std::chrono::high_resolution_clock::now();
    float buildTime = std::chrono::duration<float>(endTime - startTime).count();

    // Update statistics
    m_stats.voxelsProcessed = m_context->nodesProcessed;
    m_stats.leavesCreated = m_context->leavesCreated;
    m_stats.buildTimeSeconds = buildTime;
    m_stats.averageBranchingFactor = calculateBranchingFactor(m_context->rootNode.get());

    return std::move(m_context->octree);
}

std::unique_ptr<Octree> SVOBuilder::buildFromVoxelGrid(
    const std::vector<uint8_t>& voxelData,
    uint32_t resolution,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax) {

    auto startTime = std::chrono::high_resolution_clock::now();

    // Validate input
    size_t expectedSize = static_cast<size_t>(resolution) * resolution * resolution;
    if (voxelData.size() != expectedSize) {
        return nullptr;
    }

    // Initialize build context
    m_context->worldMin = worldMin;
    m_context->worldMax = worldMax;
    m_context->params = m_params;
    m_context->progressCallback = m_progressCallback;
    m_context->octree = std::make_unique<Octree>();

    // Create root node
    m_context->rootNode = std::make_unique<BuildContext::VoxelNode>();
    m_context->rootNode->position = glm::vec3(0.0f);
    m_context->rootNode->size = 1.0f;
    m_context->rootNode->level = 0;

    // Recursively build octree from voxel grid
    subdivideNodeFromVoxels(m_context->rootNode.get(), voxelData, resolution,
                            glm::ivec3(0), resolution);

    // Finalize octree structure
    finalizeOctree();

    auto endTime = std::chrono::high_resolution_clock::now();
    float buildTime = std::chrono::duration<float>(endTime - startTime).count();

    // Update statistics
    m_stats.voxelsProcessed = m_context->nodesProcessed;
    m_stats.leavesCreated = m_context->leavesCreated;
    m_stats.buildTimeSeconds = buildTime;
    m_stats.averageBranchingFactor = calculateBranchingFactor(m_context->rootNode.get());

    return std::move(m_context->octree);
}

// ============================================================================
// Recursive Subdivision
// ============================================================================

void SVOBuilder::subdivideNode(BuildContext::VoxelNode* node) {
    m_context->nodesProcessed++;

    // Memory leak guard: abort if exceeded node limit
    if (!m_context->checkMemoryLimits()) {
        node->isLeaf = true;
        m_context->leavesCreated++;
        return;
    }

    // Triangle explosion guard: force leaf if too many triangles
    if (node->triangleIndices.size() > BuildContext::MAX_TRIANGLES_PER_NODE) {
        node->isLeaf = true;
        m_context->leavesCreated++;
        node->attributes = integrateAttributes(node);
        return;
    }

    // Update progress
    if (m_context->progressCallback && m_context->nodesProcessed % 1000 == 0) {
        float progress = static_cast<float>(m_context->nodesProcessed) /
                        static_cast<float>(m_context->totalEstimatedNodes);
        m_context->progressCallback(std::min(progress, 0.99f));
    }

    // Check termination criteria
    if (shouldTerminate(node)) {
        node->isLeaf = true;
        m_context->leavesCreated++;

        // Compute leaf attributes
        node->attributes = integrateAttributes(node);

        // Construct contour if enabled
        if (m_params.enableContours) {
            node->contour = constructContour(node);
        }

        return;
    }

    // Create 8 child nodes
    node->children.resize(8);
    float childSize = node->size * 0.5f;

    // Offsets for octree child slots (binary: xyz)
    static const glm::vec3 childOffsets[8] = {
        {0.0f, 0.0f, 0.0f}, // 000
        {0.5f, 0.0f, 0.0f}, // 001
        {0.0f, 0.5f, 0.0f}, // 010
        {0.5f, 0.5f, 0.0f}, // 011
        {0.0f, 0.0f, 0.5f}, // 100
        {0.5f, 0.0f, 0.5f}, // 101
        {0.0f, 0.5f, 0.5f}, // 110
        {0.5f, 0.5f, 0.5f}, // 111
    };

    // Filter triangles to children
    for (int childIdx = 0; childIdx < 8; ++childIdx) {
        auto child = std::make_unique<BuildContext::VoxelNode>();
        child->position = node->position + childOffsets[childIdx] * node->size;
        child->size = childSize;
        child->level = node->level + 1;

        // Copy ancestor contours
        child->ancestorContours = node->ancestorContours;
        if (node->contour.has_value()) {
            child->ancestorContours.push_back(node->contour.value());
        }

        // Filter triangles that intersect this child
        filterTrianglesToChild(node, child.get(), childIdx);

        // Only create child if it has triangles
        if (!child->triangleIndices.empty()) {
            node->children[childIdx] = std::move(child);
        }
    }

    // Recursively subdivide non-empty children
    // Use parallel_for only for shallow nodes (depth 0-4) to prevent exponential thread explosion
    // Deep nodes (depth 5+) use serial execution to limit memory usage
    const int PARALLEL_DEPTH_LIMIT = 4;

    if (node->level < PARALLEL_DEPTH_LIMIT) {
        tbb::parallel_for(size_t(0), size_t(8), [&](size_t i) {
            if (node->children[i]) {
                subdivideNode(node->children[i].get());
            }
        });
    } else {
        // Serial execution for deep nodes
        for (size_t i = 0; i < 8; ++i) {
            if (node->children[i]) {
                subdivideNode(node->children[i].get());
            }
        }
    }
}

bool SVOBuilder::shouldTerminate(const BuildContext::VoxelNode* node) const {
    // Max depth reached
    if (node->level >= m_params.maxLevels) {
        return true;
    }

    // No triangles in voxel
    if (node->triangleIndices.empty()) {
        return true;
    }

    // Geometric error check
    if (m_params.enableContours) {
        float geometricError = estimateGeometricError(node);
        if (geometricError < m_params.geometryErrorThreshold) {
            return true;
        }
    }

    // Attribute variation check
    float attributeError = estimateAttributeError(node);
    if (attributeError < m_params.colorErrorThreshold) {
        return true;
    }

    return false;
}

// ============================================================================
// Triangle-Voxel Intersection
// ============================================================================

void SVOBuilder::filterTrianglesToChild(
    const BuildContext::VoxelNode* parent,
    BuildContext::VoxelNode* child,
    int childIdx) {

    // Convert normalized voxel coords to world space
    glm::vec3 worldPos = m_context->worldMin +
                        child->position * (m_context->worldMax - m_context->worldMin);
    glm::vec3 worldSize = child->size * (m_context->worldMax - m_context->worldMin);

    // AABB for child voxel
    glm::vec3 voxelMin = worldPos;
    glm::vec3 voxelMax = worldPos + worldSize;

    // Test each parent triangle against child AABB
    for (int triIdx : parent->triangleIndices) {
        m_context->triangleTests++;

        const InputTriangle& tri = m_context->triangles[triIdx];

        if (triangleIntersectsAABB(tri, voxelMin, voxelMax)) {
            child->triangleIndices.push_back(triIdx);
        }
    }
}

bool SVOBuilder::triangleIntersectsAABB(
    const InputTriangle& tri,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax) const {

    // Quick AABB-AABB test first
    glm::vec3 triMin = glm::min(glm::min(tri.vertices[0], tri.vertices[1]), tri.vertices[2]);
    glm::vec3 triMax = glm::max(glm::max(tri.vertices[0], tri.vertices[1]), tri.vertices[2]);

    if (triMax.x < aabbMin.x || triMin.x > aabbMax.x) return false;
    if (triMax.y < aabbMin.y || triMin.y > aabbMax.y) return false;
    if (triMax.z < aabbMin.z || triMin.z > aabbMax.z) return false;

    // Separating Axis Theorem (SAT) test
    // Implementation based on Akenine-Moller's optimized algorithm

    glm::vec3 boxCenter = (aabbMin + aabbMax) * 0.5f;
    glm::vec3 boxHalfSize = (aabbMax - aabbMin) * 0.5f;

    // Translate triangle as if box was at origin
    glm::vec3 v0 = tri.vertices[0] - boxCenter;
    glm::vec3 v1 = tri.vertices[1] - boxCenter;
    glm::vec3 v2 = tri.vertices[2] - boxCenter;

    // Compute triangle edges
    glm::vec3 e0 = v1 - v0;
    glm::vec3 e1 = v2 - v1;
    glm::vec3 e2 = v0 - v2;

    // Test axes a00..a22 (cross products of edges with box axes)
    auto testAxis = [&](const glm::vec3& axis) -> bool {
        float p0 = glm::dot(v0, axis);
        float p1 = glm::dot(v1, axis);
        float p2 = glm::dot(v2, axis);
        float r = boxHalfSize.x * std::abs(axis.x) +
                 boxHalfSize.y * std::abs(axis.y) +
                 boxHalfSize.z * std::abs(axis.z);
        float minP = std::min({p0, p1, p2});
        float maxP = std::max({p0, p1, p2});
        return !(maxP < -r || minP > r);
    };

    // Test 9 axes (edge cross products)
    if (!testAxis(glm::cross(glm::vec3(1, 0, 0), e0))) return false;
    if (!testAxis(glm::cross(glm::vec3(1, 0, 0), e1))) return false;
    if (!testAxis(glm::cross(glm::vec3(1, 0, 0), e2))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 1, 0), e0))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 1, 0), e1))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 1, 0), e2))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 0, 1), e0))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 0, 1), e1))) return false;
    if (!testAxis(glm::cross(glm::vec3(0, 0, 1), e2))) return false;

    // Test triangle normal
    glm::vec3 normal = glm::cross(e0, e1);
    if (!testAxis(normal)) return false;

    return true;
}

// ============================================================================
// Error Estimation
// ============================================================================

float SVOBuilder::estimateGeometricError(const BuildContext::VoxelNode* node) const {
    if (node->triangleIndices.empty()) {
        return 0.0f;
    }

    // Sample surface points within voxel
    std::vector<glm::vec3> surfacePoints;
    sampleSurfacePoints(node, surfacePoints, 16);

    if (surfacePoints.empty()) {
        return 0.0f;
    }

    // Compute voxel bounds in world space
    glm::vec3 worldPos = m_context->worldMin +
                        node->position * (m_context->worldMax - m_context->worldMin);
    glm::vec3 worldSize = node->size * (m_context->worldMax - m_context->worldMin);

    // Maximum distance from any surface point to voxel boundary
    float maxError = 0.0f;
    for (const glm::vec3& point : surfacePoints) {
        // Distance to nearest voxel face
        glm::vec3 toMin = point - worldPos;
        glm::vec3 toMax = (worldPos + worldSize) - point;

        float minDist = std::min({toMin.x, toMin.y, toMin.z,
                                  toMax.x, toMax.y, toMax.z});
        maxError = std::max(maxError, minDist);
    }

    // Normalize by voxel size
    float voxelDiagonal = glm::length(worldSize);
    return maxError / voxelDiagonal;
}

float SVOBuilder::estimateAttributeError(const BuildContext::VoxelNode* node) const {
    if (node->triangleIndices.size() < 2) {
        return 0.0f;
    }

    // Sample colors from triangles
    std::vector<glm::vec3> colors;
    colors.reserve(node->triangleIndices.size() * 3);

    for (int triIdx : node->triangleIndices) {
        const InputTriangle& tri = m_context->triangles[triIdx];
        colors.push_back(tri.colors[0]);
        colors.push_back(tri.colors[1]);
        colors.push_back(tri.colors[2]);
    }

    // Compute color variance
    glm::vec3 meanColor(0.0f);
    for (const glm::vec3& c : colors) {
        meanColor += c;
    }
    meanColor /= static_cast<float>(colors.size());

    float variance = 0.0f;
    for (const glm::vec3& c : colors) {
        variance += glm::length2(c - meanColor);
    }
    variance /= static_cast<float>(colors.size());

    return std::sqrt(variance) * 255.0f; // Convert to 0-255 scale
}

// ============================================================================
// Helper Functions
// ============================================================================

size_t SVOBuilder::estimateNodeCount() const {
    // Rough estimate based on triangle count and max depth
    size_t triangles = m_context->triangles.size();
    int depth = m_params.maxLevels;

    // Assume average branching factor of 4 (surface data)
    size_t estimate = 0;
    for (int i = 0; i < depth; ++i) {
        estimate += static_cast<size_t>(std::pow(4, i));
    }

    return std::min(estimate, triangles * 100); // Cap estimate
}

float SVOBuilder::calculateBranchingFactor(const BuildContext::VoxelNode* node) const {
    if (!node || node->isLeaf) {
        return 0.0f;
    }

    int childCount = 0;
    float childBranchingSum = 0.0f;

    for (const auto& child : node->children) {
        if (child) {
            childCount++;
            childBranchingSum += calculateBranchingFactor(child.get());
        }
    }

    if (childCount == 0) {
        return 0.0f;
    }

    return (static_cast<float>(childCount) + childBranchingSum) /
           static_cast<float>(childCount + 1);
}

void SVOBuilder::sampleSurfacePoints(
    const BuildContext::VoxelNode* node,
    std::vector<glm::vec3>& outPoints,
    int samplesPerTriangle) const {

    outPoints.clear();
    outPoints.reserve(node->triangleIndices.size() * samplesPerTriangle);

    // Convert normalized voxel coords to world space
    glm::vec3 worldPos = m_context->worldMin +
                        node->position * (m_context->worldMax - m_context->worldMin);
    glm::vec3 worldSize = node->size * (m_context->worldMax - m_context->worldMin);
    glm::vec3 voxelMin = worldPos;
    glm::vec3 voxelMax = worldPos + worldSize;

    for (int triIdx : node->triangleIndices) {
        const InputTriangle& tri = m_context->triangles[triIdx];

        // Sample points on triangle surface using barycentric coordinates
        for (int i = 0; i < samplesPerTriangle; ++i) {
            // Generate random barycentric coordinates
            float u = float(rand()) / float(RAND_MAX);
            float v = float(rand()) / float(RAND_MAX);

            // Ensure u + v <= 1 (point inside triangle)
            if (u + v > 1.0f) {
                u = 1.0f - u;
                v = 1.0f - v;
            }

            float w = 1.0f - u - v;

            // Interpolate position
            glm::vec3 point = tri.vertices[0] * w
                            + tri.vertices[1] * u
                            + tri.vertices[2] * v;

            // Check if point is inside voxel AABB
            if (point.x >= voxelMin.x && point.x <= voxelMax.x &&
                point.y >= voxelMin.y && point.y <= voxelMax.y &&
                point.z >= voxelMin.z && point.z <= voxelMax.z) {
                outPoints.push_back(point);
            }
        }
    }
}

UncompressedAttributes SVOBuilder::integrateAttributes(
    const BuildContext::VoxelNode* node) const {

    // Convert normalized voxel coords to world space
    glm::vec3 worldPos = m_context->worldMin +
                        node->position * (m_context->worldMax - m_context->worldMin);
    float worldSize = node->size * glm::length(m_context->worldMax - m_context->worldMin) / glm::sqrt(3.0f);

    std::vector<InputTriangle> voxelTriangles;
    voxelTriangles.reserve(node->triangleIndices.size());
    for (int idx : node->triangleIndices) {
        voxelTriangles.push_back(m_context->triangles[idx]);
    }

    return AttributeIntegrator::integrate(worldPos, worldSize, voxelTriangles);
}

std::optional<Contour> SVOBuilder::constructContour(
    const BuildContext::VoxelNode* node) const {

    // Sample surface points
    std::vector<glm::vec3> surfacePoints;
    sampleSurfacePoints(node, surfacePoints, 16);

    if (surfacePoints.empty()) {
        return std::nullopt;
    }

    // Extract surface normals from triangles
    std::vector<glm::vec3> surfaceNormals;
    surfaceNormals.reserve(node->triangleIndices.size());

    for (int triIdx : node->triangleIndices) {
        const InputTriangle& tri = m_context->triangles[triIdx];
        glm::vec3 edge1 = tri.vertices[1] - tri.vertices[0];
        glm::vec3 edge2 = tri.vertices[2] - tri.vertices[0];
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
        surfaceNormals.push_back(normal);
    }

    float worldSize = node->size * glm::length(m_context->worldMax - m_context->worldMin) / glm::sqrt(3.0f);

    return ContourBuilder::construct(
        node->position,
        worldSize,
        surfacePoints,
        surfaceNormals,
        node->ancestorContours,
        m_params.geometryErrorThreshold);
}

void SVOBuilder::finalizeOctree() {
    // Convert tree structure to octree blocks
    if (!m_context->rootNode) {
        return;
    }

    // For now, create a single block containing entire tree
    auto rootBlock = std::make_unique<OctreeBlock>();

    // Traverse tree and populate block
    std::function<void(BuildContext::VoxelNode*)> traverse =
        [&](BuildContext::VoxelNode* node) {
            if (!node) return;

            // Add attributes
            rootBlock->attributes.push_back(node->attributes);

            // Add contour if present
            if (node->contour.has_value()) {
                rootBlock->contours.push_back(node->contour.value());
            }

            // Build child descriptor if non-leaf
            if (!node->isLeaf && !node->children.empty()) {
                ChildDescriptor desc{};
                desc.validMask = 0;
                desc.leafMask = 0;
                desc.contourMask = 0;

                for (size_t i = 0; i < node->children.size(); ++i) {
                    if (node->children[i]) {
                        desc.validMask |= (1 << i);

                        if (node->children[i]->isLeaf) {
                            desc.leafMask |= (1 << i);
                        }

                        if (node->children[i]->contour.has_value()) {
                            desc.contourMask |= (1 << i);
                        }
                    }
                }

                rootBlock->childDescriptors.push_back(desc);
            }

            // Recurse to children
            for (auto& child : node->children) {
                if (child) {
                    traverse(child.get());
                }
            }
        };

    traverse(m_context->rootNode.get());

    // Store in octree
    m_context->octree->root = std::move(rootBlock);
    m_context->octree->maxLevels = m_params.maxLevels;
    m_context->octree->worldMin = m_context->worldMin;
    m_context->octree->worldMax = m_context->worldMax;

    // Update statistics
    m_context->octree->totalVoxels = m_context->nodesProcessed;
    m_context->octree->leafVoxels = m_context->leavesCreated;

    // Final progress update
    if (m_context->progressCallback) {
        m_context->progressCallback(1.0f);
    }
}

// ============================================================================
// Voxel Grid Subdivision
// ============================================================================

void SVOBuilder::subdivideNodeFromVoxels(
    BuildContext::VoxelNode* node,
    const std::vector<uint8_t>& voxelData,
    uint32_t gridResolution,
    const glm::ivec3& gridOffset,
    uint32_t gridSize) {

    m_context->nodesProcessed++;

    // Memory leak guard
    if (!m_context->checkMemoryLimits()) {
        node->isLeaf = true;
        m_context->leavesCreated++;
        return;
    }

    // Check if region is empty
    bool hasVoxels = false;
    for (uint32_t z = 0; z < gridSize && !hasVoxels; ++z) {
        for (uint32_t y = 0; y < gridSize && !hasVoxels; ++y) {
            for (uint32_t x = 0; x < gridSize && !hasVoxels; ++x) {
                glm::ivec3 pos = gridOffset + glm::ivec3(x, y, z);
                size_t idx = pos.z * gridResolution * gridResolution + pos.y * gridResolution + pos.x;
                if (voxelData[idx] != 0) {
                    hasVoxels = true;
                }
            }
        }
    }

    // If empty, mark as leaf with no data
    if (!hasVoxels) {
        node->isLeaf = true;
        m_context->leavesCreated++;
        return;
    }

    // Calculate current voxel size in world space
    // worldVoxelSize = (worldBoundsSize / gridResolution) * currentGridSize
    // Example: 4x4x4 world, 128 res grid, gridSize=8 → (4/128)*8 = 0.25 world units
    glm::vec3 worldSize = m_context->worldMax - m_context->worldMin;
    float worldVoxelSize = (worldSize.x / static_cast<float>(gridResolution)) * static_cast<float>(gridSize);

    // Check termination criteria
    // 1. Brick-level termination: depth reached (maxLevels - brickDepthLevels)
    //    Example: maxLevels=16, brickDepthLevels=3 → stop octree at depth 13
    //    Leaves contain 2³=8 voxels per side (8×8×8 dense brick)
    //    Structure: depth 0-13 octree → bottom 3 levels (depth 14-16) stored as dense 8³ bricks
    //    Total effective depth: still 16, but last 3 levels are dense instead of sparse
    // 2. Grid size reached 1 (single voxel, fallback if bricks disabled)
    // 3. World-space voxel size below threshold (prevents over-subdivision)
    //    Example: 4³ world, depth 22 → voxel = 0.000001 units (too small, stop at 0.01)
    // 4. Maximum total depth reached

    int octreeMaxDepth = m_params.maxLevels - m_params.brickDepthLevels; // Octree stops here
    int brickSize = (m_params.brickDepthLevels > 0) ? (1 << m_params.brickDepthLevels) : 0; // 2^N

    bool reachedBrickLevel = (m_params.brickDepthLevels > 0 && node->level >= octreeMaxDepth);
    bool reachedMinSize = (gridSize <= 1);
    bool reachedMinVoxelSize = (worldVoxelSize <= m_params.minVoxelSize);
    bool reachedMaxDepth = (node->level >= m_params.maxLevels);

    if (reachedBrickLevel || reachedMinSize || reachedMinVoxelSize || reachedMaxDepth) {
        node->isLeaf = true;
        m_context->leavesCreated++;

        // Compute average color from voxels in this region
        glm::vec3 avgColor(0.0f);
        int voxelCount = 0;
        for (uint32_t z = 0; z < gridSize; ++z) {
            for (uint32_t y = 0; y < gridSize; ++y) {
                for (uint32_t x = 0; x < gridSize; ++x) {
                    glm::ivec3 pos = gridOffset + glm::ivec3(x, y, z);
                    size_t idx = pos.z * gridResolution * gridResolution + pos.y * gridResolution + pos.x;
                    uint8_t val = voxelData[idx];
                    if (val != 0) {
                        // Use voxel value as grayscale color
                        float normalized = static_cast<float>(val) / 255.0f;
                        avgColor += glm::vec3(normalized);
                        voxelCount++;
                    }
                }
            }
        }

        if (voxelCount > 0) {
            avgColor /= static_cast<float>(voxelCount);
        }

        // Convert color to RGBA bytes
        node->attributes.red = static_cast<uint8_t>(avgColor.r * 255.0f);
        node->attributes.green = static_cast<uint8_t>(avgColor.g * 255.0f);
        node->attributes.blue = static_cast<uint8_t>(avgColor.b * 255.0f);
        node->attributes.alpha = 255;

        // Default normal (+Y axis, top face of cube)
        node->attributes.sign_and_axis = 2; // +Y face
        node->attributes.u_coordinate = 1 << 14; // Center
        node->attributes.v_coordinate = 1 << 13; // Center
        return;
    }

    // Subdivide into 8 children
    node->children.resize(8);
    uint32_t childSize = gridSize / 2;

    // Offsets for octree child slots
    static const glm::ivec3 childOffsets[8] = {
        {0, 0, 0}, // 000
        {1, 0, 0}, // 001
        {0, 1, 0}, // 010
        {1, 1, 0}, // 011
        {0, 0, 1}, // 100
        {1, 0, 1}, // 101
        {0, 1, 1}, // 110
        {1, 1, 1}  // 111
    };

    for (int i = 0; i < 8; ++i) {
        glm::ivec3 childGridOffset = gridOffset + childOffsets[i] * static_cast<int>(childSize);

        node->children[i] = std::make_unique<BuildContext::VoxelNode>();
        node->children[i]->position = node->position + glm::vec3(childOffsets[i]) * node->size * 0.5f;
        node->children[i]->size = node->size * 0.5f;
        node->children[i]->level = node->level + 1;

        subdivideNodeFromVoxels(node->children[i].get(), voxelData, gridResolution,
                                childGridOffset, childSize);
    }
}

} // namespace SVO
