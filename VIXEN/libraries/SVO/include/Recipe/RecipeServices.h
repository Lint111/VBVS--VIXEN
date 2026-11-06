#pragma once

// R841 consumer boundary.  These are the only generated recipe-service headers a VIXEN
// consumer is allowed to include; all opcode lowering, analysis and shader emission live in
// the pinned kernel provider targets.
#include "YkRecipeAnalysis.h"
#include "YkRecipeGpuLowering.h"
#include "YkRecipeRuntime.h"

#include "Recipe/RecipeRegistry.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::SVO::Recipe {

struct RecipeServiceApis {
    YkRecipeRuntimeApi cpu{};
    YkRecipeGpuApi gpu{};
    YkRecipeAnalysisApi analysis{};
    bool available = false;
};

[[nodiscard]] inline const RecipeServiceApis& GetRecipeServiceApis() noexcept {
    static const RecipeServiceApis apis = [] {
        RecipeServiceApis value;
        const auto cpuStatus = ykRecipeRuntimeGetApi(
            YK_RECIPE_RUNTIME_ABI_VERSION, &value.cpu, sizeof(value.cpu));
        const auto gpuStatus = ykRecipeGpuGetApi(
            YK_RECIPE_GPU_ABI_VERSION, &value.gpu, sizeof(value.gpu));
        const auto analysisStatus = ykRecipeAnalysisGetApi(
            YK_RECIPE_ANALYSIS_ABI_VERSION, &value.analysis, sizeof(value.analysis));
        value.available = cpuStatus == YK_RECIPE_OK && gpuStatus == YK_RECIPE_GPU_OK &&
                          analysisStatus == YK_RECIPE_ANALYSIS_OK;
        return value;
    }();
    return apis;
}

[[nodiscard]] inline std::string RecipeContentDigest(
    std::span<const SdfInstruction> instructions,
    std::span<const Yeroket::Sdf::Kernels::PathInstance> pathRows = {}) {
    const auto& services = GetRecipeServiceApis();
    if (!services.available || services.cpu.contentDigest == nullptr) return {};

    char digest[YK_RECIPE_RUNTIME_DIGEST_CHARS]{};
    const auto status = services.cpu.contentDigest(
        reinterpret_cast<const YkRecipeInstruction*>(instructions.data()),
        static_cast<std::uint32_t>(instructions.size()),
        pathRows.empty() ? nullptr : pathRows.data(),
        static_cast<std::uint32_t>(pathRows.size()),
        sizeof(Yeroket::Sdf::Kernels::PathInstance), digest);
    return status == YK_RECIPE_OK ? std::string(digest) : std::string{};
}

struct RecipeRegistrationAnalysis {
    enum class BoundSource { Authored, Derived, EngineDefault };

    BoundSource boundSource = BoundSource::EngineDefault;
    bool occupancyAvailable = false;
    std::uint32_t status = YK_RECIPE_ANALYSIS_OK;
    std::string diagnostic;
};

