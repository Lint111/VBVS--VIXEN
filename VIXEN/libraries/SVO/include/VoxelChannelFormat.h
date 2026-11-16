#pragma once
#include <cstdint>
#include "Generated/OctreeConfig.g.h"   // Vixen::Gpu::{ChannelDesc,OctreeConfig} — generated single-source (Phase C)
namespace Vixen::SVO {
// How a SCALAR field is integrated by the renderer (declared by the data, not inferred).
// The generated vocabulary consumes these stable field-kind values.
enum FieldKind : uint32_t { FK_NONE = 0, FK_DISTANCE = 1, FK_DENSITY = 2 };
}  // namespace Vixen::SVO

// Semantic identity, output slots, material-row layout, defaults, and the shader mirror are
// generated from the pinned kernel channel declarations. Runtime mode intentionally emits zero
// material rows; pack rows arrive through the runtime table seam.
#include "Generated/VoxelChannelVocabulary.g.h"

namespace Vixen::SVO {
constexpr uint32_t kVoxelsPerBrick = 512u;          // 8^3, no apron (Inc2 as-built)
constexpr uint32_t kMaxChannels    = 8u;            // fits the OctreeConfig tail (<= ~12)
using Vixen::Gpu::ChannelDesc;   // was a hand-written struct; now the generated single-source type (Phase C)

static_assert(kChannelVocabulary.size() == static_cast<std::size_t>(SEM_COUNT),
              "generated channel vocabulary must cover every semantic ordinal");
static_assert(SEM_COUNT <= 32u,
              "recipe output request masks use one uint32 bit per semantic ordinal");

// Authored Output instructions carry the generated compact output slot. GPU request masks use
// the semantic ordinal bit, and the generated material sidecar is the one descriptor that uses
// the semantic ordinal directly. Keep these ABI conversions named so a channel-table expansion
// cannot silently make slot 8 mean AO instead of MaterialId.
constexpr std::uint32_t RecipeOutputInstructionChannelFor(SemanticId semantic) noexcept {
    return RecipeOutputSlotFor(semantic);
}

constexpr std::uint32_t RecipeMaterialSidecarChannelFor(SemanticId semantic) noexcept {
    return static_cast<std::uint32_t>(semantic);
}

constexpr std::uint32_t RecipeOutputRequestMaskFor(SemanticId semantic) noexcept {
    return 1u << RecipeMaterialSidecarChannelFor(semantic);
}

// Resolve ordinary output descriptors by generated slot. RecipePipelineCppEmitter also appends
// a material sidecar descriptor with channel SEM_MATERIAL_ID; recognize that one explicit
// generated descriptor in addition to the ordinary slot encoding.
constexpr const RecipeOutputChannelSpec* FindRecipeOutputChannelForDescriptor(
    std::uint32_t channel) noexcept {
    if (const auto* spec = FindRecipeOutputChannel(channel); spec != nullptr) return spec;
    if (channel != RecipeMaterialSidecarChannelFor(SEM_MATERIAL_ID)) return nullptr;
    for (const auto& spec : kOutputtableChannelSpecs)
        if (spec.semantic == SEM_MATERIAL_ID) return &spec;
    return nullptr;
}

static_assert(RecipeOutputInstructionChannelFor(SEM_MATERIAL_ID) !=
                  RecipeMaterialSidecarChannelFor(SEM_MATERIAL_ID),
              "generated MaterialId slot and material-sidecar channel must remain distinct");
}  // namespace Vixen::SVO
