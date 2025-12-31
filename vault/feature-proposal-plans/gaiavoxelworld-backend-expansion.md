---
tags: [feature, proposal, architecture, voxel, backend]
created: 2025-12-31
status: proposal
priority: critical
complexity: very-high
related: [GaiaVoxelWorld, timeline-execution-system]
---

# Feature Proposal: GaiaVoxelWorld Backend Expansion & Optimization

## Overview

**Objective:** Transform GaiaVoxelWorld from a basic ECS-backed sparse voxel storage system into a high-performance, production-grade voxel backend capable of handling massive datasets, out-of-core rendering, procedural generation, and multi-threaded streaming operations.

**Current State:** Basic sparse voxel storage with Morton code indexing, providing 11x memory reduction vs dense storage. Limited to in-core data, single-threaded creation, no save/load, single transform space, and materialized-only data.

**Target State:** Industrial-strength voxel backend with:
- High-capacity procedural generation (100M+ voxels/second)
- Persistent storage with streaming save/load
- Multi-space architecture with independent transform hierarchies
- Out-of-core rendering support for datasets exceeding VRAM
- **GigaVoxels GPU-driven usage-based caching** (research-backed, 90%+ cache hit rate)
- Parallel queue-based operations for continuous data streams
- High-throughput query systems for real-time manipulation
- Implicit data representation (generators instead of materialized voxels)
- Industry-standard optimizations (compression, LOD, virtual texturing)

**Phase:** Proposal - Architecture Design & Implementation Planning

---

## 1. Current Architecture Analysis

### 1.1 Existing System Capabilities

**Strengths:**
- ECS-backed storage via Gaia-ECS (SoA, cache-friendly)
- Morton code spatial indexing for O(1) position lookups
- 11x memory reduction vs dense storage for sparse scenes
- Component-based architecture (flexible attributes per voxel)
- Zero-copy brick queries (512 voxels via `getBrickEntitiesInto()`)
- Spatial chunk organization with parent-child relationships

**Core Files:**
- `libraries/GaiaVoxelWorld/include/GaiaVoxelWorld.h:684` - Main interface
- `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp:300` - Implementation
- `libraries/Core/include/MortonEncoding.h` - Morton key generation
- `libraries/GaiaArchetypes/include/GaiaArchetypes.h` - Component definitions

### 1.2 Current Limitations

**1. Inefficient Data Generation:**
```cpp
// Current: Single-threaded voxel creation
for (int i = 0; i < 1000000; ++i) {
    world.createVoxel({
        glm::vec3(x, y, z),
        {Density{1.0f}, Color{red}}
    });
    // Creates entity + updates Morton index + invalidates cache
    // ~10K voxels/second - UNACCEPTABLE for procedural generation
}
```

**Problem:** No batch optimization, repeated cache invalidation, serial entity creation.

**2. No Persistence:**
```cpp
// Current: No save/load API
// Data lost on application shutdown
// Cannot stream large datasets from disk
```

**Problem:** Cannot handle datasets larger than RAM, no asset pipeline integration.

**3. Single Transform Space:**
```cpp
// Current: All voxels share global world space
// Cannot rotate/scale voxel objects independently
// Every voxel baked into final world-space position
```

**Problem:** No instancing, no dynamic objects, no transform hierarchies.

**4. In-Core Rendering Only:**
```cpp
// Current: All voxel data must fit in VRAM
// No paging, no streaming, no virtual texturing
// 4GB VRAM = ~40M voxels max (at 100 bytes/voxel)
```

**Problem:** Cannot render massive worlds (100M+ voxels).

**5. Single-Threaded Operations:**
```cpp
// Current: createVoxelsBatch() creates entities serially
// No parallel query processing
// No multi-threaded injection queues
```

**Problem:** Wastes multi-core CPUs, slow for real-time streaming.

**6. No Implicit Data:**
```cpp
// Current: All voxels materialized as entities
// 1M voxel sphere = 1M entities, even if procedurally defined
// Formula: sphere(center, radius) → 1M ECS entities
```

**Problem:** Massive memory waste for procedural/parametric geometry.

**7. Limited Query Performance:**
```cpp
// Current: queryRegion() iterates ALL voxels linearly
// O(N) spatial queries, no spatial acceleration
auto voxels = world.queryRegion(min, max); // Scans entire ECS
```

**Problem:** Slow for large datasets, no BVH/octree acceleration.

---

## 2. Proposed Features

### 2.1 High-Capacity Procedural Generation

**Concept:** Batch-optimized voxel generation with parallel entity creation, deferred cache invalidation, and SIMD-accelerated Morton encoding.

**Current Performance:**
```
Single-threaded: 10,000 voxels/second
Target: 100,000,000 voxels/second (10,000x improvement)
```

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelGenerator.h

class VoxelGenerator {
public:
    /**
     * Generate voxels in parallel using thread pool.
     *
     * @param generatorFunc User function: (glm::ivec3 pos) -> VoxelCreationRequest
     * @param bounds AABB to generate voxels within
     * @param threadCount Number of worker threads (default: hardware_concurrency)
     * @return Count of voxels generated
     */
    uint64_t generateParallel(
        std::function<std::optional<VoxelCreationRequest>(glm::ivec3)> generatorFunc,
        const AABB& bounds,
        uint32_t threadCount = 0);

    /**
     * Batch Morton encoding with SIMD (8 positions per AVX2 instruction).
     * 16x faster than scalar Morton encoding.
     */
    void encodeMortonBatch(
        std::span<const glm::ivec3> positions,
        std::span<MortonKey> outKeys);

    /**
     * Deferred entity creation: accumulate in buffer, flush when full.
     * Reduces ECS archetype thrashing and cache invalidation overhead.
     */
    struct EntityBatch {
        static constexpr size_t BATCH_SIZE = 4096;
        std::array<VoxelCreationRequest, BATCH_SIZE> requests;
        size_t count = 0;

        void add(const VoxelCreationRequest& req);
        void flush(GaiaVoxelWorld& world);
    };

private:
    ThreadPool threadPool_;
    std::vector<EntityBatch> perThreadBatches_;
};

// Example Usage: Generate 10M voxel sphere
VoxelGenerator generator;
generator.generateParallel(
    [](glm::ivec3 pos) -> std::optional<VoxelCreationRequest> {
        float dist = glm::length(glm::vec3(pos));
        if (dist > 100.0f) return std::nullopt; // Outside sphere

        return VoxelCreationRequest{
            glm::vec3(pos),
            {Density{1.0f}, Color{glm::vec3(1, 0, 0)}}
        };
    },
    AABB{glm::vec3(-100), glm::vec3(100)},
    16 // 16 threads
);

// Performance: 10M voxels in ~0.1 seconds (100M voxels/sec)
```

**Benefits:**
- **100M voxels/second** generation (vs 10K current)
- **Linear scaling** with CPU cores (16 cores = 16x speedup)
- **Zero cache thrashing** (deferred invalidation)
- **SIMD acceleration** (16x faster Morton encoding)

**Implementation Complexity:** **High**
- Thread pool integration
- SIMD Morton encoding (AVX2/NEON)
- Lock-free entity batch queues
- Deferred cache invalidation
- Estimated effort: 3-4 weeks

**Cost:**
- Memory: ~64 KB per thread for batching
- Performance: 10,000x speedup for bulk generation

---

### 2.2 Persistent Storage (Save/Load)

**Concept:** Serialize voxel world to disk with compression, streaming load, and incremental save.

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelPersistence.h

struct VoxelArchiveFormat {
    // Header
    uint32_t magic = 0x56584C57; // "VXLW"
    uint32_t version = 1;
    uint64_t voxelCount;
    uint64_t chunkCount;
    AABB worldBounds;

    // Compression
    enum class CompressionMode : uint8_t {
        None,
        LZ4,     // Fast: 500 MB/s compress, 3 GB/s decompress
        ZSTD,    // Balanced: 400 MB/s compress, 1 GB/s decompress
        ZSTD_Max // Best ratio: 50 MB/s compress, 1 GB/s decompress
    };
    CompressionMode compression = CompressionMode::LZ4;

    // Component layout (dynamic schema)
    std::vector<ComponentSchema> componentTypes;
};

class VoxelPersistence {
public:
    /**
     * Save entire world to file.
     *
     * @param world GaiaVoxelWorld to save
     * @param filepath Output file path
     * @param compression Compression mode (default: LZ4)
     * @return Success flag
     */
    bool saveWorld(
        const GaiaVoxelWorld& world,
        const std::filesystem::path& filepath,
        CompressionMode compression = CompressionMode::LZ4);

    /**
     * Load entire world from file.
     *
     * @param filepath Input file path
     * @param outWorld GaiaVoxelWorld to load into (will be cleared)
     * @return Success flag
     */
    bool loadWorld(
        const std::filesystem::path& filepath,
        GaiaVoxelWorld& outWorld);

    /**
     * Streaming load: Load chunks on-demand as camera moves.
     * Unloads distant chunks to maintain memory budget.
     *
     * @param filepath Archive file
     * @param cameraPos Current camera position
     * @param loadRadius Radius around camera to load chunks
     * @param memoryBudgetMB Maximum memory to use for loaded chunks
     */
    void streamingLoad(
        const std::filesystem::path& filepath,
        const glm::vec3& cameraPos,
        float loadRadius,
        uint64_t memoryBudgetMB);

    /**
     * Incremental save: Only save modified chunks since last save.
     * Fast for iterative editing workflows.
     */
    bool saveModifiedChunks(
        const GaiaVoxelWorld& world,
        const std::filesystem::path& filepath,
        const std::unordered_set<ChunkID>& modifiedChunks);

    /**
     * Archive statistics (query without loading entire file).
     */
    struct ArchiveStats {
        uint64_t voxelCount;
        uint64_t compressedSizeBytes;
        uint64_t uncompressedSizeBytes;
        float compressionRatio;
        AABB worldBounds;
    };
    ArchiveStats queryArchiveStats(const std::filesystem::path& filepath);

private:
    // Chunk-based file layout for streaming
    struct ChunkTOC {
        uint64_t chunkID;
        uint64_t fileOffset;
        uint64_t compressedSize;
        uint64_t uncompressedSize;
        AABB bounds;
    };

    std::vector<ChunkTOC> tableOfContents_;
    MemoryMappedFile archiveFile_;
    LRUCache<ChunkID, ChunkData> chunkCache_;
};
```

**File Format:**

```
┌─────────────────────────────────────────────────────┐
│ Header (256 bytes)                                  │
│ - Magic: "VXLW"                                     │
│ - Version: 1                                        │
│ - Voxel count: 10,000,000                           │
│ - Chunk count: 19,531 (512 voxels/chunk)            │
│ - Compression: LZ4                                  │
│ - World bounds: AABB                                │
├─────────────────────────────────────────────────────┤
│ Component Schema (variable)                         │
│ - Component 0: Density (float, 4 bytes)             │
│ - Component 1: Color (vec3, 12 bytes)               │
│ - Component 2: Normal (vec3, 12 bytes)              │
│ - Component 3: Material (uint32, 4 bytes)           │
├─────────────────────────────────────────────────────┤
│ Chunk Table of Contents (19,531 × 48 bytes)         │
│ - Chunk 0: Offset=1MB, Size=4KB, Bounds=AABB(...)   │
│ - Chunk 1: Offset=1MB+4KB, Size=4KB, Bounds=...     │
│ - ...                                               │
├─────────────────────────────────────────────────────┤
│ Compressed Chunk Data (bulk of file)                │
│ - Chunk 0: LZ4(Morton keys + component SoA)         │
│ - Chunk 1: LZ4(Morton keys + component SoA)         │
│ - ...                                               │
└─────────────────────────────────────────────────────┘

Total size: ~300 MB (10M voxels, 3:1 compression)
Load time: ~0.5 seconds (streaming), ~2 seconds (full)
```

**Benefits:**
- **Asset pipeline integration** (save/load voxel worlds)
- **Streaming load** (100M+ voxel datasets)
- **3:1 compression ratio** (LZ4 on voxel SoA data)
- **Incremental save** (fast iteration for editing)
- **Memory-mapped files** (zero-copy load)

**Implementation Complexity:** **Medium-High**
- Chunk-based serialization
- LZ4/ZSTD compression integration
- Streaming loader with LRU cache
- Memory-mapped file I/O
- Estimated effort: 3-4 weeks

**Cost:**
- Disk: ~30 bytes/voxel compressed (vs ~100 bytes in-memory)
- Load time: ~500 MB/s (LZ4 decompression)

---

### 2.3 Multi-Space Transform Hierarchies

**Concept:** Support multiple independent voxel spaces with per-object transforms, enabling rotation, translation, and scale of voxel objects while maintaining consistent resolution.

**Current Problem:**
```cpp
// Current: All voxels in global world space
// Cannot rotate a voxel car without re-baking all voxels
// Cannot instance a voxel building at multiple locations
```

**Proposed Solution:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelSpace.h

/**
 * VoxelSpace: Independent voxel storage with local coordinate frame.
 * Multiple spaces can coexist, each with own transform.
 */
class VoxelSpace {
public:
    using SpaceID = uint32_t;

    /**
     * Create new voxel space with transform.
     *
     * @param transform Local-to-world transform matrix
     * @param resolution Voxels per world-space unit (LOD control)
     * @return Space ID for referencing this space
     */
    SpaceID createSpace(
        const glm::mat4& transform,
        float resolution = 1.0f);

    /**
     * Set space transform (rotation, translation, scale).
     * Updates all voxels in this space without re-baking.
     */
    void setSpaceTransform(SpaceID space, const glm::mat4& transform);

    /**
     * Get space transform.
     */
    glm::mat4 getSpaceTransform(SpaceID space) const;

    /**
     * Create voxel in specific space (local coordinates).
     */
    EntityID createVoxelInSpace(
        SpaceID space,
        const glm::vec3& localPosition,
        const VoxelCreationRequest& request);

    /**
     * Query voxels across all spaces in world-space region.
     * Transforms each space's local voxels to world space for query.
     */
    std::vector<EntityID> queryWorldRegion(
        const AABB& worldBounds) const;

    /**
     * Resolution scaling: Dynamically adjust voxel density.
     * scale=2.0 → half density (every other voxel)
     * scale=0.5 → double density (interpolate new voxels)
     *
     * NOTE: Not literal scale (would blur voxels), but density adjustment.
     */
    void setSpaceResolution(SpaceID space, float scale);

private:
    struct SpaceData {
        SpaceID id;
        glm::mat4 localToWorld;
        float resolution;
        GaiaVoxelWorld localVoxels; // Independent voxel storage
        std::vector<EntityID> voxelEntities;
    };

    std::unordered_map<SpaceID, SpaceData> spaces_;
    SpaceID nextSpaceID_ = 1;
};

// Example: Rotating voxel car
VoxelSpace multiSpace;

// Create car space
SpaceID carSpace = multiSpace.createSpace(
    glm::translate(glm::vec3(10, 0, 0)), // Position at (10, 0, 0)
    1.0f // 1 voxel per unit
);

// Build car in local space (only once!)
for (int x = -5; x < 5; ++x) {
    for (int y = 0; y < 3; ++y) {
        for (int z = -2; z < 2; ++z) {
            multiSpace.createVoxelInSpace(carSpace, glm::vec3(x, y, z), carVoxel);
        }
    }
}

// Rotate car (zero re-baking!)
float angle = 0.0f;
while (running) {
    angle += 0.01f;
    multiSpace.setSpaceTransform(carSpace,
        glm::translate(glm::vec3(10, 0, 0)) *
        glm::rotate(angle, glm::vec3(0, 1, 0))
    );
    // Render with new transform - voxel data unchanged!
}
```

**Benefits:**
- **Dynamic objects** (cars, characters, doors)
- **Instancing** (place same voxel building 100 times with different transforms)
- **Zero re-baking** (transform updates in <1ms)
- **Resolution control** (LOD via voxel density scaling)
- **Hierarchical transforms** (wheel space → car space → world space)

**Implementation Complexity:** **Very High**
- Multi-space ECS architecture
- Transform hierarchies (parent-child relations)
- World-space query across transformed spaces
- Resolution scaling (voxel interpolation/decimation)
- GPU shader integration (per-instance transforms)
- Estimated effort: 6-8 weeks

**Cost:**
- Memory: ~256 bytes per space + voxel data
- Performance: <1ms to update transform, no voxel re-baking

---

### 2.4 Out-of-Core Rendering Support

**Concept:** Render voxel datasets larger than VRAM by streaming bricks from system RAM or disk on-demand.

**Current Problem:**
```
4GB VRAM = ~40M voxels max (100 bytes/voxel)
Target: 1 billion voxels (requires 100GB uncompressed)
```

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/OutOfCoreRenderer.h

class OutOfCoreRenderer {
public:
    /**
     * Virtual brick pool: Fixed VRAM allocation for streaming bricks.
     * Bricks paged in/out based on camera frustum and distance.
     */
    struct VirtualBrickPool {
        VkBuffer brickBuffer;          // GPU brick data (e.g., 1GB)
        VkBuffer indirectionTable;     // Morton → Brick slot mapping
        uint32_t brickCapacity;        // Max bricks in VRAM (e.g., 2048)
        uint32_t brickSize;            // Brick side length (e.g., 8³ = 512 voxels)
    };

    /**
     * Initialize out-of-core renderer.
     *
     * @param vramBudgetMB VRAM budget for brick pool (e.g., 1024 MB)
     * @param brickSize Brick side length (default: 8 for 8³)
     */
    void initialize(uint64_t vramBudgetMB, uint32_t brickSize = 8);

    /**
     * Update brick pool: Page in visible bricks, evict distant bricks.
     *
     * @param world GaiaVoxelWorld source
     * @param cameraFrustum Camera frustum for visibility
     * @param cameraPos Camera position for distance sorting
     * @param maxBricksPerFrame Streaming budget (e.g., 64 bricks/frame)
     */
    void updateBrickPool(
        const GaiaVoxelWorld& world,
        const Frustum& cameraFrustum,
        const glm::vec3& cameraPos,
        uint32_t maxBricksPerFrame = 64);

    /**
     * Render voxels using virtual brick pool.
     * Shader uses indirection table to map Morton codes to brick slots.
     */
    void render(VkCommandBuffer cmd, const RenderParams& params);

private:
    // Brick paging priority queue (sorted by distance to camera)
    struct BrickRequest {
        MortonCode64 brickBaseMorton;
        float distanceToCamera;
        uint32_t priority; // 0 = high, 1 = medium, 2 = low
    };

    std::priority_queue<BrickRequest> pageInQueue_;

    // LRU cache for brick eviction
    LRUCache<MortonCode64, uint32_t> brickSlotCache_; // Morton → VRAM slot

    // Async brick loading (background thread)
    ThreadPool loaderThreads_;
    std::queue<BrickRequest> asyncLoadQueue_;
};

// Shader: Virtual brick access
// GLSL compute shader
layout(std430, set = 0, binding = 0) buffer IndirectionTable {
    uint mortonToBrickSlot[]; // Morton key → brick slot in VRAM
};

layout(std430, set = 0, binding = 1) buffer BrickPool {
    VoxelData brickData[]; // Flat array of brick data (2048 bricks × 512 voxels)
};

void main() {
    ivec3 worldPos = ...;
    uint64_t mortonKey = encodeMorton(worldPos);

    // Indirection lookup
    uint brickSlot = mortonToBrickSlot[mortonKey >> 9]; // Brick index (divide by 512)
    uint localOffset = mortonKey & 0x1FF; // Voxel within brick (mod 512)

    // Access voxel
    VoxelData voxel = brickData[brickSlot * 512 + localOffset];
}
```

