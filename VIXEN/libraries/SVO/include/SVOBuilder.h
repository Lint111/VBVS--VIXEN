#pragma once

#include "SVOTypes.h"
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <string>

namespace SVO {

/**
 * Input triangle for voxelization.
 */
struct InputTriangle {
    glm::vec3 vertices[3];
    glm::vec3 normals[3];
    glm::vec3 colors[3];
    glm::vec2 uvs[3];
};

/**
 * Input mesh data for voxelization.
 */
struct InputMesh {
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> colors;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;

    // Bounding box
    glm::vec3 minBounds;
    glm::vec3 maxBounds;
};

/**
 * Octree block - contiguous region of voxel data.
 * Corresponds to localized portion of octree hierarchy.
 */
struct OctreeBlock {
    std::vector<ChildDescriptor> childDescriptors;
    std::vector<Contour> contours;
    std::vector<UncompressedAttributes> attributes;
    std::vector<AttributeLookup> attributeLookups;

    BlockInfo info;

    // Memory layout helpers
    size_t getTotalSize() const;
    void serialize(std::vector<uint8_t>& buffer) const;
};

/**
 * Complete octree structure.
 */
struct Octree {
    // Root block
    std::unique_ptr<OctreeBlock> root;

    // All blocks (including root)
    std::vector<std::unique_ptr<OctreeBlock>> blocks;

    // Metadata
    int maxLevels;
    glm::vec3 worldMin;
    glm::vec3 worldMax;

    // Statistics
    size_t totalVoxels = 0;
    size_t leafVoxels = 0;
    size_t memoryUsage = 0;

    // Serialize to file
    bool saveToFile(const std::string& filename) const;
    bool loadFromFile(const std::string& filename);
};

/**
 * Sparse Voxel Octree Builder.
 *
 * Implements the construction algorithm from Laine & Karras 2010:
 * 1. Top-down recursive subdivision
 * 2. Triangle filtering to child voxels
 * 3. Contour construction via greedy algorithm
 * 4. Attribute integration (colors, normals)
 * 5. Error-based termination
 */
class SVOBuilder {
public:
    explicit SVOBuilder(const BuildParams& params = BuildParams{});
    ~SVOBuilder();

    /**
     * Build octree from input mesh.
     * Returns nullptr on failure.
     */
    std::unique_ptr<Octree> build(const InputMesh& mesh);

    /**
     * Build octree from triangle soup.
     */
    std::unique_ptr<Octree> build(const std::vector<InputTriangle>& triangles,
                                   const glm::vec3& worldMin,
                                   const glm::vec3& worldMax);

    /**
     * Set progress callback.
     * Called periodically during build with progress in [0,1].
     */
    void setProgressCallback(std::function<void(float)> callback);

    /**
     * Get build statistics from last build.
     */
    struct BuildStats {
        size_t voxelsProcessed = 0;
        size_t leavesCreated = 0;
        size_t contoursGenerated = 0;
        float buildTimeSeconds = 0.0f;
        float averageBranchingFactor = 0.0f;
    };

    const BuildStats& getLastBuildStats() const { return m_stats; }

private:
    BuildParams m_params;
    BuildStats m_stats;
    std::function<void(float)> m_progressCallback;

    // Build implementation - internal state during octree construction
    struct BuildContext {
        // Input data
        std::vector<InputTriangle> triangles;
        glm::vec3 worldMin;
        glm::vec3 worldMax;
        BuildParams params;

        // Output octree
        std::unique_ptr<Octree> octree;

        // Current build state
        struct VoxelNode {
            glm::vec3 position;           // Normalized position [0,1]
            float size;                   // Size in normalized coords
            int level;                    // Depth in octree (0 = root)
            std::vector<int> triangleIndices; // Triangles intersecting this voxel
            std::vector<Contour> ancestorContours; // Contours from parents

            // Child nodes (8 if subdivided, empty if leaf)
            std::vector<std::unique_ptr<VoxelNode>> children;

            // Computed data
            UncompressedAttributes attributes;
            std::optional<Contour> contour;
            bool isLeaf = false;
        };

