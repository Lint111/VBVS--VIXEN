#pragma once

#include "Recipe/RecipeTileSpecialization.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace Vixen::SVO::Recipe {

enum class RecipeOutputChannel : std::uint8_t {
    Geometry,
    Material,
    Provenance,
    Smoothness,
    Emission,
    Count,
};

enum class RecipeChannelDependency : std::uint8_t {
    Undeclared,
    Independent,
    GeometryWinner,
};

inline constexpr std::uint32_t RecipeOutputChannelBit(RecipeOutputChannel channel) {
    return 1u << static_cast<std::uint32_t>(channel);
}

inline constexpr std::uint32_t kAllRecipeOutputChannels =
    (1u << static_cast<std::uint32_t>(RecipeOutputChannel::Count)) - 1u;

struct RecipeWholeDomainCompactionRequest {
    // The caller supplies the source recipe's revision. Compaction never edits source history.
    std::uint64_t sourceRevision = 0;
    RecipeTileDomain domain{};
    bool domainIsEnforced = false;
    Yeroket::Sdf::Generated::RecipeProofBackend backend =
        Yeroket::Sdf::Generated::RecipeProofBackend::CpuStrict;
    std::uint32_t requiredOutputChannels = kAllRecipeOutputChannels;
    std::array<RecipeChannelDependency,
        static_cast<std::size_t>(RecipeOutputChannel::Count)> channelDependencies = {
            RecipeChannelDependency::GeometryWinner,
            RecipeChannelDependency::Undeclared,
            RecipeChannelDependency::Undeclared,
            RecipeChannelDependency::Undeclared,
            RecipeChannelDependency::Undeclared,
        };

    // Required non-geometry outputs must be independent of the winner, or the caller must
    // affirm that duplicate branches carry matching tie, material, and provenance behavior.
    bool duplicateSelectionAndProvenanceEquivalent = false;
};

struct RecipeWholeDomainCompactionStats {
    std::uint32_t sourceInstructions = 0;
    std::uint32_t afterIdentityNoOps = 0;
    std::uint32_t afterIdempotentDuplicates = 0;
    std::uint32_t finalInstructions = 0;
    std::uint32_t removedIdentityNoOps = 0;
    std::uint32_t removedIdempotentDuplicateInstructions = 0;
    std::uint32_t removedByIntervalProof = 0;
    std::uint32_t intervalEvaluations = 0;
    std::uint64_t proofNanoseconds = 0;
    bool channelDependenciesBlocked = false;
    bool domainNotEnforced = false;
    bool intervalProofApplied = false;
    RecipeTileFallback intervalFallback = RecipeTileFallback::None;
    std::vector<RecipeOutputChannel> blockedChannels;
};

struct RecipeWholeDomainCompactionResult {
    std::vector<SdfInstruction> instructions;
    std::uint64_t sourceRevision = 0;
    RecipeWholeDomainCompactionStats stats;
};