**Benefits:**
- **1 billion+ voxels** (100x larger than VRAM)
- **Constant VRAM usage** (1GB for brick pool)
- **Seamless streaming** (64 bricks/frame = 32K voxels/frame)
- **LOD-aware paging** (distant bricks at lower LOD)

**Implementation Complexity:** **Very High**
- Virtual brick pool management
- Indirection table (Morton → VRAM slot)
- LRU cache for brick eviction
- Async brick loading (background threads)
- GPU shader integration (indirection lookup)
- Frustum-based brick culling
- Estimated effort: 8-10 weeks

**Cost:**
- VRAM: 1GB for brick pool (configurable)
- CPU: ~5ms/frame for brick paging (64 bricks × 80μs)
- Disk: ~500 MB/s streaming (if loading from disk)

---

### 2.5 Parallel Queue-Based Operations

**Concept:** Multi-threaded voxel injection queues with continuous data streams and multi-frame amortization.

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelInjectionQueue.h

class VoxelInjectionQueue {
public:
    /**
     * Thread-safe voxel injection queue.
     * Multiple threads can enqueue, single consumer processes.
     */
    void enqueue(const VoxelCreationRequest& request);
    void enqueueBatch(std::span<const VoxelCreationRequest> requests);

    /**
     * Process queued voxels with budget awareness.
     *
     * @param world GaiaVoxelWorld to inject into
     * @param maxVoxelsPerFrame Budget (e.g., 10,000 voxels/frame)
     * @param maxTimeMs Time budget (e.g., 2ms)
     * @return Count of voxels processed
     */
    uint32_t processQueue(
        GaiaVoxelWorld& world,
        uint32_t maxVoxelsPerFrame,
        float maxTimeMs);

    /**
     * Multi-frame amortization: Spread large operations across frames.
     * Example: 1M voxel sphere → 100 frames × 10K voxels
     */
    void setAmortizationMode(bool enabled, uint32_t voxelsPerFrame);

    /**
     * Streaming input: Feed voxels from external source (network, disk).
     */
    void connectStream(std::shared_ptr<VoxelStream> stream);

    /**
     * Query queue stats.
     */
    struct QueueStats {
        uint64_t queuedVoxels;
        uint64_t processedVoxels;
        float avgProcessTimeMs;
        uint64_t memoryUsageBytes;
    };
    QueueStats getStats() const;

private:
    // Lock-free MPSC queue (multiple producers, single consumer)
    MPSCQueue<VoxelCreationRequest> queue_;

    // Batch accumulator (reduce world.createVoxel() overhead)
    static constexpr size_t PROCESS_BATCH_SIZE = 4096;
    std::array<VoxelCreationRequest, PROCESS_BATCH_SIZE> processBatch_;

    // Amortization state
    bool amortizationEnabled_ = false;
    uint32_t voxelsPerFrameBudget_ = 10000;

    // Stats tracking
    std::atomic<uint64_t> totalEnqueued_{0};
    std::atomic<uint64_t> totalProcessed_{0};
    MovingAverage<float, 60> avgProcessTime_; // 60-frame rolling average
};

// Example: Multi-threaded procedural generation
VoxelInjectionQueue injectionQueue;

// Producer threads (generate voxels)
std::vector<std::thread> generators;
for (int i = 0; i < 16; ++i) {
    generators.emplace_back([&, i]() {
        for (int x = i * 100; x < (i + 1) * 100; ++x) {
            for (int y = 0; y < 100; ++y) {
                for (int z = 0; z < 100; ++z) {
                    injectionQueue.enqueue({
                        glm::vec3(x, y, z),
                        {Density{1.0f}, Color{randomColor()}}
                    });
                }
            }
        }
    });
}

// Consumer (main thread, process 10K voxels/frame)
while (running) {
    injectionQueue.processQueue(world, 10000, 2.0f); // 10K voxels, 2ms budget
    renderFrame();
}

// Result: 1.6M voxels created across 160 frames (smooth 60 FPS)
```

**Benefits:**
- **Multi-threaded generation** (16 threads = 16x speedup)
- **Smooth frame times** (budget-aware processing)
- **Continuous streaming** (network/disk data streams)
- **Lock-free queues** (zero contention)

**Implementation Complexity:** **Medium**
- MPSC lock-free queue
- Budget-aware processing
- Multi-frame amortization
- Stream integration
- Estimated effort: 2-3 weeks

**Cost:**
- Memory: ~64 bytes per queued voxel + queue overhead
- Performance: <2ms/frame for 10K voxels

---

### 2.6 High-Throughput Query Systems

**Concept:** Accelerated spatial queries using BVH, octree, or GPU-based parallel queries.

**Current Problem:**
```cpp
// O(N) linear scan - 10M voxels = 100ms query
auto voxels = world.queryRegion(min, max);
```

**Proposed Solution:**

```cpp
// New file: GaiaVoxelWorld/include/SpatialAcceleration.h

class SpatialAccelerationStructure {
public:
    /**
     * Build BVH (Bounding Volume Hierarchy) for fast spatial queries.
     *
     * @param world GaiaVoxelWorld to build BVH for
     * @param maxLeafVoxels Max voxels per leaf node (default: 512)
     */
    void buildBVH(const GaiaVoxelWorld& world, uint32_t maxLeafVoxels = 512);

    /**
     * Fast spatial query using BVH traversal.
     *
     * @param bounds AABB query region
     * @return Voxel entities in region (O(log N + K), K = result count)
     */
    std::vector<EntityID> queryRegion(const AABB& bounds) const;

    /**
     * Ray-voxel intersection.
     *
     * @param ray Ray origin + direction
     * @param maxDistance Max ray distance
     * @return First voxel hit, or nullopt if no hit
     */
    std::optional<RaycastHit> raycast(
        const Ray& ray,
        float maxDistance = 1000.0f) const;

    /**
     * GPU-accelerated query: Parallel AABB test on GPU.
     * 1000x faster than CPU for large queries.
     *
     * @param bounds AABB query region
     * @param outBuffer GPU buffer to write results
     * @return Count of voxels in region
     */
    uint32_t queryRegionGPU(
        const AABB& bounds,
        VkBuffer outBuffer) const;

    /**
     * Incremental BVH update (after voxel modifications).
     * Faster than full rebuild for small changes.
     *
     * @param modifiedVoxels Voxels added/removed since last build
     */
    void updateBVH(const std::vector<EntityID>& modifiedVoxels);

private:
    struct BVHNode {
        AABB bounds;
        uint32_t leftChild;  // Index of left child (0 = leaf)
        uint32_t rightChild; // Index of right child
        std::vector<EntityID> voxels; // Leaf voxels (empty for interior nodes)
    };

    std::vector<BVHNode> bvhNodes_;
    uint32_t rootNodeIndex_ = 0;
};

// Performance Comparison:
// Query 1000x1000x1000 region in 10M voxel world
// - Linear scan:  100ms (O(N))
// - BVH query:    0.1ms (O(log N + K))
// - GPU query:    0.01ms (parallel AABB test)
// Speedup: 1000x - 10,000x
```

**Benefits:**
- **1000x faster queries** (BVH vs linear)
- **10,000x faster** (GPU vs linear)
- **Raycasting** (voxel picking, physics)
- **Incremental updates** (fast for editing)

**Implementation Complexity:** **High**
- BVH construction (SAH partitioning)
- BVH traversal (stack-based)
- GPU query kernels
- Incremental update (refit vs rebuild)
- Estimated effort: 4-5 weeks

**Cost:**
- Memory: ~48 bytes per BVH node (~N/512 nodes for N voxels)
- Build time: ~50ms for 10M voxels (one-time or incremental)

---

### 2.7 Implicit Data Representation

**Concept:** Store voxel generators instead of materialized voxels, generating data on-demand.

**Current Problem:**
```cpp
// 1M voxel sphere = 1M entities (100 MB)
// Formula: sphere(center, radius) → 1M explicit voxels
```

**Proposed Solution:**

```cpp
// New file: GaiaVoxelWorld/include/ImplicitVoxels.h

/**
 * ImplicitVoxelGenerator: Procedural voxel source.
 * Stores only generator parameters, not voxel data.
 */
class ImplicitVoxelGenerator {
public:
    enum class GeneratorType {
        Sphere,
        Box,
        Noise,
        SDF, // Signed Distance Field
        Custom
    };

    /**
     * Register implicit generator for a region.
     *
     * @param type Generator type
     * @param bounds Region to apply generator
     * @param params Generator-specific parameters
     * @return Generator ID for reference
     */
    uint32_t registerGenerator(
        GeneratorType type,
        const AABB& bounds,
        const GeneratorParams& params);

    /**
     * Evaluate generator at position (lazy materialization).
     *
     * @param position World position
     * @return Voxel data if position is inside any generator
     */
    std::optional<VoxelData> evaluate(const glm::vec3& position) const;

    /**
     * Materialize generator into explicit voxels.
     * Used when need to modify voxels (can't modify generator output).
     *
     * @param generatorID Generator to materialize
     * @param world GaiaVoxelWorld to materialize into
     */
    void materialize(uint32_t generatorID, GaiaVoxelWorld& world);

    /**
     * GPU evaluation: Evaluate generators in compute shader.
     * 1000x faster than CPU evaluation.
     */
    void evaluateGPU(
        const AABB& queryRegion,
        VkBuffer outVoxelBuffer) const;

private:
    struct Generator {
        uint32_t id;
        GeneratorType type;
        AABB bounds;

        // Generator params (union for different types)
        union {
            SphereParams sphere;
            NoiseParams noise;
            SDFParams sdf;
        } params;

        // Custom generator function (C++ lambda or shader)
        std::function<std::optional<VoxelData>(glm::vec3)> customFunc;
    };

    std::vector<Generator> generators_;
    uint32_t nextGeneratorID_ = 1;
};

// Example: Implicit 1M voxel sphere (only 48 bytes storage!)
ImplicitVoxelGenerator implicit;

implicit.registerGenerator(
    GeneratorType::Sphere,
    AABB{glm::vec3(-100), glm::vec3(100)}, // 200³ region
    SphereParams{
        .center = glm::vec3(0, 0, 0),
        .radius = 100.0f,
        .color = glm::vec3(1, 0, 0)
    }
);

// Query voxel at position (generates on-demand)
auto voxel = implicit.evaluate(glm::vec3(50, 0, 0));
// Result: Red voxel (inside sphere)

// Memory: 48 bytes (generator params)
// vs 100 MB (1M explicit voxels)
// Reduction: 2,000,000x!
```

**Benefits:**
- **2,000,000x memory reduction** (generator params vs explicit voxels)
- **Infinite resolution** (generate at any LOD on-demand)
- **Real-time modification** (change generator params, instant update)
- **GPU evaluation** (1000x faster than CPU)

**Implementation Complexity:** **Medium-High**
- Generator parameter encoding
- Lazy evaluation (check all generators for query)
- GPU shader integration (evaluate in compute)
- Materialization (convert implicit → explicit)
- Estimated effort: 3-4 weeks

**Cost:**
- Memory: ~48 bytes per generator (vs MB for explicit voxels)
- Evaluation: ~100ns per voxel (cached), ~10μs per voxel (uncached)

---

### 2.8 Fully Simulated Voxel World (Noita-Style Falling Sand)

**Concept:** Every voxel is actively simulated with physics, chemistry, and interactions—inspired by Noita's "every pixel simulated" approach, but extended to 3D with lossy optimizations for performance.

**Research Foundation:** Cellular automata, falling sand games (Powder Toy, Noita), lossy compression for simulation state.

**Key Challenge:** Bandwidth explosion in 3D.

**Bandwidth Reality Check:**

| Scenario | Voxel Count | Update Rate | Bandwidth | Feasible? |
|----------|-------------|-------------|-----------|-----------|
| Noita (2D) | 3.7M pixels | 60 FPS | 1.8 GB/s | ✅ Yes |
| Naive 512³ (all active) | 134M voxels | 60 FPS | **804 GB/s** | ❌ **NO** (16x over RAM bandwidth!) |
| Sparse 512³ (1% active) | 1.3M voxels | 60 FPS | 8 GB/s | ✅ Yes |
| Chunked (32³ active) | 32K voxels | 60 FPS | 192 MB/s | ✅ **Excellent** |

**Conclusion:** We MUST use sparse, lossy, chunked simulation to achieve Noita-style behavior in 3D.

---

#### 2.8.1 Lossy Simulation Architecture

**Core Idea:** Accept approximations to reduce computational load by 100-1000x.

**Lossy Optimizations:**

1. **Spatial Quantization (Coarsen distant chunks)**
   ```cpp
   // Near player: Full resolution (1 voxel = 1cm)
   // 10m away: 2x2x2 mega-voxels (8 voxels → 1 simulated unit)
   // 50m away: 4x4x4 mega-voxels (64 voxels → 1)
   // 100m+ away: Frozen (no simulation)

   float getSimulationResolution(float distanceToPlayer) {
       if (distanceToPlayer < 10.0f) return 1.0f;  // Full res
       if (distanceToPlayer < 50.0f) return 2.0f;  // Half res
       if (distanceToPlayer < 100.0f) return 4.0f; // Quarter res
       return 0.0f; // Frozen
   }
   ```

2. **Temporal Quantization (Update less frequently)**
   ```cpp
   // Near player: Every frame (60 FPS)
   // 10m away: Every 2 frames (30 FPS)
   // 50m away: Every 10 frames (6 FPS)
   // 100m+ away: Frozen

   int getUpdateFrequency(float distanceToPlayer) {
       if (distanceToPlayer < 10.0f) return 1;   // Every frame
       if (distanceToPlayer < 50.0f) return 2;   // 30 FPS
       if (distanceToPlayer < 100.0f) return 10; // 6 FPS
       return 0; // Frozen
   }
   ```

3. **State Compression (Reduce data per voxel)**
   ```cpp
   // Full state (for rendering): 100 bytes
   struct VoxelRenderState {
       glm::vec3 color;     // 12 bytes
       glm::vec3 normal;    // 12 bytes
       float density;       // 4 bytes
       uint32_t material;   // 4 bytes
       // ... etc (100 bytes total)
   };

   // Simulation state (lossy): 16 bytes
   struct VoxelSimState {
       uint16_t materialID;    // 2 bytes (65K material types)
       uint8_t temperature;    // 1 byte (0-255°C, lossy!)
       uint8_t pressure;       // 1 byte (lossy!)
       glm::vec3 velocity;     // 12 bytes (could quantize to 6 bytes)
       // Total: 16 bytes (6.25x compression!)
   };
   ```

4. **Activity-Based Sparse Simulation**
   ```cpp
   // Only simulate voxels that are "active"
   // Active = recently changed, has velocity, is falling, etc.

   struct ActiveVoxelSet {
       std::unordered_set<uint64_t> activeVoxels; // Morton keys

       void markActive(uint64_t mortonKey) {
           activeVoxels.insert(mortonKey);
       }

       void simulate() {
           // Only simulate active voxels!
           for (uint64_t key : activeVoxels) {
               simulateVoxel(key);

               // Mark as inactive if settled
               if (isSettled(key)) {
                   activeVoxels.erase(key);
               }
           }
       }
   };

   // Typical: 1% of voxels are active
   // 134M × 0.01 = 1.34M active voxels
   // Bandwidth: 1.34M × 16 bytes × 60 FPS = 1.3 GB/s ✅
   ```

5. **Aggregate Materials (Homogeneous regions)**
   ```cpp
   // Instead of simulating 1000 water voxels individually,
   // simulate them as a single "water blob" with mass

   struct MaterialBlob {
       uint32_t materialID;
       uint32_t voxelCount;
       glm::vec3 centerOfMass;
       glm::vec3 velocity;
       AABB bounds;
   };

   // 1000 voxels → 1 blob (1000x reduction!)
   ```

---

#### 2.8.2 Cellular Automata Rules (Noita-Style)

**Physics Rules (Falling Sand):**

```cpp
// New file: GaiaVoxelWorld/include/VoxelPhysics.h

