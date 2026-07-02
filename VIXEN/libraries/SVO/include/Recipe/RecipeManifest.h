#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Vixen::SVO {

struct RecipeManifestEntry {
    std::string  namespacedId; // e.g. "game.ship.hull"
    uint32_t     recipeId;     // stable U32 key in the registry
    std::string  blobPath;     // relative or absolute path to the blob file
};

using RecipeManifest = std::vector<RecipeManifestEntry>;

// ValidateManifest — rejects duplicate namespacedId or duplicate recipeId.
// err is set to a descriptive message on failure; untouched on success.
inline bool ValidateManifest(const RecipeManifest& m, std::string& err) {
    std::set<std::string> names;
    std::set<uint32_t>    ids;
    for (const auto& e : m) {
        if (!names.insert(e.namespacedId).second) {
            err = "duplicate namespacedId: " + e.namespacedId;
            return false;
        }
        if (!ids.insert(e.recipeId).second) {
            err = "duplicate recipeId: " + std::to_string(e.recipeId);
            return false;
        }
    }
    return true;
}

} // namespace Vixen::SVO