namespace whole_domain_detail {

inline bool IsGeneratedIdentityNoOp(const auto& metadata) {
    using Yeroket::Sdf::Generated::RecipeControlBehavior;
    using Yeroket::Sdf::Generated::RecipeDerivativeRule;
    using Yeroket::Sdf::Generated::RecipeExecutionClass;

    // These generated properties describe the project's exact no-op forms without an
    // opcode list: they emit no execution/control, preserve both stacks, and preserve value
    // derivatives. InvokeRecipe is excluded by its generated control behavior.
    return metadata.execution == RecipeExecutionClass::LoweredAway
        && metadata.control == RecipeControlBehavior::None
        && metadata.gradientRule == RecipeDerivativeRule::Identity
        && metadata.vpop == metadata.vpush
        && metadata.ppop == metadata.ppush;
}

inline bool InstructionBitsEqual(const SdfInstruction& a, const SdfInstruction& b) {
    if (a.opCode != b.opCode) return false;
    for (std::size_t i = 0; i < std::size(a.data); ++i) {
        if (std::bit_cast<std::uint32_t>(a.data[i])
            != std::bit_cast<std::uint32_t>(b.data[i]))
            return false;
    }
    return true;
}

inline bool ProgramRangesEqual(const std::vector<SdfInstruction>& program,
    std::size_t aBegin, std::size_t aEnd, std::size_t bBegin, std::size_t bEnd) {
    if (aEnd - aBegin != bEnd - bBegin) return false;
    for (std::size_t offset = 0; offset < aEnd - aBegin; ++offset) {
        if (!InstructionBitsEqual(program[aBegin + offset], program[bBegin + offset]))
            return false;
    }
    return true;
}

struct ProgramRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

inline bool CanCollapseDuplicateForChannels(
    const RecipeWholeDomainCompactionRequest& request) {
    if (request.duplicateSelectionAndProvenanceEquivalent) return true;
    for (std::size_t channel = 1;
         channel < static_cast<std::size_t>(RecipeOutputChannel::Count); ++channel) {
        const auto output = static_cast<RecipeOutputChannel>(channel);
        if ((request.requiredOutputChannels & RecipeOutputChannelBit(output)) == 0) continue;
        if (request.channelDependencies[channel] != RecipeChannelDependency::Independent)
            return false;
    }
    return true;
}

inline bool RemoveIdempotentDuplicateBranches(
    std::span<const SdfInstruction> source,
    bool duplicateOutputsAreSafe,
    std::vector<SdfInstruction>& output,
    std::uint32_t& removedInstructions) {
    using Yeroket::Sdf::Generated::RecipeIntervalRule;

    std::vector<ProgramRange> stack;
    stack.reserve(64);
    output.clear();
    output.reserve(source.size());
    removedInstructions = 0;

    for (const SdfInstruction& instruction : source) {
        const auto* metadata = FindRecipeOpcodeMetadata(instruction.opCode);
        if (metadata == nullptr || metadata->ppop != 0 || metadata->ppush != 0)
            return false;

        if ((metadata->intervalRule == RecipeIntervalRule::Sphere
                || metadata->intervalRule == RecipeIntervalRule::AxisAlignedBox)
            && metadata->vpop == 0 && metadata->vpush == 1) {
            const std::size_t begin = output.size();
            output.push_back(instruction);
            stack.push_back({begin, output.size()});
            continue;
        }

        const bool hardUnion = metadata->intervalRule == RecipeIntervalRule::HardUnion;
        const bool hardIntersect = metadata->intervalRule == RecipeIntervalRule::HardIntersect;
        const bool hardSubtract = metadata->intervalRule == RecipeIntervalRule::HardSubtract;
        if ((!hardUnion && !hardIntersect && !hardSubtract)
            || metadata->vpop != 2 || metadata->vpush != 1 || stack.size() < 2)
            return false;

        const ProgramRange left = stack[stack.size() - 2];
        const ProgramRange right = stack.back();
        if (left.end != right.begin) return false;
        const bool duplicate = duplicateOutputsAreSafe && !hardSubtract
            && ProgramRangesEqual(output, left.begin, left.end, right.begin, right.end);
        if (duplicate) {
            output.resize(left.end);
            stack.pop_back();
            stack.back() = left;
            removedInstructions += static_cast<std::uint32_t>(right.end - right.begin + 1);
            continue;
        }

        output.push_back(instruction);
        stack.pop_back();
        stack.back() = {left.begin, output.size()};
    }

    return stack.size() == 1 && stack.front().begin == 0
        && stack.front().end == output.size();
}

} // namespace whole_domain_detail

