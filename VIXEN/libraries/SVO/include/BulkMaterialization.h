#pragma once

#include "Recipe/SdfInstruction.h"
#include "ShellOctreeGpu.h"
#include "KernelDispatch/Dispatcher.h"

#include <cstddef>
#include <cstdint>
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

struct MaterializationBatchResult {
    std::vector<BulkMaterializationResult> results;
    bool succeeded = false;
};

/**
 * Materialize an owned batch through the shared typed dispatcher.
 *
 * Requests and result slots retain input order. The adapter owns the requests for
 * the duration of the synchronous dispatch, while KernelDispatch owns worker
 * admission, cancellation, and barrier behavior.
 */
[[nodiscard]] MaterializationBatchResult DispatchMaterializationBatch(
    std::vector<BulkMaterializationRequest> requests,
    const IMaterializationBackend& backend,
    size_t workerCount,
    std::stop_token stopToken = {});

[[nodiscard]] uint64_t canonicalHash(const SerializedOctree& page);

} // namespace Vixen::SVO
