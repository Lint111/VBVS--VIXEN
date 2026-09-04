#include "BulkMaterialization.h"

#include "MipBake.h"
#include "SdfBake.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

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

class SimdRecipeBatchEvaluator {
public:
    SimdRecipeBatchEvaluator(const Recipe::LoweredRecipeProgram& program,
                             const glm::vec3& center,
                             std::stop_token stopToken)
        : m_program(program), m_center(center), m_stopToken(stopToken) {}

    void eval4(const glm::vec3* points, std::size_t count, float* values) const {
        if (m_stopToken.stop_requested()) {
            throw MaterializationCancelled{};
        }
        glm::vec3 local[4]{};
        for (std::size_t lane = 0; lane < count; ++lane)
            local[lane] = points[lane] - m_center;
        for (std::size_t lane = count; lane < 4; ++lane)
            local[lane] = local[count - 1];
        m_program.Evaluate4(local, values);
    }

private:
    const Recipe::LoweredRecipeProgram& m_program;
    glm::vec3 m_center;
    std::stop_token m_stopToken;
};

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

    Recipe::LoweredRecipeProgram lowered;
    std::string loweringError;
    if (!lowered.Lower(request.recipe.data(), static_cast<uint32_t>(request.recipe.size()),
                       request.parameters, loweringError)) {
        throw std::invalid_argument("CPU recipe SIMD lowering failed: " + loweringError);
    }
    SimdRecipeBatchEvaluator evaluator(lowered, request.center, stopToken);
    auto baked = BakeSdfWorld(
        evaluator,
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

MaterializationBatchResult DispatchMaterializationBatch(
    std::vector<BulkMaterializationRequest> requests,
    const IMaterializationBackend& backend,
    size_t workerCount,
    std::stop_token stopToken) {
    if (requests.size() > std::numeric_limits<uint32_t>::max() ||
        requests.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::invalid_argument("materialization batch is too large for typed dispatch");
    }
    MaterializationBatchResult batch;
    batch.results.resize(requests.size());
    for (size_t i = 0; i < requests.size(); ++i) {
        batch.results[i].key = requests[i].key;
    }
    if (requests.empty()) {
        batch.succeeded = true;
        return batch;
    }

    KernelDispatch::Stage stage;
    stage.owner = "SVO.BulkMaterialization";
    stage.itemCount = static_cast<uint32_t>(requests.size());
    stage.backend = KernelDispatch::Backend::CpuTbb;
    stage.writes = {{1, static_cast<int32_t>(requests.size()), "materialized pages"}};
    stage.perElement = [&](uint32_t index) {
        auto& result = batch.results[index];
        if (stopToken.stop_requested()) {
            result.status = MaterializationStatus::Cancelled;
            return;
        }
        try {
            result.page = backend.materialize(requests[index], stopToken);
            result.canonicalHash = canonicalHash(result.page);
            result.status = MaterializationStatus::Completed;
        } catch (const MaterializationCancelled&) {
            result.status = MaterializationStatus::Cancelled;
        } catch (const std::exception& error) {
            result.status = MaterializationStatus::Failed;
            result.error = error.what();
            throw;
        } catch (...) {
            result.status = MaterializationStatus::Failed;
            result.error = "unknown materialization failure";
            throw;
        }
    };

    const int workers = static_cast<int>(std::clamp<size_t>(
        workerCount == 0 ? KernelDispatch::DefaultWorkerCount() : workerCount,
        1, static_cast<size_t>(std::numeric_limits<int>::max())));
    const KernelDispatch::Handle handle = KernelDispatch::RunPerElementStage(
        stage, {}, workers, stopToken);
    batch.succeeded = handle.ok();
    for (const auto& result : batch.results) {
        batch.succeeded = batch.succeeded && result.status == MaterializationStatus::Completed;
    }
    return batch;
}

} // namespace Vixen::SVO