enum class MaterialPhysics {
    Static,      // Stone, metal (doesn't move)
    Powder,      // Sand, gravel (falls, piles up)
    Liquid,      // Water, lava (flows, settles flat)
    Gas,         // Steam, smoke (rises)
    Plasma       // Fire (consumes fuel, rises)
};

struct MaterialProperties {
    MaterialPhysics physics;
    float density;           // kg/m³
    float friction;          // 0-1
    float viscosity;         // For liquids
    float flammability;      // 0-1
    float meltingPoint;      // °C
    uint16_t materialID;
};

class VoxelPhysicsSimulator {
public:
    /**
     * Simulate single voxel (cellular automaton rules).
     * Called for each active voxel every frame.
     */
    void simulateVoxel(uint64_t mortonKey, VoxelSimState& state) {
        auto props = getMaterialProperties(state.materialID);

        switch (props.physics) {
            case MaterialPhysics::Powder:
                simulatePowder(mortonKey, state, props);
                break;
            case MaterialPhysics::Liquid:
                simulateLiquid(mortonKey, state, props);
                break;
            case MaterialPhysics::Gas:
                simulateGas(mortonKey, state, props);
                break;
            // ... etc
        }
    }

private:
    void simulatePowder(uint64_t key, VoxelSimState& state,
                        const MaterialProperties& props) {
        // Check voxel below
        uint64_t belowKey = getMortonBelow(key);

        if (isEmpty(belowKey)) {
            // Fall straight down
            moveVoxel(key, belowKey);
        } else if (isDenser(belowKey, props.density)) {
            // Try diagonal fall (Noita-style)
            uint64_t diagLeft = getMortonBelowLeft(key);
            uint64_t diagRight = getMortonBelowRight(key);

            if (isEmpty(diagLeft)) {
                moveVoxel(key, diagLeft);
            } else if (isEmpty(diagRight)) {
                moveVoxel(key, diagRight);
            }
            // Else: settled (mark as inactive)
        }
    }

    void simulateLiquid(uint64_t key, VoxelSimState& state,
                        const MaterialProperties& props) {
        // Liquids fall AND spread horizontally

        // 1. Try to fall
        uint64_t belowKey = getMortonBelow(key);
        if (isEmpty(belowKey) || isLighterLiquid(belowKey, props)) {
            moveVoxel(key, belowKey);
            return;
        }

        // 2. Spread horizontally (flow)
        int flowDistance = computeFlowDistance(key, props.viscosity);
        for (int i = 1; i <= flowDistance; ++i) {
            uint64_t leftKey = getMortonLeft(key, i);
            uint64_t rightKey = getMortonRight(key, i);

            if (isEmpty(leftKey) && isEmpty(rightKey)) {
                // Split (lossy: pick one direction randomly)
                if (rand() % 2) moveVoxel(key, leftKey);
                else moveVoxel(key, rightKey);
                return;
            } else if (isEmpty(leftKey)) {
                moveVoxel(key, leftKey);
                return;
            } else if (isEmpty(rightKey)) {
                moveVoxel(key, rightKey);
                return;
            }
        }

        // Settled
    }

    void simulateGas(uint64_t key, VoxelSimState& state,
                     const MaterialProperties& props) {
        // Gases rise and disperse

        uint64_t aboveKey = getMortonAbove(key);
        if (isEmpty(aboveKey) || isHeavierGas(aboveKey, props)) {
            moveVoxel(key, aboveKey);
        } else {
            // Disperse randomly (lossy: disappear after N frames)
            state.temperature--; // Decay counter
            if (state.temperature == 0) {
                removeVoxel(key);
            }
        }
    }
};
```

**Chemical Reactions:**

```cpp
struct Reaction {
    uint16_t reactant1;      // Material A
    uint16_t reactant2;      // Material B
    uint16_t product;        // Result material
    uint8_t minTemperature;  // Activation energy
    float probability;       // Per-frame chance (lossy!)
};

// Example reactions:
// Wood + Fire → Fire + Smoke
// Water + Lava → Stone + Steam
// Gunpowder + Fire → Fire + Explosion

void checkReactions(uint64_t key, VoxelSimState& state) {
    // Check 6 neighbors for reactions
    for (int axis = 0; axis < 3; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
            uint64_t neighborKey = getMortonNeighbor(key, axis, dir);
            auto neighbor = getVoxelState(neighborKey);

            for (const auto& reaction : reactions) {
                if (state.materialID == reaction.reactant1 &&
                    neighbor.materialID == reaction.reactant2 &&
                    state.temperature >= reaction.minTemperature) {

                    // Lossy: Probabilistic reaction
                    if (randomFloat() < reaction.probability) {
                        state.materialID = reaction.product;
                        markActive(key);
                        markActive(neighborKey);
                    }
                }
            }
        }
    }
}
```

---

#### 2.8.3 GPU-Accelerated Simulation

**Idea:** Offload cellular automata to GPU compute shaders (1000x parallelism!).

```glsl
// New file: shaders/VoxelSimulation.comp.glsl

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

// Simulation state buffers
layout(std430, set = 0, binding = 0) buffer SimulationState {
    VoxelSimState voxels[]; // Current state
};

layout(std430, set = 0, binding = 1) buffer SimulationStateNext {
    VoxelSimState voxelsNext[]; // Next state (double-buffered)
};

layout(std430, set = 0, binding = 2) buffer ActiveVoxelList {
    uint activeVoxels[];    // Morton keys of active voxels
    uint activeCount;
};

uniform MaterialProperties materials[65536]; // All material types

void main() {
    // Get voxel index from active list
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= activeCount) return;

    uint64_t mortonKey = activeVoxels[idx];
    VoxelSimState state = voxels[mortonKey];
    MaterialProperties props = materials[state.materialID];

    // Simulate based on physics type
    VoxelSimState nextState = state;

    switch (props.physics) {
        case PHYSICS_POWDER:
            nextState = simulatePowderGPU(mortonKey, state, props);
            break;
        case PHYSICS_LIQUID:
            nextState = simulateLiquidGPU(mortonKey, state, props);
            break;
        // ... etc
    }

    // Write to next state buffer
    voxelsNext[mortonKey] = nextState;

    // Mark neighbors as active if state changed significantly
    if (stateChanged(state, nextState)) {
        markNeighborsActive(mortonKey);
    }
}

VoxelSimState simulatePowderGPU(uint64_t key, VoxelSimState state,
                                 MaterialProperties props) {
    // Check below
    uint64_t belowKey = getMortonBelow(key);
    VoxelSimState below = voxels[belowKey];

    if (below.materialID == 0) { // Empty
        // Move down
        state.velocity.y -= 9.8; // Gravity
        return state; // Movement handled by separate pass
    }

    // Settled
    state.velocity = vec3(0);
    return state;
}
```

**Performance:**

```
GPU: RTX 4090
CUDA cores: 16,384
Voxels simulated: 1.3M active voxels
Parallelism: 16,384 voxels in parallel
Time per update: 1.3M / 16,384 = 79 batches
Batch time: ~10μs (memory-bound)
Total: 79 × 10μs = 0.79ms per frame

Result: 60 FPS with 1.3M active voxels! ✅
```

---

#### 2.8.4 Chunk-Based Simulation (Scalability)

**Idea:** Only simulate chunks near the player, freeze distant chunks.

```cpp
struct SimulationChunk {
    glm::ivec3 chunkOrigin;          // World position (512³ chunks)
    uint32_t activeVoxelCount;       // How many voxels are active
    SimulationState state;           // Frozen, Simulating, etc.

    // Lossy: Store only active voxels
    std::vector<uint64_t> activeVoxelKeys;  // Morton keys
    std::vector<VoxelSimState> activeStates;
};

class ChunkedSimulator {
public:
    void update(const glm::vec3& playerPos) {
        // 1. Determine which chunks to simulate
        auto activeChunks = getChunksInRadius(playerPos, simulationRadius_);

        // 2. Simulate each chunk (can parallelize!)
        for (auto& chunk : activeChunks) {
            float distance = glm::distance(playerPos,
                                           glm::vec3(chunk.chunkOrigin) * 512.0f);

            // Lossy: Coarser simulation for distant chunks
            int updateFreq = getUpdateFrequency(distance);
            if (frameCounter_ % updateFreq == 0) {
                simulateChunk(chunk, distance);
            }
        }

        // 3. Freeze distant chunks
        freezeDistantChunks(playerPos, simulationRadius_ * 2.0f);

        frameCounter_++;
    }

private:
    void simulateChunk(SimulationChunk& chunk, float distance) {
        // Lossy: Reduce resolution for distant chunks
        int megaVoxelSize = getSimulationResolution(distance);

        if (megaVoxelSize == 1) {
            // Full resolution simulation
            for (size_t i = 0; i < chunk.activeVoxelCount; ++i) {
                simulateVoxel(chunk.activeVoxelKeys[i],
                              chunk.activeStates[i]);
            }
        } else {
            // Mega-voxel simulation (coarser)
            simulateMegaVoxels(chunk, megaVoxelSize);
        }
    }
};
```

**Bandwidth With Chunking:**

```
Active chunks: 4 (32³ voxels each)
Total active voxels: 4 × 32³ = 131,072 voxels
Active (moving) voxels: 1% = 1,310 voxels

Bandwidth per frame:
1,310 voxels × 16 bytes = 20 KB/frame
@ 60 FPS: 1.2 MB/s

Result: Trivial bandwidth! ✅✅✅
```

---

#### 2.8.5 Integration with GaiaVoxelWorld

**Architecture:**

```cpp
class GaiaVoxelWorld {
    // Existing: Rendering state (100 bytes/voxel)
    gaia::ecs::World renderWorld_;

    // NEW: Simulation state (16 bytes/voxel, sparse)
    std::unique_ptr<VoxelPhysicsSimulator> simulator_;
    std::unordered_map<uint64_t, VoxelSimState> simStates_;

public:
    /**
     * Enable voxel simulation.
     * @param simulationRadius Radius around player to simulate (in voxels)
     */
    void enableSimulation(float simulationRadius = 512.0f) {
        simulator_ = std::make_unique<VoxelPhysicsSimulator>();
        simulator_->setSimulationRadius(simulationRadius);
    }

    /**
     * Update simulation (call every frame).
     * @param deltaTime Time since last frame
     * @param playerPos Player position (for chunking)
     */
    void updateSimulation(float deltaTime, const glm::vec3& playerPos) {
        if (!simulator_) return;

        // 1. Simulate active voxels
        simulator_->update(simStates_, playerPos, deltaTime);

        // 2. Sync simulation state → render state (lossy!)
        // Only update render entities that changed
        for (const auto& [mortonKey, simState] : simStates_) {
            if (simState.isDirty) {
                syncToRenderWorld(mortonKey, simState);
                simState.isDirty = false;
            }
        }
    }

private:
    void syncToRenderWorld(uint64_t mortonKey, const VoxelSimState& simState) {
        // Find or create entity for this voxel
        auto entity = getEntityByMorton(mortonKey);
        if (!entity.valid()) {
            // Voxel moved here, create new entity
            glm::vec3 pos = mortonToWorldPos(mortonKey);
            entity = createVoxel(pos);
        }

        // Update render properties from sim state
        auto material = getMaterialProperties(simState.materialID);
        setComponent<Color>(entity, material.color);
        setComponent<Density>(entity, material.density);

        // Temperature affects color (lossy: simple ramp)
        if (simState.temperature > 100) {
            // Glowing hot
            float glow = (simState.temperature - 100) / 155.0f;
            auto color = getComponentValue<Color>(entity).value();
            color = glm::mix(color, glm::vec3(1, 0.5, 0), glow);
            setComponent<Color>(entity, color);
        }
    }
};
```

---

#### 2.8.6 Lossy Compression Summary

**Compression Techniques:**

| Technique | Reduction | Quality Loss |
|-----------|-----------|--------------|
| Active voxel culling | 100x (1% active) | None (perfect for settled voxels) |
| State compression | 6.25x (16 bytes vs 100) | Minimal (quantized temp/pressure) |
| Spatial coarsening | 8x (2x2x2 mega-voxels) | Low (distant chunks blurred) |
| Temporal coarsening | 10x (update every 10 frames) | Low (distant chunks laggy) |
| Material aggregation | 1000x (blob instead of voxels) | Medium (lost individual voxel detail) |
| **Combined** | **500,000x** | **Imperceptible to player** |

**Bandwidth:**

```
Naive 512³: 804 GB/s ❌
With active culling (1%): 8 GB/s ⚠️
With chunking (32³): 192 MB/s ✅
With all optimizations: 1.2 MB/s ✅✅✅

Result: Fully feasible!
```

---

#### 2.8.7 Noita Comparison

| Feature | Noita (2D) | Proposed (3D) | Notes |
|---------|------------|---------------|-------|
| **Grid Size** | 2560×1440 (3.7M) | 512³ (134M) | 36x more voxels |
| **Active Pixels/Voxels** | ~1M (30%) | ~1.3M (1%) | Similar active count! |
| **Update Rate** | 60 FPS | 60 FPS (near), 6 FPS (far) | Lossy temporal |
| **Bandwidth** | 1.8 GB/s | 1.2 MB/s (with optimizations) | **1,500x more efficient!** |
| **Simulation** | CPU (single-threaded) | GPU compute | Massively parallel |
| **Physics** | Falling sand, liquids, fire | Same + 3D gravity | Full Noita feature set |
| **Reactions** | Wood burns, water extinguishes | Same | Chemical reactions |
| **Quality** | Pixel-perfect | Lossy (distant chunks) | Imperceptible loss |

---

#### 2.8.8 Structural Integrity, Rigid Bodies & Animated Voxels

**Problem Statement:** Not all voxels should simulate continuously:
1. **Player-built structures** should be stable (not fall apart)
2. **Rigid objects** (boulders, trees) should behave as solid bodies
3. **Animated objects** (grass, flags) should sway without moving voxels
4. **Bonded vs unbonded** voxels need different physics

**Key Insight:** Use a **state machine** with multiple simulation modes.

---

##### 2.8.8.1 Voxel State Machine

**States:**

```cpp
enum class VoxelSimulationState : uint8_t {
    // STABLE STATES (no bandwidth cost)
    Frozen,        // Never simulates (bedrock, distant chunks)
    Sleeping,      // Structurally stable, wakes on impact (player house)
    Static,        // Permanently static (placed blocks)

    // ACTIVE STATES (bandwidth cost)
    Active,        // Fully simulating (falling sand, flowing water)
    RigidBody,     // Part of bonded structure (boulder, tree)
    Animated,      // Procedurally animated (grass, flags)
};

struct VoxelSimState {
    uint16_t materialID;
    uint8_t temperature;
    uint8_t pressure;
    glm::vec3 velocity;

    // NEW: Simulation state
    VoxelSimulationState simState;  // 1 byte
    uint32_t structureID;           // If part of rigid body/structure (4 bytes)
    uint16_t bondFlags;             // Which neighbors bonded (6 bits used)
};
```

**State Transitions:**

```
         [Player builds]
Frozen ──────────────────→ Sleeping ──[Impact]──→ Active
  ↑                           ↑                       │
  │                           │                       │
  └──[Distance > 100m]────────┴───[Settled]───────────┘

         [Connect to structure]
Active ────────────────────────────→ RigidBody
  ↑                                      │
  │                                      │
  └────────[Structure breaks]────────────┘

         [Place animated block]
Static ────────────────────────────→ Animated
  ↑                                      │
  │                                      │
  └────────[Stop animation]──────────────┘
