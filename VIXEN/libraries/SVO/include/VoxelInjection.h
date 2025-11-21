#pragma once

#include "ISVOStructure.h"
#include "SVOTypes.h"
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>

// VoxelData library
#include <AttributeRegistry.h>

namespace SVO {

// Forward declarations
template<typename BrickDataLayout> class BrickStorage;
struct DefaultLeafData;
struct Octree;

/**
 * Direct voxel data for injection into SVO structure.
 * Bypasses mesh triangulation - use for procedural generation,
 * noise fields, SDFs, terrain, etc.
 */
struct VoxelData {
    glm::vec3 position;      // World-space position
    glm::vec3 color;         // RGB color [0,1]
    glm::vec3 normal;        // Surface normal (normalized)
    float density = 1.0f;    // Density/occupancy [0,1]
    float occlusion = 1.0f;  // Ambient occlusion [0,1]

    bool isSolid() const { return density > 0.5f; }
};

/**
 * Sparse voxel input - only occupied voxels.
 * Most efficient for sparse data (terrain, noise).
 */
struct SparseVoxelInput {
    std::vector<VoxelData> voxels;
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    int resolution;  // Grid resolution along each axis
};

/**
 * Dense voxel grid input - 3D array.
 * Use for dense volumetric data (fog, clouds, etc.).
 */
struct DenseVoxelInput {
    std::vector<VoxelData> voxels;  // Size = resolution^3
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    glm::ivec3 resolution;          // Resolution per axis

    size_t getIndex(int x, int y, int z) const {
        return x + y * resolution.x + z * resolution.x * resolution.y;
    }

    const VoxelData& at(int x, int y, int z) const {
        return voxels[getIndex(x, y, z)];
    }
};

/**
 * Procedural voxel sampler - callback-based.
 * Most flexible - generates voxels on-demand during build.
 * Perfect for infinite terrain, noise functions, SDFs, etc.
 */
class IVoxelSampler {
public:
    virtual ~IVoxelSampler() = default;

    /**
     * Sample voxel data at given position.
     * @return true if voxel is solid, false if empty
     */
    virtual bool sample(const glm::vec3& position, VoxelData& outData) const = 0;

    /**
     * Get bounding box of valid data.
     * Return infinite bounds if unbounded (e.g., infinite terrain).
     */
    virtual void getBounds(glm::vec3& outMin, glm::vec3& outMax) const = 0;

    /**
     * Estimate density at given scale.
     * Used for early termination and LOD.
     * @param center Center of region
     * @param size Size of region
     * @return Approximate density [0,1]
     */
    virtual float estimateDensity(const glm::vec3& center, float size) const {
        // Default: always subdivide
        return 1.0f;
    }
};

/**
 * Lambda-based sampler for convenience.
 */
class LambdaVoxelSampler : public IVoxelSampler {
public:
    using SampleFunc = std::function<bool(const glm::vec3&, VoxelData&)>;
    using BoundsFunc = std::function<void(glm::vec3&, glm::vec3&)>;
    using DensityFunc = std::function<float(const glm::vec3&, float)>;

    explicit LambdaVoxelSampler(
        SampleFunc sampleFunc,
        BoundsFunc boundsFunc,
        DensityFunc densityFunc = nullptr)
        : m_sampleFunc(std::move(sampleFunc))
        , m_boundsFunc(std::move(boundsFunc))
        , m_densityFunc(std::move(densityFunc)) {}

    bool sample(const glm::vec3& position, VoxelData& outData) const override {
        return m_sampleFunc(position, outData);
    }

    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override {
        m_boundsFunc(outMin, outMax);
    }

    float estimateDensity(const glm::vec3& center, float size) const override {
        return m_densityFunc ? m_densityFunc(center, size) : 1.0f;
    }

private:
    SampleFunc m_sampleFunc;
    BoundsFunc m_boundsFunc;
    DensityFunc m_densityFunc;
};

/**
 * Common procedural samplers.
 */
namespace Samplers {

/**
 * 3D Perlin/Simplex noise sampler.
 * Use for terrain, clouds, organic shapes.
 */
class NoiseSampler : public IVoxelSampler {
public:
    struct Params {
        float frequency = 1.0f;
        float amplitude = 1.0f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float persistence = 0.5f;
        float threshold = 0.0f;  // Density threshold for solid
        glm::vec3 offset{0.0f};
    };

    explicit NoiseSampler(const Params& params = Params{});

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    float estimateDensity(const glm::vec3& center, float size) const override;

private:
    Params m_params;
};

/**
 * Signed Distance Field (SDF) sampler.
 * Use for CSG operations, smooth blending, etc.
 */
class SDFSampler : public IVoxelSampler {
public:
    using SDFFunc = std::function<float(const glm::vec3&)>;

