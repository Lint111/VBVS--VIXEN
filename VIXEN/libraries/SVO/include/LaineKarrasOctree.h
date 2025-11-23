#pragma once

#include "ISVOStructure.h"
#include "SVOTypes.h"
#include "SVOBuilder.h"
#include "BrickReference.h"
#include "AttributeRegistry.h"
#include <memory>
#include <optional>
#include <limits>

namespace SVO {


/**
 * Laine & Karras (2010) Sparse Voxel Octree implementation.
 *
 * Features:
 * - 64-bit child descriptors (15-bit pointers, masks)
 * - 32-bit contours (parallel planes for tight surface approximation)
 * - Compressed attributes (DXT-style color + custom normal compression)
 * - Page headers every 8KB for block management
 * - Beam optimization support for primary rays
 *
 * Memory layout per voxel: ~5 bytes average
 * - 1 byte: hierarchy (child descriptor amortized over children)
 * - 1 byte: contours (optional, amortized)
 * - 1 byte: color (DXT compression)
 * - 2 bytes: normals (custom compression)
 */
class LaineKarrasOctree : public ISVOStructure {
public:
    LaineKarrasOctree(int maxLevels = 23, int brickDepthLevels = 3);
    explicit LaineKarrasOctree(::VoxelData::AttributeRegistry* registry, int maxLevels = 23, int brickDepthLevels = 3);
    ~LaineKarrasOctree() override;

    // ISVOStructure interface
    bool voxelExists(const glm::vec3& position, int scale) const override;
    std::optional<ISVOStructure::VoxelData> getVoxelData(const glm::vec3& position, int scale) const override;
    uint8_t getChildMask(const glm::vec3& position, int scale) const override;
    ISVOStructure::VoxelBounds getVoxelBounds(const glm::vec3& position, int scale) const override;

    ISVOStructure::RayHit castRay(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float tMin = 0.0f,
        float tMax = (std::numeric_limits<float>::max)()) const override;

    ISVOStructure::RayHit castRayLOD(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float lodBias,
        float tMin = 0.0f,
        float tMax = (std::numeric_limits<float>::max)()) const override;

    glm::vec3 getWorldMin() const override { return m_worldMin; }
    glm::vec3 getWorldMax() const override { return m_worldMax; }
    int getMaxLevels() const override { return m_maxLevels; }
    float getVoxelSize(int scale) const override;
    size_t getVoxelCount() const override { return m_voxelCount; }
    size_t getMemoryUsage() const override { return m_memoryUsage; }
    std::string getStats() const override;

    std::vector<uint8_t> serialize() const override;
    bool deserialize(std::span<const uint8_t> data) override;

    ISVOStructure::GPUBuffers getGPUBuffers() const override;
    std::string getGPUTraversalShader() const override;

    // Construction interface (called by builder)
    void setOctree(std::unique_ptr<Octree> octree);
    const Octree* getOctree() const { return m_octree.get(); }
    Octree* getOctreeMutable() { return m_octree.get(); } // For direct modification (additive insertion)

    // Additive insertion support - ensure octree is initialized
    void ensureInitialized(const glm::vec3& worldMin, const glm::vec3& worldMax, int maxLevels);

private:
    std::unique_ptr<Octree> m_octree;
    ::VoxelData::AttributeRegistry* m_registry = nullptr; // Non-owning pointer

    // NOTE: Key attribute is ALWAYS index 0 in AttributeRegistry (guaranteed by design)
    // This eliminates the need to cache or lookup the key index

    // Cached metadata
    glm::vec3 m_worldMin{0.0f};
    glm::vec3 m_worldMax{1.0f};
    int m_maxLevels = 23;  // Octree depth - default 23 for standard ESVO [1,2] normalized space
    int m_brickDepthLevels = 3;  // Brick dense storage depth (3 = 8³ bricks, 4 = 16³ bricks)
                                  // Traversal switches to brick DDA when depth >= (maxLevels - brickDepthLevels)
    size_t m_voxelCount{ 0 };
    size_t m_memoryUsage{ 0 };

    // ========================================================================
    // ADOPTED FROM: NVIDIA ESVO Reference (cuda/Raycast.inl)
    // Copyright (c) 2009-2011, NVIDIA Corporation (BSD 3-Clause)
    // ========================================================================

    // ESVO internal scale range - normalized to [1,2] space with 23-bit mantissa precision
    // This constant enables ESVO's float bit manipulation tricks to work for ANY user depth
    // User scales are mapped: userScale -> ESVO_MAX_SCALE - (m_maxLevels - 1 - userScale)
    static constexpr int ESVO_MAX_SCALE = 22;  // Root scale in ESVO normalized space

