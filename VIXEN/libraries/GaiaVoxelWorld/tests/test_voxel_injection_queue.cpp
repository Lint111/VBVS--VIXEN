#include <gtest/gtest.h>
#include "VoxelInjectionQueue.h"
#include "GaiaVoxelWorld.h"
#include "ComponentData.h"  // VoxelCreationRequest moved here
#include <glm/glm.hpp>
#include <thread>
#include <chrono>

using namespace Vixen::GaiaVoxel;

// ===========================================================================
// Queue Lifecycle Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, CreateAndDestroy) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    // Queue should be constructible and destructible without errors
    SUCCEED();
}

TEST(VoxelInjectionQueueTest, StartAndStop) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    EXPECT_FALSE(queue.isRunning());

    queue.start(1);
    EXPECT_TRUE(queue.isRunning());

    queue.stop();
    EXPECT_FALSE(queue.isRunning());
}

TEST(VoxelInjectionQueueTest, StartMultipleWorkers) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(4); // 4 worker threads
    EXPECT_TRUE(queue.isRunning());

    queue.stop();
    EXPECT_FALSE(queue.isRunning());
}

TEST(VoxelInjectionQueueTest, StopWithoutStart) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    // Should be safe to call stop without start
    queue.stop();
    EXPECT_FALSE(queue.isRunning());
}

// ===========================================================================
// Enqueue Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, EnqueueSingleVoxel) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    VoxelCreationRequest request{glm::vec3(10.0f, 5.0f, 3.0f), components};
    bool success = queue.enqueue(request);
    EXPECT_TRUE(success);

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 1);
}

TEST(VoxelInjectionQueueTest, EnqueueMultipleVoxels) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    for (int i = 0; i < 100; ++i) {
        glm::vec3 pos(static_cast<float>(i), 0.0f, 0.0f);
        VoxelCreationRequest request{pos, components};
        bool success = queue.enqueue(request);
        EXPECT_TRUE(success);
    }

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 100);
}

TEST(VoxelInjectionQueueTest, EnqueueBatch) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    size_t enqueued = 0;
    for (int i = 0; i < 50; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        if (queue.enqueue(request)) {
            enqueued++;
        }
    }

    EXPECT_EQ(enqueued, 50);

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 50);
}

TEST(VoxelInjectionQueueTest, EnqueueUntilFull) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world, 100); // Small capacity

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    size_t successCount = 0;
    for (int i = 0; i < 150; ++i) { // Try to enqueue more than capacity
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        if (queue.enqueue(request)) {
            successCount++;
        }
    }

    EXPECT_LE(successCount, 100); // Should not exceed capacity
}

// ===========================================================================
// Processing Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, ProcessSingleVoxel) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    VoxelCreationRequest request{glm::vec3(10.0f, 5.0f, 3.0f), components};
    queue.enqueue(request);

    // Wait for processing
    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 1);
    EXPECT_TRUE(world.exists(entities[0]));

    queue.stop();
}

TEST(VoxelInjectionQueueTest, ProcessMultipleVoxels) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(2); // 2 workers

    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    for (int i = 0; i < 100; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 100);

    for (auto entity : entities) {
        EXPECT_TRUE(world.exists(entity));
    }

    queue.stop();
}

TEST(VoxelInjectionQueueTest, ProcessBatchCreation) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(4); // 4 workers for parallel processing

    for (int i = 0; i < 1000; ++i) {
        ComponentQueryRequest components[] = {
            Density{1.0f},
            Color{glm::vec3(static_cast<float>(i % 256) / 255.0f, 0.5f, 0.5f)},
            Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
        };

        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 1000);

    queue.stop();
}

TEST(VoxelInjectionQueueTest, VerifyCreatedEntitiesHaveCorrectAttributes) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{0.8f},
        Color{glm::vec3(0.2f, 0.4f, 0.6f)},
        Normal{glm::vec3(0.0f, 0.0f, 1.0f)}
    };

    glm::vec3 expectedPos(100.0f, 50.0f, 25.0f);
    VoxelCreationRequest request{expectedPos, components};
    queue.enqueue(request);

    queue.flush();

    auto entities = queue.getCreatedEntities();
    ASSERT_EQ(entities.size(), 1);

    auto entity = entities[0];
    ASSERT_TRUE(world.exists(entity));

    // Verify attributes in GaiaVoxelWorld
    auto pos = world.getPosition(entity);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos.value(), expectedPos);

    auto density = world.getComponentValue<Density>(entity);
    ASSERT_TRUE(density.has_value());
    EXPECT_FLOAT_EQ(density.value(), 0.8f);

    auto color = world.getComponentValue<Color>(entity);
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color.value(), glm::vec3(0.2f, 0.4f, 0.6f));

    auto normal = world.getComponentValue<Normal>(entity);
    ASSERT_TRUE(normal.has_value());
    EXPECT_EQ(normal.value(), glm::vec3(0.0f, 0.0f, 1.0f));

    queue.stop();
}

