#include <gtest/gtest.h>

#include "BulkMaterialization.h"
#include "GaiaVoxelWorld.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Vixen::SVO;

namespace {

BulkMaterializationRequest makeRequest(uint64_t region) {
    Recipe::SdfInstruction instruction{};
    instruction.opCode = static_cast<uint8_t>(Recipe::SdfOpCode::Sphere);
    instruction.data[3] = static_cast<float>(region) + 0.25f;
    return BulkMaterializationRequest{
        .key = {.region = region, .generation = 7},
        .recipe = {instruction},
        .parameters = {static_cast<float>(region)},
        .center = glm::vec3(8.0f),
        .resolution = 16,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    };
}

class DeterministicBackend final : public IMaterializationBackend {
public:
    SerializedOctree materialize(
        const BulkMaterializationRequest& request,
        std::stop_token stopToken) const override {
        if (stopToken.stop_requested()) {
            throw MaterializationCancelled{};
        }
        SerializedOctree page;
        const uint64_t marker = request.key.region ^
            std::bit_cast<uint32_t>(request.recipe.at(0).data[3]) ^
            std::bit_cast<uint32_t>(request.parameters.at(0));
        page.nodes.resize(sizeof(marker));
        std::memcpy(page.nodes.data(), &marker, sizeof(marker));
        page.nodeCount = 1;
        return page;
    }
};

class BlockingBackend final : public IMaterializationBackend {
public:
    mutable std::atomic<size_t> started{0};

    SerializedOctree materialize(
        const BulkMaterializationRequest&,
        std::stop_token stopToken) const override {
        started.fetch_add(1, std::memory_order_release);
        while (!stopToken.stop_requested()) {
            std::this_thread::yield();
        }
        throw MaterializationCancelled{};
    }
};

class FailingBackend final : public IMaterializationBackend {
public:
    SerializedOctree materialize(
        const BulkMaterializationRequest& request,
        std::stop_token) const override {
        if (request.key.region == 3) {
            throw std::runtime_error("materialization failed");
        }
        return SerializedOctree{};
    }
};

} // namespace

TEST(BulkMaterializationDispatchTest, OwnedPayloadProducesOrderedTerminalResults) {
    MaterializationBatchResult batch;
    {
        std::vector<BulkMaterializationRequest> requests;
        requests.push_back(makeRequest(41));
        DeterministicBackend backend;
        batch = DispatchMaterializationBatch(std::move(requests), backend, 1);
    }

    ASSERT_TRUE(batch.succeeded);
    ASSERT_EQ(batch.results.size(), 1u);
    EXPECT_EQ(batch.results[0].key.region, 41u);
    EXPECT_EQ(batch.results[0].status, MaterializationStatus::Completed);
    EXPECT_NE(batch.results[0].canonicalHash, 0u);
}

TEST(BulkMaterializationDispatchTest, AssemblyWorkersNeverMutateGaiaWorld) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;
    const auto before = world.getStats().totalEntities;
    DeterministicBackend backend;
    std::vector<BulkMaterializationRequest> requests;
    requests.push_back(makeRequest(1));

    const auto batch = DispatchMaterializationBatch(std::move(requests), backend, 4);
    ASSERT_TRUE(batch.succeeded);
    EXPECT_EQ(world.getStats().totalEntities, before);
}

TEST(BulkMaterializationDispatchTest, OneVsNWorkerCanonicalHashParity) {
    std::vector<BulkMaterializationRequest> serialRequests;
    std::vector<BulkMaterializationRequest> parallelRequests;
    for (uint64_t i = 0; i < 32; ++i) {
        serialRequests.push_back(makeRequest(i));
        parallelRequests.push_back(makeRequest(i));
    }

    DeterministicBackend backend;
    const auto serial = DispatchMaterializationBatch(std::move(serialRequests), backend, 1);
    const auto parallel = DispatchMaterializationBatch(std::move(parallelRequests), backend, 4);
    ASSERT_TRUE(serial.succeeded);
    ASSERT_TRUE(parallel.succeeded);
    ASSERT_EQ(serial.results.size(), parallel.results.size());
    for (size_t i = 0; i < serial.results.size(); ++i) {
        EXPECT_EQ(serial.results[i].key, parallel.results[i].key);
        EXPECT_EQ(serial.results[i].canonicalHash, parallel.results[i].canonicalHash);
    }
}

TEST(BulkMaterializationDispatchTest, EmptyBatchCompletesWithoutDispatch) {
    DeterministicBackend backend;
    const auto batch = DispatchMaterializationBatch({}, backend, 4);
    EXPECT_TRUE(batch.succeeded);
    EXPECT_TRUE(batch.results.empty());
}

TEST(BulkMaterializationDispatchTest, CanonicalHashIncludesTypedStreamLengths) {
    SerializedOctree empty;
    SerializedOctree oneZeroEntry;
    oneZeroEntry.perBrickOccupiedVoxelCount.push_back(0);
    EXPECT_NE(canonicalHash(empty), canonicalHash(oneZeroEntry));
}

TEST(BulkMaterializationDispatchTest, FailureProducesIndexedTerminalResult) {
    FailingBackend backend;
    std::vector<BulkMaterializationRequest> requests;
    for (uint64_t i = 0; i < 6; ++i) {
        requests.push_back(makeRequest(i));
    }

    const auto batch = DispatchMaterializationBatch(std::move(requests), backend, 4);
    ASSERT_FALSE(batch.succeeded);
    ASSERT_EQ(batch.results.size(), 6u);
    EXPECT_EQ(batch.results[3].status, MaterializationStatus::Failed);
    EXPECT_EQ(batch.results[3].error, "materialization failed");
}

TEST(BulkMaterializationDispatchTest, CancellationProducesOneTerminalResultPerRequest) {
    std::vector<BulkMaterializationRequest> requests;
    for (uint64_t i = 0; i < 8; ++i) {
        requests.push_back(makeRequest(i));
    }
    BlockingBackend backend;
    std::stop_source stop;
    MaterializationBatchResult batch;
    std::jthread worker([&] {
        batch = DispatchMaterializationBatch(std::move(requests), backend, 4, stop.get_token());
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (backend.started.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool started = backend.started.load(std::memory_order_acquire) != 0;
    stop.request_stop();
    worker.join();

    ASSERT_TRUE(started);
    ASSERT_FALSE(batch.succeeded);
    ASSERT_EQ(batch.results.size(), 8u);
    for (const auto& result : batch.results) {
        EXPECT_EQ(result.status, MaterializationStatus::Cancelled);
    }
}
