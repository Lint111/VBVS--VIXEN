#pragma once

#include "ISVOStructure.h"
#include "SVOTypes.h"
#include "SVOBuilder.h"
#include "BrickReference.h"
#include "AttributeRegistry.h"
#include "VoxelComponents.h"  // For Transform component
#include <gaia.h>  // For gaia::ecs::World and gaia::ecs::Entity
#include <memory>
#include <optional>
#include <limits>
#include <unordered_map>
#include <shared_mutex>  // For std::shared_mutex concurrency control

namespace SVO {


/**
 * Laine & Karras (2010) Sparse Voxel Octree implementation.
 *
 * Features:
 * - 64-bit child descriptors (15-bit pointers, masks)
 * - EntityBrickView zero-copy entity access (8 bytes/entity vs 64+)
 * - Hierarchical rebuild from GaiaVoxelWorld entities
 * - Page headers every 8KB for block management
 * - Beam optimization support for primary rays
 *
 * ============================================================================
 * RECOMMENDED WORKFLOW:
 * ============================================================================
 * 1. Create GaiaVoxelWorld and populate with entities:
 *      GaiaVoxelWorld world;
 *      world.createVoxel(VoxelCreationRequest{position, {Density{1.0f}, Color{red}}});
 *
 * 2. Create octree and rebuild from entities:
 *      LaineKarrasOctree octree(world, maxLevels, brickDepth);
 *      octree.rebuild(world, worldMin, worldMax);
 *
 * 3. Ray cast using entity-based SVO:
 *      auto hit = octree.castRay(origin, direction);
 *      if (hit.hit) {
 *          auto color = world.getComponentValue<Color>(hit.entity);
 *      }
 *
 * Legacy VoxelInjector::inject() workflow deprecated - use rebuild() instead.
 * ============================================================================
 */
class LaineKarrasOctree : public ISVOStructure {
public:

    // Entity-based constructor (pure spatial index)
    // SVO stores entity IDs via EntityBrickView (8 bytes/entity)
    // Use rebuild() to populate octree from GaiaVoxelWorld entities
    explicit LaineKarrasOctree(::GaiaVoxel::GaiaVoxelWorld& voxelWorld,::VoxelData::AttributeRegistry* registry = nullptr, int maxLevels = 23, int brickDepthLevels = 3);

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

    // ========================================================================
    // DEPRECATED: Incremental Insertion API (removed in favor of rebuild())
    // ========================================================================
    // Use rebuild() for octree population from GaiaVoxelWorld entities.
    // For incremental updates, use updateBlock() API (Phase 3 TODO).
    //
    // void insert(gaia::ecs::Entity entity);  // REMOVED - use rebuild()
    // void remove(gaia::ecs::Entity entity);  // REMOVED - use rebuild()

    // ========================================================================
    // Octree Rebuild API (Phase 3)
    // ========================================================================

    /**
     * Rebuild entire octree from GaiaVoxelWorld entities.
     * Queries all entities, builds ESVO hierarchy, creates EntityBrickView instances.
     *
     * @param world Source of entity data (must match constructor voxelWorld)
     * @param worldMin AABB minimum for octree bounds
     * @param worldMax AABB maximum for octree bounds
     *
     * NOTE: Clears existing octree structure. Use updateBlock() for incremental changes.
     */
    void rebuild(::GaiaVoxel::GaiaVoxelWorld& world, const glm::vec3& worldMin, const glm::vec3& worldMax);

    /**
     * Update specific octree block region.
     * Re-queries entities in block region, updates ChildDescriptors + EntityBrickView.
     *
     * @param blockWorldMin Lower-left corner of block to update
     * @param blockDepth Block depth (matches brick depth, typically 3 for 8³)
     */
    void updateBlock(const glm::vec3& blockWorldMin, uint8_t blockDepth);

    /**
     * Remove block from octree (e.g., all entities in region destroyed).
     * More efficient than updateBlock() when block is known to be empty.
     */
    void removeBlock(const glm::vec3& blockWorldMin, uint8_t blockDepth);

