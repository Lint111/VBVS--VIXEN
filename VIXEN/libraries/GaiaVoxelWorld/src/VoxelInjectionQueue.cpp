#define NOMINMAX  // Prevent Windows min/max macros
#include "VoxelInjectionQueue.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include <algorithm>
#include <iostream>

namespace GaiaVoxel {

VoxelInjectionQueue::VoxelInjectionQueue(GaiaVoxelWorld& world, size_t capacity)
    : m_world(world), m_capacity(capacity) {
    m_ringBuffer.resize(m_capacity);
    m_createdEntities.reserve(m_capacity / 4); // Reserve 25% for typical usage
}

VoxelInjectionQueue::~VoxelInjectionQueue() {
    if (m_running.load(std::memory_order_acquire)) {
        stop();
    }
}

// ============================================================================
// Queue Control
// ============================================================================

void VoxelInjectionQueue::start(size_t numThreads) {
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return; // Already running
    }

    // Spawn worker threads
    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this]() { processWorker(); });
    }
}

void VoxelInjectionQueue::stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return; // Not running
    }

    // Wake all workers
    m_cv.notify_all();

    // Join worker threads
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

bool VoxelInjectionQueue::isRunning() const {
    return m_running.load(std::memory_order_acquire);
}

// ============================================================================
// Enqueue Operations
// ============================================================================

bool VoxelInjectionQueue::enqueue(const VoxelCreationRequest& request) {

    size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
    size_t nextWrite = (currentWrite + 1) % m_capacity;

    // Check if queue is full
    if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
        return false; // Queue full
    }

    // Write request to queue
    m_ringBuffer[currentWrite] = request;

    m_writeIndex.store(nextWrite, std::memory_order_release);

    // Wake up one worker
    m_cv.notify_one();

    return true;
}

// ============================================================================
// Entity Access
// ============================================================================

std::vector<gaia::ecs::Entity> VoxelInjectionQueue::getCreatedEntities() {
    std::lock_guard<std::mutex> lock(m_createdEntitiesMutex);
    std::vector<gaia::ecs::Entity> result;
    result.swap(m_createdEntities); // Move semantics, no copy
    return result;
}

std::vector<gaia::ecs::Entity> VoxelInjectionQueue::peekCreatedEntities() const {
    std::lock_guard<std::mutex> lock(m_createdEntitiesMutex);
    return m_createdEntities; // Copy
}

size_t VoxelInjectionQueue::getCreatedEntityCount() const {
    std::lock_guard<std::mutex> lock(m_createdEntitiesMutex);
    return m_createdEntities.size();
}

// ============================================================================
// Statistics
// ============================================================================

VoxelInjectionQueue::Stats VoxelInjectionQueue::getStats() const {
    Stats stats;

    size_t write = m_writeIndex.load(std::memory_order_relaxed);
    size_t read = m_readIndex.load(std::memory_order_relaxed);
    stats.pendingCount = (write >= read) ? (write - read) : (m_capacity - read + write);

    stats.processedCount = m_processedCount.load(std::memory_order_relaxed);
    stats.entitiesCreated = m_entitiesCreated.load(std::memory_order_relaxed);
    stats.failedCount = m_failedCount.load(std::memory_order_relaxed);
    stats.isProcessing = m_running.load(std::memory_order_relaxed);

    return stats;
}

void VoxelInjectionQueue::flush() {
    while (m_readIndex.load(std::memory_order_relaxed) !=
           m_writeIndex.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ============================================================================
// Worker Thread
// ============================================================================

void VoxelInjectionQueue::processWorker() {
    constexpr size_t BATCH_SIZE = 256;
    std::vector<VoxelCreationRequest> batch;
    batch.reserve(BATCH_SIZE);

    while (m_running.load(std::memory_order_relaxed)) {
        // Wait for work
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            m_cv.wait(lock, [this]() {
                return !m_running.load(std::memory_order_relaxed) ||
                       m_readIndex.load(std::memory_order_relaxed) !=
                       m_writeIndex.load(std::memory_order_relaxed);
            });
        }

        if (!m_running.load(std::memory_order_relaxed)) {
            break;
        }

        // Collect batch
        batch.clear();
        size_t currentRead = m_readIndex.load(std::memory_order_relaxed);
        size_t currentWrite = m_writeIndex.load(std::memory_order_acquire);

        while (currentRead != currentWrite && batch.size() < BATCH_SIZE) {
            batch.push_back(m_ringBuffer[currentRead]);
            currentRead = (currentRead + 1) % m_capacity;
        }

        m_readIndex.store(currentRead, std::memory_order_release);

        // Process batch - use createVoxelsBatch for fast bulk creation
        if (!batch.empty()) {
            try {
                // Use batch API (skips per-entity chunk parenting and cache invalidation)
                auto entities = m_world.createVoxelsBatch(batch);

                // Store created entities
                {
                    std::lock_guard<std::mutex> lock(m_createdEntitiesMutex);
                    m_createdEntities.insert(m_createdEntities.end(), entities.begin(), entities.end());
                }

                // Update statistics
                m_processedCount.fetch_add(batch.size(), std::memory_order_relaxed);
                m_entitiesCreated.fetch_add(entities.size(), std::memory_order_relaxed);

                if (entities.size() < batch.size()) {
                    m_failedCount.fetch_add(batch.size() - entities.size(), std::memory_order_relaxed);
                }
            } catch (const std::exception& e) {
                std::cerr << "[VoxelInjectionQueue] Batch creation failed: " << e.what() << "\n";
                m_failedCount.fetch_add(batch.size(), std::memory_order_relaxed);
            }
        }
    }
}

} // namespace GaiaVoxel
