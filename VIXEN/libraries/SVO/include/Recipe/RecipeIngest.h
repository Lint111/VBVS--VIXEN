#pragma once
#include <cstddef>
#include <cstdint>
#include "Recipe/RecipeRegistry.h"
#include "Recipe/generated/RecipeContainer.g.h"

namespace Vixen::SVO {

enum class IngestResult { Ok, BadContainer, RegistryRejected };

// IngestBlob — parse a serialised recipe container blob and register it.
// Maps ReadRecipeContainer failure → BadContainer;
// RecipeRegistry::Register failure → RegistryRejected.
inline IngestResult IngestBlob(uint32_t recipeId,
                               const uint8_t* blob, size_t len,
                               RecipeRegistry& reg)
{
    Yeroket::Sdf::Generated::RecipeContainerView view{};
    if (!Yeroket::Sdf::Generated::ReadRecipeContainer(blob, len, view))
        return IngestResult::BadContainer;

    RecipeRegistry::RecipeEntry entry{};
    entry.bakeResolution = view.header.bakeResolution;
    entry.bandVoxels     = view.header.bandVoxels;
    entry.brickDepth     = view.header.brickDepth;
    entry.octreeSlot     = RecipeRegistry::kUnbakedSlot;
    entry.bytecode.assign(view.instructions,
                          view.instructions + view.header.instructionCount);

    if (reg.Register(recipeId, entry) != RecipeRegistry::RegisterResult::Ok)
        return IngestResult::RegistryRejected;

    return IngestResult::Ok;
}

} // namespace Vixen::SVO
