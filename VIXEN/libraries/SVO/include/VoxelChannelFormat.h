#pragma once
#include <cstdint>
#include "Generated/OctreeConfig.g.h"   // Vixen::Gpu::{ChannelDesc,OctreeConfig} — generated single-source (Phase C)
namespace Vixen::SVO {
// Semantic identity of a per-voxel channel (the engine's reusable vocabulary).
enum SemanticId : uint32_t {
    SEM_SDF = 0, SEM_COLOR = 1, SEM_ROUGHNESS = 2,
    SEM_NORMAL = 3, SEM_METALLIC = 4, SEM_EMISSION = 5, SEM_DENSITY = 6,
    SEM_COUNT
};
// How a SCALAR field is integrated by the renderer (declared by the data, not inferred).
enum FieldKind : uint32_t { FK_NONE = 0, FK_DISTANCE = 1, FK_DENSITY = 2 };
// Component count per voxel for a channel's element type.
inline uint32_t SemanticElemCount(SemanticId s) {
    return (s == SEM_COLOR || s == SEM_NORMAL || s == SEM_EMISSION) ? 3u : 1u;
}
constexpr uint32_t kVoxelsPerBrick = 512u;          // 8^3, no apron (Inc2 as-built)
constexpr uint32_t kMaxChannels    = 8u;            // fits the OctreeConfig tail (<= ~12)
using Vixen::Gpu::ChannelDesc;   // was a hand-written struct; now the generated single-source type (Phase C)
}  // namespace Vixen::SVO
