#pragma once

#include "ISVOStructure.h"
#include "VoxelInjection.h"
#include <DynamicVoxelStruct.h>
#include <memory>
#include <vector>

namespace SVO {

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
 * - Worker thread pool: Process queue in background using batch processing
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
     * Does NOT take ownership of octree or registry.
     */
    explicit VoxelInjectionQueue(
        ISVOStructure* targetOctree,
        ::VoxelData::AttributeRegistry* registry = nullptr,
        const Config& config = Config{});
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
    bool enqueue(const glm::vec3& position, const ::VoxelData::DynamicVoxelScalar& data);

    /**
     * Enqueue batch of voxels.
     * More efficient than individual enqueue() calls.
     */
    size_t enqueueBatch(const std::vector<::VoxelData::DynamicVoxelScalar>& voxels);

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

} // namespace SVO
