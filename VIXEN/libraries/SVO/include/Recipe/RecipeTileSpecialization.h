#pragma once

#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/RecipeSimd.g.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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

struct RecipeInterval {
    double lower = 0.0;
    double upper = 0.0;
};

struct RecipeTapeNode {
    SdfInstruction instruction{};
    RecipeInterval interval{};
    std::unique_ptr<RecipeTapeNode> left;
    std::unique_ptr<RecipeTapeNode> right;
    std::uint32_t sourceInstructions = 1;
};

inline bool FiniteDomain(const RecipeTileDomain& domain) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(domain.minimum[axis]) || !std::isfinite(domain.maximum[axis])
            || domain.minimum[axis] > domain.maximum[axis])
            return false;
    }
    return true;
}

inline RecipeInterval WidenForFloatBackend(double lower, double upper, double scale) {
    // The interval geometry is computed in double, while the declared C++/GLSL bodies use
    // float operations (subtraction, squares, reduction, sqrt, and final subtraction).
    // A 64-epsilon envelope covers those rounded stages, including a fused reduction, and
    // the absolute term covers subnormal rounding near zero. If this envelope becomes
    // non-finite, callers decline the proof.
    constexpr double kFloatEpsilon = std::numeric_limits<float>::epsilon();
    const double error = std::max(1.0, scale) * 64.0 * kFloatEpsilon
        + 64.0 * std::numeric_limits<float>::denorm_min();
    return {lower - error, upper + error};
}

inline bool TrySphereInterval(const SdfInstruction& instruction,
    const RecipeTileDomain& domain, int centerOffset, int radiusOffset,
    RecipeInterval& interval) {
    const double radius = instruction.data[radiusOffset];
    if (!std::isfinite(radius) || radius < 0.0) return false;

    double minimumSquared = 0.0;
    double maximumSquared = 0.0;
    double scale = std::max(1.0, std::abs(radius));
    for (int axis = 0; axis < 3; ++axis) {
        const double center = instruction.data[centerOffset + axis];
        const double lo = static_cast<double>(domain.minimum[axis]) - center;
        const double hi = static_cast<double>(domain.maximum[axis]) - center;
        if (!std::isfinite(center) || !std::isfinite(lo) || !std::isfinite(hi)) return false;
        const double minAbs = lo <= 0.0 && hi >= 0.0
            ? 0.0 : std::min(std::abs(lo), std::abs(hi));
        const double maxAbs = std::max(std::abs(lo), std::abs(hi));
        minimumSquared += minAbs * minAbs;
        maximumSquared += maxAbs * maxAbs;
        scale = std::max({scale, std::abs(center),
            std::abs(static_cast<double>(domain.minimum[axis])),
            std::abs(static_cast<double>(domain.maximum[axis])), maxAbs});
    }
    if (!std::isfinite(maximumSquared)
        || maximumSquared > static_cast<double>(std::numeric_limits<float>::max()) / 8.0)
        return false;

    const double lower = std::sqrt(minimumSquared) - radius;
    const double upper = std::sqrt(maximumSquared) - radius;
    scale = std::max(scale, std::sqrt(maximumSquared) + radius);
    interval = WidenForFloatBackend(lower, upper, scale);
    return std::isfinite(interval.lower) && std::isfinite(interval.upper);
}

inline bool TryBoxInterval(const SdfInstruction& instruction,
    const RecipeTileDomain& domain, int extentOffset, RecipeInterval& interval) {
    double qLower[3]{};
    double qUpper[3]{};
    double outsideLowerSquared = 0.0;
    double outsideUpperSquared = 0.0;
    double scale = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        const double extent = instruction.data[extentOffset + axis];
        const double lo = domain.minimum[axis];
        const double hi = domain.maximum[axis];
        if (!std::isfinite(extent) || extent < 0.0) return false;
        const double minAbs = lo <= 0.0 && hi >= 0.0
            ? 0.0 : std::min(std::abs(lo), std::abs(hi));
        const double maxAbs = std::max(std::abs(lo), std::abs(hi));
        qLower[axis] = minAbs - extent;
        qUpper[axis] = maxAbs - extent;
        const double outsideLower = std::max(qLower[axis], 0.0);
        const double outsideUpper = std::max(qUpper[axis], 0.0);
        outsideLowerSquared += outsideLower * outsideLower;
        outsideUpperSquared += outsideUpper * outsideUpper;
        scale = std::max({scale, std::abs(lo), std::abs(hi), extent,
            std::abs(qLower[axis]), std::abs(qUpper[axis])});
    }
    if (!std::isfinite(outsideUpperSquared)
        || outsideUpperSquared > static_cast<double>(std::numeric_limits<float>::max()) / 8.0)
        return false;

    const double insideLower = std::min(std::max({qLower[0], qLower[1], qLower[2]}), 0.0);
    const double insideUpper = std::min(std::max({qUpper[0], qUpper[1], qUpper[2]}), 0.0);
    const double lower = std::sqrt(outsideLowerSquared) + insideLower;
    const double upper = std::sqrt(outsideUpperSquared) + insideUpper;
    scale = std::max(scale, std::sqrt(outsideUpperSquared) + std::abs(insideLower));
    interval = WidenForFloatBackend(lower, upper, scale);
    return std::isfinite(interval.lower) && std::isfinite(interval.upper);
}

