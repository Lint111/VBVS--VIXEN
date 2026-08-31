#pragma once

#include "Recipe/SdfInstruction.h"
#include "ShellOctreeGpu.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace Vixen::SVO {

struct MaterializationKey {
    uint64_t region = 0;
    uint64_t generation = 0;

    friend bool operator==(const MaterializationKey&, const MaterializationKey&) = default;
};

/** Owned recipe snapshot accepted by both CPU and future GPU materializers. */
struct BulkMaterializationRequest {
    MaterializationKey key{};
    std::vector<Recipe::SdfInstruction> recipe;
    std::vector<float> parameters;
    glm::vec3 center{};
    uint32_t resolution = 0;
    float bandVoxels = 0.0f;
    uint32_t brickDepth = 3;
};

class MaterializationCancelled final : public std::runtime_error {
public:
    MaterializationCancelled(): std::runtime_error("materialization cancelled") {}
};

class IMaterializationBackend {
public:
    virtual ~IMaterializationBackend() = default;
    [[nodiscard]] virtual SerializedOctree materialize(
        const BulkMaterializationRequest& request,
        std::stop_token stopToken) const = 0;
};

/** First backend: recipe evaluation on the existing CPU bake path. */
class CpuRecipeMaterializer final : public IMaterializationBackend {
public:
    [[nodiscard]] SerializedOctree materialize(
        const BulkMaterializationRequest& request,
        std::stop_token stopToken) const override;
};

enum class MaterializationStatus {
    Completed,
    Cancelled,
    Failed
};

struct BulkMaterializationResult {
    MaterializationKey key{};
    MaterializationStatus status = MaterializationStatus::Cancelled;
    SerializedOctree page{};
    uint64_t canonicalHash = 0;
    std::string error;
};

enum class EnqueueResult {
    Accepted,
    Full,
    Closed
};

enum class ProcessResult {
    Processed,
    NoWork,
    OutputBackpressure
};

struct BulkMaterializationQueueStats {
    size_t pending = 0;
    size_t inFlight = 0;
    size_t ready = 0;
    size_t accepted = 0;
    size_t completed = 0;
    size_t cancelled = 0;
    size_t failed = 0;
    bool closed = false;
};

/**
 * Bounded owned-work queue. It owns no threads: the caller dispatches a batch
 * through oneTBB's sanctioned arena, preventing another hardware-sized pool.
 */
class BulkMaterializationQueue {
public:
    BulkMaterializationQueue(size_t inputCapacity, size_t outputCapacity);

    EnqueueResult tryEnqueue(BulkMaterializationRequest request);
    ProcessResult process(
        const IMaterializationBackend& backend,
        size_t workerCount,
        std::stop_token stopToken = {});
    std::optional<BulkMaterializationResult> tryPop();
    void close();

    [[nodiscard]] BulkMaterializationQueueStats stats() const;

private:
    struct QueuedRequest {
        BulkMaterializationRequest request;
    };

    const size_t m_inputCapacity;
    const size_t m_outputCapacity;
    std::mutex m_processMutex;
    mutable std::mutex m_mutex;
    std::deque<QueuedRequest> m_pending;
    std::deque<BulkMaterializationResult> m_ready;
    size_t m_inFlight = 0;
    size_t m_accepted = 0;
    size_t m_completed = 0;
    size_t m_cancelled = 0;
    size_t m_failed = 0;
    bool m_closed = false;
};

[[nodiscard]] uint64_t canonicalHash(const SerializedOctree& page);

} // namespace Vixen::SVO