```

---

##### 2.8.8.2 Structural Bonding System

**Concept:** Voxels bond together to form stable structures (like Teardown).

**Bond Graph:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelBonds.h

struct VoxelBond {
    uint64_t voxelA;    // Morton key
    uint64_t voxelB;    // Morton key
    float strength;     // Bond strength (0-1)
    uint8_t axis;       // 0=X, 1=Y, 2=Z
};

class StructuralIntegritySystem {
public:
    /**
     * Bond two voxels together.
     * Called when player places adjacent blocks.
     */
    void createBond(uint64_t voxelA, uint64_t voxelB, float strength = 1.0f) {
        bonds_.push_back({voxelA, voxelB, strength, getAxis(voxelA, voxelB)});

        // Update bond flags (6-neighbor connectivity)
        updateBondFlags(voxelA);
        updateBondFlags(voxelB);
    }

    /**
     * Check if structure is stable (has support path to ground).
     * Uses flood-fill to find connected components.
     */
    bool isStructureStable(uint64_t voxel) {
        // Find all connected voxels
        auto connectedVoxels = floodFillBonds(voxel);

        // Check if any voxel touches ground
        for (uint64_t v : connectedVoxels) {
            glm::ivec3 pos = mortonToPos(v);
            if (pos.y == 0) return true; // Touches ground
        }

        return false; // Floating structure!
    }

    /**
     * Break bond if stress exceeds strength.
     * Called during explosions, impacts, etc.
     */
    void applyStress(uint64_t voxelA, uint64_t voxelB, float stress) {
        for (auto& bond : bonds_) {
            if ((bond.voxelA == voxelA && bond.voxelB == voxelB) ||
                (bond.voxelA == voxelB && bond.voxelB == voxelA)) {

                bond.strength -= stress;

                if (bond.strength <= 0.0f) {
                    // Bond broken!
                    removeBond(bond);

                    // Check if structure still stable
                    if (!isStructureStable(voxelA)) {
                        // Structure lost support → start falling
                        wakeStructure(voxelA);
                    }
                }
            }
        }
    }

private:
    std::vector<VoxelBond> bonds_;

    std::unordered_set<uint64_t> floodFillBonds(uint64_t startVoxel) {
        std::unordered_set<uint64_t> visited;
        std::queue<uint64_t> queue;
        queue.push(startVoxel);

        while (!queue.empty()) {
            uint64_t current = queue.front();
            queue.pop();

            if (visited.contains(current)) continue;
            visited.insert(current);

            // Find all bonded neighbors
            for (const auto& bond : bonds_) {
                uint64_t neighbor = 0;
                if (bond.voxelA == current) neighbor = bond.voxelB;
                if (bond.voxelB == current) neighbor = bond.voxelA;

                if (neighbor != 0 && !visited.contains(neighbor)) {
                    queue.push(neighbor);
                }
            }
        }

        return visited;
    }

    void wakeStructure(uint64_t voxel) {
        // Wake all connected voxels
        auto structure = floodFillBonds(voxel);
        for (uint64_t v : structure) {
            auto& state = getVoxelState(v);
            state.simState = VoxelSimulationState::Active;
            markActive(v);
        }
    }
};
```

**Example: Player Builds House**

```cpp
// Player places blocks
world.createVoxel(glm::vec3(0, 0, 0), WoodMaterial); // Foundation
world.createVoxel(glm::vec3(0, 1, 0), WoodMaterial); // Wall
world.createVoxel(glm::vec3(0, 2, 0), WoodMaterial); // Wall
world.createVoxel(glm::vec3(1, 2, 0), WoodMaterial); // Roof

// System auto-bonds adjacent blocks
structuralSystem.createBond(morton(0,0,0), morton(0,1,0), 1.0f);
structuralSystem.createBond(morton(0,1,0), morton(0,2,0), 1.0f);
structuralSystem.createBond(morton(0,2,0), morton(1,2,0), 1.0f);

// Mark as sleeping (stable delta)
for (auto voxel : houseVoxels) {
    state.simState = VoxelSimulationState::Sleeping;
}

// Later: Explosion removes foundation
world.destroyVoxel(morton(0,0,0));

// System detects lost support
if (!structuralSystem.isStructureStable(morton(0,1,0))) {
    // Wake entire structure → starts falling!
    structuralSystem.wakeStructure(morton(0,1,0));
}

// Result: House collapses realistically (Teardown-style)
```

---

##### 2.8.8.3 Rigid Body Extraction

**Concept:** Convert bonded voxel structures into rigid bodies for efficient physics simulation.

**Why?** Simulating 10,000 bonded voxels individually = expensive. Simulating 1 rigid body with 10,000 voxels = cheap!

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelRigidBodies.h

struct VoxelRigidBody {
    uint32_t structureID;               // Unique ID
    std::vector<uint64_t> voxels;       // Morton keys of all voxels

    // Physics properties (computed from voxels)
    float mass;                         // Sum of voxel masses
    glm::vec3 centerOfMass;             // Weighted average
    glm::mat3 inertiaTensor;            // For rotation

    // Simulation state
    glm::vec3 position;                 // COM position
    glm::quat orientation;              // Rotation
    glm::vec3 linearVelocity;
    glm::vec3 angularVelocity;

    // Bounding volume (for collision)
    AABB localBounds;                   // In body space
    AABB worldBounds;                   // In world space
};

class RigidBodyExtractor {
public:
    /**
     * Extract rigid body from bonded structure.
     * Called when structure becomes unstable (starts falling).
     */
    VoxelRigidBody extractRigidBody(const std::unordered_set<uint64_t>& voxels) {
        VoxelRigidBody body;
        body.structureID = nextStructureID_++;
        body.voxels = std::vector(voxels.begin(), voxels.end());

        // Compute mass and center of mass
        float totalMass = 0.0f;
        glm::vec3 com(0);
        for (uint64_t voxel : voxels) {
            glm::vec3 pos = mortonToWorldPos(voxel);
            float voxelMass = getMaterialDensity(voxel);
            totalMass += voxelMass;
            com += pos * voxelMass;
        }
        body.mass = totalMass;
        body.centerOfMass = com / totalMass;

        // Compute inertia tensor (for rotation)
        body.inertiaTensor = computeInertiaTensor(voxels, body.centerOfMass);

        // Compute bounds
        body.localBounds = computeAABB(voxels);

        // Initialize state
        body.position = body.centerOfMass;
        body.orientation = glm::quat(1, 0, 0, 0); // Identity
        body.linearVelocity = glm::vec3(0);
        body.angularVelocity = glm::vec3(0);

        return body;
    }

    /**
     * Simulate rigid body (much cheaper than per-voxel simulation).
     */
    void simulateRigidBody(VoxelRigidBody& body, float deltaTime) {
        // Apply gravity
        body.linearVelocity += glm::vec3(0, -9.8f, 0) * deltaTime;

        // Update position
        body.position += body.linearVelocity * deltaTime;

        // Update rotation
        glm::quat spin(0, body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z);
        body.orientation += 0.5f * spin * body.orientation * deltaTime;
        body.orientation = glm::normalize(body.orientation);

        // Update world bounds
        body.worldBounds = transformAABB(body.localBounds, body.position, body.orientation);

        // Check collision with terrain
        checkTerrainCollision(body);
    }

    /**
     * Check collision between rigid body and simulated voxels.
     */
    void checkTerrainCollision(VoxelRigidBody& body) {
        // For each voxel in rigid body
        for (uint64_t voxel : body.voxels) {
            // Transform to world space
            glm::vec3 localPos = mortonToWorldPos(voxel) - body.centerOfMass;
            glm::vec3 worldPos = body.position + body.orientation * localPos;

            // Check if overlapping terrain voxel
            uint64_t terrainKey = worldPosToMorton(worldPos);
            if (isTerrainVoxel(terrainKey)) {
                // Collision! Resolve
                resolveCollision(body, terrainKey);

                // Maybe break rigid body if impact too hard
                if (glm::length(body.linearVelocity) > 10.0f) {
                    breakRigidBody(body);
                }
            }
        }
    }

    /**
     * Break rigid body back into simulated voxels.
     * Called on high-impact collision.
     */
    void breakRigidBody(VoxelRigidBody& body) {
        // Convert back to individual active voxels
        for (uint64_t voxel : body.voxels) {
            auto& state = getVoxelState(voxel);
            state.simState = VoxelSimulationState::Active;
            state.velocity = body.linearVelocity; // Inherit velocity
            markActive(voxel);
        }

        // Remove rigid body
        removeRigidBody(body.structureID);
    }

private:
    uint32_t nextStructureID_ = 1;
    std::unordered_map<uint32_t, VoxelRigidBody> rigidBodies_;
};
```

**Performance:**

```
Bonded structure: 10,000 voxels
Per-voxel simulation: 10,000 updates/frame = expensive
Rigid body simulation: 1 update/frame = cheap!

Speedup: 10,000x for stable structures
```

---

##### 2.8.8.4 Stable Objects in Unstable Environments

**Problem:** Boulder (rigid) sitting in sand (simulated). How to handle?

**Solution:** **Collision Carving** - Rigid body "carves out" space in simulation grid.

```cpp
class RigidBodySimulationIntegration {
public:
    /**
     * Carve rigid body out of simulation grid.
     * Prevents sand from simulating into boulder's space.
     */
    void carveRigidBodySpace(const VoxelRigidBody& body) {
        // For each voxel in rigid body's world bounds
        for (int x = body.worldBounds.min.x; x < body.worldBounds.max.x; ++x) {
            for (int y = body.worldBounds.min.y; y < body.worldBounds.max.y; ++y) {
                for (int z = body.worldBounds.min.z; z < body.worldBounds.max.z; ++z) {
                    uint64_t key = worldPosToMorton(glm::vec3(x, y, z));

                    // Check if this position is inside rigid body
                    if (isInsideRigidBody(glm::vec3(x, y, z), body)) {
                        // Mark as occupied by rigid body
                        auto& state = getVoxelState(key);
                        state.simState = VoxelSimulationState::RigidBody;
                        state.structureID = body.structureID;

                        // Deactivate simulation for this voxel
                        markInactive(key);
                    }
                }
            }
        }
    }

    /**
     * Update simulation around moving rigid body.
     */
    void updateRigidBodyCarving(VoxelRigidBody& body, const AABB& previousBounds) {
        // 1. Free voxels that rigid body left
        for (int x = previousBounds.min.x; x < previousBounds.max.x; ++x) {
            for (int y = previousBounds.min.y; y < previousBounds.max.y; ++y) {
                for (int z = previousBounds.min.z; z < previousBounds.max.z; ++z) {
                    uint64_t key = worldPosToMorton(glm::vec3(x, y, z));

                    // If no longer inside rigid body
                    if (!isInsideRigidBody(glm::vec3(x, y, z), body)) {
                        auto& state = getVoxelState(key);
                        if (state.structureID == body.structureID) {
                            // Free this space for simulation
                            state.simState = VoxelSimulationState::Active;
                            state.structureID = 0;

                            // Wake neighbors (sand can flow in)
                            wakeNeighbors(key);
                        }
                    }
                }
            }
        }

        // 2. Carve new space
        carveRigidBodySpace(body);
    }
};

// Example: Boulder rolling down sand dune
VoxelRigidBody boulder = extractRigidBody(boulderVoxels);

while (boulder.linearVelocity.y < 0) { // Falling
    // Simulate boulder (rigid body physics)
    simulateRigidBody(boulder, deltaTime);

    // Update carving (clear path, block space)
    AABB previousBounds = boulder.worldBounds;
    updateRigidBodyCarving(boulder, previousBounds);

    // Sand voxels automatically flow into cleared space
    // (handled by cellular automata simulation)
}

// Result: Boulder rolls through sand, leaving a trail
```

---

##### 2.8.8.5 Animated Voxels (Grass, Flags, etc.)

**Problem:** Grass swaying in wind - do we move voxels or regenerate?

**Answer:** **Procedural Regeneration** - Don't move voxels, regenerate them each frame from animation curve.

**Why?**
- Moving voxels creates gaps (looks bad)
- Moving voxels is expensive (delete + create every frame)
- Procedural is cheaper and looks better

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/VoxelAnimation.h

struct AnimationCurve {
    enum class Type {
        Sine,         // Smooth oscillation (grass sway)
        Perlin,       // Organic noise (tree branches)
        Bezier,       // Custom curve (flag flutter)
        Physics       // Spring simulation (dangling rope)
    };

    Type type;
    float frequency;      // Oscillations per second
    float amplitude;      // Max displacement (voxels)
    glm::vec3 direction;  // Primary motion axis
    float phase;          // Offset (for variety)
};

struct AnimatedVoxelRegion {
    AABB baseRegion;                    // Original region (at rest)
    AnimationCurve curve;               // How it moves
    uint16_t baseMaterialID;            // Material when static

    // Generator function: basePos → currentPos
    std::function<glm::vec3(glm::vec3, float)> animationFunc;
};

class VoxelAnimationSystem {
public:
    /**
     * Register animated region (e.g., grass patch).
     */
    uint32_t registerAnimation(
        const AABB& region,
        const AnimationCurve& curve,
        uint16_t materialID) {

        AnimatedVoxelRegion anim;
        anim.baseRegion = region;
        anim.curve = curve;
        anim.baseMaterialID = materialID;

        // Create animation function based on curve type
        anim.animationFunc = createAnimationFunc(curve);

        animations_.push_back(anim);
        return animations_.size() - 1;
    }

    /**
     * Update animated voxels (called every frame).
     * DOES NOT MOVE VOXELS - regenerates them at new positions!
     */
    void updateAnimations(float currentTime) {
        for (auto& anim : animations_) {
            // 1. Clear previous frame's voxels (mark as inactive)
            clearAnimatedRegion(anim);

            // 2. Generate new voxels at animated positions
            for (int x = anim.baseRegion.min.x; x < anim.baseRegion.max.x; ++x) {
                for (int y = anim.baseRegion.min.y; y < anim.baseRegion.max.y; ++y) {
                    for (int z = anim.baseRegion.min.z; z < anim.baseRegion.max.z; ++z) {
                        glm::vec3 basePos(x, y, z);

                        // Compute animated position
                        glm::vec3 animPos = anim.animationFunc(basePos, currentTime);

                        // Create voxel at animated position
                        uint64_t key = worldPosToMorton(animPos);
                        createAnimatedVoxel(key, anim.baseMaterialID);
                    }
                }
            }
        }
    }

private:
    std::vector<AnimatedVoxelRegion> animations_;

    std::function<glm::vec3(glm::vec3, float)> createAnimationFunc(
        const AnimationCurve& curve) {

        switch (curve.type) {
            case AnimationCurve::Type::Sine:
                return [=](glm::vec3 basePos, float time) {
                    // Sine wave (grass sway)
                    float offset = sin(time * curve.frequency + basePos.x * 0.1f + curve.phase);
                    return basePos + curve.direction * curve.amplitude * offset;
                };

            case AnimationCurve::Type::Perlin:
                return [=](glm::vec3 basePos, float time) {
                    // Perlin noise (organic motion)
                    float noise = perlin3D(basePos * 0.1f + glm::vec3(time * curve.frequency));
                    return basePos + curve.direction * curve.amplitude * noise;
                };

            case AnimationCurve::Type::Physics:
                return [=](glm::vec3 basePos, float time) {
                    // Spring physics (rope, cloth)
                    // ... physics simulation ...
                    return basePos; // Placeholder
                };

            default:
                return [](glm::vec3 basePos, float time) { return basePos; };
        }
    }

    void clearAnimatedRegion(const AnimatedVoxelRegion& anim) {
        // Mark all voxels in animated region as inactive
        // (Will be regenerated this frame at new positions)
    }
};

// Example: Grass patch
AnimationCurve grassSway{
    .type = AnimationCurve::Type::Sine,
    .frequency = 2.0f,      // 2 sways per second
    .amplitude = 0.3f,      // 0.3 voxels displacement
    .direction = glm::vec3(1, 0, 0), // Sway in X direction
    .phase = 0.0f
};

AABB grassPatch{
    .min = glm::vec3(0, 0, 0),
    .max = glm::vec3(10, 3, 10)  // 10×3×10 grass blades
};

animSystem.registerAnimation(grassPatch, grassSway, GrassMaterialID);

// Every frame:
animSystem.updateAnimations(currentTime);

// Result: Grass sways smoothly, no voxels actually moved!
```

**Optimization: Implicit Animated Voxels**

For better performance, don't materialize animated voxels at all - just store the animation and generate on-demand during rendering:

```cpp
// Don't create actual voxel entities
// Instead, shader samples animation function during ray-casting

// GLSL shader
vec3 sampleAnimatedVoxel(vec3 worldPos, float time) {
    // Check if worldPos is inside animated region
    for (int i = 0; i < animatedRegionCount; ++i) {
        if (isInsideAABB(worldPos, animatedRegions[i].bounds)) {
            // Compute base position (reverse animation)
            vec3 basePos = inverseAnimation(worldPos, time, animatedRegions[i].curve);

            // Check if basePos was originally occupied
            if (wasVoxelAtBase(basePos, animatedRegions[i])) {
                return animatedRegions[i].color; // Hit!
            }
        }
    }

    return vec3(0); // Miss
}

// Result: Zero voxel creation overhead!
// Grass is purely a shader effect
```

---

##### 2.8.8.6 Hybrid Approach: Dynamic State Switching

**Best Practice:** Voxels switch between states dynamically based on conditions.

```cpp
void updateVoxelState(uint64_t voxel, VoxelSimState& state) {
    switch (state.simState) {
        case VoxelSimulationState::Sleeping:
            // Check if should wake (impact, neighbor movement)
            if (hasActiveNeighbor(voxel) || recentlyImpacted(voxel)) {
                state.simState = VoxelSimulationState::Active;
                markActive(voxel);
            }
            break;

        case VoxelSimulationState::Active:
            // Check if should sleep (settled, bonded)
            if (isSettled(state) && hasSupportBelow(voxel)) {
                state.simState = VoxelSimulationState::Sleeping;
                markInactive(voxel);
            }

            // Check if should become rigid body (bonded to structure)
            if (isPartOfBondedStructure(voxel)) {
                auto structure = extractRigidBody(getBondedVoxels(voxel));
                state.simState = VoxelSimulationState::RigidBody;
                state.structureID = structure.structureID;
            }
            break;

        case VoxelSimulationState::RigidBody:
            // Check if rigid body broke (impact, explosion)
            auto& body = getRigidBody(state.structureID);
            if (body.broken) {
                state.simState = VoxelSimulationState::Active;
                state.structureID = 0;
                state.velocity = body.linearVelocity; // Inherit
                markActive(voxel);
            }
            break;

        // ... other states ...
    }
}
```

---

##### 2.8.8.7 Bandwidth Impact

**State Distribution (Typical):**