inline void EmitPostfix(const RecipeTapeNode& node, std::vector<SdfInstruction>& output) {
    if (node.left) EmitPostfix(*node.left, output);
    if (node.right) EmitPostfix(*node.right, output);
    output.push_back(node.instruction);
}

} // namespace detail

// Builds a disposable per-tile postfix tape from the original recipe. The caller retains
// the source program unchanged; every uncertain or unsupported input returns that program
// verbatim so this derived form can never erase edit history or become a false negative.
inline RecipeTileSpecializationResult SpecializeRecipeTapeForTile(
    std::span<const SdfInstruction> program, const RecipeTileDomain& domain) {
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

    std::array<std::unique_ptr<detail::RecipeTapeNode>, 64> stack{};
    int sp = 0;
    for (const SdfInstruction& instruction : program) {
        const auto* metadata = FindRecipeOpcodeMetadata(instruction.opCode);
        if (metadata == nullptr) return fallback(RecipeTileFallback::UnsupportedOpcode);

        using Yeroket::Sdf::Generated::RecipeIntervalRule;
        if (metadata->intervalRule == RecipeIntervalRule::Sphere
            || metadata->intervalRule == RecipeIntervalRule::AxisAlignedBox) {
            if (metadata->vpop != 0 || metadata->vpush != 1 || sp >= static_cast<int>(stack.size()))
                return fallback(RecipeTileFallback::InvalidStack);
            auto node = std::make_unique<detail::RecipeTapeNode>();
            node->instruction = instruction;
            ++result.stats.intervalEvaluations;
            const bool valid = metadata->intervalRule == RecipeIntervalRule::Sphere
                ? detail::TrySphereInterval(instruction, domain, metadata->intervalData0,
                    metadata->intervalData1, node->interval)
                : detail::TryBoxInterval(instruction, domain, metadata->intervalData0,
                    node->interval);
            if (!valid) return fallback(RecipeTileFallback::InvalidPrimitive);
            stack[sp++] = std::move(node);
            continue;
        }

        if (metadata->intervalRule != RecipeIntervalRule::HardUnion
            && metadata->intervalRule != RecipeIntervalRule::HardSubtract
            && metadata->intervalRule != RecipeIntervalRule::HardIntersect)
            return fallback(RecipeTileFallback::UnsupportedOpcode);
        if (metadata->vpop != 2 || metadata->vpush != 1 || sp < 2
            || !stack[sp - 2] || !stack[sp - 1])
            return fallback(RecipeTileFallback::InvalidStack);

        auto a = std::move(stack[sp - 2]);
        auto b = std::move(stack[sp - 1]);
        detail::RecipeInterval combined{};
        int selectedChild = -1;
        ++result.stats.intervalEvaluations;
        switch (metadata->intervalRule) {
            case RecipeIntervalRule::HardUnion:
                combined = {std::min(a->interval.lower, b->interval.lower),
                    std::min(a->interval.upper, b->interval.upper)};
                if (a->interval.upper < b->interval.lower) selectedChild = 0;
                else if (b->interval.upper < a->interval.lower) selectedChild = 1;
                break;
            case RecipeIntervalRule::HardIntersect:
                combined = {std::max(a->interval.lower, b->interval.lower),
                    std::max(a->interval.upper, b->interval.upper)};
                if (a->interval.lower > b->interval.upper) selectedChild = 0;
                else if (b->interval.lower > a->interval.upper) selectedChild = 1;
                break;
            case RecipeIntervalRule::HardSubtract: {
                const detail::RecipeInterval negatedB{-b->interval.upper, -b->interval.lower};
                combined = {std::max(a->interval.lower, negatedB.lower),
                    std::max(a->interval.upper, negatedB.upper)};
                // If the cutter wins, its negation is still observable; only remove it
                // when the base is strictly greater than -cutter throughout the tile.
                if (a->interval.lower > negatedB.upper) selectedChild = 0;
                break;
            }
            default:
                return fallback(RecipeTileFallback::UnsupportedOpcode);
        }

        if (!std::isfinite(combined.lower) || !std::isfinite(combined.upper))
            return fallback(RecipeTileFallback::InvalidPrimitive);
        if (selectedChild >= 0) {
            const std::uint32_t removed = (selectedChild == 0 ? b->sourceInstructions
                : a->sourceInstructions) + 1u;
            result.stats.prunedInstructions += removed;
            ++result.stats.prunedBranches;
            stack[sp - 2] = selectedChild == 0 ? std::move(a) : std::move(b);
        } else {
            auto node = std::make_unique<detail::RecipeTapeNode>();
            node->instruction = instruction;
            node->interval = combined;
            node->sourceInstructions = a->sourceInstructions + b->sourceInstructions + 1u;
            node->left = std::move(a);
            node->right = std::move(b);
            stack[sp - 2] = std::move(node);
        }
        --sp;
    }

    if (sp != 1 || !stack[0]) return fallback(RecipeTileFallback::InvalidStack);
    result.instructions.clear();
    result.instructions.reserve(stack[0]->sourceInstructions);
    detail::EmitPostfix(*stack[0], result.instructions);
    result.stats.retainedInstructions = static_cast<std::uint32_t>(result.instructions.size());
    result.stats.specialized = result.stats.prunedInstructions != 0;
    return finish();
}

} // namespace Vixen::SVO::Recipe