    /**
     * Lock octree for read-only access during frame rendering.
     * Prevents rebuild/update operations that would invalidate EntityBrickView spans.
     *
     * USAGE:
     *   octree.lockForRendering();  // At frame start
     *   // ... ray casting ...
     *   octree.unlockAfterRendering();  // At frame end
     *
     * NOTE: rebuild() and updateBlock() will block if rendering lock is held.
     */
    void lockForRendering();
    void unlockAfterRendering();

    // ========================================================================
    // Public Type Definitions for Traversal
    // ========================================================================
    // These types are exposed publicly so helper functions in the .cpp can use them.

    /**
     * Complete traversal state for ESVO ray casting.
     * Encapsulates all mutable variables needed during octree traversal.
     *
     * MIRRORED SPACE ARCHITECTURE:
     * - idx: Current octant in MIRRORED space (ray-direction dependent)
     * - mirroredValidMask: Parent's validMask converted to mirrored space
     * - mirroredLeafMask: Parent's leafMask converted to mirrored space
     *
     * This allows direct comparison: (mirroredValidMask & (1 << idx)) works correctly
     * without per-check conversion. Masks are converted once in fetchChildDescriptor().
     */
    struct ESVOTraversalState {
        // Current node and octant
        const ChildDescriptor* parent = nullptr;
        uint64_t child_descriptor = 0;
        int idx = 0;              // Current child octant index (0-7) in MIRRORED space
        int scale = 0;            // Current ESVO scale (0-22)
        float scale_exp2 = 0.5f;  // 2^(scale - ESVO_MAX_SCALE)

        // Position in normalized [1,2] space
        glm::vec3 pos{1.0f, 1.0f, 1.0f};

        // Active ray t-span
        float t_min = 0.0f;
        float t_max = 1.0f;
        float h = 0.0f;  // Horizon value for stack management

        // Center values (used for octant selection after DESCEND)
        float tx_center = 0.0f;
        float ty_center = 0.0f;
        float tz_center = 0.0f;

        // Mirrored masks - converted from world-space once per descriptor fetch
        // These are set in fetchChildDescriptor() based on octant_mask
        uint8_t mirroredValidMask = 0;  // Parent's validMask in mirrored space
        uint8_t mirroredLeafMask = 0;   // Parent's leafMask in mirrored space

        // Iteration counter
        int iter = 0;
    };

    /**
     * Ray coefficients for parametric traversal.
     * Precomputed values for efficient t-value calculations.
     */
    struct ESVORayCoefficients {
        float tx_coef = 0.0f;
        float ty_coef = 0.0f;
        float tz_coef = 0.0f;
        float tx_bias = 0.0f;
        float ty_bias = 0.0f;
        float tz_bias = 0.0f;
        int octant_mask = 7;
        glm::vec3 rayDir{0.0f};      // Normalized ray direction
        glm::vec3 normOrigin{1.0f};  // Ray origin in normalized [1,2] space
    };

    /**
     * Result from ADVANCE phase - indicates next action to take.
     */
    enum class AdvanceResult {
        CONTINUE,     // Continue to next iteration
        POP_NEEDED,   // Need to pop up the hierarchy
        EXIT_OCTREE   // Ray exited the octree
    };

    /**
     * Result from POP phase - indicates traversal status.
     */
    enum class PopResult {
        CONTINUE,     // Continue traversal at new scale
        EXIT_OCTREE   // Ray exited the octree (popped above root)
    };

private:
    std::unique_ptr<Octree> m_octree;

    // Entity-based storage (NEW architecture)
    ::GaiaVoxel::GaiaVoxelWorld* m_voxelWorld = nullptr;  // Non-owning pointer to voxel world

    // Concurrency control - prevents rebuild during frame rendering
    mutable std::shared_mutex m_renderLock;
    // Write lock held during rendering, read lock held during rebuild/update

    // DEPRECATED: Will be removed once migration to entity-based storage is complete
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

    // Transform: maps local [0, worldSize] space ↔ world space
    // localToWorld: transforms local-space positions to world space
    // worldToLocal: transforms world-space rays into local space for traversal
    glm::mat4 m_localToWorld{1.0f};   // identity by default
    glm::mat4 m_worldToLocal{1.0f};   // inverse of localToWorld

