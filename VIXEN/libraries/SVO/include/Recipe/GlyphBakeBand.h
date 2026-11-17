#pragma once

// The exact band a recipe needs around each glyph row it uses (R892.19, controller 11-17).
//
// A baked glyph is exact only within the band of its bake; farther out the walk returns a lower
// bound. That is enough for marching, but an operation that moves the surface away from the
// outline (Round, Onion, a smooth blend) puts the rendered surface at that offset, so the band
// must cover the largest offset a recipe applies to the glyph. Registration derives the offset
// from the program and refuses a recipe no bake of the font covers.

#include "Recipe/SdfInstruction.h"
#include "Recipe/generated/SdfOpCodes.g.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Vixen::SVO::Recipe {

struct GlyphBandNeed {
    std::uint32_t instructionIndex = 0;
    // The GlyphOutline operand: the row index.
    std::uint32_t row = 0;
    // The largest offset applied to the glyph's distance, in the row's own units; infinity when
    // the glyph's distance goes through an operation whose effect on it is not bounded here.
    float offsetRowUnits = 0.0f;
};

// Follows every GlyphOutline value through the program's value stack. Round and Onion add their
// radius, a smooth union its blend width; a plain combine carries the glyph on; RestorePos
// changes the unit by the distance scale of its Transform. Anything else that consumes the
// value makes the need unbounded: only a bake that is exact everywhere serves such a recipe.
[[nodiscard]] inline std::vector<GlyphBandNeed> DeriveGlyphBandNeeds(std::span<const SdfInstruction> program) {
    struct Contribution {
        std::size_t need;
        float valuePerRowUnit; // the stack value is the row-unit distance times this
    };
    std::vector<GlyphBandNeed> needs;
    std::vector<std::vector<Contribution>> values;
    std::vector<float> distanceScales;
    const float unbounded = std::numeric_limits<float>::infinity();
    auto grow = [&](const std::vector<Contribution>& slot, float amount) {
        for (const Contribution& c : slot)
            needs[c.need].offsetRowUnits += c.valuePerRowUnit != 0.0f ? std::abs(amount / c.valuePerRowUnit) : unbounded;
    };
    for (std::size_t index = 0; index < program.size(); ++index) {
        const SdfInstruction& in = program[index];
        const auto op = static_cast<SdfOpCode>(in.opCode);
        const StackArity arity = RecipeStackStaticArity(op);
        if (op == SdfOpCode::RestorePos) {
            const float scale = distanceScales.empty() ? 1.0f : distanceScales.back();
            if (!distanceScales.empty()) distanceScales.pop_back();
            if (!values.empty())
                for (Contribution& c : values.back()) c.valuePerRowUnit *= scale;
            continue;
        }
        for (int pop = 0; pop < arity.pPop && !distanceScales.empty(); ++pop) distanceScales.pop_back();
        for (int push = 0; push < arity.pPush; ++push)
            distanceScales.push_back(op == SdfOpCode::Transform ? in.data[11] : 1.0f);

        std::vector<Contribution> consumed;
        for (int pop = 0; pop < arity.vPop && !values.empty(); ++pop) {
            consumed.insert(consumed.end(), values.back().begin(), values.back().end());
            values.pop_back();
        }
        switch (op) {
            case SdfOpCode::GlyphOutline:
                needs.push_back({static_cast<std::uint32_t>(index), std::bit_cast<std::uint32_t>(in.data[0]), 0.0f});
                consumed.push_back({needs.size() - 1, 1.0f});
                break;
            case SdfOpCode::Round:
            case SdfOpCode::Onion:
                grow(consumed, in.data[0]);
                break;
            case SdfOpCode::SmoothUnion:
                grow(consumed, in.data[2]);
                break;
            case SdfOpCode::Union:
            case SdfOpCode::Subtract:
            case SdfOpCode::Intersect:
            case SdfOpCode::Xor:
            case SdfOpCode::MathMin:
            case SdfOpCode::MathMax:
            case SdfOpCode::MathNegate:
            case SdfOpCode::Passthrough:
            case SdfOpCode::Output:
                break;
            default:
                for (const Contribution& c : consumed) needs[c.need].offsetRowUnits = unbounded;
                break;
        }
        for (int push = 0; push < arity.vPush; ++push)
            values.push_back(push == 0 ? consumed : std::vector<Contribution>{});
    }
    return needs;
}

} // namespace Vixen::SVO::Recipe