    explicit SDFSampler(SDFFunc sdfFunc, const glm::vec3& min, const glm::vec3& max);

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;

private:
    SDFFunc m_sdfFunc;
    glm::vec3 m_min, m_max;

    glm::vec3 estimateNormal(const glm::vec3& p) const;
};

/**
 * Heightmap terrain sampler.
 * Use for terrain generation from heightmap data.
 */
class HeightmapSampler : public IVoxelSampler {
public:
    struct Params {
        std::vector<float> heights;  // Height values
        int width, height;            // Heightmap dimensions
        float minHeight = 0.0f;
        float maxHeight = 100.0f;
        float horizontalScale = 1.0f;
        glm::vec3 baseColor{0.5f, 0.4f, 0.3f};
    };

    explicit HeightmapSampler(const Params& params);

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    float estimateDensity(const glm::vec3& center, float size) const override;

private:
    Params m_params;

    float sampleHeight(float x, float z) const;
    glm::vec3 computeNormal(float x, float z) const;
};

} // namespace Samplers

/**
 * Voxel injection configuration.
 */
struct InjectionConfig {
    int maxLevels = 16;
    int brickDepthLevels = 0;   // Bottom N levels stored as dense bricks (0 = no bricks, pure octree)
                                 // Example: brickDepthLevels=3 → bottom 3 levels = 8×8×8 voxel bricks
    float errorThreshold = 0.001f;
    float minVoxelSize = 0.0f;  // Minimum voxel size - prevents over-subdivision (0 = use errorThreshold)
    bool enableContours = true;
    bool enableCompression = true;

    // LOD control
    bool enableLOD = true;
    float lodBias = 0.0f;  // Negative = finer, positive = coarser

    // Filtering
    enum class FilterMode {
        None,           // No filtering
        Box,            // Box filter (average)
        Gaussian,       // Gaussian filter
    };
    FilterMode filterMode = FilterMode::Box;

    // Memory limits
    size_t maxMemoryBytes = 0;  // 0 = unlimited
};

/**
 * Voxel data injector - builds SVO from raw voxel data.
 *
 * Observes AttributeRegistry for key attribute changes - when key changes,
 * spatial structure must be rebuilt.
 */
class VoxelInjector : public ::VoxelData::IAttributeRegistryObserver {
public:
    VoxelInjector() = default;
    explicit VoxelInjector(::VoxelData::AttributeRegistry* registry)
        : m_attributeRegistry(registry) {
        if (m_attributeRegistry) {
            m_attributeRegistry->addObserver(this);
        }
    }

    // Legacy constructor for backwards compatibility (deprecated)
    explicit VoxelInjector(BrickStorage<DefaultLeafData>* brickStorage)
        : m_brickStorage(brickStorage) {}

    ~VoxelInjector() {
        if (m_attributeRegistry) {
            m_attributeRegistry->removeObserver(this);
        }
    }

    // IAttributeRegistryObserver implementation
    void onKeyChanged(const std::string& oldKey, const std::string& newKey) override;
    void onAttributeAdded(const std::string& name, ::VoxelData::AttributeType type) override;
    void onAttributeRemoved(const std::string& name) override;