        std::unique_ptr<VoxelNode> rootNode;

        // Statistics
        size_t nodesProcessed = 0;
        size_t leavesCreated = 0;
        size_t triangleTests = 0;

        // Progress tracking
        std::function<void(float)> progressCallback;
        std::atomic<size_t> processedNodes{0};
        size_t totalEstimatedNodes = 0;

        // Memory leak guards
        static constexpr size_t MAX_NODES = 10'000'000;  // 10M node limit (~2GB max)
        static constexpr size_t MAX_TRIANGLES_PER_NODE = 100'000;  // Prevent triangle explosion

        bool checkMemoryLimits() const {
            return nodesProcessed < MAX_NODES;
        }
    };

    std::unique_ptr<BuildContext> m_context;

    // Recursive subdivision
    void subdivideNode(BuildContext::VoxelNode* node);
    bool shouldTerminate(const BuildContext::VoxelNode* node) const;

    // Triangle filtering
    void filterTrianglesToChild(const BuildContext::VoxelNode* parent,
                                 BuildContext::VoxelNode* child,
                                 int childIdx);
    bool triangleIntersectsAABB(const InputTriangle& tri,
                                const glm::vec3& aabbMin,
                                const glm::vec3& aabbMax) const;

    // Error estimation
    float estimateGeometricError(const BuildContext::VoxelNode* node) const;
    float estimateAttributeError(const BuildContext::VoxelNode* node) const;
    void sampleSurfacePoints(const BuildContext::VoxelNode* node,
                            std::vector<glm::vec3>& outPoints,
                            int samplesPerTriangle) const;

    // Attribute and contour integration
    UncompressedAttributes integrateAttributes(const BuildContext::VoxelNode* node) const;
    std::optional<Contour> constructContour(const BuildContext::VoxelNode* node) const;

    // Finalization
    void finalizeOctree();

    // Helpers
    size_t estimateNodeCount() const;
    float calculateBranchingFactor(const BuildContext::VoxelNode* node) const;
};

/**
 * Contour construction helper.
 * Implements greedy algorithm from paper Section 7.2.
 */
class ContourBuilder {
public:
    /**
     * Construct optimal contour for a voxel.
     *
     * @param voxelPos Voxel position in normalized coordinates [0,1]
     * @param voxelSize Size of voxel
     * @param surfacePoints Points on original surface within voxel
     * @param surfaceNormals Normals at surface points
     * @param ancestorContours Contours from parent voxels
     * @param errorThreshold Maximum allowed geometric error
     * @return Constructed contour, or nullopt if cube is sufficient
     */
    static std::optional<Contour> construct(
        const glm::vec3& voxelPos,
        float voxelSize,
        const std::vector<glm::vec3>& surfacePoints,
        const std::vector<glm::vec3>& surfaceNormals,
        const std::vector<Contour>& ancestorContours,
        float errorThreshold);

private:
    static float evaluateOverestimation(
        const glm::vec3& direction,
        const glm::vec3& voxelPos,
        float voxelSize,
        const std::vector<glm::vec3>& surfacePoints,
        const std::vector<Contour>& ancestorContours);
};

/**
 * Attribute integration helper.
 * Implements weighted filtering from paper Section 7.3.
 */
class AttributeIntegrator {
public:
    /**
     * Integrate color and normal from triangles within voxel.
     * Uses box filter with mip-mapped texture sampling.
     */
    static UncompressedAttributes integrate(
        const glm::vec3& voxelPos,
        float voxelSize,
        const std::vector<InputTriangle>& triangles);

private:
    static glm::vec3 integrateColor(
        const glm::vec3& voxelPos,
        float voxelSize,
        const std::vector<InputTriangle>& triangles);

    static glm::vec3 integrateNormal(
        const glm::vec3& voxelPos,
        float voxelSize,
        const std::vector<InputTriangle>& triangles);

    static uint32_t encodeColor(const glm::vec3& color);
    static uint32_t encodeNormal(const glm::vec3& normal);
};

} // namespace SVO