```
512³ chunk (134M voxels):
- Frozen (distant): 80% = 107M voxels → 0 bandwidth
- Sleeping (stable): 15% = 20M voxels → 0 bandwidth (until woken)
- Static (terrain): 4% = 5.4M voxels → 0 bandwidth
- Active (falling): 0.5% = 670K voxels → 40 MB/s
- RigidBody (structures): 0.4% = 536K voxels → 5 MB/s (just rigid bodies, not voxels!)
- Animated (grass): 0.1% = 134K voxels → 0 bandwidth (procedural shader)

Total bandwidth: ~45 MB/s (vs 804 GB/s naive!)
Reduction: 17,866x
```

**Key Insight:** Most voxels spend most of their time sleeping/frozen!

---

##### 2.8.8.8 Summary Table

| Feature | Approach | Bandwidth | Quality |
|---------|----------|-----------|---------|
| **Player-built house** | Sleeping → bonds → wake on impact | 0 (until impacted) | Perfect |
| **Falling structure** | Extract rigid body → physics sim | 5 MB/s (1 body vs 10K voxels) | Perfect |
| **Boulder in sand** | Rigid body + collision carving | 5 MB/s | Perfect |
| **Grass swaying** | Procedural shader (implicit) | 0 | Perfect |
| **Tree in wind** | Animated region (regenerate) | 2 MB/s | Perfect |
| **Sand flowing** | Active cellular automata | 40 MB/s | Perfect |

**Total:** ~52 MB/s for fully dynamic world with structures, physics, and animation!

---

#### 2.8.9 Sparse Force Fields (System Decoupling via Force Distribution)

**Concept:** Decouple simulation systems using sparse 3D force fields as a shared communication layer. Instead of direct voxel-to-voxel or voxel-to-rigid-body interactions, all systems write forces to fields and read forces from fields.

**Key Insight:** Think of it as an **event bus for physics** - but instead of discrete events, it's continuous force distribution across 3D space.

**Problem with Direct Interactions:**

```cpp
// BAD: Direct voxel-to-voxel interactions (O(N²) worst case)
for (Voxel a : activeVoxels) {
    for (Voxel b : neighbors(a)) {
        applyForce(a, b); // N × 6 neighbors = 6N interactions
    }
}

// BAD: Rigid body checking all neighboring voxels
RigidBody boulder(10,000 voxels);
for (Voxel v : boulder.voxels) {
    for (Voxel neighbor : getNeighbors(v)) {
        checkCollision(v, neighbor); // 10K × 6 = 60K checks!
    }
}
```

**Solution: Sparse Force Fields**

```cpp
// GOOD: Write-read pattern via force fields
// 1. Systems WRITE forces to fields
windSystem.addForce(position, kineticField, windForce);
explosionSystem.addForce(position, pressureField, blastForce);
frictionSystem.addForce(position, frictionField, dragForce);

// 2. Systems READ forces from fields
Vector3 netForce = kineticField.sample(position) +
                   pressureField.sample(position) +
                   thermalField.sample(position);

// 3. Apply to object (rigid body, voxel, etc.)
applyForce(object, netForce);
```

---

##### 2.8.9.1 Force Field Architecture

**Field Types:**

```cpp
// New file: GaiaVoxelWorld/include/ForceFields.h

enum class ForceFieldType {
    Kinetic,       // Velocity/momentum (wind, currents)
    Pressure,      // Compression/expansion (explosions, sound)
    Thermal,       // Heat transfer (fire, cooling)
    Friction,      // Drag/resistance (air resistance, fluid viscosity)
    Gravity,       // Gravitational (usually uniform, but can be local)
    Magnetic,      // Magnetic fields (for advanced effects)
};

struct ForceFieldCell {
    glm::vec3 force;      // Force vector (N - Newtons)
    float magnitude;      // Scalar magnitude (for thermal, pressure)
    float decay;          // Time decay rate (0-1 per second)
    uint32_t lastUpdate;  // Frame number of last write
};

class SparseForceField {
public:
    /**
     * Sparse 3D force field.
     * Only stores cells with non-zero forces.
     * Resolution independent of voxel grid (typically coarser).
     */
    SparseForceField(float cellSize, float decayRate)
        : cellSize_(cellSize), decayRate_(decayRate) {}

    /**
     * Add force to field at world position.
     * Multiple writes to same cell accumulate.
     */
    void addForce(const glm::vec3& worldPos, const glm::vec3& force, float magnitude = 0.0f) {
        uint64_t cellKey = worldPosToCellKey(worldPos);

        auto& cell = cells_[cellKey];
        cell.force += force;
        cell.magnitude += magnitude;
        cell.lastUpdate = currentFrame_;
    }

    /**
     * Sample force from field at world position.
     * Trilinearly interpolates between neighboring cells.
     */
    glm::vec3 sampleForce(const glm::vec3& worldPos) const {
        // Get 8 neighboring cells for trilinear interpolation
        glm::ivec3 baseCell = glm::floor(worldPos / cellSize_);

        glm::vec3 totalForce(0);
        float totalWeight = 0.0f;

        for (int dx = 0; dx <= 1; ++dx) {
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dz = 0; dz <= 1; ++dz) {
                    glm::ivec3 cellPos = baseCell + glm::ivec3(dx, dy, dz);
                    uint64_t cellKey = cellPosToKey(cellPos);

                    auto it = cells_.find(cellKey);
                    if (it != cells_.end()) {
                        // Trilinear interpolation weight
                        glm::vec3 cellCenter = glm::vec3(cellPos) * cellSize_;
                        glm::vec3 offset = worldPos - cellCenter;
                        float weight = (1.0f - abs(offset.x / cellSize_)) *
                                      (1.0f - abs(offset.y / cellSize_)) *
                                      (1.0f - abs(offset.z / cellSize_));

                        totalForce += it->second.force * weight;
                        totalWeight += weight;
                    }
                }
            }
        }

        return (totalWeight > 0.0f) ? totalForce / totalWeight : glm::vec3(0);
    }

    /**
     * Sample force across volume (for rigid bodies).
     * Returns net force and torque.
     */
    struct VolumeForceResult {
        glm::vec3 netForce;      // Total linear force
        glm::vec3 netTorque;     // Total torque around center
        glm::vec3 centerOfForce; // Where force is concentrated
    };

    VolumeForceResult sampleVolume(const AABB& volume, const glm::vec3& centerOfMass) const {
        VolumeForceResult result{};

        // Sample at regular intervals across volume
        int sampleCountX = std::max(1, (int)(volume.size().x / cellSize_));
        int sampleCountY = std::max(1, (int)(volume.size().y / cellSize_));
        int sampleCountZ = std::max(1, (int)(volume.size().z / cellSize_));

        for (int x = 0; x < sampleCountX; ++x) {
            for (int y = 0; y < sampleCountY; ++y) {
                for (int z = 0; z < sampleCountZ; ++z) {
                    glm::vec3 samplePos = volume.min + glm::vec3(
                        x / (float)sampleCountX,
                        y / (float)sampleCountY,
                        z / (float)sampleCountZ
                    ) * volume.size();

                    glm::vec3 localForce = sampleForce(samplePos);
                    result.netForce += localForce;

                    // Compute torque: r × F
                    glm::vec3 r = samplePos - centerOfMass;
                    result.netTorque += glm::cross(r, localForce);
                }
            }
        }

        // Normalize by sample count
        int totalSamples = sampleCountX * sampleCountY * sampleCountZ;
        result.netForce /= totalSamples;
        result.netTorque /= totalSamples;

        return result;
    }

    /**
     * Update field (decay forces over time).
     */
    void update(float deltaTime) {
        currentFrame_++;

        // Decay forces
        for (auto it = cells_.begin(); it != cells_.end();) {
            auto& cell = it->second;

            // Exponential decay
            cell.force *= std::exp(-decayRate_ * deltaTime);
            cell.magnitude *= std::exp(-decayRate_ * deltaTime);

            // Remove if force negligible
            if (glm::length(cell.force) < 0.01f && cell.magnitude < 0.01f) {
                it = cells_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * Clear field (reset all forces).
     */
    void clear() {
        cells_.clear();
    }

    /**
     * Get active cell count (for debugging).
     */
    size_t getActiveCellCount() const {
        return cells_.size();
    }

private:
    float cellSize_;     // Size of each force field cell (in voxels)
    float decayRate_;    // Force decay per second
    uint32_t currentFrame_ = 0;

    // Sparse storage: only cells with non-zero forces
    std::unordered_map<uint64_t, ForceFieldCell> cells_;

    uint64_t worldPosToCellKey(const glm::vec3& worldPos) const {
        glm::ivec3 cellPos = glm::floor(worldPos / cellSize_);
        return cellPosToKey(cellPos);
    }

    uint64_t cellPosToKey(const glm::ivec3& cellPos) const {
        // Morton encoding for spatial coherence
        return mortonEncode(cellPos.x, cellPos.y, cellPos.z);
    }
};
```

---

##### 2.8.9.2 Force Field System Manager

**Orchestrates multiple field types:**

```cpp
class ForceFieldSystem {
public:
    /**
     * Initialize force fields with resolution.
     * @param cellSize Size of force field cells (typically 4-8 voxels)
     */
    void initialize(float cellSize = 4.0f) {
        fields_[ForceFieldType::Kinetic] = std::make_unique<SparseForceField>(cellSize, 0.5f); // Fast decay
        fields_[ForceFieldType::Pressure] = std::make_unique<SparseForceField>(cellSize, 2.0f); // Medium decay
        fields_[ForceFieldType::Thermal] = std::make_unique<SparseForceField>(cellSize, 0.1f); // Slow decay
        fields_[ForceFieldType::Friction] = std::make_unique<SparseForceField>(cellSize, 1.0f); // Fast decay
        fields_[ForceFieldType::Gravity] = std::make_unique<SparseForceField>(cellSize, 0.0f); // No decay
    }

    /**
     * Add force to specific field type.
     */
    void addForce(ForceFieldType type, const glm::vec3& worldPos, const glm::vec3& force, float magnitude = 0.0f) {
        fields_[type]->addForce(worldPos, force, magnitude);
    }

    /**
     * Sample combined forces from all fields.
     */
    glm::vec3 sampleCombinedForce(const glm::vec3& worldPos) const {
        glm::vec3 totalForce(0);
        for (const auto& [type, field] : fields_) {
            totalForce += field->sampleForce(worldPos);
        }
        return totalForce;
    }

    /**
     * Sample specific field type.
     */
    glm::vec3 sampleForce(ForceFieldType type, const glm::vec3& worldPos) const {
        return fields_.at(type)->sampleForce(worldPos);
    }

    /**
     * Sample forces across rigid body volume.
     */
    SparseForceField::VolumeForceResult sampleVolume(
        ForceFieldType type,
        const AABB& volume,
        const glm::vec3& centerOfMass) const {
        return fields_.at(type)->sampleVolume(volume, centerOfMass);
    }

    /**
     * Update all fields (decay over time).
     */
    void update(float deltaTime) {
        for (auto& [type, field] : fields_) {
            field->update(deltaTime);
        }
    }

    /**
     * Get statistics for debugging.
     */
    struct FieldStats {
        size_t kineticCells;
        size_t pressureCells;
        size_t thermalCells;
        size_t frictionCells;
        size_t totalCells;
    };

    FieldStats getStats() const {
        FieldStats stats{};
        stats.kineticCells = fields_.at(ForceFieldType::Kinetic)->getActiveCellCount();
        stats.pressureCells = fields_.at(ForceFieldType::Pressure)->getActiveCellCount();
        stats.thermalCells = fields_.at(ForceFieldType::Thermal)->getActiveCellCount();
        stats.frictionCells = fields_.at(ForceFieldType::Friction)->getActiveCellCount();
        stats.totalCells = stats.kineticCells + stats.pressureCells + stats.thermalCells + stats.frictionCells;
        return stats;
    }

private:
    std::unordered_map<ForceFieldType, std::unique_ptr<SparseForceField>> fields_;
};
```

---

##### 2.8.9.3 Integration Examples

**Example 1: Wind System**

```cpp
class WindSystem {
public:
    void update(ForceFieldSystem& forceFields, float deltaTime) {
        // Generate wind forces
        for (const auto& windZone : windZones_) {
            // Add kinetic forces across wind zone
            for (int x = windZone.min.x; x < windZone.max.x; x += 4) {
                for (int y = windZone.min.y; y < windZone.max.y; y += 4) {
                    for (int z = windZone.min.z; z < windZone.max.z; z += 4) {
                        glm::vec3 pos(x, y, z);

                        // Wind varies with noise
                        glm::vec3 windForce = windZone.baseDirection * windZone.strength;
                        windForce += perlin3D(pos * 0.1f + glm::vec3(time_)) * windZone.turbulence;

                        forceFields.addForce(ForceFieldType::Kinetic, pos, windForce);
                    }
                }
            }
        }
    }

private:
    struct WindZone {
        AABB bounds;
        glm::vec3 baseDirection;
        float strength;
        float turbulence;
    };
    std::vector<WindZone> windZones_;
    float time_ = 0.0f;
};
```

**Example 2: Rigid Body Reading Wind**

```cpp
void simulateRigidBody(VoxelRigidBody& body, const ForceFieldSystem& forceFields, float deltaTime) {
    // Sample kinetic field across rigid body volume
    auto windForce = forceFields.sampleVolume(
        ForceFieldType::Kinetic,
        body.worldBounds,
        body.centerOfMass
    );

    // Apply linear force (drag from wind)
    body.linearVelocity += (windForce.netForce / body.mass) * deltaTime;

    // Apply torque (wind causes rotation)
    body.angularVelocity += (body.inverseInertiaTensor * windForce.netTorque) * deltaTime;

    // Result: Rigid body sways in wind without checking individual voxels!
}
```

**Example 3: Explosion**

```cpp
void createExplosion(const glm::vec3& center, float radius, float strength, ForceFieldSystem& forceFields) {
    // Write pressure field
    for (int x = -radius; x <= radius; x += 4) {
        for (int y = -radius; y <= radius; y += 4) {
            for (int z = -radius; z <= radius; z += 4) {
                glm::vec3 offset(x, y, z);
                float distance = glm::length(offset);

                if (distance < radius) {
                    glm::vec3 pos = center + offset;

                    // Radial force (outward from explosion)
                    glm::vec3 direction = glm::normalize(offset);
                    float falloff = 1.0f - (distance / radius); // Linear falloff
                    glm::vec3 force = direction * strength * falloff;

                    forceFields.addForce(ForceFieldType::Pressure, pos, force, strength * falloff);
                }
            }
        }
    }
}

// All nearby objects respond automatically:
// - Rigid bodies: Sample volume → apply force
// - Active voxels: Sample position → add velocity
// - Sleeping voxels: Sample pressure → wake if > threshold
```

**Example 4: Sliding Sand Creates Friction**

```cpp
void simulateSandVoxel(uint64_t voxel, VoxelSimState& state, ForceFieldSystem& forceFields) {
    // Sand is falling
    if (state.velocity.y < 0) {
        glm::vec3 pos = mortonToWorldPos(voxel);

        // Add friction to field (affects neighbors)
        glm::vec3 frictionForce = -state.velocity * 0.5f; // Proportional to velocity
        forceFields.addForce(ForceFieldType::Friction, pos, frictionForce);

        // Sand also reads friction from field
        glm::vec3 externalFriction = forceFields.sampleForce(ForceFieldType::Friction, pos);
        state.velocity += externalFriction * deltaTime;
    }
}

// Result: Avalanche creates friction field → wakes sleeping voxels → chain reaction
```

**Example 5: Fire Creates Thermal Field**

```cpp
void simulateFireVoxel(uint64_t voxel, VoxelSimState& state, ForceFieldSystem& forceFields) {
    glm::vec3 pos = mortonToWorldPos(voxel);

    // Fire adds heat to thermal field
    float heatOutput = 100.0f; // °C per second
    forceFields.addForce(ForceFieldType::Thermal, pos, glm::vec3(0), heatOutput);

    // Check neighbors for ignition
    for (int axis = 0; axis < 3; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
            glm::vec3 neighborPos = pos + glm::vec3(axis == 0 ? dir : 0,
                                                     axis == 1 ? dir : 0,
                                                     axis == 2 ? dir : 0);

            // Sample thermal field at neighbor
            float neighborTemp = forceFields.sampleForce(ForceFieldType::Thermal, neighborPos).x; // magnitude

            // Ignite if hot enough
            if (neighborTemp > 50.0f) { // Ignition threshold
                uint64_t neighborKey = worldPosToMorton(neighborPos);
                auto& neighborState = getVoxelState(neighborKey);

                if (neighborState.materialID == WoodMaterialID) {
                    neighborState.materialID = FireMaterialID; // Wood → Fire
                    markActive(neighborKey);
                }
            }
        }
    }
}

// Result: Fire spreads via thermal field, not direct voxel checks
```

---

##### 2.8.9.4 Performance Characteristics

**Bandwidth Analysis:**

```
Force field resolution: 4³ voxel cells (64 voxels per cell)
512³ chunk: 134M voxels / 64 = 2.1M potential cells

Typical active cells:
- Kinetic (wind): 10% of chunk = 210K cells
- Pressure (explosions): 1% = 21K cells (localized)
- Thermal (fire): 0.5% = 10.5K cells (sparse)
- Friction (sliding): 0.5% = 10.5K cells (sparse)

Cell size: 16 bytes (vec3 force + float magnitude + metadata)

Memory:
210K × 16 = 3.4 MB (kinetic)
21K × 16 = 336 KB (pressure)
10.5K × 16 = 168 KB (thermal)
10.5K × 16 = 168 KB (friction)

Total: ~4 MB for all force fields

Bandwidth per frame (with decay updates):
4 MB × 60 FPS = 240 MB/s

But: Most cells unchanged → update only active
Active cells (~10% change per frame): 24 MB/s

Result: Trivial bandwidth cost!
```

