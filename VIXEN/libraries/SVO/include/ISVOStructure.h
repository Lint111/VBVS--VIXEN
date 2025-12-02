#pragma once

#include <glm/glm.hpp>
#include <gaia.h>  // For gaia::ecs::Entity (value type, cannot forward declare)
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <string>
#include <functional>

// Forward declaration for LOD parameters
namespace Vixen::SVO {
    struct LODParameters;
}

namespace Vixen::SVO {

/**
 * Abstract interface for sparse voxel structure implementations.
 *
 * This allows experimentation with different SVO variants:
 * - Classic Laine & Karras octree with contours
 * - DAG (Directed Acyclic Graph) with shared subtrees
 * - SVDAG (Symmetric Voxel DAG)
 * - Hash-based sparse voxel grids
 * - Hierarchical Z-order curves
 * - etc.
 */
class ISVOStructure {
public:
    virtual ~ISVOStructure() = default;

    // ========================================================================
    // Query Interface
    // ========================================================================

    /**
     * Check if a voxel exists at given position and scale.
     * @param position World-space position
     * @param scale Detail level (higher = coarser)
     * @return true if voxel exists
     */
    virtual bool voxelExists(const glm::vec3& position, int scale) const = 0;

    /**
     * Get voxel data at position.
     * @return Voxel data if exists, nullopt otherwise
     */
    struct VoxelData {
        glm::vec3 color;
        glm::vec3 normal;
        float occlusion = 1.0f;
        bool isLeaf = false;
    };
    virtual std::optional<VoxelData> getVoxelData(const glm::vec3& position, int scale) const = 0;

    /**
     * Get children of a voxel.
     * @param position Parent voxel position
     * @param scale Parent voxel scale
     * @return Mask of which child slots (0-7) are occupied
     */
    virtual uint8_t getChildMask(const glm::vec3& position, int scale) const = 0;

    /**
     * Get geometric bounds of voxel.
     * For octrees this is a cube, but could be tighter (e.g., with contours).
     */
    struct VoxelBounds {
        glm::vec3 min;
        glm::vec3 max;
        // Optional tighter representation (e.g., oriented slab from contour)
        std::optional<std::pair<glm::vec3, glm::vec3>> orientedBounds;
    };
    virtual VoxelBounds getVoxelBounds(const glm::vec3& position, int scale) const = 0;

    // ========================================================================
    // Traversal Interface
    // ========================================================================

    /**
     * Ray-voxel intersection result.
     *
     * REFACTORED: Now returns entity reference instead of copying voxel data.
     *
     * Memory: 53 bytes (was: 64+ bytes with data copy) - 17% reduction
     * - entity: 8 bytes (lightweight reference to Gaia ECS entity)
     * - hitPoint: 12 bytes (vec3)
     * - normal: 12 bytes (vec3) - computed from hit face during traversal
     * - tMin/tMax: 8 bytes (2 floats)
     * - scale: 4 bytes
     * - hit: 1 byte
     *
     * Benefits:
     * - Zero-copy access to voxel attributes via entity
     * - Surface normal computed during traversal (face-accurate for lighting)
     * - 17% smaller hit structure
     * - SVO stores only entity IDs (8 bytes), not full data (64+ bytes)
     */
    struct RayHit {
        gaia::ecs::Entity entity;    // Entity reference (8 bytes) - access attributes via GaiaVoxelWorld
        glm::vec3 hitPoint;          // Hit position in world space (12 bytes)
        glm::vec3 normal;            // Surface normal at hit point (12 bytes) - computed from face
        float tMin;                  // Entry t-value (4 bytes)
        float tMax;                  // Exit t-value (4 bytes)
        int scale;                   // Detail level of hit voxel (4 bytes)
        bool hit;                    // Whether ray hit anything (1 byte)

        // Traversal state (opaque, implementation-specific)
        std::shared_ptr<void> traversalState;
    };

    /**
     * Cast ray through structure.
     * @param origin Ray origin in world space
     * @param direction Ray direction (normalized)
     * @param tMin Minimum t-value
     * @param tMax Maximum t-value
     * @return Closest hit, or miss
     */
    virtual RayHit castRay(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float tMin = 0.0f,
        float tMax = 1e30f) const = 0;  // Use large float instead of max()

    /**
     * Cast ray with LOD control.
     * @param lodBias Bias for level-of-detail selection (higher = coarser)
     */
    virtual RayHit castRayLOD(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float lodBias,
        float tMin = 0.0f,
        float tMax = 1e30f) const = 0;  // Use large float instead of max()

    /**
     * Cast ray with screen-space LOD termination.
     *
     * Based on Laine & Karras (2010) Section 4.4 "Level-of-detail".
     * Terminates traversal when voxel projects to less than one pixel.
     *
     * ESVO formula (Raycast.inl line 181):
     *   if (tc_max * ray.dir_sz + ray_orig_sz >= scale_exp2) break;
     *
     * @param origin Ray origin in world space
     * @param direction Ray direction (normalized)
     * @param fovY Vertical field of view in radians
     * @param screenHeight Screen resolution in pixels (vertical)
     * @param tMin Minimum t-value
     * @param tMax Maximum t-value
     * @return Closest hit (may be at coarser LOD for distant voxels)
     */
    virtual RayHit castRayScreenSpaceLOD(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float fovY,
        int screenHeight,
        float tMin = 0.0f,
        float tMax = 1e30f) const = 0;