// Registration is intentionally one service call for the whole family.  In particular, the
// authored sphere is supplied to the kernel as boundSupplied, so a service-side conservative
// derivation can never replace an authored bound.
[[nodiscard]] inline RecipeRegistrationAnalysis AnalyzeRecipeRegistration(
    RecipeRegistry::RecipeEntry& entry,
    float defaultBoundRadius,
    float defaultStepRelaxation) {
    RecipeRegistrationAnalysis summary;
    const auto& services = GetRecipeServiceApis();
    if (!services.available || services.analysis.analyze == nullptr) {
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = defaultBoundRadius;
        entry.stepRelaxation = defaultStepRelaxation;
        return summary;
    }

    YkRecipeAnalysisRequest request{};
    request.structSize = sizeof(request);
    request.flags = YK_RECIPE_ANALYSIS_BOUND | YK_RECIPE_ANALYSIS_OCCUPANCY;
    request.backend = YK_RECIPE_ANALYSIS_BACKEND_CPU_STRICT;
    request.parameterCount = static_cast<std::uint32_t>(entry.parameterValues.size());
    request.parameters = entry.parameterValues.empty() ? nullptr : entry.parameterValues.data();
    request.boundSupplied = entry.boundRadius > 0.0f ? 1u : 0u;
    request.boundCenter[0] = entry.boundCenter.x;
    request.boundCenter[1] = entry.boundCenter.y;
    request.boundCenter[2] = entry.boundCenter.z;
    request.boundRadius = entry.boundRadius;
    request.occupancyDense = 64u;
    request.occupancyCoarse = 16u;

    YkRecipeAnalysisResult* result = nullptr;
    YkRecipeAnalysisDiagnostic diagnostic{};
    summary.status = services.analysis.analyze(
        &request,
        reinterpret_cast<const YkRecipeAnalysisInstruction*>(entry.bytecode.data()),
        static_cast<std::uint32_t>(entry.bytecode.size()), &result, &diagnostic);
    if (summary.status != YK_RECIPE_ANALYSIS_OK || result == nullptr) {
        summary.diagnostic = diagnostic.message;
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = defaultBoundRadius;
        entry.stepRelaxation = defaultStepRelaxation;
        return summary;
    }

    YkRecipeAnalysisBound bound{};
    bound.structSize = sizeof(bound);
    if (services.analysis.bound(result, &bound) == YK_RECIPE_ANALYSIS_OK && bound.available != 0u) {
        if (request.boundSupplied != 0u) {
            summary.boundSource = RecipeRegistrationAnalysis::BoundSource::Authored;
        } else {
            entry.boundCenter = glm::vec3(bound.center[0], bound.center[1], bound.center[2]);
            entry.boundRadius = bound.radius;
            summary.boundSource = RecipeRegistrationAnalysis::BoundSource::Derived;
        }
    } else {
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = defaultBoundRadius;
        summary.boundSource = RecipeRegistrationAnalysis::BoundSource::EngineDefault;
    }
    if (!(entry.boundRadius > 0.0f)) {
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = defaultBoundRadius;
        summary.boundSource = RecipeRegistrationAnalysis::BoundSource::EngineDefault;
    }
    if (!(entry.stepRelaxation > 0.0f)) entry.stepRelaxation = defaultStepRelaxation;

    YkRecipeAnalysisOccupancy occupancy{};
    occupancy.structSize = sizeof(occupancy);
    if (services.analysis.occupancy(result, &occupancy, nullptr, 0u) == YK_RECIPE_ANALYSIS_OK &&
        occupancy.available != 0u && occupancy.valueCount != 0u) {
        std::vector<float> values(occupancy.valueCount);
        const auto occupancyStatus = services.analysis.occupancy(
            result, &occupancy, values.data(), static_cast<std::uint32_t>(values.size()));
        if (occupancyStatus == YK_RECIPE_ANALYSIS_OK) {
            entry.occupancyGridValues = std::move(values);
            entry.occupancyGridDim = occupancy.dim;
            entry.occupancyGridAabbMin = glm::vec3(
                occupancy.aabbMinimum[0], occupancy.aabbMinimum[1], occupancy.aabbMinimum[2]);
            entry.occupancyGridCellSize = occupancy.cellSize;
            summary.occupancyAvailable = true;
        }
    }
    if (!summary.occupancyAvailable) {
        entry.occupancyGridValues.clear();
        entry.occupancyGridDim = 0u;
        entry.occupancyGridAabbMin = glm::vec3(0.0f);
        entry.occupancyGridCellSize = 0.0f;
    }

    services.analysis.release(result);
    return summary;
}

struct RecipeSceneVariantResult {
    std::vector<SdfInstruction> instructions;
    YkRecipeAnalysisVariantStats stats{};
    std::uint32_t status = YK_RECIPE_ANALYSIS_UNAVAILABLE;
};

struct RecipeSceneAnalysis {
    RecipeSceneVariantResult tile;
    RecipeSceneVariantResult compacted;
    std::uint32_t status = YK_RECIPE_ANALYSIS_UNAVAILABLE;
};