inline RecipeWholeDomainCompactionResult CompactRecipeOverWholeDomain(
    std::span<const SdfInstruction> source,
    const RecipeWholeDomainCompactionRequest& request) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();

    RecipeWholeDomainCompactionResult result;
    result.sourceRevision = request.sourceRevision;
    result.stats.sourceInstructions = static_cast<std::uint32_t>(source.size());
    result.instructions.assign(source.begin(), source.end());

    const auto finish = [&]() {
        result.stats.finalInstructions = static_cast<std::uint32_t>(result.instructions.size());
        result.stats.proofNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
        return result;
    };

    if (source.empty() || source.size() > UINT32_MAX) {
        result.stats.intervalFallback = RecipeTileFallback::InvalidProgram;
        return finish();
    }

    using Yeroket::Sdf::Generated::RecipeProofBackend;
    // Duplicate min/max folding has no GPU non-interference certificate.
    // Keep these operators on GPU: they may flush a denormal even when their
    // corresponding strict host operation is idempotent. Lowered-away no-ops
    // remain backend-independent because the generated declaration emits no execution.
    const bool duplicateIdentitiesAdmitted =
        (request.backend == RecipeProofBackend::CpuStrict || request.backend == RecipeProofBackend::SimdStrict)
        && Yeroket::Sdf::Generated::RecipeStrictEnvironment();
    std::vector<SdfInstruction> withoutNoOps;
    withoutNoOps.reserve(source.size());
    for (const SdfInstruction& instruction : source) {
        const auto* metadata = FindRecipeOpcodeMetadata(instruction.opCode);
        if (metadata != nullptr && whole_domain_detail::IsGeneratedIdentityNoOp(*metadata)) {
            ++result.stats.removedIdentityNoOps;
            continue;
        }
        withoutNoOps.push_back(instruction);
    }
    // An empty SDF program is not a valid recipe result; keep its original history intact.
    if (withoutNoOps.empty()) {
        result.stats.removedIdentityNoOps = 0;
        result.stats.intervalFallback = RecipeTileFallback::InvalidProgram;
        return finish();
    }
    result.stats.afterIdentityNoOps = static_cast<std::uint32_t>(withoutNoOps.size());

    const auto recordBlockedChannels = [&]() {
        const std::uint32_t geometryBit = RecipeOutputChannelBit(RecipeOutputChannel::Geometry);
        if ((request.requiredOutputChannels & geometryBit) == 0
            || request.channelDependencies[static_cast<std::size_t>(RecipeOutputChannel::Geometry)]
                != RecipeChannelDependency::GeometryWinner) {
            result.stats.blockedChannels.push_back(RecipeOutputChannel::Geometry);
        }
        for (std::size_t index = 1;
             index < static_cast<std::size_t>(RecipeOutputChannel::Count); ++index) {
            const auto channel = static_cast<RecipeOutputChannel>(index);
            if ((request.requiredOutputChannels & RecipeOutputChannelBit(channel)) != 0
                && request.channelDependencies[index] == RecipeChannelDependency::Undeclared)
                result.stats.blockedChannels.push_back(channel);
        }
    };
    recordBlockedChannels();
    if (!result.stats.blockedChannels.empty()) {
        result.instructions = std::move(withoutNoOps);
        result.stats.channelDependenciesBlocked = true;
        result.stats.afterIdempotentDuplicates =
            static_cast<std::uint32_t>(result.instructions.size());
        return finish();
    }

    if (!request.domainIsEnforced) {
        result.instructions = std::move(withoutNoOps);
        result.stats.domainNotEnforced = true;
        result.stats.afterIdempotentDuplicates =
            static_cast<std::uint32_t>(result.instructions.size());
        return finish();
    }

    // Validate the complete supported recipe before algebraic simplification. In particular,
    // two bit-identical but invalid primitives are not enough evidence to rewrite their history.
    const auto preflight = SpecializeRecipeTapeForTile(withoutNoOps, request.domain, request.backend);
    result.stats.intervalEvaluations += preflight.stats.intervalEvaluations;
    if (preflight.stats.fallback != RecipeTileFallback::None) {
        result.instructions = std::move(withoutNoOps);
        result.stats.intervalFallback = preflight.stats.fallback;
        result.stats.afterIdempotentDuplicates =
            static_cast<std::uint32_t>(result.instructions.size());
        return finish();
    }
    const std::uint32_t intervalRemovedBeforeDuplicates = static_cast<std::uint32_t>(
        withoutNoOps.size() - preflight.instructions.size());

    std::vector<SdfInstruction> withoutDuplicates;
    std::uint32_t removedDuplicates = 0;
    const bool duplicateOutputsAreSafe =
        duplicateIdentitiesAdmitted && whole_domain_detail::CanCollapseDuplicateForChannels(request);
    if (whole_domain_detail::RemoveIdempotentDuplicateBranches(
            preflight.instructions, duplicateOutputsAreSafe, withoutDuplicates, removedDuplicates)) {
        result.instructions = std::move(withoutDuplicates);
        result.stats.removedIdempotentDuplicateInstructions = removedDuplicates;
    } else {
        result.instructions = preflight.instructions;
    }
    result.stats.afterIdempotentDuplicates =
        static_cast<std::uint32_t>(result.instructions.size());

    const auto specialized = SpecializeRecipeTapeForTile(result.instructions, request.domain, request.backend);
    result.stats.intervalEvaluations += specialized.stats.intervalEvaluations;
    result.stats.intervalFallback = specialized.stats.fallback;
    if (specialized.stats.fallback == RecipeTileFallback::None) {
        result.stats.intervalProofApplied = true;
        result.stats.removedByIntervalProof = intervalRemovedBeforeDuplicates
            + static_cast<std::uint32_t>(result.instructions.size() - specialized.instructions.size());
        result.instructions = specialized.instructions;
    } else {
        result.instructions = preflight.instructions;
        result.stats.intervalProofApplied = true;
        result.stats.removedByIntervalProof = intervalRemovedBeforeDuplicates;
        result.stats.removedIdempotentDuplicateInstructions = 0;
        result.stats.afterIdempotentDuplicates =
            static_cast<std::uint32_t>(result.instructions.size());
    }
    return finish();
}

} // namespace Vixen::SVO::Recipe