**Speedup vs Direct Interactions:**

```
Rigid body (10K voxels) vs neighbors (60K voxel-voxel checks):
- Direct: 60K collision checks = expensive
- Force field: Sample ~156 cells (10K voxels / 64) = cheap!

Speedup: 60K / 156 = 384x faster!
```

---

##### 2.8.9.5 Benefits Summary

| Benefit | Description | Impact |
|---------|-------------|--------|
| **Decoupling** | Systems don't know about each other | Maintainable, composable |
| **O(1) interactions** | Write to field, read from field | No N² complexity |
| **Multi-scale** | Rigid body samples entire volume at once | 384x faster than per-voxel |
| **Lossy by design** | Coarse resolution (4³ cells) | Invisible quality loss |
| **Sparse storage** | Only active cells stored | ~4 MB total memory |
| **System composition** | Wind + explosion + fire all coexist | Emergent interactions |
| **Bandwidth efficient** | 24 MB/s for all force fields | Negligible cost |

---

##### 2.8.9.6 Advanced: GPU Force Field Propagation

**Concept:** Propagate forces on GPU using compute shaders (diffusion, advection).

```glsl
// Compute shader: Force field diffusion
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(std430, set = 0, binding = 0) buffer ForceFieldCells {
    vec4 cells[]; // xyz = force, w = magnitude
};

uniform float diffusionRate;
uniform float deltaTime;

void main() {
    ivec3 cellPos = ivec3(gl_GlobalInvocationID.xyz);
    uint cellIdx = cellPosToIndex(cellPos);

    vec4 currentCell = cells[cellIdx];

    // Diffuse forces to neighbors (heat equation)
    vec4 neighborSum = vec4(0);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;

                ivec3 neighborPos = cellPos + ivec3(dx, dy, dz);
                uint neighborIdx = cellPosToIndex(neighborPos);
                neighborSum += cells[neighborIdx];
            }
        }
    }

    // Laplacian diffusion
    vec4 laplacian = neighborSum / 26.0 - currentCell;
    cells[cellIdx] += laplacian * diffusionRate * deltaTime;

    // Decay
    cells[cellIdx] *= exp(-decayRate * deltaTime);
}
```

**Result:** Forces naturally propagate through space (wind currents, heat diffusion, pressure waves).

---

##### 2.8.9.7 Integration with Existing Systems

**Update Flow:**

```cpp
// Main simulation loop
void updateSimulation(float deltaTime, const glm::vec3& playerPos) {
    // 1. Clear previous frame's transient forces (keep persistent ones)
    forceFields.update(deltaTime); // Decay forces

    // 2. Systems WRITE to force fields
    windSystem.update(forceFields, deltaTime);
    explosionSystem.update(forceFields, deltaTime);
    frictionSystem.update(forceFields, deltaTime);
    thermalSystem.update(forceFields, deltaTime);

    // 3. Systems READ from force fields and apply
    // Rigid bodies
    for (auto& body : rigidBodies) {
        auto forces = forceFields.sampleVolume(ForceFieldType::Kinetic, body.worldBounds, body.centerOfMass);
        body.linearVelocity += (forces.netForce / body.mass) * deltaTime;
        body.angularVelocity += (body.inverseInertiaTensor * forces.netTorque) * deltaTime;
    }

    // Active voxels
    for (uint64_t voxel : activeVoxels) {
        auto& state = getVoxelState(voxel);
        glm::vec3 pos = mortonToWorldPos(voxel);

        // Sample combined forces
        glm::vec3 netForce = forceFields.sampleCombinedForce(pos);

        // Apply to voxel
        state.velocity += netForce * deltaTime;

        // Cellular automata simulation
        simulateVoxel(voxel, state);
    }

    // Sleeping voxels (check if should wake)
    for (uint64_t voxel : sleepingVoxels) {
        glm::vec3 pos = mortonToWorldPos(voxel);

        // Check pressure field (explosion nearby?)
        float pressure = forceFields.sampleForce(ForceFieldType::Pressure, pos).length();
        if (pressure > wakeThreshold) {
            wakeVoxel(voxel);
        }
    }

    // 4. Propagate forces on GPU (optional advanced feature)
    forceFields.propagateOnGPU(deltaTime);
}
```

---

### 2.10 Industry-Standard Optimizations

**Concept:** Additional optimizations used in production voxel engines.

**Features:**

1. **Brick Compression:**
   - Run-length encoding for homogeneous bricks
   - 10:1 compression for solid regions
   - Decompress on GPU during rendering

2. **Sparse Virtual Texturing (SVT):**
   - Treat voxels as 3D virtual texture
   - Page in 64³ tiles on-demand
   - Shared with out-of-core rendering

3. **Level-of-Detail (LOD):**
   - Mip-mapped voxel data (1x, 2x, 4x, 8x downsampled)
   - Render distant regions at lower LOD
   - 4x memory reduction for distant voxels

4. **GPU Voxelization:**
   - Convert meshes to voxels on GPU
   - 1000x faster than CPU voxelization
   - Real-time dynamic voxelization

5. **Mesh Caching:**
   - Cache generated meshes for visualization
   - Marching cubes / dual contouring
   - Reuse mesh if voxels unchanged

---

### 2.11 GigaVoxels GPU-Driven Usage-Based Caching

**Concept:** Implement GigaVoxels research paper techniques for GPU-driven demand streaming, usage-based cache replacement, and sparse voxel octree ray-casting.

**Research Foundation:** Based on "GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering" (Crassin et al., 2009)

**Key Innovations:**
1. GPU determines what data to load (not CPU pre-computation)
2. Usage tracking per-brick during ray-casting
3. Cache replacement based on actual rendering usage
4. Sparse voxel octree (SVO) with mipmap pyramid
5. Asynchronous brick production on-demand

**Architecture:**

```cpp
// New file: GaiaVoxelWorld/include/GigaVoxelsCache.h

/**
 * GigaVoxels-style GPU-driven usage-based caching system.
 * GPU ray-caster tracks which bricks are accessed during rendering,
 * then updates cache based on actual usage (not heuristics).
 */
class GigaVoxelsCache {
public:
    /**
     * Sparse Voxel Octree node structure.
     * Stores both pointers to children and brick data.
     */
    struct SVONode {
        // Child pointers (8 children for octree)
        // If 0, child is empty. If MSB set, child is constant (no subdivision).
        uint32_t childPointers[8];

        // Brick pointer: Index into brick pool
        // 0 = no brick loaded (use parent's brick for mipmap)
        uint32_t brickPointer;

        // Usage timestamp (updated by GPU during ray-casting)
        uint32_t lastUsedFrame;

        // Mipmap level (0 = finest, 7 = coarsest)
        uint8_t level;

        // Constant value optimization (if all voxels same)
        uint8_t isConstant;
        VoxelData constantValue;
    };

    /**
     * GPU Usage Tracker: Records which bricks GPU accessed during rendering.
     * GPU atomically increments usage counters during ray-casting.
     */
    struct GPUUsageTracker {
        VkBuffer usageCounterBuffer;   // GPU buffer: uint32_t[brickCount]
        VkBuffer requestQueueBuffer;   // GPU buffer: requested brick IDs

        /**
         * GPU shader atomically increments usage counter when accessing brick:
         *
         * // In ray-casting shader:
         * uint brickID = getCurrentBrickID(ray);
         * atomicAdd(usageCounters[brickID], 1);
         *
         * if (brickPointer == 0) { // Brick not loaded
         *     uint requestIdx = atomicAdd(requestQueueSize, 1);
         *     requestQueue[requestIdx] = brickID;
         * }
         */
    };

    /**
     * Initialize GigaVoxels cache system.
     *
     * @param vramBudgetMB VRAM budget for brick pool
     * @param octreeDepth Max octree depth (e.g., 10 = 1024³ resolution)
     * @param brickSize Brick side length (e.g., 8³ = 512 voxels)
     */
    void initialize(
        uint64_t vramBudgetMB,
        uint32_t octreeDepth = 10,
        uint32_t brickSize = 8);

    /**
     * Build sparse voxel octree from GaiaVoxelWorld.
     * Creates hierarchical structure with mipmap levels.
     *
     * @param world Source voxel data
     * @param maxDepth Max octree depth (controls resolution)
     */
    void buildOctree(const GaiaVoxelWorld& world, uint32_t maxDepth);

    /**
     * GPU ray-casting with usage tracking.
     * Ray-caster traverses octree, accesses bricks, and records usage.
     *
     * @param cmd Vulkan command buffer
     * @param camera Camera parameters
     * @param targetImage Output render target
     */
    void rayCast(
        VkCommandBuffer cmd,
        const Camera& camera,
        VkImage targetImage);

    /**
     * Update cache based on GPU usage data.
     * Reads usage counters from GPU, evicts unused bricks, loads requested bricks.
     *
     * GIGAVOXELS ALGORITHM:
     * 1. Read GPU usage counters (which bricks were accessed)
     * 2. Read GPU request queue (which bricks are missing)
     * 3. Evict least-recently-used bricks (LRU based on lastUsedFrame)
     * 4. Load most-frequently-requested bricks (priority queue)
     * 5. Update octree node pointers
     *
     * @param currentFrame Current frame number (for timestamp-based LRU)
     * @param maxBricksPerFrame Streaming budget (e.g., 64 bricks/frame)
     */
    void updateCache(uint32_t currentFrame, uint32_t maxBricksPerFrame = 64);

    /**
     * Asynchronous brick production: Generate brick data on-demand.
     * Called when GPU requests brick that doesn't exist.
     *
     * Options:
     * 1. Load from disk (pre-baked data)
     * 2. Generate procedurally (ImplicitVoxelGenerator)
     * 3. Upsample from parent brick (mipmap filtering)
     *
     * @param brickID Brick identifier (octree node + level)
     * @param outBrickData Output buffer for brick voxel data
     */
    void produceBrickAsync(
        uint64_t brickID,
        std::span<VoxelData> outBrickData);

    /**
     * Query cache statistics (for debugging and tuning).
     */
    struct CacheStats {
        uint64_t totalBricks;           // Total bricks in octree
        uint64_t cachedBricks;          // Bricks currently in VRAM
        uint64_t usedBricksThisFrame;   // Bricks accessed by GPU this frame
        uint64_t requestedBricks;       // Bricks GPU wants to load
        float cacheHitRate;             // % of accessed bricks already cached
        float memoryUsageMB;            // VRAM used by brick pool

        // Usage distribution (for cache tuning)
        std::array<uint32_t, 10> usageHistogram; // Buckets: 0-9, 10-99, 100-999, etc.
    };
    CacheStats getStats() const;

private:
    // Octree structure (GPU buffer)
    VkBuffer octreeBuffer_;             // SVONode[nodeCount]
    uint32_t octreeDepth_;
    uint32_t octreeNodeCount_;

    // Brick pool (GPU buffer)
    VkBuffer brickPoolBuffer_;          // VoxelData[brickCapacity * brickSize³]
    uint32_t brickCapacity_;
    uint32_t brickSize_;

    // Usage tracking (GPU buffers)
    GPUUsageTracker usageTracker_;

    // Cache replacement policy
    struct BrickCacheEntry {
        uint64_t brickID;
        uint32_t poolSlot;              // Index in brick pool
        uint32_t lastUsedFrame;         // For LRU eviction
        uint32_t usageCount;            // For frequency-based prioritization
        bool isLoaded;
    };
    std::unordered_map<uint64_t, BrickCacheEntry> brickCache_;

    // Priority queue for brick loading (sorted by usage frequency)
    std::priority_queue<
        std::pair<uint32_t, uint64_t>,  // (usageCount, brickID)
        std::vector<std::pair<uint32_t, uint64_t>>,
        std::greater<>                   // Min-heap (lowest usage first for eviction)
    > evictionQueue_;

    // Async brick production thread pool
    ThreadPool brickProducerThreads_;

    // Mipmap pyramid (coarser LODs for distant rendering)
    struct MipmapLevel {
        VkBuffer brickBuffer;           // Downsampled brick data
        uint32_t brickCount;
        uint32_t resolution;            // Voxels per brick at this level
    };
    std::array<MipmapLevel, 8> mipmapPyramid_; // 8 LOD levels

    // Helper: Compute brick priority (frequency × recency)
    float computeBrickPriority(const BrickCacheEntry& entry, uint32_t currentFrame) const;

    // Helper: Evict least-valuable brick
    uint32_t evictBrick();

    // Helper: Load brick into pool slot
    void loadBrick(uint64_t brickID, uint32_t poolSlot);
};

// GPU Shader: Ray-Casting with Usage Tracking
// GLSL compute shader
layout(std430, set = 0, binding = 0) buffer OctreeNodes {
    SVONode nodes[];
};

layout(std430, set = 0, binding = 1) buffer BrickPool {
    VoxelData bricks[];
};

layout(std430, set = 0, binding = 2) buffer UsageCounters {
    uint usageCounters[];
};

layout(std430, set = 0, binding = 3) buffer RequestQueue {
    uint requestQueueSize;
    uint requestedBricks[MAX_REQUESTS];
};

void main() {
    // Ray-cast through octree
    Ray ray = generateCameraRay(gl_GlobalInvocationID.xy);

    uint nodeIdx = 0; // Start at root
    float t = 0.0;

    while (t < maxDistance && nodeIdx != 0) {
        SVONode node = nodes[nodeIdx];

        // Mark brick as used (GPU usage tracking!)
        if (node.brickPointer != 0) {
            atomicAdd(usageCounters[node.brickPointer], 1);

            // Sample brick data
            vec3 localPos = (ray.origin + ray.dir * t) - getBrickWorldPos(nodeIdx);
            VoxelData voxel = sampleBrick(node.brickPointer, localPos);

            if (voxel.density > 0.0) {
                imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(voxel.color, 1.0));
                return;
            }
        } else {
            // Brick not loaded - request it!
            uint requestIdx = atomicAdd(requestQueueSize, 1);
            if (requestIdx < MAX_REQUESTS) {
                requestedBricks[requestIdx] = nodeIdx;
            }

            // Use parent brick's mipmap for now (graceful degradation)
            VoxelData coarseLOD = sampleParentBrick(nodeIdx);
            if (coarseLOD.density > 0.0) {
                imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(coarseLOD.color * 0.5, 1.0));
                return;
            }
        }

        // Traverse octree
        nodeIdx = getNextNode(ray, nodeIdx, t);
    }
}
```

**GigaVoxels Cache Update Algorithm:**

```cpp
void GigaVoxelsCache::updateCache(uint32_t currentFrame, uint32_t maxBricksPerFrame) {
    // Step 1: Read GPU usage counters
    std::vector<uint32_t> usageCounts(brickCapacity_);
    vkCmdCopyBuffer(cmd, usageTracker_.usageCounterBuffer, stagingBuffer, ...);
    // Wait for GPU → CPU transfer
    readStagingBuffer(usageCounts.data(), usageCounts.size() * sizeof(uint32_t));

    // Step 2: Update cache entries with usage data
    for (auto& [brickID, entry] : brickCache_) {
        if (usageCounts[entry.poolSlot] > 0) {
            entry.lastUsedFrame = currentFrame;
            entry.usageCount += usageCounts[entry.poolSlot];
        }
    }

    // Step 3: Read GPU request queue (missing bricks)
    uint32_t requestCount;
    std::vector<uint64_t> requestedBricks;
    vkCmdCopyBuffer(cmd, usageTracker_.requestQueueBuffer, stagingBuffer, ...);
    readStagingBuffer(&requestCount, sizeof(uint32_t));
    requestedBricks.resize(requestCount);
    readStagingBuffer(requestedBricks.data(), requestCount * sizeof(uint64_t));

    // Step 4: Sort requests by priority (most-used first)
    std::sort(requestedBricks.begin(), requestedBricks.end(), [&](uint64_t a, uint64_t b) {
        // Estimate priority: closer to camera = higher priority
        float distA = estimateDistanceToCamera(a);
        float distB = estimateDistanceToCamera(b);
        return distA < distB;
    });

    // Step 5: Evict LRU bricks to make room
    uint32_t bricksToEvict = std::min(requestCount, maxBricksPerFrame);
    std::vector<uint64_t> evictedBricks;

    for (uint32_t i = 0; i < bricksToEvict; ++i) {
        // Find LRU brick
        uint64_t lruBrickID = 0;
        uint32_t oldestFrame = currentFrame;

        for (auto& [brickID, entry] : brickCache_) {
            if (entry.lastUsedFrame < oldestFrame) {
                oldestFrame = entry.lastUsedFrame;
                lruBrickID = brickID;
            }
        }

        if (lruBrickID != 0) {
            uint32_t freedSlot = brickCache_[lruBrickID].poolSlot;
            evictedBricks.push_back(lruBrickID);
            brickCache_.erase(lruBrickID);

            // Load new brick into freed slot
            uint64_t newBrickID = requestedBricks[i];
            produceBrickAsync(newBrickID, getBrickDataSpan(freedSlot));

            brickCache_[newBrickID] = {
                .brickID = newBrickID,
                .poolSlot = freedSlot,
                .lastUsedFrame = currentFrame,
                .usageCount = 0,
                .isLoaded = false // Will be set when async load completes
            };
        }
    }

    // Step 6: Clear GPU usage counters for next frame
    vkCmdFillBuffer(cmd, usageTracker_.usageCounterBuffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, usageTracker_.requestQueueBuffer, 0, sizeof(uint32_t), 0);
}
```

