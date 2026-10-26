#include "PackRecipeSource.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace Vixen::Editor {
namespace {

constexpr const char* kRecipeKind = "render_recipe";
constexpr const char* kManifestFile = "manifest.utdl";

bool ReadText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool IsIdChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

}  // namespace

bool PackRecipeSource::ParseAssetFacet(const std::string& schemaJson, std::string& directory,
                                       std::string& extension, std::string& error) {
    const auto schema = nlohmann::json::parse(schemaJson, nullptr, /*allow_exceptions=*/false);
    if (schema.is_discarded() || !schema.is_object()) {
        error = "pack schema is not a JSON object";
        return false;
    }
    if (!schema.contains(kRecipeKind) || !schema.at(kRecipeKind).is_object()) {
        error = "pack schema declares no render_recipe kind";
        return false;
    }
    const auto& kind = schema.at(kRecipeKind);
    if (!kind.contains("assetFiles") || !kind.at("assetFiles").is_object()) {
        error = "render_recipe declares no asset files (AssetFilesAt)";
        return false;
    }
    const auto& facet = kind.at("assetFiles");
    if (!facet.contains("directory") || !facet.at("directory").is_string() ||
        !facet.contains("extension") || !facet.at("extension").is_string()) {
        error = "render_recipe asset facet needs string directory and extension";
        return false;
    }
    directory = facet.at("directory").get<std::string>();
    extension = facet.at("extension").get<std::string>();
    return true;
}

bool PackRecipeSource::ParseManifestId(const std::string& manifestText, std::string& packId,
                                       std::string& error) {
    // The manifest row is `{ type: manifest, id: <bare id>, ... }`; the bare id is the only colon-free id
    // in the language. Comment lines never start a key, so scan line starts for `id:`.
    if (manifestText.find("manifest") == std::string::npos) {
        error = "manifest.utdl declares no manifest row";
        return false;
    }
    std::istringstream lines(manifestText);
    std::string line;
    while (std::getline(lines, line)) {
        size_t at = line.find_first_not_of(" \t{");
        if (at == std::string::npos || line.compare(at, 3, "id:") != 0) continue;
        at = line.find_first_not_of(" \t", at + 3);
        if (at == std::string::npos) break;
        if (line[at] == '"') ++at;
        size_t end = at;
        while (end < line.size() && IsIdChar(line[end])) ++end;
        if (end == at) break;
        packId = line.substr(at, end - at);
        return true;
    }
    error = "manifest.utdl has no `id:` field";
    return false;
}

bool PackRecipeSource::Configure(const std::string& packRoot, const std::string& schemaPath,
                                 std::string& error) {
    configured_ = false;
    recipes_.clear();
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(packRoot, ec);
    if (ec || !std::filesystem::is_directory(root)) {
        error = "pack root is not a directory: " + packRoot;
        return false;
    }
    std::string manifest;
    if (!ReadText(root / kManifestFile, manifest)) {
        error = "pack root has no manifest.utdl: " + root.string();
        return false;
    }
    std::string schema;
    if (!ReadText(schemaPath, schema)) {
        error = "cannot read pack schema: " + schemaPath;
        return false;
    }
    if (!ParseManifestId(manifest, packId_, error)) return false;
    if (!ParseAssetFacet(schema, assetDirectory_, assetExtension_, error)) return false;
    packRoot_ = root.string();
    configured_ = true;
    Refresh();
    return true;
}

void PackRecipeSource::Refresh() {
    recipes_.clear();
    if (!configured_) return;
    const std::filesystem::path directory = std::filesystem::path(packRoot_) / assetDirectory_;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension().string() != assetExtension_) continue;
        recipes_.push_back({packId_ + ":" + entry.path().stem().string(), entry.path().string()});
    }
    std::sort(recipes_.begin(), recipes_.end(),
              [](const PackRecipe& a, const PackRecipe& b) { return a.id < b.id; });
}

bool PackRecipeSource::TryResolve(const std::string& id, std::string& outPath) const {
    for (const auto& recipe : recipes_) {
        if (recipe.id != id) continue;
        outPath = recipe.path;
        return true;
    }
    return false;
}

bool PackRecipeSource::WriteAtomically(const std::string& path, const std::vector<uint8_t>& bytes,
                                       std::string& error) {
    const std::filesystem::path target(path);
    const std::filesystem::path temporary = target.string() + ".saving";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot open " + temporary.string();
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "write failed: " + temporary.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = "cannot replace " + target.string();
        return false;
    }
    return true;
}

}  // namespace Vixen::Editor