// ===========================================================================
// Entity Access Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, GetCreatedEntitiesClearsBuffer) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    for (int i = 0; i < 10; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    // First call - should return entities
    auto entities1 = queue.getCreatedEntities();
    EXPECT_EQ(entities1.size(), 10);

    // Second call - buffer should be cleared
    auto entities2 = queue.getCreatedEntities();
    EXPECT_EQ(entities2.size(), 0);

    queue.stop();
}

TEST(VoxelInjectionQueueTest, PeekCreatedEntitiesDoesNotClear) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    for (int i = 0; i < 10; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    // Peek should not clear buffer
    auto peeked1 = queue.peekCreatedEntities();
    EXPECT_EQ(peeked1.size(), 10);

    auto peeked2 = queue.peekCreatedEntities();
    EXPECT_EQ(peeked2.size(), 10);

    // Get should clear buffer
    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 10);

    auto peeked3 = queue.peekCreatedEntities();
    EXPECT_EQ(peeked3.size(), 0);

    queue.stop();
}

TEST(VoxelInjectionQueueTest, GetCreatedEntityCount) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    for (int i = 0; i < 25; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    size_t count = queue.getCreatedEntityCount();
    EXPECT_EQ(count, 25);

    queue.stop();
}

// ===========================================================================
// Statistics Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, GetStats_InitialState) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 0);
    EXPECT_EQ(stats.processedCount, 0);
    EXPECT_EQ(stats.entitiesCreated, 0);
    EXPECT_EQ(stats.failedCount, 0);
    EXPECT_FALSE(stats.isProcessing);
}

TEST(VoxelInjectionQueueTest, GetStats_AfterEnqueue) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    for (int i = 0; i < 50; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 50);
    EXPECT_EQ(stats.processedCount, 0);
}

TEST(VoxelInjectionQueueTest, GetStats_AfterProcessing) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    for (int i = 0; i < 100; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 0);
    EXPECT_EQ(stats.processedCount, 100);
    EXPECT_EQ(stats.entitiesCreated, 100);

    queue.stop();
}

// ===========================================================================
// Thread Safety Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, ConcurrentEnqueue) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world, 10000);

    queue.start(4);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    // Multiple threads enqueueing concurrently
    std::vector<std::thread> threads;
    std::atomic<int> totalEnqueued{0};

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&queue, &components, &totalEnqueued, t]() {
            for (int i = 0; i < 250; ++i) {
                glm::vec3 pos(static_cast<float>(t * 250 + i), 0.0f, 0.0f);
                VoxelCreationRequest request{pos, components};
                if (queue.enqueue(request)) {
                    totalEnqueued++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), totalEnqueued.load());

    queue.stop();
}

// ===========================================================================
// Performance Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, HighThroughputEnqueue) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world, 100000);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Should be very fast (lock-free enqueue)
    EXPECT_LT(duration.count(), 100); // Less than 100ms for 10k enqueues

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 10000);
}

TEST(VoxelInjectionQueueTest, ParallelProcessingThroughput) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world, 100000);

    queue.start(4); // 4 parallel workers

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    queue.flush();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 10000);

    // Report performance (not a hard requirement, but useful)
    double throughput = 10000.0 / (duration.count() / 1000.0); // entities/sec
    EXPECT_GT(throughput, 1000); // At least 1000 entities/sec

    queue.stop();
}

// ===========================================================================
// Memory Efficiency Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, QueueEntrySize) {
    // Verify that queue entries are compact (40 bytes vs 64+ bytes)
    // MortonKey (8 bytes) + VoxelCreationRequest (32 bytes) = 40 bytes

    // This is a compile-time check - if it compiles, the size is correct
    static_assert(sizeof(MortonKey) == 8, "MortonKey should be 8 bytes");
    static_assert(sizeof(VoxelCreationRequest) == 32, "VoxelCreationRequest should be 32 bytes");

    SUCCEED();
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

TEST(VoxelInjectionQueueTest, StopDuringProcessing) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(2);

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    // Enqueue a large batch
    for (int i = 0; i < 1000; ++i) {
        VoxelCreationRequest request{glm::vec3(static_cast<float>(i), 0.0f, 0.0f), components};
        queue.enqueue(request);
    }

    // Stop immediately (should flush remaining items)
    queue.stop();

    auto stats = queue.getStats();
    EXPECT_EQ(stats.pendingCount, 0); // All processed or cleared
}

TEST(VoxelInjectionQueueTest, FlushEmptyQueue) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);

    // Flush with no items - should not block or crash
    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 0);

    queue.stop();
}

TEST(VoxelInjectionQueueTest, RestartAfterStop) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);

    queue.start(1);
    queue.stop();

    // Restart queue
    queue.start(1);
    EXPECT_TRUE(queue.isRunning());

    ComponentQueryRequest components[] = {
        Density{1.0f}
    };

    VoxelCreationRequest request{glm::vec3(0.0f), components};
    queue.enqueue(request);
    queue.flush();

    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 1);

    queue.stop();
}