// Scene preparation owns the domain facts and asks the same analysis result for both variants.
// The helper is deliberately data-only so render-graph callers can retain the returned tapes
// without importing the retired tile/compaction implementation headers.
[[nodiscard]] inline RecipeSceneAnalysis AnalyzeRecipeScene(
    std::span<const SdfInstruction> instructions,
    std::span<const float> parameters,
    glm::vec3 domainMinimum,
    glm::vec3 domainMaximum,
    std::uint32_t backend,
    bool domainEnforced,
    std::uint32_t requiredOutputChannels,
    std::array<std::uint8_t, YK_RECIPE_ANALYSIS_MAX_CHANNELS> channelDependencies,
    bool duplicateSelectionAndProvenanceEquivalent,
    std::uint64_t sourceRevision) {
    RecipeSceneAnalysis output;
    const auto& services = GetRecipeServiceApis();
    if (!services.available || services.analysis.analyze == nullptr) return output;

    YkRecipeAnalysisRequest request{};
    request.structSize = sizeof(request);
    request.flags = YK_RECIPE_ANALYSIS_TILE | YK_RECIPE_ANALYSIS_COMPACTED;
    request.backend = backend;
    request.parameterCount = static_cast<std::uint32_t>(parameters.size());
    request.parameters = parameters.empty() ? nullptr : parameters.data();
    request.domainMinimum[0] = domainMinimum.x;
    request.domainMinimum[1] = domainMinimum.y;
    request.domainMinimum[2] = domainMinimum.z;
    request.domainMaximum[0] = domainMaximum.x;
    request.domainMaximum[1] = domainMaximum.y;
    request.domainMaximum[2] = domainMaximum.z;
    request.domainEnforced = domainEnforced ? 1u : 0u;
    request.requiredOutputChannels = requiredOutputChannels;
    for (std::size_t index = 0; index < channelDependencies.size(); ++index)
        request.channelDependencies[index] = channelDependencies[index];
    request.duplicateSelectionAndProvenanceEquivalent = duplicateSelectionAndProvenanceEquivalent ? 1u : 0u;
    request.sourceRevision = sourceRevision;

    YkRecipeAnalysisResult* result = nullptr;
    YkRecipeAnalysisDiagnostic diagnostic{};
    output.status = services.analysis.analyze(
        &request,
        reinterpret_cast<const YkRecipeAnalysisInstruction*>(instructions.data()),
        static_cast<std::uint32_t>(instructions.size()), &result, &diagnostic);
    if (output.status != YK_RECIPE_ANALYSIS_OK || result == nullptr) return output;

    const auto readVariant = [&](std::uint32_t kind, RecipeSceneVariantResult& target) {
        target.stats.structSize = sizeof(target.stats);
        std::uint32_t count = 0;
        target.status = services.analysis.variant(result, kind, nullptr, 0u, &count, &target.stats);
        if (target.status != YK_RECIPE_ANALYSIS_OK || count == 0u) return;
        target.instructions.resize(count);
        target.status = services.analysis.variant(
            result, kind,
            reinterpret_cast<YkRecipeAnalysisInstruction*>(target.instructions.data()),
            count, &count, &target.stats);
        if (target.status == YK_RECIPE_ANALYSIS_OK) target.instructions.resize(count);
        else target.instructions.clear();
    };
    readVariant(YK_RECIPE_ANALYSIS_VARIANT_TILE, output.tile);
    readVariant(YK_RECIPE_ANALYSIS_VARIANT_COMPACTED, output.compacted);
    services.analysis.release(result);
    return output;
}

[[nodiscard]] inline std::string RecipeGpuField(
    std::span<const SdfInstruction> instructions,
    std::uint32_t recipeId,
    bool declaredPositionOutParam,
    std::span<const Yeroket::Sdf::Kernels::PathInstance> pathRows = {},
    std::uint32_t glyphIndexBase = 0u,
    bool boundedGlyph = false) {
    const auto& services = GetRecipeServiceApis();
    if (!services.available || services.gpu.lower == nullptr || services.gpu.text == nullptr) return {};

    YkRecipeGpuRequest request{};
    request.structSize = sizeof(request);
    request.backend = YK_RECIPE_GPU_BACKEND_GLSL_FIELD;
    request.flags = (declaredPositionOutParam ? YK_RECIPE_GPU_FIELD_DECLARED_POSITION : 0u) |
                    (boundedGlyph ? YK_RECIPE_GPU_FIELD_BOUNDED_GLYPH : 0u);
    request.alias = recipeId;
    request.glyphIndexBase = glyphIndexBase;
    YkRecipeGpuModule* module = nullptr;
    YkRecipeGpuDiagnostic diagnostic{};
    const auto status = services.gpu.lower(
        &request,
        reinterpret_cast<const YkRecipeGpuInstruction*>(instructions.data()),
        static_cast<std::uint32_t>(instructions.size()),
        pathRows.empty() ? nullptr : pathRows.data(),
        static_cast<std::uint32_t>(pathRows.size()),
        sizeof(Yeroket::Sdf::Kernels::PathInstance), &module, &diagnostic);
    if (status != YK_RECIPE_GPU_OK || module == nullptr) return {};

    std::size_t length = 0;
    std::string field;
    if (services.gpu.text(module, YK_RECIPE_GPU_PART_FIELD, nullptr, 0u, &length) == YK_RECIPE_GPU_OK) {
        std::vector<char> buffer(length + 1u, '\0');
        if (services.gpu.text(module, YK_RECIPE_GPU_PART_FIELD, buffer.data(), buffer.size(), &length) == YK_RECIPE_GPU_OK)
            field.assign(buffer.data(), length);
    }
    services.gpu.release(module);
    return field;
}

} // namespace Vixen::SVO::Recipe
