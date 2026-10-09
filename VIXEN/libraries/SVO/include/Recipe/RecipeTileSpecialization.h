#pragma once

#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <span>
#include <vector>

namespace Vixen::SVO::Recipe {

struct RecipeTileDomain {
    glm::vec3 minimum;
    glm::vec3 maximum;
};

enum class RecipeTileFallback : std::uint8_t {
    None,
    InvalidDomain,
    InvalidProgram,
    UnsupportedOpcode,
    InvalidPrimitive,
    InvalidStack,
    BackendContract,
};

struct RecipeTileSpecializationStats {
    std::uint32_t sourceInstructions = 0;
    std::uint32_t retainedInstructions = 0;
    std::uint32_t intervalEvaluations = 0;
    std::uint32_t prunedBranches = 0;
    std::uint32_t prunedInstructions = 0;
    std::uint64_t specializationNanoseconds = 0;
    RecipeTileFallback fallback = RecipeTileFallback::None;
    bool specialized = false;
};

struct RecipeTileSpecializationResult {
    std::vector<SdfInstruction> instructions;
    RecipeTileSpecializationStats stats;
};

namespace detail {

inline bool FiniteDomain(const RecipeTileDomain& domain) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(domain.minimum[axis]) || !std::isfinite(domain.maximum[axis])
            || domain.minimum[axis] > domain.maximum[axis])
            return false;
    }
    return true;
}

} // namespace detail

// Builds a disposable per-tile postfix tape from the original recipe. The caller retains
// the source program unchanged; every uncertain or unsupported input returns that program
// verbatim so this derived form can never erase edit history or become a false negative.
inline RecipeTileSpecializationResult SpecializeRecipeTapeForTile(
    std::span<const SdfInstruction> program, const RecipeTileDomain& domain,
    Yeroket::Sdf::Generated::RecipeProofBackend backend =
        Yeroket::Sdf::Generated::RecipeProofBackend::CpuStrict) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    RecipeTileSpecializationResult result;
    result.stats.sourceInstructions = static_cast<std::uint32_t>(program.size());
    result.stats.retainedInstructions = result.stats.sourceInstructions;

    const auto finish = [&]() {
        result.stats.specializationNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
        return result;
    };
    const auto fallback = [&](RecipeTileFallback reason) {
        result.stats.fallback = reason;
        result.stats.specialized = false;
        result.stats.prunedBranches = 0;
        result.stats.prunedInstructions = 0;
        result.stats.retainedInstructions = result.stats.sourceInstructions;
        result.instructions.assign(program.begin(), program.end());
        return finish();
    };

    if (!detail::FiniteDomain(domain)) return fallback(RecipeTileFallback::InvalidDomain);
    if (program.empty() || program.size() > std::numeric_limits<std::uint32_t>::max())
        return fallback(RecipeTileFallback::InvalidProgram);

    using namespace Yeroket::Sdf::Generated;
    for (const auto& instruction : program) {
        const auto* coverage = FindRecipeAnalysisCoverage(instruction.opCode);
        if (!coverage || !coverage->pure || !RecipeBackendCovered(*coverage, backend))
            return fallback(backend == RecipeProofBackend::GpuUncertified
                ? RecipeTileFallback::BackendContract : RecipeTileFallback::UnsupportedOpcode);
    }
    const RecipeRange3 bounds{
        {domain.minimum.x, domain.maximum.x, true, false},
        {domain.minimum.y, domain.maximum.y, true, false},
        {domain.minimum.z, domain.maximum.z, true, false}};
    const auto analysis = AnalyzeRecipeValues(program, bounds, backend, false);
    if (analysis.diagnostic != RecipeAnalysisDiagnostic::None) {
        if (analysis.diagnostic == RecipeAnalysisDiagnostic::StackShape)
            return fallback(RecipeTileFallback::InvalidStack);
        if (analysis.diagnostic == RecipeAnalysisDiagnostic::BackendContract)
            return fallback(RecipeTileFallback::BackendContract);
        return fallback(RecipeTileFallback::UnsupportedOpcode);
    }
    for (const auto& value : analysis.values)
        if (!RecipeFinite(value.fact)) return fallback(RecipeTileFallback::InvalidPrimitive);
    result.stats.intervalEvaluations = static_cast<std::uint32_t>(analysis.values.size());
    result.stats.prunedBranches = analysis.prunedValues;
    // Reconstruct the ordered source tape from generated dependency/selection data.
    // Structural CSE identities are deliberately not used by the postfix evaluator.
    std::vector<std::pair<int, bool>> pending{{analysis.output, false}};
    result.instructions.reserve(program.size());
    while (!pending.empty()) {
        const auto [index, emit] = pending.back(); pending.pop_back();
        const auto& value = analysis.values[index];
        if (emit) result.instructions.push_back(program[value.sourceInstruction]);
        else if (value.selected >= 0) pending.emplace_back(value.selected, false);
        else {
            pending.emplace_back(index, true);
            for (auto it = value.inputs.rbegin(); it != value.inputs.rend(); ++it)
                pending.emplace_back(*it, false);
        }
    }
    result.stats.retainedInstructions = static_cast<std::uint32_t>(result.instructions.size());
    result.stats.prunedInstructions = result.stats.sourceInstructions - result.stats.retainedInstructions;
    result.stats.specialized = result.stats.prunedInstructions != 0;
    return finish();
}

} // namespace Vixen::SVO::Recipe