    // Transform: maps normalized [0,1]³ octree space ↔ world space
    ::GaiaVoxel::VolumeTransform m_transform;

    // Integer grid bounds for quantized voxel coordinates
    // Stores the actual data bounds; normalization uses power-of-2 padded extent
    ::GaiaVoxel::VolumeGrid m_volumeGrid;

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

    // Ray casting helpers (legacy - kept for compatibility)
    struct TraversalState {
        ChildDescriptor* parent = nullptr;
        int childIdx = 0;
        int scale = 0;
        glm::vec3 position{0.0f};
    };

    // Main ray casting implementation
    ISVOStructure::RayHit castRayImpl(const glm::vec3& origin, const glm::vec3& direction,
                       float tMin, float tMax, float rayBias) const;

    // ========================================================================
    // Refactored Traversal Phase Methods
    // ========================================================================
    // Each method handles one logical phase of the ESVO traversal algorithm

    /**
     * Validate ray input parameters.
     * Returns false if ray is invalid (null direction, NaN, etc.)
     */
    bool validateRayInput(
        const glm::vec3& origin,
        const glm::vec3& direction,
        glm::vec3& rayDirOut) const;

    /**
     * Initialize traversal state for ray casting.
     * Sets up stack, initial position, and octant selection.
     */
    void initializeTraversalState(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef,
        CastStack& stack) const;

    /**
     * Fetch child descriptor for current node if not cached.
     * Mirrors validMask and leafMask based on octant_mask for correct traversal.
     */
    void fetchChildDescriptor(ESVOTraversalState& state, const ESVORayCoefficients& coef) const;

    /**
     * Check if current child is valid and compute t-span intersection.
     * Returns true if the voxel should be processed (valid and intersected).
     */
    bool checkChildValidity(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef,
        bool& isLeaf,
        float& tv_max) const;

    /**
     * PUSH phase: Descend into child node.
     * Updates parent pointer, scale, and position for child traversal.
     */
    void executePushPhase(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef,
        CastStack& stack,
        float tv_max) const;

    /**
     * ADVANCE phase: Move to next sibling voxel.
     * Returns result indicating next action to take.
     */
    AdvanceResult executeAdvancePhase(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef) const;

    /**
     * POP phase: Ascend hierarchy when exiting parent voxel.
     * Uses integer bit manipulation for correct scale computation.
     */
    PopResult executePopPhase(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef,
        CastStack& stack,
        int step_mask) const;

    /**
     * Handle leaf hit: perform brick traversal and return hit result.
     * Returns nullopt if traversal should continue (brick miss).
     */
    std::optional<ISVOStructure::RayHit> handleLeafHit(
        ESVOTraversalState& state,
        const ESVORayCoefficients& coef,
        const glm::vec3& origin,
        float tRayStart,
        float tEntry,
        float tExit,
        float tv_max) const;

    /**
     * Traverse brick and return hit result.
     * Ray is in volume local space (origin at volumeGridMin = 0,0,0).
     * EntityBrickView uses local gridOrigin (brickIndex * brickSideLength).
     */
    std::optional<ISVOStructure::RayHit> traverseBrickAndReturnHit(
        const ::GaiaVoxel::EntityBrickView& brickView,
        const glm::vec3& localRayOrigin,  // Ray origin in volume local space
        const glm::vec3& rayDir,
        float tEntry) const;

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

    /**
     * EntityBrickView-based DDA traversal (Phase 3).
     * Queries entities on-demand via MortonKey instead of copying voxel data.
     *
     * @param brickView Zero-copy view over brick entities
     * @param brickWorldMin Brick minimum corner in world space
     * @param brickVoxelSize Size of one voxel in world units
     * @param rayOrigin Ray origin in world space
     * @param rayDir Ray direction (normalized)
     * @param tMin Ray parameter for brick entry
     * @param tMax Ray parameter for brick exit
     * @return RayHit with entity reference, or nullopt if miss
     */
    std::optional<ISVOStructure::RayHit> traverseBrickView(
        const ::GaiaVoxel::EntityBrickView& brickView,
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
