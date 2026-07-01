#pragma once
#include <filesystem>
#include <string>
#include "Recipe/RecipeManifest.h"
#include "Recipe/RecipeRegistry.h"

namespace Vixen::SVO {

// LoadRecipePack — parse a manifest file, load each blob, and ingest into reg.
// Fails loud: returns false + sets err to a descriptive message naming the
// offending recipeId/blob on any bad blob or registry rejection.
// Filesystem I/O is isolated to the .cpp; this header is the interface only.
bool LoadRecipePack(const std::filesystem::path& manifestPath,
                    RecipeRegistry& reg,
                    std::string& err);

} // namespace Vixen::SVO
