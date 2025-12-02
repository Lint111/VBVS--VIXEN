/**
 * Voxel Injection Tests - Updated for GaiaVoxelWorld + rebuild() workflow
 *
 * NOTE: The old VoxelInjector, DynamicVoxelScalar, SparseVoxelInput, DenseVoxelInput,
 * LambdaVoxelSampler, NoiseSampler, SDFSampler APIs have been deprecated.
 *
 * The new workflow is:
 * 1. Create GaiaVoxelWorld
 * 2. Create voxel entities with createVoxel(VoxelCreationRequest)
 * 3. Create LaineKarrasOctree(world, registry, maxLevels, brickDepth)
 * 4. Call octree.rebuild(world, worldMin, worldMax)
 *
 * See test_ray_casting_comprehensive.cpp for the new API pattern.
 */

#include <gtest/gtest.h>
#include "GaiaVoxelWorld.h"
#include "VoxelInjectionQueue.h"
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"
#include "AttributeRegistry.h"
#include <chrono>

using namespace Vixen::GaiaVoxel;
using namespace Vixen::SVO;
using namespace Vixen::VoxelData;

// ===========================================================================
// Helper: Create octree with voxels using NEW workflow
// ===========================================================================

class VoxelInjectionNewAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_shared<AttributeRegistry>();
        registry->registerKey("density", AttributeType::Float, 1.0f);
        registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

        voxelWorld = std::make_shared<GaiaVoxelWorld>();
    }

    std::unique_ptr<LaineKarrasOctree> createOctreeWithVoxels(
        const std::vector<glm::vec3>& positions,
        int maxDepth = 6)
    {
        // Compute bounds
        glm::vec3 testMin(1e30f), testMax(-1e30f);
        for (const auto& pos : positions) {
            testMin = glm::min(testMin, pos);
            testMax = glm::max(testMax, pos);
        }
        testMin -= glm::vec3(1.0f);
        testMax += glm::vec3(1.0f);

        // Create voxel entities
        for (const auto& pos : positions) {
            ComponentQueryRequest components[] = {
                Density{1.0f},
                Color{glm::vec3(1.0f, 0.0f, 0.0f)}
            };
            VoxelCreationRequest request{pos, components};
            voxelWorld->createVoxel(request);
        }

        // Create octree and rebuild
        auto octree = std::make_unique<LaineKarrasOctree>(
            *voxelWorld, registry.get(), maxDepth, 3);
        octree->rebuild(*voxelWorld, testMin, testMax);

        return octree;
    }

    std::shared_ptr<GaiaVoxelWorld> voxelWorld;
    std::shared_ptr<AttributeRegistry> registry;
};

// ===========================================================================
// Sparse Voxel Tests (New API)
// ===========================================================================

TEST_F(VoxelInjectionNewAPITest, SparseVoxels) {
    std::vector<glm::vec3> positions;
    for (int i = 0; i < 10; ++i) {
        positions.push_back(glm::vec3(static_cast<float>(i), 5.0f, 5.0f));
    }

    auto octree = createOctreeWithVoxels(positions);
    ASSERT_NE(octree, nullptr);

    // Cast ray to verify voxels exist
    auto hit = octree->castRay(glm::vec3(-5, 5, 5), glm::vec3(1, 0, 0), 0.0f, 100.0f);
    EXPECT_TRUE(hit.hit) << "Should hit first voxel in line";
}

// ===========================================================================
// Dense Grid Tests (New API)
// ===========================================================================

TEST_F(VoxelInjectionNewAPITest, DenseGrid) {
    std::vector<glm::vec3> positions;

    // 4x4x4 grid with checkerboard pattern
    for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                if ((x + y + z) % 2 == 0) {
                    positions.push_back(glm::vec3(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z)));
                }
            }
        }
    }

    auto octree = createOctreeWithVoxels(positions);
    ASSERT_NE(octree, nullptr);

    // Cast ray through grid
    auto hit = octree->castRay(glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0), 0.0f, 100.0f);
    EXPECT_TRUE(hit.hit) << "Should hit voxel in grid";
}

// ===========================================================================
// Multiple Voxels Spread Test
// ===========================================================================

