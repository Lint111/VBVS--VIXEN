#include "BulkMaterialization.h"

#include "MipBake.h"
#include "SdfBake.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

namespace Vixen::SVO {
namespace {

void hashBytes(uint64_t& hash, const void* data, size_t size) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(uint64_t& hash, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    hashBytes(hash, &value, sizeof(value));
}

void hashVector(uint64_t& hash, std::span<const uint8_t> bytes) {
    hashValue(hash, static_cast<uint64_t>(bytes.size()));
    hashBytes(hash, bytes.data(), bytes.size());
}

template <typename T>
void hashTypedVector(uint64_t& hash, std::span<const T> values) {
    static_assert(std::is_trivially_copyable_v<T>);
    hashValue(hash, static_cast<uint64_t>(values.size()));
    hashBytes(hash, values.data(), values.size_bytes());
}

} // namespace

SerializedOctree CpuRecipeMaterializer::materialize(
    const BulkMaterializationRequest& request,
    std::stop_token stopToken) const {
    if (request.recipe.empty()) {
        throw std::invalid_argument("CPU recipe materialization requires non-empty bytecode");
    }
    if (request.recipe.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("CPU recipe materialization bytecode is too large");
    }
    if (request.resolution == 0 || request.resolution > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("CPU recipe materialization resolution is out of range");
    }
    if (request.brickDepth > 7) {
        throw std::invalid_argument("CPU recipe materialization brick depth is out of range");
    }
    if (!std::isfinite(request.bandVoxels) || request.bandVoxels < 0.0f) {
        throw std::invalid_argument("CPU recipe materialization band must be finite and non-negative");
    }
    if (stopToken.stop_requested()) {
        throw MaterializationCancelled{};
    }

    auto baked = BakeSdfWorld(
        [&](const glm::vec3& p) {
            if (stopToken.stop_requested()) {
                throw MaterializationCancelled{};
            }
            return Recipe::evalRecipe(
                request.recipe.data(),
                static_cast<uint32_t>(request.recipe.size()),
                p - request.center,
                request.parameters);
        },
        request.center,
        static_cast<int>(request.resolution),
        request.bandVoxels,
        static_cast<int>(request.brickDepth));

    if (stopToken.stop_requested()) {
        throw MaterializationCancelled{};
    }
    auto body = BuildSdfBodyOctree(baked, static_cast<int>(request.brickDepth));
    if (stopToken.stop_requested()) {
        throw MaterializationCancelled{};
    }
    auto page = SerializeSdfWithMips(body);
    if (stopToken.stop_requested()) {
        throw MaterializationCancelled{};
    }
    return page;
}

uint64_t canonicalHash(const SerializedOctree& page) {
    uint64_t hash = 14695981039346656037ULL;
    hashVector(hash, page.nodes);
    hashVector(hash, page.bricks);
    hashVector(hash, page.materials);
    hashVector(hash, page.channelPool);
    hashVector(hash, page.brickGridLookup);
    hashVector(hash, page.mipPool);
    hashTypedVector(hash, std::span<const TierRef>(page.tierRefs));
    hashTypedVector(hash, std::span<const uint32_t>(page.perBrickOccupiedVoxelCount));
    hashValue(hash, page.config);
    hashValue(hash, page.channelCount);
    hashValue(hash, page.brickStrideFloats);
    hashBytes(hash, page.channels, sizeof(page.channels));
    hashValue(hash, page.nodeCount);
    hashValue(hash, page.brickCount);
    hashValue(hash, page.occupiedVoxelCount);
    return hash;
}

BulkMaterializationQueue::BulkMaterializationQueue(size_t inputCapacity, size_t outputCapacity)
    : m_inputCapacity(inputCapacity), m_outputCapacity(outputCapacity) {
    if (inputCapacity == 0 || outputCapacity == 0) {
        throw std::invalid_argument("bulk materialization queue capacities must be non-zero");
    }
}

EnqueueResult BulkMaterializationQueue::tryEnqueue(BulkMaterializationRequest request) {
    std::lock_guard lock(m_mutex);
    if (m_closed) {
        return EnqueueResult::Closed;
    }
    if (m_pending.size() >= m_inputCapacity) {
        return EnqueueResult::Full;
    }
    m_pending.push_back(QueuedRequest{std::move(request)});
    ++m_accepted;
    return EnqueueResult::Accepted;
}

ProcessResult BulkMaterializationQueue::process(
    const IMaterializationBackend& backend,
    size_t workerCount,
    std::stop_token stopToken) {
    // One dispatcher at a time preserves acceptance order across batches while
    // the work inside each batch still fans out across the sanctioned arena.
    std::lock_guard processLock(m_processMutex);
    std::vector<QueuedRequest> batch;
    std::vector<BulkMaterializationResult> results;
    {
        std::lock_guard lock(m_mutex);
        if (m_pending.empty()) {
            return ProcessResult::NoWork;
        }
        const size_t outputSpace = m_outputCapacity - m_ready.size();
        if (outputSpace == 0) {
            return ProcessResult::OutputBackpressure;
        }
        const size_t count = std::min(outputSpace, m_pending.size());
        batch.reserve(count);
        results.resize(count);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(std::move(m_pending.front()));
            m_pending.pop_front();
        }
        m_inFlight += count;
    }