    // Traversal stack depth - maximum supported
    static constexpr int MAX_STACK_DEPTH = 32;

    // Traversal stack structure
    // Uses SCALE-INDEXED storage like ESVO (not LIFO stack)
    // Each scale level has one slot: stack.entries[scale]
    // This works for ANY octree depth (not just depth 23)
    struct CastStack {
        const ChildDescriptor* nodes[MAX_STACK_DEPTH];
        float tMax[MAX_STACK_DEPTH];

        void push(int scale, const ChildDescriptor* node, float t) {
            if (scale >= 0 && scale < MAX_STACK_DEPTH) {
                nodes[scale] = node;
                tMax[scale] = t;
            }
        }

        const ChildDescriptor* getNode(int scale) const {
            if (scale >= 0 && scale < MAX_STACK_DEPTH) {
                return nodes[scale];
            }
            return nullptr;
        }

        float getTMax(int scale) const {
            if (scale >= 0 && scale < MAX_STACK_DEPTH) {
                return tMax[scale];
            }
            return 0.0f;
        }
    };

    // Scale mapping: Convert between user scale and ESVO internal scale
    // This allows ESVO's bit manipulation tricks to work for any octree depth
    //
    // For depth 8: userScales [0-7] map to esvoScales [15-22]
    // For depth 23: userScales [0-22] map to esvoScales [0-22]
    inline int userToESVOScale(int userScale) const {
        return ESVO_MAX_SCALE - (m_maxLevels - 1 - userScale);
    }

    inline int esvoToUserScale(int esvoScale) const {
        return esvoScale - (ESVO_MAX_SCALE - m_maxLevels + 1);
    }

    // Ray casting helpers
    struct TraversalState {
        ChildDescriptor* parent = nullptr;
        int childIdx = 0;
        int scale = 0;
        glm::vec3 position{0.0f};
    };

    ISVOStructure::RayHit castRayImpl(const glm::vec3& origin, const glm::vec3& direction,
                       float tMin, float tMax, float rayBias) const;

    // Traversal helpers (implements algorithm from Appendix A)
    bool intersectVoxel(const VoxelCube& voxel, const Contour* contour,
                       const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                       float& tMin, float& tMax) const;

    void advanceRay(VoxelCube& voxel, int& childIdx,
                    const glm::vec3& rayDir, float& t) const;

    int selectFirstChild(const VoxelCube& voxel,
                        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        float tMin) const;

    // ========================================================================
    // Brick DDA Traversal (Dense Voxel Grid Ray Marching)
    // ========================================================================

    /**
     * 3D DDA ray traversal through dense brick voxels.
     *
     * @param brickRef Brick reference (ID + depth) from octree leaf
     * @param brickWorldMin Brick minimum corner in world space
     * @param brickVoxelSize Size of one voxel in world units
     * @param rayOrigin Ray origin in world space
     * @param rayDir Ray direction in world space (normalized)
     * @param tMin Ray parameter where brick entry occurs
     * @param tMax Ray parameter where brick exit would occur
     * @return RayHit with brick voxel hit, or miss if ray exits brick
     *
     * Algorithm:
     * 1. Transform ray to brick-local [0, N]³ space
     * 2. Initialize 3D DDA state (current voxel, step dirs, t_delta, t_next)
     * 3. March through brick voxels using DDA
     * 4. At each voxel: sample brick storage for occupancy
     * 5. Return first occupied voxel or miss on brick exit
     */
    std::optional<ISVOStructure::RayHit> traverseBrick(
        const BrickReference& brickRef,
        const glm::vec3& brickWorldMin,
        float brickVoxelSize,
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        float tMin,
        float tMax) const;
};

/**
 * Builder for Laine-Karras octree.
 */
class LaineKarrasBuilder : public ISVOBuilder {
public:
    LaineKarrasBuilder();
    ~LaineKarrasBuilder() override;

    std::unique_ptr<ISVOStructure> build(
        const InputGeometry& geometry,
        const BuildConfig& config) override;

    void setProgressCallback(ProgressCallback callback) override {
        m_progressCallback = std::move(callback);
    }

private:
    std::unique_ptr<SVOBuilder> m_impl;
    ProgressCallback m_progressCallback;

    // Convert interface types
    BuildParams convertConfig(const BuildConfig& config);
    InputMesh convertGeometry(const InputGeometry& geometry);
};

} // namespace SVO
