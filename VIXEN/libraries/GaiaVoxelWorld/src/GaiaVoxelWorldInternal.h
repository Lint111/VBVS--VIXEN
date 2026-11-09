#pragma once

#include "GaiaVoxelWorld.h"

#include <unordered_map>

namespace Vixen::GaiaVoxel {

// The implementation is shared by the private translation units, while the public
// header keeps the ECS world opaque to callers.
struct GaiaVoxelWorld::Impl {
    gaia::ecs::World world;

    // Morton code -> Entity index for O(1) spatial lookup.
    std::unordered_map<uint64_t, gaia::ecs::Entity> mortonIndex;

    void Init();
    void rebuildMortonIndex();
};

} // namespace Vixen::GaiaVoxel
