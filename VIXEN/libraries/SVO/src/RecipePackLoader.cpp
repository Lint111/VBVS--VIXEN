#include "Recipe/RecipePackLoader.h"
#include "Recipe/RecipeIngest.h"
#include "Recipe/RecipeManifest.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace Vixen::SVO {

// Manifest file format (text, one entry per line, fields tab-separated):
//   namespacedId\trecipeId\tblobPath
// Lines starting with '#' are comments. Empty lines are skipped.
// blobPath is resolved relative to the manifest's parent directory.

static RecipeManifest ParseManifestFile(const std::filesystem::path& p, std::string& err) {
    RecipeManifest manifest;
    std::ifstream f(p);
    if (!f.is_open()) { err = "cannot open manifest: " + p.string(); return {}; }
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        RecipeManifestEntry e;
        std::string idStr;
        if (!(ss >> e.namespacedId >> idStr >> e.blobPath)) {
            err = "manifest line " + std::to_string(lineNo) + ": expected namespacedId recipeId blobPath";
            return {};
        }
        try { e.recipeId = static_cast<uint32_t>(std::stoul(idStr)); }
        catch (...) {
            err = "manifest line " + std::to_string(lineNo) + ": invalid recipeId '" + idStr + "'";
            return {};
        }
        manifest.push_back(std::move(e));
    }
    return manifest;
}

bool LoadRecipePack(const std::filesystem::path& manifestPath,
                    RecipeRegistry& reg,
                    std::string& err)
{
    RecipeManifest manifest = ParseManifestFile(manifestPath, err);
    if (!err.empty()) return false;

    if (!ValidateManifest(manifest, err)) return false;

    const auto base = manifestPath.parent_path();
    for (const auto& e : manifest) {
        const auto blobPath = base / e.blobPath;
        std::ifstream bf(blobPath, std::ios::binary | std::ios::ate);
        if (!bf.is_open()) {
            err = "recipe " + std::to_string(e.recipeId) + ": cannot open blob: " + blobPath.string();
            return false;
        }
        const auto sz = static_cast<size_t>(bf.tellg());
        bf.seekg(0);
        std::vector<uint8_t> blob(sz);
        bf.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(sz));
        if (!bf) {
            err = "recipe " + std::to_string(e.recipeId) + ": read error on blob: " + blobPath.string();
            return false;
        }

        const IngestResult ir = IngestBlob(e.recipeId, blob.data(), blob.size(), reg);
        if (ir == IngestResult::BadContainer) {
            err = "recipe " + std::to_string(e.recipeId) + ": bad container in: " + blobPath.string();
            return false;
        }
        if (ir == IngestResult::RegistryRejected) {
            err = "recipe " + std::to_string(e.recipeId) + ": registry rejected: " + e.namespacedId;
            return false;
        }
    }
    return true;
}

} // namespace Vixen::SVO
