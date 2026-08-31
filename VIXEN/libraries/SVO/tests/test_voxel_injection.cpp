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
#include "BulkMaterialization.h"
#include "MipBake.h"
#include "SdfBake.h"
#include "LaineKarrasOctree.h"
#include "VoxelComponents.h"
#include "AttributeRegistry.h"

#include <chrono>
#include <thread>

using namespace Vixen::GaiaVoxel;
using namespace Vixen::SVO;
using namespace Vixen::VoxelData;

namespace {

SerializedOctree materializeScalarReference(const BulkMaterializationRequest& request) {
    auto baked = BakeRecipeInstructionsToSdfWorld(
        request.recipe.data(), static_cast<uint32_t>(request.recipe.size()),
        request.center, static_cast<int>(request.resolution), request.bandVoxels,
        static_cast<int>(request.brickDepth), request.parameters);
    auto body = BuildSdfBodyOctree(baked, static_cast<int>(request.brickDepth));
    return SerializeSdfWithMips(body);
}

Recipe::SdfInstruction makeOpcode(Recipe::SdfOpCode opcode) {
    Recipe::SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(opcode);
    return instruction;
}

} // namespace

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
        glm::vec3 testMin, testMax;  // set below — canonical frame, not shrink-wrapped

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
        // rebuild()'s world frame follows the production convention (VoxelSceneCacher::
        // BuildOctree): origin-anchored power-of-2 cube — worldMin=0, worldMax=resolution,
        // maxLevels=log2(resolution). Shrink-wrapped/anisotropic bounds put the integer-grid
        // MortonKey quantization and the octree cell mapping out of agreement and rays miss
        // real voxels (this suite's long-standing failure).
        testMin = glm::vec3(0.0f);
        testMax = glm::vec3(static_cast<float>(1 << maxDepth));
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

    // Aim through cell CENTERS: a voxel created at integer pos occupies [pos, pos+1)^3.
    // Integer-coordinate rays graze cell boundaries, where hit/miss is DDA tie-breaking —
    // not what this test is about (voxel occupancy).
    auto hit = octree->castRay(glm::vec3(-5, 5.5f, 5.5f), glm::vec3(1, 0, 0), 0.0f, 100.0f);
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
        // Through the voxel's center (integer-coordinate rays graze cell boundaries).
        glm::vec3 rayOrigin = pos + glm::vec3(-5.0f, 0.5f, 0.5f);
        auto hit = octree->castRay(rayOrigin, glm::vec3(1, 0, 0), 0.0f, 20.0f);
        if (hit.hit) hits++;
    }
    EXPECT_EQ(hits, 8) << "Should hit all 8 corner voxels";
}

// ===========================================================================
// Materialized-delta path: owned recipe -> upload-ready ESVO page
// ===========================================================================

TEST(BulkMaterializationIntegrationTest, CpuRecipeProducesUploadReadyEsvoPage) {
    Recipe::SdfInstruction sphere{};
    sphere.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 5.0f;

    BulkMaterializationRequest request{
        .key = {.region = 12, .generation = 3},
        .recipe = {sphere},
        .parameters = {},
        .center = glm::vec3(8.0f),
        .resolution = 16,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    };
    BulkMaterializationQueue queue(1, 1);
    ASSERT_EQ(queue.tryEnqueue(std::move(request)), EnqueueResult::Accepted);

    CpuRecipeMaterializer backend;
    ASSERT_EQ(queue.process(backend, 2), ProcessResult::Processed);
    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->status, MaterializationStatus::Completed) << result->error;
    EXPECT_FALSE(result->page.nodes.empty());
    EXPECT_FALSE(result->page.bricks.empty());
    EXPECT_FALSE(result->page.channelPool.empty());
    EXPECT_EQ(result->page.nodeCount,
              result->page.nodes.size() / sizeof(ChildDescriptor));
    EXPECT_EQ(result->page.brickCount,
              result->page.bricks.size() / SerializedOctree::kBrickStrideBytes);
    EXPECT_NE(result->canonicalHash, 0u);
}