**Benefits:**

1. **GPU-Driven Streaming:**
   - GPU determines what to load (zero CPU overhead for visibility)
   - Eliminates CPU-GPU sync for frustum culling
   - Naturally handles complex camera motion

2. **Usage-Based Cache Replacement:**
   - Evicts bricks that GPU didn't access (true LRU)
   - Prioritizes frequently-accessed bricks
   - Adapts to actual rendering patterns (not heuristics)

3. **Graceful Degradation:**
   - Missing bricks render at coarser LOD (mipmap parent)
   - No holes or pop-in artifacts
   - Smooth LOD transitions

4. **Asynchronous Brick Production:**
   - Generate/load bricks on background threads
   - Zero frame hitches for streaming
   - Supports both disk loading and procedural generation

5. **Scalability:**
   - Handles infinite-resolution octrees (limited only by depth)
   - Constant VRAM usage regardless of dataset size
   - Efficient for both static and dynamic scenes

**Performance Characteristics:**

| Metric | Traditional Paging | GigaVoxels | Improvement |
|--------|-------------------|------------|-------------|
| Cache Hit Rate | 60-70% (heuristic) | 90-95% (usage-based) | **1.3-1.5x** |
| CPU Overhead | 5-10ms (frustum culling) | <1ms (GPU-driven) | **5-10x faster** |
| Pop-in Artifacts | Visible (missing data) | None (mipmap fallback) | **Quality improvement** |
| Memory Efficiency | Fixed tiles | Adaptive (SVO) | **2-4x better** |

**Implementation Complexity:** **Very High**

- Sparse voxel octree construction (with mipmap pyramid)
- GPU usage tracking (atomic counters in shaders)
- GPU-CPU readback pipeline (usage counters + request queue)
- LRU eviction with priority sorting
- Asynchronous brick production (multi-threaded)
- GPU ray-casting with octree traversal
- Mipmap filtering (downsample parent bricks)
- Estimated effort: **10-12 weeks**

**Cost:**
- VRAM: Same as base out-of-core (1GB brick pool)
- CPU: <1ms/frame for cache update (vs 5-10ms for traditional)
- GPU: ~0.5ms for usage tracking (atomic operations)
- Memory: +10% for octree structure (vs flat brick array)

**Integration with Existing Features:**

1. **Out-of-Core Rendering (2.4):**
   - Replace LRU cache with usage-based GigaVoxels cache
   - Use GPU request queue instead of CPU frustum culling

2. **Implicit Data (2.7):**
   - Integrate with asynchronous brick production
   - Generate bricks from ImplicitVoxelGenerator on-demand

3. **Multi-Space Transforms (2.3):**
   - Build per-space octrees
   - Transform ray into local space before traversal

4. **Persistent Storage (2.2):**
   - Save octree structure to disk
   - Stream bricks from archive on GPU request

### 2.11.1 Extending Existing ESVO for GigaVoxels

**Context:** VIXEN already has a complete ESVO (Efficient Sparse Voxel Octrees) implementation based on Laine & Karras (2010). This section explains how to modify the existing ESVO structure to support GigaVoxels features.

**Existing ESVO Architecture (from `libraries/SVO/`):**

```cpp
// Current: libraries/SVO/include/SVOTypes.h
struct ChildDescriptor {
    // Hierarchy (32 bits)
    uint32_t childPointer  : 15;  // Offset to first child
    uint32_t farBit        : 1;   // Indirect reference flag
    uint32_t validMask     : 8;   // Which children exist
    uint32_t leafMask      : 8;   // Which children are leaves

    // Brick/Contour data (32 bits)
    uint32_t contourPointer : 24; // Brick index OR contour offset
    uint32_t contourMask    : 8;  // Brick flags OR contour mask
};
```

**Compatibility Analysis:**

| ESVO Feature | GigaVoxels Requirement | Compatibility |
|--------------|------------------------|---------------|
| 64-bit `ChildDescriptor` | Needs brick pointer field | ✅ **Perfect fit** - `contourPointer` can be brick pool slot |
| Hierarchical octree | Sparse voxel octree structure | ✅ **Direct match** |
| Brick storage at leaves | Virtual brick pool | ✅ **Excellent foundation** |
| ESVO traversal (PUSH/ADVANCE/POP) | GPU ray-casting | ✅ **Already implemented** |
| Entity references | Usage tracking | ✅ **Compatible** - `gaia::ecs::Entity` IDs |
| Contour system | Mipmap pyramid | ⚠️ **Needs extension** - contours are single-LOD |

**Verdict:** ESVO is an **excellent foundation** for GigaVoxels! Primary modifications needed:
1. Extend `ChildDescriptor` for brick pool indirection
2. Add mipmap pyramid storage
3. Integrate GPU usage tracking into ESVO traversal shaders
4. Build cache management on top of existing brick system

---

### 2.11.2 Concrete Modifications to ESVO

#### Modification 1: Extended ChildDescriptor for Virtual Bricks

**Current:**
```cpp
struct ChildDescriptor {
    uint32_t contourPointer : 24; // Direct brick index
    uint32_t contourMask    : 8;  // Flags
};
```

**GigaVoxels Extension:**
```cpp
struct ChildDescriptor {
    // ... hierarchy fields unchanged ...

    // Brick data (32 bits) - EXTENDED INTERPRETATION
    uint32_t brickPointer : 24; // Now: Virtual brick ID (not pool slot!)
    uint32_t brickFlags   : 8;  // Flags:
                                 //   Bit 0: inVRAM (1 = loaded, 0 = missing)
                                 //   Bit 1-3: mipLevel (0-7)
                                 //   Bit 4: isDirty (needs flush to disk)
                                 //   Bit 5-7: reserved
};
```

**Key Changes:**
- `brickPointer` is now a **virtual brick ID**, not a physical pool slot
- Indirection table maps `brickPointer` → pool slot
- `inVRAM` flag indicates if brick is currently cached
- `mipLevel` enables per-brick LOD tracking

**Backward Compatibility:**
- Old ESVO files: Set `inVRAM=1`, `mipLevel=0` (all bricks at full resolution)
- New GigaVoxels mode: `inVRAM` updated by cache manager

---

#### Modification 2: Brick Pool Indirection Table

**New Structure:**
```cpp
// New file: libraries/SVO/include/BrickPoolIndirection.h

struct BrickPoolIndirection {
    /**
     * Maps virtual brick ID → physical pool slot.
     * Updated by GigaVoxels cache manager.
     */
    std::unordered_map<uint32_t, uint32_t> virtualToPhysical;

    /**
     * Reverse map: pool slot → virtual brick ID.
     * Used during eviction to update ChildDescriptors.
     */
    std::vector<uint32_t> physicalToVirtual;

    /**
     * Get physical pool slot for virtual brick ID.
     * Returns INVALID_SLOT if brick not loaded.
     */
    static constexpr uint32_t INVALID_SLOT = 0xFFFFFFu;

    uint32_t getPoolSlot(uint32_t virtualBrickID) const {
        auto it = virtualToPhysical.find(virtualBrickID);
        return (it != virtualToPhysical.end()) ? it->second : INVALID_SLOT;
    }

    /**
     * Allocate pool slot for virtual brick.
     * Called by cache manager during brick loading.
     */
    void mapBrick(uint32_t virtualBrickID, uint32_t poolSlot) {
        virtualToPhysical[virtualBrickID] = poolSlot;
        physicalToVirtual[poolSlot] = virtualBrickID;
    }

    /**
     * Free pool slot (brick evicted).
     */
    void unmapBrick(uint32_t virtualBrickID) {
        auto it = virtualToPhysical.find(virtualBrickID);
        if (it != virtualToPhysical.end()) {
            uint32_t poolSlot = it->second;
            physicalToVirtual[poolSlot] = INVALID_SLOT;
            virtualToPhysical.erase(it);
        }
    }
};
```

**Integration with ESVO:**
```cpp
// libraries/SVO/include/ISVOStructure.h - ADD MEMBER

class ISVOStructure {
    // ... existing members ...

    // NEW: GigaVoxels indirection (optional, only if streaming enabled)
    std::unique_ptr<BrickPoolIndirection> brickIndirection_;

public:
    // Enable GigaVoxels mode
    void enableStreaming(uint32_t brickPoolCapacity) {
        brickIndirection_ = std::make_unique<BrickPoolIndirection>();
        brickIndirection_->physicalToVirtual.resize(brickPoolCapacity,
                                                     BrickPoolIndirection::INVALID_SLOT);
    }

    // Check if brick is cached
    bool isBrickCached(uint32_t virtualBrickID) const {
        if (!brickIndirection_) return true; // Non-streaming mode
        return brickIndirection_->getPoolSlot(virtualBrickID) !=
               BrickPoolIndirection::INVALID_SLOT;
    }
};
```

---

#### Modification 3: GPU Shader Integration

**Current ESVO Traversal:** `shaders/ESVOTraversal.glsl`

**GigaVoxels Extension:**
```glsl
// shaders/GigaVoxels-ESVO-Traversal.glsl

// EXISTING ESVO bindings (unchanged)
layout(std430, set = 0, binding = 0) buffer OctreeNodes {
    ChildDescriptor nodes[];
};

// NEW: GigaVoxels bindings
layout(std430, set = 0, binding = 1) buffer BrickIndirectionTable {
    uint virtualToPhysical[]; // Virtual brick ID → pool slot
};

layout(std430, set = 0, binding = 2) buffer BrickPool {
    VoxelData bricks[]; // Flat array: poolSlot * 512 + localOffset
};

layout(std430, set = 0, binding = 3) buffer UsageCounters {
    uint usageCounters[]; // GPU usage tracking
};

layout(std430, set = 0, binding = 4) buffer RequestQueue {
    uint requestQueueSize;
    uint requestedBricks[MAX_REQUESTS];
};

// MODIFIED: ESVO PUSH phase with GigaVoxels brick access
void executePushPhase(inout TraversalState state, int childIdx) {
    // ... EXISTING ESVO PUSH logic (stack save, bounds calc, etc.) ...

    ChildDescriptor desc = nodes[state.descriptorIndex];

    // NEW: Check if this is a brick leaf
    if (desc.isLeaf(childIdx)) {
        uint virtualBrickID = desc.brickPointer;
        uint brickFlags = desc.brickFlags;
        bool inVRAM = (brickFlags & 0x01) != 0;

        if (inVRAM) {
            // Brick is cached - access via indirection
            uint poolSlot = virtualToPhysical[virtualBrickID];

            // GIGAVOXELS: Track brick usage
            atomicAdd(usageCounters[poolSlot], 1);

            // Sample brick data
            vec3 localPos = (ray.origin + ray.dir * t) - state.pos;
            VoxelData voxel = sampleBrick(poolSlot, localPos);

            if (voxel.density > 0.0) {
                // Hit!
                imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy),
                          vec4(voxel.color, 1.0));
                return;
            }
        } else {
            // Brick not loaded - request it!
            uint requestIdx = atomicAdd(requestQueueSize, 1);
            if (requestIdx < MAX_REQUESTS) {
                requestedBricks[requestIdx] = virtualBrickID;
            }

            // GIGAVOXELS: Graceful degradation
            // Sample parent brick's mipmap (if available)
            uint parentMipLevel = (brickFlags >> 1) & 0x07;
            if (parentMipLevel < 7) {
                // Try parent LOD
                uint parentBrickID = getParentBrickID(virtualBrickID);
                uint parentSlot = virtualToPhysical[parentBrickID];
                if (parentSlot != INVALID_SLOT) {
                    VoxelData coarseLOD = sampleBrickCoarse(parentSlot, localPos * 2.0);
                    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy),
                              vec4(coarseLOD.color * 0.5, 1.0)); // Dimmed for LOD
                    return;
                }
            }
        }
    }

    // ... EXISTING ESVO ADVANCE/POP logic ...
}
```

---

#### Modification 4: Mipmap Pyramid Generation

**New Builder Extension:**
```cpp
// libraries/SVO/src/SVOBuilder.cpp - ADD METHOD

void SVOBuilder::buildMipmapPyramid(
    const std::vector<ChildDescriptor>& baseOctree,
    std::vector<MipmapLevel>& outMipmap) {

    outMipmap.resize(8); // 8 LOD levels (0 = full res, 7 = 1/128 res)

    // Level 0: Copy base octree
    outMipmap[0].bricks = baseOctree; // Full resolution

    // Levels 1-7: Downsample
    for (int level = 1; level < 8; ++level) {
        const auto& prevLevel = outMipmap[level - 1];
        auto& currLevel = outMipmap[level];

        // For each brick in previous level, create downsampled version
        for (size_t i = 0; i < prevLevel.bricks.size(); i += 8) {
            // Downsample 8 child bricks into 1 parent brick
            ChildDescriptor parentDesc = downsampleBricks(
                std::span(&prevLevel.bricks[i], 8));

            currLevel.bricks.push_back(parentDesc);
        }
    }
}

ChildDescriptor SVOBuilder::downsampleBricks(std::span<const ChildDescriptor> children) {
    // Average voxel data from 8 children
    // This is similar to mipmap generation for textures

    ChildDescriptor parent{};
    parent.childPointer = 0; // No children (this is a leaf at this LOD)
    parent.validMask = 0xFF; // All slots valid (downsampled)
    parent.leafMask = 0xFF;  // All slots are leaves

    // For each of the 512 voxels in parent brick (8³)
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                // Sample from 2³ voxels in child bricks
                glm::vec3 avgColor(0);
                float avgDensity = 0.0f;
                int sampleCount = 0;

                for (int dz = 0; dz < 2; ++dz) {
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            int childX = x * 2 + dx;
                            int childY = y * 2 + dy;
                            int childZ = z * 2 + dz;

                            // Determine which child brick
                            int childIdx = (childZ / 8) * 4 + (childY / 8) * 2 + (childX / 8);
                            int localX = childX % 8;
                            int localY = childY % 8;
                            int localZ = childZ % 8;

                            // Get voxel from child brick
                            VoxelData voxel = getVoxelFromBrick(children[childIdx],
                                                                 localX, localY, localZ);

                            avgColor += voxel.color;
                            avgDensity += voxel.density;
                            sampleCount++;
                        }
                    }
                }

                // Average samples
                VoxelData downsampled;
                downsampled.color = avgColor / float(sampleCount);
                downsampled.density = avgDensity / float(sampleCount);

                // Store in parent brick
                setVoxelInBrick(parent, x, y, z, downsampled);
            }
        }
    }

    return parent;
}
```

---

#### Modification 5: Cache Manager Integration

**New Class:**
```cpp
// libraries/SVO/include/GigaVoxelsCacheManager.h

class GigaVoxelsCacheManager {
public:
    /**
     * Initialize cache with ESVO structure.
     */
    void initialize(
        ISVOStructure* svoStructure,
        BrickPoolIndirection* indirection,
        uint32_t brickPoolCapacity) {

        svo_ = svoStructure;
        indirection_ = indirection;
        capacity_ = brickPoolCapacity;
    }

    /**
     * Update cache based on GPU usage data.
     * Reads usage counters from GPU, evicts LRU bricks, loads requested bricks.
     */
    void updateCache(
        VkCommandBuffer cmd,
        const std::vector<uint32_t>& gpuUsageCounts,
        const std::vector<uint32_t>& gpuRequestedBricks,
        uint32_t currentFrame) {

        // Step 1: Update usage timestamps
        for (auto& [virtualBrickID, entry] : brickCache_) {
            uint32_t poolSlot = indirection_->getPoolSlot(virtualBrickID);
            if (poolSlot != BrickPoolIndirection::INVALID_SLOT &&
                gpuUsageCounts[poolSlot] > 0) {
                entry.lastUsedFrame = currentFrame;
                entry.usageCount += gpuUsageCounts[poolSlot];
            }
        }

        // Step 2: Evict LRU bricks to make room
        for (uint32_t requestedBrickID : gpuRequestedBricks) {
            if (indirection_->getPoolSlot(requestedBrickID) !=
                BrickPoolIndirection::INVALID_SLOT) {
                continue; // Already loaded
            }

            // Find LRU brick
            uint32_t lruBrickID = findLRUBrick(currentFrame);
            uint32_t freedSlot = indirection_->getPoolSlot(lruBrickID);

            // Evict LRU brick
            indirection_->unmapBrick(lruBrickID);

            // Update ChildDescriptor inVRAM flag
            updateBrickFlags(lruBrickID, /*inVRAM=*/false);

            // Load new brick into freed slot
            loadBrickIntoSlot(requestedBrickID, freedSlot);

            // Map new brick
            indirection_->mapBrick(requestedBrickID, freedSlot);

            // Update ChildDescriptor inVRAM flag
            updateBrickFlags(requestedBrickID, /*inVRAM=*/true);

            brickCache_[requestedBrickID] = {
                .lastUsedFrame = currentFrame,
                .usageCount = 0,
                .isLoaded = true
            };
        }
    }

private:
    ISVOStructure* svo_;
    BrickPoolIndirection* indirection_;
    uint32_t capacity_;

    struct CacheEntry {
        uint32_t lastUsedFrame;
        uint32_t usageCount;
        bool isLoaded;
    };
    std::unordered_map<uint32_t, CacheEntry> brickCache_;

    void updateBrickFlags(uint32_t virtualBrickID, bool inVRAM) {
        // Find ChildDescriptor containing this brick
        // (requires reverse map: brick ID → descriptor index)
        // Update brickFlags field
    }
};
```

---

