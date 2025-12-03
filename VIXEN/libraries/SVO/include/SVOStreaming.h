#pragma once

#define NOMINMAX

#include "ISVOStructure.h"
#include <MortonEncoding.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Vixen::SVO {

/**
 * Streaming foundation for out-of-core SVO management.
 *
 * Based on industry patterns from:
 * - Unity HDRP: Streaming Virtual Texturing (SVT)
 * - Unreal Nanite: Hierarchical page streaming
 * - Frostbite: Virtual texture streaming
 * - id Tech 6: MegaTexture streaming
 *
 * Goals:
 * - Handle voxel datasets larger than GPU/CPU memory
 * - Stream bricks on-demand based on camera frustum and LOD
 * - Minimize memory footprint (keep only visible bricks resident)
 * - Prioritize loading for nearby/high-detail bricks
 * - Evict distant/low-detail bricks when under memory pressure
 *
 * Architecture:
 * - Persistent structure: Always-resident top levels (depth 0-N)
 * - Streamable bricks: On-demand lower levels (depth N+1 to max)
 * - LRU cache: Manage resident brick pool
 * - Priority queue: Order loading by screen coverage and distance
 */

// ============================================================================
// Brick Residency
// ============================================================================

/**
 * Residency state for a streamable brick.
 */
enum class BrickResidency : uint8_t {
    NotResident,    // Not in memory (on disk or not yet created)
    Loading,        // Currently being loaded from disk/network
    Resident,       // Fully loaded in CPU/GPU memory
    Evicting,       // Being evicted to free memory
};

/**
 * Streaming priority for a brick.
 * Higher values = load sooner.
 */
struct BrickPriority {
    float screenCoverage;       // Percentage of screen covered by brick (0-1)
    float distanceToCamera;     // World-space distance to camera
    uint32_t framesSinceAccess; // LRU eviction metric

    // Combined priority score (higher = more important)
    float getPriority() const {
        // Weight screen coverage heavily (visible > distant)
        // Penalize distance (nearby > far away)
        // Penalize age (recently used > stale)
        float priority = screenCoverage * 100.0f;
        priority -= distanceToCamera * 0.01f;
        priority -= framesSinceAccess * 0.1f;
        return priority;
    }

    bool operator<(const BrickPriority& other) const {
        return getPriority() < other.getPriority();
    }
};

/**
 * Handle to a streamable brick in the SVO.
 */
struct BrickHandle {
    Vixen::Core::MortonCode64 morton;  // Brick position in tree
    uint32_t brickIndex;               // Index in brick pool (UINT32_MAX if not resident)
    BrickResidency residency;
    BrickPriority priority;

    bool isResident() const { return residency == BrickResidency::Resident; }
};

// ============================================================================
// Streaming Configuration
// ============================================================================

/**
 * Configuration for streaming behavior.
 */
struct StreamingConfig {
    // Memory budget
    size_t maxResidentBricks = 4096;     // Maximum number of bricks in memory
    size_t maxGPUMemoryMB = 512;         // GPU memory budget in MB
    size_t maxCPUMemoryMB = 1024;        // CPU memory budget in MB

    // Persistent levels (always resident)
    uint32_t persistentLevels = 3;       // Top 3 levels (0-2) always loaded

    // Streaming thresholds
    float loadThreshold = 0.01f;         // Screen coverage to trigger load (1%)
    float evictThreshold = 0.001f;       // Screen coverage below which to evict (0.1%)
    float distanceLoadFactor = 100.0f;   // Load bricks within this distance
    float distanceEvictFactor = 200.0f;  // Evict bricks beyond this distance

    // LRU eviction
    uint32_t maxFramesBeforeEvict = 300; // Evict if unused for 300 frames (5 sec @ 60 FPS)

    // Performance tuning
    uint32_t maxLoadsPerFrame = 16;      // Max bricks to load per frame
    uint32_t maxEvictsPerFrame = 8;      // Max bricks to evict per frame
    uint32_t ioThreads = 4;              // Background I/O threads

    // Quality vs performance
    bool enableAsyncLoading = true;      // Load bricks asynchronously
    bool enablePrefetching = true;       // Prefetch bricks based on camera velocity
    bool enableCompression = true;       // Use DXT compression for resident bricks
};

// ============================================================================
// Streaming Manager Interface
// ============================================================================

/**
 * Abstract interface for SVO streaming management.
 *
 * Implementations:
 * - SVOStreamingManager: Main implementation
 * - SVOStreamingDebug: Debug visualization wrapper
 * - SVOStreamingNull: No-op for testing
 */
class ISVOStreamingManager {
public:
    virtual ~ISVOStreamingManager() = default;

    // ========================================================================
    // Configuration
    // ========================================================================

    virtual void setConfig(const StreamingConfig& config) = 0;
    virtual const StreamingConfig& getConfig() const = 0;

    // ========================================================================
    // Residency Management
    // ========================================================================