TEST(BulkMaterializationIntegrationTest, CpuRecipeOneVsNWorkerCanonicalHashParity) {
    auto makeRequest = [](uint64_t region) {
        Recipe::SdfInstruction sphere{};
        sphere.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
        sphere.data[3] = 5.0f;
        return BulkMaterializationRequest{
            .key = {.region = region, .generation = 9},
            .recipe = {sphere},
            .parameters = {},
            .center = glm::vec3(8.0f),
            .resolution = 16,
            .bandVoxels = 2.0f,
            .brickDepth = 3,
        };
    };

    BulkMaterializationQueue serial(2, 2);
    BulkMaterializationQueue parallel(2, 2);
    for (uint64_t region = 0; region < 2; ++region) {
        ASSERT_EQ(serial.tryEnqueue(makeRequest(region)), EnqueueResult::Accepted);
        ASSERT_EQ(parallel.tryEnqueue(makeRequest(region)), EnqueueResult::Accepted);
    }

    CpuRecipeMaterializer backend;
    ASSERT_EQ(serial.process(backend, 1), ProcessResult::Processed);
    ASSERT_EQ(parallel.process(backend, 2), ProcessResult::Processed);
    for (uint64_t region = 0; region < 2; ++region) {
        auto oneWorker = serial.tryPop();
        auto manyWorkers = parallel.tryPop();
        ASSERT_TRUE(oneWorker.has_value());
        ASSERT_TRUE(manyWorkers.has_value());
        ASSERT_EQ(oneWorker->status, MaterializationStatus::Completed) << oneWorker->error;
        ASSERT_EQ(manyWorkers->status, MaterializationStatus::Completed) << manyWorkers->error;
        EXPECT_EQ(oneWorker->key.region, region);
        EXPECT_EQ(manyWorkers->key.region, region);
        EXPECT_EQ(oneWorker->canonicalHash, manyWorkers->canonicalHash);
    }
}

TEST(BulkMaterializationIntegrationTest, ScalarVsSimdCanonicalHashParity) {
    Recipe::SdfInstruction sphere = makeOpcode(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 4.25f;
    Recipe::SdfInstruction box = makeOpcode(Recipe::SdfOpCode::Box);
    box.data[0] = 3.0f;
    box.data[1] = 4.5f;
    box.data[2] = 2.75f;
    Recipe::SdfInstruction smoothUnion = makeOpcode(Recipe::SdfOpCode::SmoothUnion);
    smoothUnion.data[2] = 0.625f;

    Recipe::SdfInstruction parameter = makeOpcode(Recipe::SdfOpCode::ReadParam);
    parameter.paramMask = 1;
    parameter.data[0] = 0.0f;

    std::vector<BulkMaterializationRequest> requests;
    requests.push_back(BulkMaterializationRequest{
        .key = {.region = 30, .generation = 1},
        .recipe = {sphere, box, smoothUnion},
        .parameters = {},
        .center = glm::vec3(8.0f),
        .resolution = 16,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    });
    requests.push_back(BulkMaterializationRequest{
        .key = {.region = 31, .generation = 1},
        .recipe = {sphere, parameter, makeOpcode(Recipe::SdfOpCode::MathSub)},
        .parameters = {1.125f},
        .center = glm::vec3(8.0f),
        .resolution = 16,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    });

    CpuRecipeMaterializer realBackend;
    for (const auto& request : requests) {
        const SerializedOctree scalar = materializeScalarReference(request);
        const SerializedOctree simd = realBackend.materialize(request, {});
        EXPECT_EQ(canonicalHash(scalar), canonicalHash(simd))
            << "scalar/SIMD byte identity failed for region " << request.key.region;
    }
}

TEST(BulkMaterializationIntegrationTest, SimdBatchCancellationProducesTerminalResult) {
    Recipe::SdfInstruction sphere = makeOpcode(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 28.0f;
    BulkMaterializationRequest request{
        .key = {.region = 32, .generation = 1},
        .recipe = {sphere},
        .parameters = {},
        .center = glm::vec3(64.0f),
        .resolution = 128,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    };

    BulkMaterializationQueue queue(1, 1);
    ASSERT_EQ(queue.tryEnqueue(std::move(request)), EnqueueResult::Accepted);
    CpuRecipeMaterializer realBackend;
    std::stop_source stop;
    ProcessResult processResult = ProcessResult::NoWork;
    std::jthread worker([&] {
        processResult = queue.process(realBackend, 1, stop.get_token());
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.stats().inFlight == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_EQ(queue.stats().inFlight, 1u);
    stop.request_stop();
    worker.join();

    EXPECT_EQ(processResult, ProcessResult::Processed);
    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, MaterializationStatus::Cancelled);
    EXPECT_EQ(queue.stats().cancelled, 1u);
}
