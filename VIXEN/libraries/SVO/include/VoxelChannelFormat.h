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

// Recipe output descriptors carry the shipped semantic channel ordinal (MaterialId = 7),
// while the generated vocabulary's outputSlot is the compact output-mask bit. Resolve through
// the generated semantic/slot helpers so categorical sidecars reach the matching storage spec.
constexpr const RecipeOutputChannelSpec* FindRecipeOutputChannelForDescriptor(
    std::uint32_t channel) noexcept {
    const auto semantic = static_cast<SemanticId>(channel);
    const auto slot = RecipeOutputSlotFor(semantic);
    if (const auto* spec = FindRecipeOutputChannel(slot); spec != nullptr && spec->semantic == semantic)
        return spec;
    return FindRecipeOutputChannel(channel);
}
}  // namespace Vixen::SVO
