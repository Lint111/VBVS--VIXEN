#include "pch.h"
#include "VoxelComponents.h"
#include "MortonEncoding.h"  // Core library unified Morton implementation
#include <cmath>

namespace Vixen::GaiaVoxel {

// ============================================================================
// MortonKeyUtils Implementation (Free Functions)
//
// WEEK 4 PHASE A.1: Unified Morton Architecture
// Delegates all Morton operations to Core::MortonCode64.
// This eliminates duplicate implementations and ensures consistency.
// ============================================================================

namespace MortonKeyUtils {

glm::ivec3 decode(uint64_t code) {
    return Vixen::Core::MortonCode64{code}.toWorldPos();
}

glm::ivec3 decode(const MortonKey& key) {
    return Vixen::Core::MortonCode64{key.code}.toWorldPos();
}

glm::vec3 toWorldPos(uint64_t code) {
    return Vixen::Core::MortonCode64{code}.toWorldPosF();
}

glm::vec3 toWorldPos(const MortonKey& key) {
    return Vixen::Core::MortonCode64{key.code}.toWorldPosF();
}

uint64_t encode(const glm::vec3& pos) {
    return Vixen::Core::MortonCode64::fromWorldPos(pos).code;
}

uint64_t encode(const glm::ivec3& pos) {
    return Vixen::Core::MortonCode64::fromWorldPos(pos).code;
}

MortonKey fromPosition(const glm::vec3& pos) {
    return MortonKey{Vixen::Core::MortonCode64::fromWorldPos(pos).code};
}

MortonKey fromPosition(const glm::ivec3& pos) {
    return MortonKey{Vixen::Core::MortonCode64::fromWorldPos(pos).code};
}

} // namespace MortonKeyUtils

} // namespace Vixen::GaiaVoxel