    /**
     * Cast ray with explicit LOD parameters.
     *
     * Use LODParameters::fromCamera() to compute parameters from camera settings.
     *
     * @param origin Ray origin in world space
     * @param direction Ray direction (normalized)
     * @param lodParams Screen-space LOD parameters (ray cone)
     * @param tMin Minimum t-value
     * @param tMax Maximum t-value
     * @return Closest hit (may be at coarser LOD for distant voxels)
     */
    virtual RayHit castRayWithLOD(
        const glm::vec3& origin,
        const glm::vec3& direction,
        const LODParameters& lodParams,
        float tMin = 0.0f,
        float tMax = 1e30f) const = 0;

    // ========================================================================
    // Metadata Interface
    // ========================================================================

    /**
     * Get world-space bounding box.
     */
    virtual glm::vec3 getWorldMin() const = 0;
    virtual glm::vec3 getWorldMax() const = 0;

    /**
     * Get maximum detail level.
     * Higher values = finer detail.
     */
    virtual int getMaxLevels() const = 0;

    /**
     * Get voxel size at given scale level.
     */
    virtual float getVoxelSize(int scale) const = 0;

    /**
     * Get total number of voxels.
     */
    virtual size_t getVoxelCount() const = 0;

    /**
     * Get memory usage in bytes.
     */
    virtual size_t getMemoryUsage() const = 0;

    /**
     * Get implementation-specific statistics.
     */
    virtual std::string getStats() const = 0;

    // ========================================================================
    // Serialization Interface
    // ========================================================================

    /**
     * Serialize to binary blob.
     */
    virtual std::vector<uint8_t> serialize() const = 0;

    /**
     * Deserialize from binary blob.
     * @return true on success
     */
    virtual bool deserialize(std::span<const uint8_t> data) = 0;

    /**
     * Save to file.
     */
    virtual bool saveToFile(const std::string& filename) const;

    /**
     * Load from file.
     */
    virtual bool loadFromFile(const std::string& filename);

    // ========================================================================
    // GPU Interface
    // ========================================================================

    /**
     * Get GPU-compatible representation.
     * Returns buffer data that can be uploaded to GPU.
     */
    struct GPUBuffers {
        std::vector<uint8_t> hierarchyBuffer;    // Octree structure
        std::vector<uint8_t> attributeBuffer;    // Colors, normals, etc.
        std::vector<uint8_t> auxBuffer;          // Contours, metadata, etc.

        // ====================================================================
        // DXT Compressed Buffers (Week 3)
        // ====================================================================
        // For GPU binding 7 (CompressedColorBuffer) and 8 (CompressedNormalBuffer)
        //
        // Layout: 32 DXT blocks per 8x8x8 brick
        //   Color:  uvec2 (8 bytes) per block = 256 bytes/brick
        //   Normal: uvec4 (16 bytes) per block = 512 bytes/brick
        // ====================================================================
        std::vector<uint8_t> compressedColorBuffer;   // Binding 7: DXT1 color blocks
        std::vector<uint8_t> compressedNormalBuffer;  // Binding 8: DXT normal blocks
    };
    virtual GPUBuffers getGPUBuffers() const = 0;

    /**
     * Get shader code for GPU traversal.
     * Returns GLSL code specific to this structure type.
     */
    virtual std::string getGPUTraversalShader() const = 0;
};

/**
 * Abstract builder interface for SVO structures.
 */
class ISVOBuilder {
public:
    virtual ~ISVOBuilder() = default;

    /**
     * Input geometry representation.
     */
    struct InputGeometry {
        std::vector<glm::vec3> vertices;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec3> colors;
        std::vector<uint32_t> indices;
        glm::vec3 minBounds;
        glm::vec3 maxBounds;
    };

    /**
     * Build configuration.
     */
    struct BuildConfig {
        int maxLevels = 16;
        float errorThreshold = 0.001f;
        bool enableCompression = true;
        int numThreads = 0; // 0 = auto
    };

    /**
     * Build SVO structure from geometry.
     */
    virtual std::unique_ptr<ISVOStructure> build(
        const InputGeometry& geometry,
        const BuildConfig& config) = 0;

    /**
     * Set progress callback (optional).
     */
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;
    virtual void setProgressCallback(ProgressCallback callback) = 0;
};

/**
 * Factory for creating SVO implementations.
 */
class SVOFactory {
public:
    enum class Type {
        LaineKarrasOctree,      // Classic implementation with contours
        CompressedOctree,       // DXT-compressed attributes
        DAG,                    // Directed Acyclic Graph
        SVDAG,                  // Symmetric Voxel DAG
        HashGrid,               // Hash-based sparse grid
    };

    /**
     * Create builder for specified type.
     */
    static std::unique_ptr<ISVOBuilder> createBuilder(Type type);

    /**
     * Create empty structure of specified type.
     */
    static std::unique_ptr<ISVOStructure> createStructure(Type type);

    /**
     * Detect type from serialized data.
     */
    static std::optional<Type> detectType(std::span<const uint8_t> data);
};

} // namespace Vixen::SVO