### 2.11.3 Migration Path: ESVO → GigaVoxels

**Phase 1: Backward-Compatible Extension (2 weeks)**
1. Add `BrickPoolIndirection` structure (optional member)
2. Add `inVRAM` flag to `ChildDescriptor.brickFlags` (bit 0)
3. Add `enableStreaming()` API to `ISVOStructure`
4. Default behavior: `inVRAM=1` for all bricks (non-streaming mode)
5. **Result:** ESVO works identically, GigaVoxels infrastructure ready

**Phase 2: GPU Usage Tracking (2 weeks)**
1. Add GPU buffer bindings to ESVO traversal shaders
2. Implement atomic usage counters in PUSH phase
3. Add CPU readback pipeline (usage counters → staging buffer)
4. **Result:** GPU reports which bricks it accessed

**Phase 3: Mipmap Pyramid (3 weeks)**
1. Extend `SVOBuilder` to generate 8 LOD levels
2. Modify `ChildDescriptor.brickFlags` to encode `mipLevel` (bits 1-3)
3. Implement mipmap fallback in shaders (sample parent LOD if missing)
4. **Result:** Graceful degradation for missing bricks

**Phase 4: Cache Manager (3 weeks)**
1. Implement `GigaVoxelsCacheManager` (LRU eviction)
2. Integrate with ESVO's `ISVOStructure` interface
3. Add `updateCache()` call in main render loop
4. **Result:** Streaming brick pool with usage-based replacement

**Phase 5: Optimization & Polish (2 weeks)**
1. Double-buffering for usage counters (hide latency)
2. Async brick loading (background threads)
3. Compression for brick data (DXT compression)
4. **Result:** Production-ready GigaVoxels implementation

**Total Migration Effort:** 12 weeks (vs 10-12 weeks for building from scratch)

**Key Advantage:** Can develop incrementally, testing at each phase without breaking existing ESVO functionality.

---

**Research References:**
- Crassin et al., "GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering", I3D 2009
- Crassin et al., "Octree-Based Sparse Voxelization Using the GPU Hardware Rasterizer", OpenGL Insights 2012
- Kämpe et al., "High Resolution Sparse Voxel DAGs", SIGGRAPH 2013
- **Laine, S., & Karras, T.**, "Efficient Sparse Voxel Octrees", IEEE TVCG 2010 (VIXEN's current ESVO base)

---

## 3. Implementation Roadmap

### Phase 1: High-Performance Generation (4-5 weeks)

**Goals:**
- 100M voxels/second generation
- Parallel entity creation
- SIMD Morton encoding

**Deliverables:**
1. `VoxelGenerator.h/.cpp` - Parallel generation API
2. `SIMDMortonEncoding.h` - AVX2/NEON Morton encoding
3. `EntityBatch.h` - Deferred entity creation
4. Benchmarks (10M voxel generation in <0.1s)
5. Tests (thread safety, correctness)

**Success Criteria:**
- 100M+ voxels/second on 16-core CPU
- Linear scaling with core count
- Zero crashes under parallel load

---

### Phase 2: Persistent Storage (3-4 weeks)

**Goals:**
- Save/load voxel worlds
- LZ4 compression (3:1 ratio)
- Streaming load (1GB/s)

**Deliverables:**
1. `VoxelPersistence.h/.cpp` - Save/load API
2. LZ4 compression integration
3. Chunk-based file format
4. Memory-mapped file loader
5. Tests (save/load roundtrip, streaming)

**Success Criteria:**
- 10M voxels saved in <1s
- 3:1 compression ratio
- Streaming load at 500 MB/s

---

### Phase 3: Multi-Space Transforms (6-8 weeks)

**Goals:**
- Independent voxel spaces
- Per-space transforms
- Hierarchical transforms

**Deliverables:**
1. `VoxelSpace.h/.cpp` - Multi-space API
2. Transform hierarchy system
3. World-space query across spaces
4. GPU shader integration (per-instance transforms)
5. Examples (rotating car, instanced buildings)

**Success Criteria:**
- 100+ independent spaces
- <1ms transform updates
- Zero re-baking of voxel data

---

### Phase 4: Out-of-Core Rendering (8-10 weeks)

**Goals:**
- 1 billion+ voxel rendering
- Virtual brick pool
- Streaming from disk

**Deliverables:**
1. `OutOfCoreRenderer.h/.cpp` - Virtual brick pool
2. Indirection table (Morton → VRAM slot)
3. LRU brick cache
4. Async brick loading (background threads)
5. GPU shaders (indirection lookup)

**Success Criteria:**
- 1B voxels renderable (100GB dataset)
- Constant 1GB VRAM usage
- 60 FPS with streaming

---

### Phase 5: Parallel Operations (2-3 weeks)

**Goals:**
- Multi-threaded injection queues
- Budget-aware processing
- Continuous data streams

**Deliverables:**
1. `VoxelInjectionQueue.h/.cpp` - Parallel queue API
2. MPSC lock-free queue
3. Budget-aware processing
4. Stream integration
5. Tests (multi-threaded stress test)

**Success Criteria:**
- 16 threads enqueuing concurrently
- <2ms processing time for 10K voxels
- Zero lock contention

---

### Phase 6: Query Acceleration (4-5 weeks)

**Goals:**
- 1000x faster spatial queries
- BVH construction
- GPU queries

**Deliverables:**
1. `SpatialAcceleration.h/.cpp` - BVH API
2. SAH-based BVH builder
3. BVH traversal (stack-based)
4. GPU query kernels
5. Raycasting support

**Success Criteria:**
- 1000x speedup vs linear scan
- <100ms BVH build for 10M voxels
- Raycasting at 1M rays/second

---

### Phase 7: Implicit Data (3-4 weeks)

**Goals:**
- Generator-based voxels
- Lazy evaluation
- GPU evaluation

**Deliverables:**
1. `ImplicitVoxels.h/.cpp` - Generator API
2. Built-in generators (sphere, box, noise, SDF)
3. Custom generator support
4. GPU evaluation shaders
5. Materialization (implicit → explicit)

**Success Criteria:**
- 2,000,000x memory reduction
- <100ns per voxel evaluation (cached)
- GPU evaluation at 1B voxels/second

---

### Phase 8: Polish & Optimization (3-4 weeks)

**Goals:**
- Industry optimizations
- Performance tuning
- Documentation

**Deliverables:**
1. Brick compression (RLE)
2. LOD system (mip-mapped voxels)
3. GPU voxelization
4. Mesh caching
5. Comprehensive documentation
6. Migration guide

---

### Phase 9: GigaVoxels GPU-Driven Caching (10-12 weeks)

**Goals:**
- Sparse voxel octree (SVO) construction
- GPU-driven usage tracking
- Usage-based cache replacement
- GPU ray-casting with octree traversal

**Deliverables:**
1. `GigaVoxelsCache.h/.cpp` - GPU-driven caching API
2. Sparse voxel octree builder with mipmap pyramid
3. GPU usage tracking shaders (atomic counters)
4. GPU-CPU readback pipeline (usage counters + request queue)
5. LRU eviction with usage-based prioritization
6. GPU ray-casting compute shaders
7. Asynchronous brick production system
8. Integration with out-of-core renderer (2.4)
9. Integration with implicit data (2.7) for procedural bricks
10. Benchmarks (cache hit rate, CPU overhead, quality)

**Success Criteria:**
- 90-95% cache hit rate (vs 60-70% for heuristic methods)
- <1ms CPU overhead per frame (vs 5-10ms for frustum culling)
- Zero pop-in artifacts (graceful LOD degradation)
- GPU ray-casting at 60 FPS for 1B+ voxel datasets
- Successful octree build for 100M+ voxel worlds

**Risks:**
- GPU-CPU readback latency (1-2 frame delay)
- Atomic operations performance on some GPUs
- Octree construction complexity
- Integration with existing rendering pipeline

**Mitigation:**
- Double-buffering for usage counters (hide latency)
- Fallback to compute shader prefix sum (no atomics)
- Incremental octree updates (don't rebuild every frame)
- Phased integration (start with ray-casting only)

---

## 4. Cost-Benefit Analysis

### 4.1 Development Cost

| Phase | Duration | Complexity | Risk |
|-------|----------|------------|------|
| High-Perf Generation | 4-5 weeks | High | Medium |
| Persistent Storage | 3-4 weeks | Medium-High | Low |
| Multi-Space Transforms | 6-8 weeks | Very High | High |
| Out-of-Core Rendering | 8-10 weeks | Very High | High |
| Parallel Operations | 2-3 weeks | Medium | Medium |
| Query Acceleration | 4-5 weeks | High | Medium |
| Implicit Data | 3-4 weeks | Medium-High | Low |
| Polish | 3-4 weeks | Low | Low |
| **GigaVoxels Caching** | **10-12 weeks** | **Very High** | **High** |
| **Total** | **43-55 weeks** | **Very High** | **Medium-High** |

**Team Requirements:**
- 1 senior engineer (voxel engine architecture, ECS expert)
- 1 GPU engineer (compute shaders, ray-casting, Vulkan expert) **[critical for GigaVoxels]**
- 1 mid-level engineer (implementation, integration)
- 1 junior engineer (tools, testing)
- Part-time: Performance engineer for profiling

**Estimated Full-Time Equivalent:** 3.0 FTE over 11-13 months

### 4.2 Performance Benefits

| Feature | Current | Target | Improvement |
|---------|---------|--------|-------------|
| Generation Speed | 10K voxels/s | 100M voxels/s | **10,000x** |
| Max Dataset Size | 40M voxels | 1B+ voxels | **25x** |
| Query Speed | 100ms (O(N)) | 0.1ms (O(log N)) | **1,000x** |
| Memory Efficiency | 100 bytes/voxel | 0.05 bytes/voxel (implicit) | **2,000x** |
| Save/Load | N/A | 500 MB/s | **New feature** |
| Multi-Threading | Single thread | 16 threads | **16x** |
| **Cache Hit Rate** | **60-70% (heuristic)** | **90-95% (GigaVoxels)** | **1.3-1.5x** |
| **Streaming CPU Overhead** | **5-10ms/frame** | **<1ms/frame (GPU-driven)** | **5-10x faster** |

### 4.3 Memory Cost

| Feature | Overhead |
|---------|----------|
| VoxelGenerator | ~64 KB per thread |
| VoxelPersistence | ~1 MB for TOC (10M voxels) |
| VoxelSpace | ~256 bytes per space |
| OutOfCoreRenderer | 1 GB VRAM (brick pool) |
| VoxelInjectionQueue | ~64 bytes per queued voxel |
| BVH | ~48 bytes per node (~N/512 nodes) |
| ImplicitVoxels | ~48 bytes per generator |

**Typical Application:**
- 10 spaces × 256 bytes = 2.5 KB
- 1 GB VRAM for out-of-core
- BVH for 10M voxels = ~1 MB
- 10 generators × 48 bytes = 480 bytes
- **Total overhead:** ~1 GB VRAM + 2 MB RAM (negligible)

---

## 5. Risks & Mitigation

### 5.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| SIMD Morton encoding bugs | Medium | High | Extensive unit tests, scalar fallback |
| Multi-space transform artifacts | High | Medium | Per-vertex transform validation |
| Out-of-core thrashing | Medium | High | Careful brick budget, LRU tuning |
| BVH build performance | Medium | Medium | Incremental updates, SAH tuning |
| Lock contention in queues | Low | Medium | Lock-free MPSC queue |
| GPU memory fragmentation | Medium | High | Fixed brick pool allocation |

### 5.2 Schedule Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Scope creep | High | High | Strict phase boundaries, MVP focus |
| Underestimated complexity | Medium | Medium | 25% buffer in estimates |
| Key person unavailable | Low | High | Documentation, pair programming |
| ECS integration issues | Medium | Medium | Early prototyping with Gaia-ECS |

---

## 6. Success Metrics

### 6.1 Performance Metrics

| Metric | Baseline | Target | Measurement |
|--------|----------|--------|-------------|
| Generation Speed | 10K voxels/s | 100M voxels/s | Benchmark suite |
| Max Renderable Voxels | 40M | 1B+ | Out-of-core test scene |
| Query Time (1M region) | 100ms | 0.1ms | BVH benchmark |
| Save/Load Speed | N/A | 500 MB/s | Persistence benchmark |
| Memory (1M voxels) | 100 MB | 50 KB (implicit) | Memory profiler |

### 6.2 Usability Metrics

| Metric | Baseline | Target | Measurement |
|--------|----------|--------|-------------|
| API Complexity | Medium | Low | User study |
| Time to Generate 10M Voxels | N/A (too slow) | <1 second | Tutorial timing |
| Transform Update Time | N/A | <1ms | Interactive demo |

### 6.3 Correctness Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Thread Safety | 0 races | Thread sanitizer |
| Memory Leaks | 0 | Valgrind / AddressSanitizer |
| Save/Load Lossless | 100% | Roundtrip validation |
| Transform Precision | <0.01 voxel error | Regression tests |

---

## 7. Open Questions

### 7.1 Design Questions

1. **Multi-Space Rendering:** How to blend voxels from overlapping spaces?
   - Proposal: Priority-based (higher priority space occludes)

2. **Resolution Scaling:** Interpolation method for upscaling?
   - Proposal: Trilinear interpolation (smooth) or nearest-neighbor (sharp)

3. **Implicit Evaluation Cache:** How long to cache evaluated voxels?
   - Proposal: LRU cache with 1M voxel capacity

4. **BVH Rebuild Frequency:** Full rebuild vs incremental update threshold?
   - Proposal: Incremental if <10% voxels modified, else rebuild

5. **Brick Size:** 8³ (512 voxels) vs 16³ (4096 voxels)?
   - Proposal: 8³ (better streaming granularity)

### 7.2 Implementation Questions

1. **Morton Encoding SIMD:** AVX2 only or NEON support for ARM?
   - Proposal: Both (AVX2 for x64, NEON for ARM/Apple Silicon)

2. **Compression:** LZ4 or ZSTD default?
   - Proposal: LZ4 (faster), ZSTD option for offline assets

3. **GPU Query:** Compute shader or rasterization-based?
   - Proposal: Compute shader (more flexible)

4. **Thread Pool Size:** Fixed or dynamic?
   - Proposal: `std::thread::hardware_concurrency()` (dynamic)

---

## 8. Related Documentation

- [[GaiaVoxelWorld|GaiaVoxelWorld Library]]
- [[timeline-execution-system|Timeline Execution System Proposal]]
- [[../../01-Architecture/Voxel-Architecture|Voxel Engine Architecture]]
- [[../../Libraries/Core/MortonEncoding|Morton Encoding Reference]]

---

## 9. Conclusion

This proposal transforms GaiaVoxelWorld from a basic sparse voxel storage system into an industrial-strength backend capable of:

**Key Benefits:**
- **10,000x faster generation** (100M voxels/second vs 10K)
- **25x larger datasets** (1B voxels vs 40M)
- **1,000x faster queries** (BVH acceleration)
- **2,000,000x memory reduction** (implicit generators)
- **90-95% cache hit rate** (GigaVoxels usage-based caching vs 60-70% heuristic)
- **5-10x lower CPU overhead** (GPU-driven streaming vs CPU frustum culling)
- **Zero pop-in artifacts** (graceful LOD degradation with mipmap fallback)
- **Persistent storage** (save/load with compression)
- **Multi-space transforms** (dynamic voxel objects)
- **Out-of-core rendering** (datasets exceeding VRAM)
- **Parallel operations** (multi-threaded, queue-based)

**Key Challenges:**
- Very high implementation complexity (11-13 months, 3.0 FTE)
- Multi-space transform artifacts
- Out-of-core VRAM budget tuning
- SIMD Morton encoding correctness
- ECS integration complexity
- **GPU-CPU readback latency for GigaVoxels**
- **Sparse voxel octree construction complexity**

**Recommendation:** Proceed with phased implementation, starting with Phase 1 (High-Performance Generation). Re-evaluate after Phase 1 based on performance results and API ergonomics. Phases 1-2-5-6 provide immediate value with lower risk, while Phases 3-4-7-9 unlock advanced features. **Phase 9 (GigaVoxels) is the crown jewel - research-backed GPU-driven caching that dramatically improves streaming quality and performance.**

**Implementation Priority Tiers:**

**Tier 1 (Foundation - Immediate Value):**
- Phase 1: High-Performance Generation
- Phase 2: Persistent Storage
- Phase 5: Parallel Operations

**Tier 2 (Advanced Features):**
- Phase 6: Query Acceleration (BVH)
- Phase 7: Implicit Data
- Phase 8: Polish & Optimization

**Tier 3 (Cutting-Edge Research):**
- Phase 4: Out-of-Core Rendering
- **Phase 9: GigaVoxels GPU-Driven Caching** (requires Phase 4)
- Phase 3: Multi-Space Transforms

**Next Steps:**
1. Review and approve this proposal
2. Allocate engineering resources (3.0 FTE, including GPU specialist for GigaVoxels)
3. Begin Phase 1 implementation (Parallel Generation + SIMD)
4. Design review after Phase 1 (validate 100M voxels/s target)
5. Continue to Tier 1 phases if Phase 1 successful
6. Prototype GigaVoxels (Phase 9) after Phase 4 completion
7. Evaluate GigaVoxels cache hit rate improvements (target: 90%+)

---

*Proposal Author: Claude (VIXEN Architect)*
*Date: 2025-12-31*
*Version: 1.1*
*Based on: GaiaVoxelWorld current architecture + industry voxel engine best practices + GigaVoxels research (Crassin et al., 2009)*
