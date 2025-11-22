#include "VoxelInjectionQueue.h"
#include "LaineKarrasOctree.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace SVO {

// ============================================================================
// VoxelInjectionQueue Implementation
// ============================================================================

struct VoxelInjectionQueue::Impl {
    ISVOStructure* targetOctree;
    Config config;
    VoxelInjector injector;
    ::VoxelData::AttributeRegistry* registry;

    // Lock-free ring buffer for voxel queue
    struct VoxelEntry {
        glm::vec3 position;
        ::VoxelData::DynamicVoxelScalar attributes;
        bool valid = false;

        VoxelEntry() = default;
        VoxelEntry(const glm::vec3& pos, const ::VoxelData::DynamicVoxelScalar& attr)
            : position(pos), attributes(attr), valid(true) {}
    };

    std::vector<VoxelEntry> queue;
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> readIndex{0};
    std::atomic<bool> running{false};

    // Worker threads
    std::vector<std::thread> workers;
    std::condition_variable cv;
    std::mutex cvMutex;

    // Statistics
    std::atomic<size_t> processedVoxels{0};
    std::atomic<size_t> failedInsertions{0};

    Impl(ISVOStructure* octree, ::VoxelData::AttributeRegistry* reg, const Config& cfg)
        : targetOctree(octree), config(cfg), injector(reg), registry(reg) {
        queue.resize(config.maxQueueSize);
    }

    ~Impl() {
        if (running.load()) {
            stop();
        }
    }

    void start() {
        if (running.exchange(true)) {
            return; // Already running
        }

        // Spawn worker threads
        for (size_t i = 0; i < config.numWorkerThreads; ++i) {
            workers.emplace_back([this]() { workerThread(); });
        }
    }

    void stop() {
        if (!running.exchange(false)) {
            return; // Not running
        }

        cv.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
    }

    bool enqueue(const glm::vec3& position, const ::VoxelData::DynamicVoxelScalar& data) {
        size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) % config.maxQueueSize;

        // Check if queue is full
        if (nextWrite == readIndex.load(std::memory_order_acquire)) {
            return false; // Queue full
        }

        // Write to queue
        queue[currentWrite] = VoxelEntry(position, data);
        writeIndex.store(nextWrite, std::memory_order_release);

        // Wake up one worker
        cv.notify_one();

        return true;
    }

    void workerThread() {
        std::vector<VoxelInjector::VoxelData> batch;
        batch.reserve(config.batchSize);

        while (running.load(std::memory_order_relaxed)) {
            // Wait for work
            {
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait(lock, [this]() {
                    return !running.load() ||
                           readIndex.load(std::memory_order_relaxed) != writeIndex.load(std::memory_order_relaxed);
                });
            }

            if (!running.load()) break;

            // Collect batch
            batch.clear();
            size_t currentRead = readIndex.load(std::memory_order_relaxed);
            size_t currentWrite = writeIndex.load(std::memory_order_acquire);

            while (currentRead != currentWrite && batch.size() < config.batchSize) {
                if (queue[currentRead].valid) {
                    VoxelInjector::VoxelData vd;
                    vd.position = queue[currentRead].position;
                    vd.attributes = queue[currentRead].attributes;
                    batch.push_back(vd);
                    queue[currentRead].valid = false;
                }
                currentRead = (currentRead + 1) % config.maxQueueSize;
            }

            readIndex.store(currentRead, std::memory_order_release);

            // Process batch
            if (!batch.empty()) {
                size_t inserted = injector.insertVoxelsBatch(*targetOctree, batch, config.injectionConfig);
                processedVoxels.fetch_add(inserted, std::memory_order_relaxed);
                failedInsertions.fetch_add(batch.size() - inserted, std::memory_order_relaxed);
            }
        }
    }
};

VoxelInjectionQueue::VoxelInjectionQueue(
    ISVOStructure* targetOctree,
    ::VoxelData::AttributeRegistry* registry,
    const Config& config)
    : m_impl(std::make_unique<Impl>(targetOctree, registry, config)) {
}

VoxelInjectionQueue::~VoxelInjectionQueue() = default;

void VoxelInjectionQueue::start() {
    m_impl->start();
}

void VoxelInjectionQueue::stop() {
    m_impl->stop();
}

bool VoxelInjectionQueue::enqueue(const glm::vec3& position, const ::VoxelData::DynamicVoxelScalar& data) {
    return m_impl->enqueue(position, data);
}

size_t VoxelInjectionQueue::enqueueBatch(const std::vector<::VoxelData::DynamicVoxelScalar>& voxels) {
    size_t count = 0;
    for (const auto& voxel : voxels) {
        if (voxel.has("position")) {
            glm::vec3 pos = voxel.get<glm::vec3>("position");
            if (enqueue(pos, voxel)) {
                count++;
            }
        }
    }
    return count;
}

const ISVOStructure* VoxelInjectionQueue::getSnapshot() {
    // Simple implementation: return direct pointer
    // TODO: Implement copy-on-write or double-buffering for true frame coherence
    return m_impl->targetOctree;
}

VoxelInjectionQueue::Stats VoxelInjectionQueue::getStats() const {
    Stats stats;
    size_t write = m_impl->writeIndex.load(std::memory_order_relaxed);
    size_t read = m_impl->readIndex.load(std::memory_order_relaxed);
    stats.pendingVoxels = (write >= read) ? (write - read) : (m_impl->config.maxQueueSize - read + write);
    stats.processedVoxels = m_impl->processedVoxels.load(std::memory_order_relaxed);
    stats.failedInsertions = m_impl->failedInsertions.load(std::memory_order_relaxed);
    stats.avgProcessTimeMs = 0.0f; // TODO: Implement timing
    stats.isProcessing = m_impl->running.load(std::memory_order_relaxed);
    return stats;
}

void VoxelInjectionQueue::flush() {
    while (m_impl->readIndex.load(std::memory_order_relaxed) !=
           m_impl->writeIndex.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace SVO