TEST_F(VoxelInjectionNewAPITest, MultipleVoxelsSpread) {
    std::vector<glm::vec3> positions = {
        {1.0f, 1.0f, 1.0f},
        {9.0f, 1.0f, 1.0f},
        {1.0f, 9.0f, 1.0f},
        {9.0f, 9.0f, 1.0f},
        {1.0f, 1.0f, 9.0f},
        {9.0f, 1.0f, 9.0f},
        {1.0f, 9.0f, 9.0f},
        {9.0f, 9.0f, 9.0f},
    };

    auto octree = createOctreeWithVoxels(positions);
    ASSERT_NE(octree, nullptr);

    // Verify all 8 corners can be hit
    int hits = 0;
    for (const auto& pos : positions) {
        glm::vec3 rayOrigin = pos - glm::vec3(5.0f, 0.0f, 0.0f);
        auto hit = octree->castRay(rayOrigin, glm::vec3(1, 0, 0), 0.0f, 20.0f);
        if (hit.hit) hits++;
    }
    EXPECT_EQ(hits, 8) << "Should hit all 8 corner voxels";
}

// ===========================================================================
// Async Voxel Injection Queue Tests (GaiaVoxelWorld Integration)
// ===========================================================================

TEST(VoxelInjectionQueueTest, AsyncInjection100kVoxels) {
    // Create GaiaVoxelWorld and injection queue
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world, 100000);  // 100k capacity ring buffer

    // Start background processing with single worker
    std::cout << "\n[AsyncQueue] Starting background worker...\n";
    queue.start(1);
    EXPECT_TRUE(queue.isRunning());

    // Enqueue 100,000 voxels
    std::cout << "[AsyncQueue] Enqueuing 100,000 voxels...\n";
    size_t enqueued = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    // Shared component definition (reused for all voxels)
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}
    };

    for (int i = 0; i < 100000; ++i) {
        glm::vec3 pos(
            (i % 100) * 0.1f,
            ((i / 100) % 100) * 0.1f,
            (i / 10000) * 0.1f
        );

        VoxelCreationRequest request{pos, components};
        if (queue.enqueue(request)) {
            enqueued++;
        }

        // Print progress every 10k voxels
        if (i % 10000 == 0 && i > 0) {
            auto stats = queue.getStats();
            std::cout << "[AsyncQueue] Enqueued: " << i
                      << " | Pending: " << stats.pendingCount
                      << " | Processed: " << stats.processedCount
                      << " | Entities: " << stats.entitiesCreated << "\n";
        }
    }

    auto enqueueEnd = std::chrono::high_resolution_clock::now();
    float enqueueMs = std::chrono::duration<float, std::milli>(enqueueEnd - startTime).count();

    std::cout << "[AsyncQueue] Enqueue complete: " << enqueued << " voxels in "
              << enqueueMs << "ms (" << (enqueued / enqueueMs * 1000.0f) << " voxels/sec)\n";

    // Flush queue (blocks until all requests processed)
    std::cout << "[AsyncQueue] Flushing queue...\n";
    queue.flush();

    auto processEnd = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(processEnd - startTime).count();

    // Get final stats
    auto finalStats = queue.getStats();
    std::cout << "\n[AsyncQueue] Final Statistics:\n";
    std::cout << "  Enqueued: " << enqueued << "\n";
    std::cout << "  Processed: " << finalStats.processedCount << "\n";
    std::cout << "  Entities Created: " << finalStats.entitiesCreated << "\n";
    std::cout << "  Failed: " << finalStats.failedCount << "\n";
    std::cout << "  Total time: " << totalMs << "ms\n";
    std::cout << "  Throughput: " << (finalStats.processedCount / totalMs * 1000.0f) << " voxels/sec\n";

    // Stop queue
    queue.stop();
    EXPECT_FALSE(queue.isRunning());

    // Verify results
    EXPECT_GT(finalStats.processedCount, 0) << "Should process voxels";
    EXPECT_EQ(finalStats.pendingCount, 0) << "Queue should be empty after flush";
    EXPECT_EQ(finalStats.entitiesCreated, enqueued) << "All voxels should create entities";
    EXPECT_EQ(finalStats.failedCount, 0) << "No entity creation failures";

    std::cout << "[AsyncQueue] Test complete!\n";
}