    for (size_t i = 0; i < batch.size(); ++i) {
        results[i].key = batch[i].request.key;
    }

    try {
        oneapi::tbb::task_group_context context;
        std::stop_callback cancelCallback(stopToken, [&] {
            context.cancel_group_execution();
        });
        const int arenaConcurrency = static_cast<int>(std::clamp<size_t>(
            workerCount, 1, static_cast<size_t>(std::numeric_limits<int>::max())));
        oneapi::tbb::task_arena arena(arenaConcurrency);
        arena.execute([&] {
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, batch.size()),
                [&](const oneapi::tbb::blocked_range<size_t>& range) {
                    for (size_t i = range.begin(); i != range.end(); ++i) {
                        if (stopToken.stop_requested() || context.is_group_execution_cancelled()) {
                            results[i].status = MaterializationStatus::Cancelled;
                            continue;
                        }
                        try {
                            results[i].page = backend.materialize(batch[i].request, stopToken);
                            results[i].canonicalHash = canonicalHash(results[i].page);
                            results[i].status = MaterializationStatus::Completed;
                        } catch (const MaterializationCancelled&) {
                            results[i].status = MaterializationStatus::Cancelled;
                        } catch (const std::exception& error) {
                            results[i].status = MaterializationStatus::Failed;
                            results[i].error = error.what();
                        } catch (...) {
                            results[i].status = MaterializationStatus::Failed;
                            results[i].error = "unknown materialization failure";
                        }
                    }
                },
                context);
        });
    } catch (const std::exception& error) {
        for (auto& result : results) {
            if (result.status != MaterializationStatus::Completed) {
                result.status = MaterializationStatus::Failed;
                result.error = error.what();
            }
        }
    } catch (...) {
        for (auto& result : results) {
            if (result.status != MaterializationStatus::Completed) {
                result.status = MaterializationStatus::Failed;
                result.error = "unknown scheduler failure";
            }
        }
    }

    std::lock_guard lock(m_mutex);
    m_inFlight -= results.size();
    for (auto& result : results) {
        switch (result.status) {
            case MaterializationStatus::Completed: ++m_completed; break;
            case MaterializationStatus::Cancelled: ++m_cancelled; break;
            case MaterializationStatus::Failed: ++m_failed; break;
        }
        m_ready.push_back(std::move(result));
    }
    return ProcessResult::Processed;
}

std::optional<BulkMaterializationResult> BulkMaterializationQueue::tryPop() {
    std::lock_guard lock(m_mutex);
    if (m_ready.empty()) {
        return std::nullopt;
    }
    BulkMaterializationResult result = std::move(m_ready.front());
    m_ready.pop_front();
    return result;
}

void BulkMaterializationQueue::close() {
    std::lock_guard lock(m_mutex);
    m_closed = true;
}

BulkMaterializationQueueStats BulkMaterializationQueue::stats() const {
    std::lock_guard lock(m_mutex);
    return BulkMaterializationQueueStats{
        .pending = m_pending.size(),
        .inFlight = m_inFlight,
        .ready = m_ready.size(),
        .accepted = m_accepted,
        .completed = m_completed,
        .cancelled = m_cancelled,
        .failed = m_failed,
        .closed = m_closed,
    };
}

} // namespace Vixen::SVO