    /**
     * Inject sparse voxel data.
     * Most efficient for sparse data (individual voxels, particle systems).
     */
    std::unique_ptr<ISVOStructure> inject(
        const SparseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Inject dense voxel grid.
     * Use for volumetric data (medical scans, fluid sim, fog).
     */
    std::unique_ptr<ISVOStructure> inject(
        const DenseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Inject procedural voxels via sampler.
     * Samples on-demand during octree construction.
     * Perfect for infinite terrain, noise, SDFs.
     */
    std::unique_ptr<ISVOStructure> inject(
        const IVoxelSampler& sampler,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Merge voxel data into existing SVO structure.
     * Use for dynamic content updates, scene composition.
     */
    bool merge(
        ISVOStructure& target,
        const SparseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    bool merge(
        ISVOStructure& target,
        const IVoxelSampler& sampler,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * BOTTOM-UP ADDITIVE API - Insert single voxel at world position.
     *
     * This is the core additive operation:
     * 1. Computes brick/leaf coordinates from world position
     * 2. Creates brick if doesn't exist (thread-safe)
     * 3. Inserts voxel into brick
     * 4. Propagates "has child" flags up tree (idempotent)
     *
     * THREAD-SAFE: Multiple threads can call concurrently.
     * The operation is idempotent - inserting same voxel twice is safe.
     *
     * @param svo Target SVO structure (must be LaineKarrasOctree)
     * @param position World-space position
     * @param data Voxel appearance data
     * @param config Configuration (brick depth, LOD settings)
     * @return true if voxel inserted/updated, false if out of bounds
     */
    bool insertVoxel(
        ISVOStructure& svo,
        const glm::vec3& position,
        const VoxelData& data,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Compact octree into ESVO format after additive insertions.
     * Call this after using insertVoxel() to reorganize descriptors.
     *
     * @param svo Target SVO structure
     * @return true on success
     */
    bool compactToESVOFormat(ISVOStructure& svo);

    /**
     * Batch insert multiple voxels (parallel).
     * More efficient than individual insertVoxel() calls.
     *
     * @param svo Target SVO structure
     * @param voxels List of voxels to insert
     * @param config Configuration
     * @return Number of voxels successfully inserted
     */
    size_t insertVoxelsBatch(
        ISVOStructure& svo,
        const std::vector<VoxelData>& voxels,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Set progress callback.
     */
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;
    void setProgressCallback(ProgressCallback callback) {
        m_progressCallback = std::move(callback);
    }

    /**
     * Get injection statistics.
     */
    struct Stats {
        size_t voxelsProcessed = 0;
        size_t leavesCreated = 0;
        size_t emptyVoxelsCulled = 0;
        float buildTimeSeconds = 0.0f;
        size_t memoryUsed = 0;
    };

    const Stats& getLastStats() const { return m_stats; }

private:
    ProgressCallback m_progressCallback;
    Stats m_stats;
    ::VoxelData::AttributeRegistry* m_attributeRegistry = nullptr;  // Non-owning pointer
    BrickStorage<DefaultLeafData>* m_brickStorage = nullptr;        // Non-owning pointer (legacy, deprecated)

    // Maps parent descriptor index → [octant 0-7] → child descriptor index
    // Used during additive insertion to track which child octant leads to which descriptor
    // Cleared after each compactToESVOFormat() call
    std::unordered_map<uint32_t, std::array<uint32_t, 8>> m_childMapping;

    // Maps descriptor index to brick ID for additive insertion
    // Used to track which descriptors have bricks during insertVoxel
    std::unordered_map<uint32_t, uint32_t> m_descriptorToBrickID;

    // Maps spatial location (quantized) to brick ID for reusing bricks
    // Key is Morton code of brick's min corner at brick resolution
    std::unordered_map<uint64_t, uint32_t> m_spatialToBrickID;

    // Implementation helpers
    struct BuildContext;
    std::unique_ptr<ISVOStructure> buildFromSampler(
        const IVoxelSampler& sampler,
        const glm::vec3& min,
        const glm::vec3& max,
        const InjectionConfig& config);

    // Brick management helpers - unified for all insertion paths
    struct BrickAllocation {
        uint32_t brickID;
        bool hasSolidVoxels;
        VoxelData firstSolidVoxel;  // For node attributes
    };

    // Find existing brick or allocate new one for the given position
    // Returns brick ID and whether it's newly allocated
    std::pair<uint32_t, bool> findOrAllocateBrick(
        const glm::vec3& worldCenter,
        float worldSize,
        const InjectionConfig& config);

    // Populate a brick with voxel data
    // Can be used for both new and existing bricks
    BrickAllocation populateBrick(
        uint32_t brickID,
        const glm::vec3& worldCenter,
        float worldSize,
        const IVoxelSampler* sampler,  // For procedural sampling
        const VoxelData* singleVoxel,  // For additive insertion
        const InjectionConfig& config,
        bool isNewBrick);  // If false, updates existing brick

    // Legacy function - allocates and populates in one step
    BrickAllocation allocateAndPopulateBrick(
        const glm::vec3& worldCenter,
        float worldSize,
        const IVoxelSampler* sampler,
        const VoxelData* singleVoxel,
        const InjectionConfig& config);

    // Check if a voxel should terminate at brick depth
    bool shouldCreateBrick(int level, const InjectionConfig& config) const;

    // Add brick reference to octree during compaction
    void addBrickReferenceToOctree(Octree* octree, uint32_t brickID, uint32_t brickDepth);
};

/**
 * STREAMING VOXEL INJECTION QUEUE
 *
 * Thread-safe, async voxel insertion with frame-coherent snapshots.
 *
 * Use case: Dynamic world updates (destruction, terrain editing, particle effects)
 * that need to be processed in background while renderer samples octree each frame.
 *
 * Architecture:
 * - Producer thread(s): Call enqueue() to register voxel insertions
 * - Worker thread pool: Process queue in background using TBB
 * - Render thread: Call getSnapshot() each frame for safe read-only access
 *
 * Thread safety:
 * - enqueue(): Lock-free ring buffer (multiple producers)
 * - process(): Background thread pool with atomic validMask updates
 * - getSnapshot(): Copy-on-write or double-buffering for frame coherence
 *
 * Example:
 *   VoxelInjectionQueue queue(octree, workerThreads=8);
 *   queue.start();
 *
 *   // Game thread: Enqueue destruction debris
 *   for (auto& debris : explosion.getDebris()) {
 *       queue.enqueue(debris.position, debris.voxelData);
 *   }
 *
 *   // Render thread: Safe snapshot each frame
 *   while (rendering) {
 *       const ISVOStructure* snapshot = queue.getSnapshot();
 *       raytracer.render(snapshot);
 *   }
 *
 *   queue.stop(); // Flush remaining voxels
 */
class VoxelInjectionQueue {
public:
    struct Config {
        size_t maxQueueSize = 65536;      // Max pending voxels before blocking
        size_t batchSize = 256;            // Process this many voxels per batch
        size_t numWorkerThreads = 8;      // Background worker threads
        bool enableSnapshots = true;       // Enable frame-safe snapshots (adds memory overhead)
        InjectionConfig injectionConfig;   // Config for insertVoxel()
    };

    /**
     * Create streaming injection queue for target octree.
     * Does NOT take ownership of octree.
     */
    explicit VoxelInjectionQueue(ISVOStructure* targetOctree, const Config& config = Config{});
    ~VoxelInjectionQueue();

    // Disable copy (thread synchronization state)
    VoxelInjectionQueue(const VoxelInjectionQueue&) = delete;
    VoxelInjectionQueue& operator=(const VoxelInjectionQueue&) = delete;

    /**
     * Start background processing.
     * Spawns worker threads that process enqueued voxels.
     */
    void start();

    /**
     * Stop background processing and flush queue.
     * Blocks until all pending voxels are processed.
     */
    void stop();

    /**
     * Enqueue single voxel for async insertion.
     * Thread-safe - can be called from multiple threads.
     * Returns false if queue is full.
     */
    bool enqueue(const glm::vec3& position, const VoxelData& data);

    /**
     * Enqueue batch of voxels.
     * More efficient than individual enqueue() calls.
     */
    size_t enqueueBatch(const std::vector<VoxelData>& voxels);

    /**
     * Get frame-coherent snapshot for safe rendering.
     * Returns pointer valid for current frame only.
     * Next getSnapshot() may invalidate previous pointer.
     *
     * Thread-safe to call concurrently with enqueue().
     * NOT thread-safe to call from multiple render threads.
     */
    const ISVOStructure* getSnapshot();

    /**
     * Get current queue statistics.
     */
    struct Stats {
        size_t pendingVoxels;      // Voxels waiting in queue
        size_t processedVoxels;    // Total voxels inserted
        size_t failedInsertions;   // Out-of-bounds or errors
        float avgProcessTimeMs;    // Average batch process time
        bool isProcessing;         // Background threads active
    };
    Stats getStats() const;

    /**
     * Manually flush queue (blocks until empty).
     * Useful for synchronization points (e.g., end of frame).
     */
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * Utility: Convert mesh to voxels.
 * Useful for hybrid pipelines.
 */
class MeshVoxelizer {
public:
    struct Params {
        int resolution = 256;        // Voxel grid resolution
        bool generateNormals = true; // Compute normals from geometry
        bool generateAO = false;     // Compute ambient occlusion
        int aoSamples = 32;          // AO ray samples
    };

    /**
     * Voxelize mesh into sparse voxels.
     */
    static SparseVoxelInput voxelize(
        const ISVOBuilder::InputGeometry& mesh,
        const Params& params = Params{});

    /**
     * Voxelize mesh into dense grid.
     */
    static DenseVoxelInput voxelizeDense(
        const ISVOBuilder::InputGeometry& mesh,
        const Params& params = Params{});
};

/**
 * Utility: Common SDF primitives for testing/demos.
 */
namespace SDF {
    float sphere(const glm::vec3& p, float radius);
    float box(const glm::vec3& p, const glm::vec3& size);
    float torus(const glm::vec3& p, float majorRadius, float minorRadius);
    float cylinder(const glm::vec3& p, float radius, float height);

    // Operations
    float unionOp(float d1, float d2);
    float subtraction(float d1, float d2);
    float intersection(float d1, float d2);
    float smoothUnion(float d1, float d2, float k);
}

} // namespace SVO
