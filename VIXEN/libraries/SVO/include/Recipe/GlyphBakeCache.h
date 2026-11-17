#pragma once
// ============================================================================
// GlyphBakeCache.h - the decompressed glyph images of a baked font, from the shared cache
// (R892.19; controller decision on loading, 2026-11-17).
//
// A baked font exists in two forms. The compressed .glyphbake file ships with a pack. The
// decompressed image is what the walk reads; it is a derived product, so it lives in the shared
// cache under its content key (font bytes + bake parameters):
//     <SharedDeviceCacheDirectory>/glyph-bake/<key>.glyphimage
// VIXEN has no Brotli decoder. The font import step (build target vixen_glyph_bake, kernel tool
// glyph-bake) writes the image and a manifest that names, per face and band, the key, the image
// it wrote and the pack file. Loading takes the image from the cache by key; on a miss it imports
// the import step's image into the cache (a file copy) and loads that. A cached file whose key is
// not the manifest's is replaced; an import image whose key is not the manifest's is an error.
//
// Manifest: one line per bake, tab separated:
//     face <TAB> band (em) <TAB> key (64 hex) <TAB> image file <TAB> pack file
// File names are relative to the manifest's directory.
// ============================================================================
#include "Recipe/GlyphBakeStream.h"
#include "RuntimeCachePaths.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace Vixen::SVO::Recipe {

struct GlyphBakeManifestEntry {
    std::string face;
    std::string band;
    std::string key;
    std::filesystem::path image; // the import step's decompressed image
    std::filesystem::path pack;  // the compressed file that ships with a pack
};

// The content key of an image as the manifest and the kernel tool write it: 64 lowercase hex.
[[nodiscard]] inline std::string GlyphBakeKeyText(const GlyphBakeImage& image) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    for (const std::uint32_t word : image.Key())
        for (int byte = 0; byte < 4; ++byte) {
            const std::uint32_t value = (word >> (byte * 8)) & 0xFFu;
            text.push_back(kHex[value >> 4]);
            text.push_back(kHex[value & 0xFu]);
        }
    return text;
}

[[nodiscard]] inline bool ReadGlyphBakeManifest(const std::filesystem::path& manifest,
                                                std::vector<GlyphBakeManifestEntry>& entries,
                                                std::string* error = nullptr) {
    entries.clear();
    std::ifstream file(manifest);
    if (!file) {
        if (error) *error = "cannot open glyph bake manifest " + manifest.string() + "; fix: build target vixen_glyph_bake";
        return false;
    }
    const std::filesystem::path directory = manifest.parent_path();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1)
            fields.push_back(line.substr(start, tab - start));
        fields.push_back(line.substr(start));
        if (fields.size() != 5 || fields[0].empty() || fields[2].size() != 64 || fields[3].empty()) {
            if (error) *error = "glyph bake manifest " + manifest.string() + " has a malformed line: " + line;
            return false;
        }
        entries.push_back({fields[0], fields[1], fields[2], directory / fields[3], directory / fields[4]});
    }
    return true;
}

[[nodiscard]] inline std::filesystem::path GlyphBakeCacheDirectory() {
    return Vixen::SharedDeviceCacheDirectory() / "glyph-bake";
}

enum class GlyphBakeCacheSource : std::uint8_t { Cache, Imported };

// The image of one manifest entry: from the cache by key, else imported into it.
[[nodiscard]] inline std::shared_ptr<const GlyphBakeImage> LoadCachedGlyphBakeImage(
    const GlyphBakeManifestEntry& entry, const std::filesystem::path& cacheDirectory,
    std::string* error = nullptr, GlyphBakeCacheSource* source = nullptr) {
    const std::filesystem::path cached = cacheDirectory / (entry.key + ".glyphimage");
    std::error_code ignored;
    if (std::filesystem::exists(cached, ignored)) {
        std::string reason;
        auto image = GlyphBakeImage::Load(cached, &reason);
        if (image && GlyphBakeKeyText(*image) == entry.key) {
            if (source) *source = GlyphBakeCacheSource::Cache;
            return image;
        }
        // Not this key's image (truncated write of a killed process, another layout): import again.
    }
    std::string reason;
    auto image = GlyphBakeImage::Load(entry.image, &reason);
    if (!image) {
        if (error) *error = "glyph image " + entry.key.substr(0, 16) + " of face " + entry.face +
            " is not in the cache (" + cached.string() + ") and cannot be imported: " + reason +
            "; fix: build target vixen_glyph_bake";
        return nullptr;
    }
    if (GlyphBakeKeyText(*image) != entry.key) {
        if (error) *error = "glyph bake manifest names key " + entry.key.substr(0, 16) + " for face " + entry.face +
            " but " + entry.image.string() + " holds " + GlyphBakeKeyText(*image).substr(0, 16) +
            "; fix: build target vixen_glyph_bake";
        return nullptr;
    }
    // Publish by rename, so a reader never sees a partial image.
    static std::atomic<std::uint64_t> serial{0};
    const std::filesystem::path temporary = cacheDirectory / (entry.key + ".tmp-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(serial.fetch_add(1)));
    std::error_code failure;
    std::filesystem::create_directories(cacheDirectory, failure);
    if (!failure) std::filesystem::copy_file(entry.image, temporary, std::filesystem::copy_options::overwrite_existing, failure);
    if (!failure) std::filesystem::rename(temporary, cached, failure);
    if (failure) {
        std::filesystem::remove(temporary, ignored);
        if (error) *error = "cannot write glyph image cache " + cached.string() + ": " + failure.message();
        return nullptr;
    }
    if (source) *source = GlyphBakeCacheSource::Imported;
    return image;
}

// The bakes of one face, every band the manifest lists for it. Null, with the reason, when the
// manifest has no bake of the face or an image is neither cached nor importable.
[[nodiscard]] inline std::shared_ptr<const GlyphBakeFace> LoadGlyphBakeFace(
    std::string name, const std::filesystem::path& manifest, const std::filesystem::path& cacheDirectory,
    std::string* error = nullptr) {
    std::vector<GlyphBakeManifestEntry> entries;
    if (!ReadGlyphBakeManifest(manifest, entries, error)) return nullptr;
    auto face = std::make_shared<GlyphBakeFace>();
    face->name = std::move(name);
    for (const GlyphBakeManifestEntry& entry : entries) {
        if (entry.face != face->name) continue;
        auto image = LoadCachedGlyphBakeImage(entry, cacheDirectory, error);
        if (!image) return nullptr;
        face->images.push_back(std::move(image));
    }
    if (face->images.empty()) {
        if (error) *error = "glyph bake manifest " + manifest.string() + " lists no bake of face " + face->name;
        return nullptr;
    }
    return face;
}

} // namespace Vixen::SVO::Recipe
