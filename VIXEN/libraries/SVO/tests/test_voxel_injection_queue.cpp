#include <gtest/gtest.h>

#include "BulkMaterialization.h"
#include "GaiaVoxelWorld.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <set>
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

std::vector<BulkMaterializationResult> popAll(BulkMaterializationQueue& queue) {
    std::vector<BulkMaterializationResult> results;
    while (auto result = queue.tryPop()) {
        results.push_back(std::move(*result));
    }
    return results;
}

} // namespace

TEST(BulkMaterializationQueueTest, OwnedPayloadSurvivesProducerLifetime) {
    BulkMaterializationQueue queue(4, 4);
    {
        auto request = makeRequest(41);
        ASSERT_EQ(queue.tryEnqueue(std::move(request)), EnqueueResult::Accepted);
    }

    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 1), ProcessResult::Processed);
    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->key.region, 41u);
    EXPECT_EQ(result->status, MaterializationStatus::Completed);
    EXPECT_NE(result->canonicalHash, 0u);
}

TEST(BulkMaterializationQueueTest, ConcurrentProducerSlotClaimsAreUnique) {
    constexpr size_t kProducerCount = 4;
    constexpr size_t kPerProducer = 250;
    BulkMaterializationQueue queue(kProducerCount * kPerProducer, kProducerCount * kPerProducer);
    std::vector<std::thread> producers;
    for (size_t producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&, producer] {
            for (size_t i = 0; i < kPerProducer; ++i) {
                const uint64_t key = producer * kPerProducer + i;
                EXPECT_EQ(queue.tryEnqueue(makeRequest(key)), EnqueueResult::Accepted);
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 4), ProcessResult::Processed);
    const auto results = popAll(queue);
    ASSERT_EQ(results.size(), kProducerCount * kPerProducer);
    std::set<uint64_t> keys;
    for (const auto& result : results) {
        EXPECT_EQ(result.status, MaterializationStatus::Completed);
        keys.insert(result.key.region);
    }
    EXPECT_EQ(keys.size(), results.size());
}

TEST(BulkMaterializationQueueTest, CompletionWaitsForTerminalResults) {
    BulkMaterializationQueue queue(16, 16);
    for (uint64_t i = 0; i < 16; ++i) {
        ASSERT_EQ(queue.tryEnqueue(makeRequest(i)), EnqueueResult::Accepted);
    }
    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 4), ProcessResult::Processed);
    const auto stats = queue.stats();
    EXPECT_EQ(stats.pending, 0u);
    EXPECT_EQ(stats.ready, 16u);
    EXPECT_EQ(stats.completed, 16u);
}

TEST(BulkMaterializationQueueTest, CloseRetainsAndDrainsAcceptedRequests) {
    BulkMaterializationQueue queue(8, 8);
    for (uint64_t i = 0; i < 8; ++i) {
        ASSERT_EQ(queue.tryEnqueue(makeRequest(i)), EnqueueResult::Accepted);
    }
    queue.close();
    EXPECT_EQ(queue.tryEnqueue(makeRequest(9)), EnqueueResult::Closed);
    EXPECT_EQ(queue.stats().pending, 8u);

    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 2), ProcessResult::Processed);
    EXPECT_EQ(popAll(queue).size(), 8u);
    EXPECT_EQ(queue.stats().pending, 0u);
}

TEST(BulkMaterializationQueueTest, AssemblyWorkersNeverMutateGaiaWorld) {
    Vixen::GaiaVoxel::GaiaVoxelWorld world;
    const auto before = world.getStats().totalEntities;
    BulkMaterializationQueue queue(4, 4);
    ASSERT_EQ(queue.tryEnqueue(makeRequest(1)), EnqueueResult::Accepted);
    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 4), ProcessResult::Processed);
    EXPECT_EQ(world.getStats().totalEntities, before);
}

TEST(BulkMaterializationQueueTest, OneVsNWorkerCanonicalHashParity) {
    BulkMaterializationQueue serial(32, 32);
    BulkMaterializationQueue parallel(32, 32);
    for (uint64_t i = 0; i < 32; ++i) {
        ASSERT_EQ(serial.tryEnqueue(makeRequest(i)), EnqueueResult::Accepted);
        ASSERT_EQ(parallel.tryEnqueue(makeRequest(i)), EnqueueResult::Accepted);
    }
    DeterministicBackend backend;
    ASSERT_EQ(serial.process(backend, 1), ProcessResult::Processed);
    ASSERT_EQ(parallel.process(backend, 4), ProcessResult::Processed);
    const auto oneWorker = popAll(serial);
    const auto manyWorkers = popAll(parallel);
    ASSERT_EQ(oneWorker.size(), manyWorkers.size());
    for (size_t i = 0; i < oneWorker.size(); ++i) {
        EXPECT_EQ(oneWorker[i].key, manyWorkers[i].key);
        EXPECT_EQ(oneWorker[i].canonicalHash, manyWorkers[i].canonicalHash);
    }
}

TEST(BulkMaterializationQueueTest, CanonicalHashIncludesTypedStreamLengths) {
    SerializedOctree empty;
    SerializedOctree oneZeroEntry;
    oneZeroEntry.perBrickOccupiedVoxelCount.push_back(0);
    EXPECT_NE(canonicalHash(empty), canonicalHash(oneZeroEntry));
}

TEST(BulkMaterializationQueueTest, CancellationProducesTerminalResults) {
    BulkMaterializationQueue queue(8, 8);
    for (uint64_t i = 0; i < 8; ++i) {
        ASSERT_EQ(queue.tryEnqueue(makeRequest(i)), EnqueueResult::Accepted);
    }
    BlockingBackend backend;
    std::stop_source stop;
    ProcessResult processResult = ProcessResult::NoWork;
    std::thread processor([&] {
        processResult = queue.process(backend, 4, stop.get_token());
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (backend.started.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool started = backend.started.load(std::memory_order_acquire) != 0;
    if (started) {
        const auto active = queue.stats();
        EXPECT_EQ(active.pending, 0u);
        EXPECT_EQ(active.inFlight, 8u);
        EXPECT_EQ(active.ready, 0u);
    }
    stop.request_stop();
    processor.join();

    ASSERT_TRUE(started);
    ASSERT_EQ(processResult, ProcessResult::Processed);

    const auto results = popAll(queue);
    ASSERT_EQ(results.size(), 8u);
    for (const auto& result : results) {
        EXPECT_EQ(result.status, MaterializationStatus::Cancelled);
    }
    const auto terminal = queue.stats();
    EXPECT_EQ(terminal.inFlight, 0u);
    EXPECT_EQ(terminal.cancelled, 8u);
}

TEST(BulkMaterializationQueueTest, BoundedQueueAppliesInputAndOutputBackpressure) {
    BulkMaterializationQueue queue(2, 1);
    ASSERT_EQ(queue.tryEnqueue(makeRequest(1)), EnqueueResult::Accepted);
    ASSERT_EQ(queue.tryEnqueue(makeRequest(2)), EnqueueResult::Accepted);
    EXPECT_EQ(queue.tryEnqueue(makeRequest(3)), EnqueueResult::Full);

    DeterministicBackend backend;
    ASSERT_EQ(queue.process(backend, 2), ProcessResult::Processed);
    EXPECT_EQ(queue.stats().pending, 1u);
    EXPECT_EQ(queue.process(backend, 2), ProcessResult::OutputBackpressure);
    ASSERT_TRUE(queue.tryPop().has_value());
    EXPECT_EQ(queue.process(backend, 2), ProcessResult::Processed);
    EXPECT_EQ(queue.stats().pending, 0u);
}
