#pragma once
// R765: the editor opens and saves render-recipe assets through the pack. A pack stores each arbitrary
// render recipe as its layered editor document (VDC1 .vxd) in the directory and extension the pack schema
// declares for the render_recipe kind (`AssetFilesAt`, projected into the generated schemas.json as
// "assetFiles"); the pack build flattens it to the runtime VRC1 blob. This source lists those documents by
// content id (`<pack id>:<file stem>`, the id a render_recipe row's `blob:` names) and resolves an id to
// the authored file the editor loads and saves. It never writes a flattened .vrc.

#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::Editor {

struct PackRecipe {
    std::string id;     // "<pack id>:<stem>"
    std::string path;   // absolute path of the layered document inside the pack
};

class PackRecipeSource {
public:
    // Configure from a pack root (the directory holding manifest.utdl) and the generated pack schema
    // (schemas.json). Reads the pack id from the manifest and the render_recipe asset directory and
    // extension from the schema, then lists the documents. Returns false with a reason on failure.
    bool Configure(const std::string& packRoot, const std::string& schemaPath, std::string& error);

    // Re-list the documents (e.g. after a save created a new one). Ordinal by id.
    void Refresh();

    bool Configured() const { return configured_; }
    const std::string& PackRoot() const { return packRoot_; }
    const std::string& PackId() const { return packId_; }
    const std::string& AssetDirectory() const { return assetDirectory_; }
    const std::string& AssetExtension() const { return assetExtension_; }
    const std::vector<PackRecipe>& Recipes() const { return recipes_; }

    // Resolve a listed recipe id to its document path.
    bool TryResolve(const std::string& id, std::string& outPath) const;

    // Replace a document atomically: write the bytes to a sibling temporary file, then rename it over
    // the target, so a failed write never leaves a half-written document in the pack.
    static bool WriteAtomically(const std::string& path, const std::vector<uint8_t>& bytes,
                                std::string& error);

    // The schema's render_recipe asset facet, parsed from schemas.json text.
    static bool ParseAssetFacet(const std::string& schemaJson, std::string& directory,
                                std::string& extension, std::string& error);
    // The pack id a manifest.utdl declares (`id: <bare id>` in its `type: manifest` row).
    static bool ParseManifestId(const std::string& manifestText, std::string& packId, std::string& error);

private:
    bool configured_ = false;
    std::string packRoot_;
    std::string packId_;
    std::string assetDirectory_;
    std::string assetExtension_;
    std::vector<PackRecipe> recipes_;
};

}  // namespace Vixen::Editor
