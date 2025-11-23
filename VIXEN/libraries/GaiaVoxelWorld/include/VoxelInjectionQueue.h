#pragma once

#include "VoxelComponents.h"
#include "ComponentData.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace GaiaVoxel {

// Forward declaration
class GaiaVoxelWorld;

/**
 * VoxelInjectionQueue - Async voxel entity creation pipeline.
 *
 * Architecture:
 * - Lock-free ring buffer for voxel creation requests
 * - Worker thread pool creates entities via GaiaVoxelWorld::createVoxelsBatch()
 * - Stores created entity IDs for optional SVO indexing
 *
 * Thread-safe:
 * - enqueue(): Lock-free, non-blocking (multiple producers)
 * - Worker threads: Batch entity creation via Gaia ECS
 * - getCreatedEntities(): Thread-safe access to entity buffer
 *
 * Example:
 *   GaiaVoxelWorld world;
 *   VoxelInjectionQueue queue(world);
 *   queue.start(4); // 4 worker threads
 *
 *   // Enqueue voxel creation
 *   queue.enqueue(glm::vec3(10, 5, 3), 1.0f, red, normal);
 *
 *   // Get created entities for SVO insertion
 *   auto entities = queue.getCreatedEntities();
 *   svoInjector.insertEntities(entities);
 *
 *   queue.stop();
 */
class VoxelInjectionQueue {
public:
    /**
     * Create injection queue for target world.
     * @param world GaiaVoxelWorld to create entities in
     * @param capacity Ring buffer size (default: 65536)
     */
    explicit VoxelInjectionQueue(GaiaVoxelWorld& world, size_t capacity = 65536);
    ~VoxelInjectionQueue();

    // Disable copy (thread synchronization state)
    VoxelInjectionQueue(const VoxelInjectionQueue&) = delete;
    VoxelInjectionQueue& operator=(const VoxelInjectionQueue&) = delete;

    // ========================================================================
    // Queue Control
    // ========================================================================

    /**
     * Start background worker threads.
     * @param numThreads Number of worker threads (default: 1)
     */
    void start(size_t numThreads = 1);

    /**
     * Stop worker threads and flush queue.
     * Blocks until all pending requests are processed.
     */
    void stop();

    /**
     * Check if queue is running.
     */
    bool isRunning() const;

    // ========================================================================
    // Enqueue Operations
    // ========================================================================

    /**
     * Enqueue single voxel creation request.
     * Lock-free, non-blocking.
     * @return false if queue is full
     */
    bool enqueue(const VoxelCreationRequest& request);

    // ========================================================================
    // Entity Access
    // ========================================================================

    /**
     * Get all created entities since last call.
     * Thread-safe. Clears internal buffer after retrieval.
     * @return Vector of entity IDs ready for SVO insertion
     */
    std::vector<gaia::ecs::Entity> getCreatedEntities();

    /**
     * Peek at created entities without clearing buffer.
     */
    std::vector<gaia::ecs::Entity> peekCreatedEntities() const;

    /**
     * Get count of created entities without copying.
     */
    size_t getCreatedEntityCount() const;

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        size_t pendingCount;       // Requests waiting in queue
        size_t processedCount;     // Total requests processed
        size_t entitiesCreated;    // Total entities created
        size_t failedCount;        // Failed entity creations
        bool isProcessing;         // Worker threads active
    };

    Stats getStats() const;

    /**
     * Manually flush queue (blocks until empty).
     */
    void flush();

private:
    // Use VoxelCreationRequest directly instead of duplicating structure
    using QueueEntry = VoxelCreationRequest;

    GaiaVoxelWorld& m_world;
    size_t m_capacity;

    // Lock-free ring buffer
    std::vector<QueueEntry> m_ringBuffer;
    std::atomic<size_t> m_readIndex{0};
    std::atomic<size_t> m_writeIndex{0};

    // Worker threads
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{false};
    std::condition_variable m_cv;
    std::mutex m_cvMutex;

    // Created entities buffer
    std::vector<gaia::ecs::Entity> m_createdEntities;
    mutable std::mutex m_createdEntitiesMutex;

    // Statistics
    std::atomic<size_t> m_processedCount{0};
    std::atomic<size_t> m_entitiesCreated{0};
    std::atomic<size_t> m_failedCount{0};

    // Worker thread entry point
    void processWorker();
};

} // namespace GaiaVoxel