    /**
     * Update streaming state based on camera view.
     * Call once per frame with current camera parameters.
     *
     * @param cameraPosition Camera position in world space
     * @param cameraDirection Camera forward direction (normalized)
     * @param fovY Vertical field of view in radians
     * @param screenWidth Screen resolution width in pixels
     * @param screenHeight Screen resolution height in pixels
     */
    virtual void update(
        const glm::vec3& cameraPosition,
        const glm::vec3& cameraDirection,
        float fovY,
        int screenWidth,
        int screenHeight) = 0;

    /**
     * Query brick residency state.
     * @param morton Morton code of brick
     * @return Handle with residency info
     */
    virtual BrickHandle queryBrick(const Vixen::Core::MortonCode64& morton) const = 0;

    /**
     * Request brick to be loaded.
     * Adds to priority queue, may not load immediately.
     *
     * @param morton Morton code of brick
     * @param priority Priority for loading (higher = sooner)
     */
    virtual void requestLoad(const Vixen::Core::MortonCode64& morton, const BrickPriority& priority) = 0;

    /**
     * Evict brick from memory.
     * Writes dirty data to disk if needed.
     *
     * @param morton Morton code of brick
     */
    virtual void evictBrick(const Vixen::Core::MortonCode64& morton) = 0;

    /**
     * Flush all pending loads and evictions.
     * Blocks until all I/O completes.
     */
    virtual void flush() = 0;

    // ========================================================================
    // Statistics
    // ========================================================================

    struct StreamingStats {
        size_t residentBricks;        // Currently loaded bricks
        size_t loadingBricks;         // Bricks being loaded
        size_t evictingBricks;        // Bricks being evicted
        size_t totalBricks;           // Total bricks in dataset

        size_t cpuMemoryUsedMB;       // CPU memory used
        size_t gpuMemoryUsedMB;       // GPU memory used

        uint32_t loadsThisFrame;      // Bricks loaded this frame
        uint32_t evictsThisFrame;     // Bricks evicted this frame

        float residentPercentage() const {
            return totalBricks > 0 ? (residentBricks * 100.0f / totalBricks) : 0.0f;
        }

        float memoryPressure() const {
            // 0 = plenty of memory, 1 = at budget limit
            return std::min(cpuMemoryUsedMB / 1024.0f, gpuMemoryUsedMB / 512.0f);
        }
    };

    virtual StreamingStats getStats() const = 0;

    // ========================================================================
    // Serialization
    // ========================================================================

    /**
     * Save streaming cache to disk.
     * Writes residency metadata and LRU state.
     */
    virtual bool saveCacheToDisk(const std::string& cacheDir) = 0;

    /**
     * Load streaming cache from disk.
     * Restores residency metadata and LRU state.
     */
    virtual bool loadCacheFromDisk(const std::string& cacheDir) = 0;
};

// ============================================================================
// Streaming Utilities
// ============================================================================

/**
 * Compute screen-space bounding box for a brick.
 * Used to calculate screen coverage for priority.
 *
 * @param brickMin Brick minimum corner in world space
 * @param brickMax Brick maximum corner in world space
 * @param viewProj View-projection matrix
 * @param screenWidth Screen width in pixels
 * @param screenHeight Screen height in pixels
 * @return Screen-space bounding box (min/max in pixel coordinates)
 */
std::pair<glm::vec2, glm::vec2> projectBrickToScreen(
    const glm::vec3& brickMin,
    const glm::vec3& brickMax,
    const glm::mat4& viewProj,
    int screenWidth,
    int screenHeight);

/**
 * Calculate screen coverage percentage for a brick.
 *
 * @param screenMin Screen-space bounding box min (pixels)
 * @param screenMax Screen-space bounding box max (pixels)
 * @param screenWidth Screen width in pixels
 * @param screenHeight Screen height in pixels
 * @return Coverage percentage (0-1, where 1 = full screen)
 */
float calculateScreenCoverage(
    const glm::vec2& screenMin,
    const glm::vec2& screenMax,
    int screenWidth,
    int screenHeight);

/**
 * Check if brick is in camera frustum.
 *
 * @param brickMin Brick minimum corner in world space
 * @param brickMax Brick maximum corner in world space
 * @param viewProj View-projection matrix
 * @return true if brick intersects frustum
 */
bool isBrickInFrustum(
    const glm::vec3& brickMin,
    const glm::vec3& brickMax,
    const glm::mat4& viewProj);

/**
 * Prefetch bricks along camera movement vector.
 * Predicts future visible bricks based on velocity.
 *
 * @param currentPosition Current camera position
 * @param velocity Camera velocity (units per second)
 * @param deltaTime Time since last frame (seconds)
 * @param prefetchDistance How far ahead to prefetch (world units)
 * @return List of Morton codes to prefetch
 */
std::vector<Vixen::Core::MortonCode64> prefetchBricksAlongPath(
    const glm::vec3& currentPosition,
    const glm::vec3& velocity,
    float deltaTime,
    float prefetchDistance);

// ============================================================================
// Factory
// ============================================================================

/**
 * Create streaming manager for an SVO structure.
 *
 * @param structure SVO structure to manage streaming for
 * @param config Streaming configuration
 * @return Streaming manager instance
 */
std::unique_ptr<ISVOStreamingManager> createStreamingManager(
    ISVOStructure* structure,
    const StreamingConfig& config = StreamingConfig{});

} // namespace Vixen::SVO
