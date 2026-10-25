#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vixen::Text {

// Bump when the generation code in GlyphAtlas (outline decomposition, edge colouring, MSDF
// encoding, cell sizing) changes in a way that alters cell bytes or metrics.
inline constexpr std::uint32_t kGlyphGeneratorVersion = 1;

// Everything a generated cell depends on. The font identity is the file's content hash, so a
// renamed or moved file keeps its cells and an edited file loses them.
struct GlyphCacheKey {
    std::uint64_t fontContentHash = 0;
    int faceIndex = 0;
    int fontSize = 0;
    std::uint32_t pageDimension = 0;
    float distanceRange = 0.0f;

    // The identity string stored inside the file and compared on load, including the msdfgen,
    // FreeType and generator-code versions.
    [[nodiscard]] std::string Identity() const;
};

// One generated glyph: metrics in atlas pixels plus the RGB cell. A zero-sized cell is a glyph
// that has an advance but nothing to draw.
struct CachedGlyph {
    std::uint32_t glyphIndex = 0;
    float advance = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float padding = 0.0f;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::vector<std::uint8_t> rgb; // width * height * 3, top row first as stored in the atlas
};

// An immutable snapshot of one store file. Readers never lock: the atlas loads one snapshot and
// only reads it afterwards; new cells are collected separately and written once at shutdown.
class GlyphCellSnapshot {
public:
    [[nodiscard]] const CachedGlyph* Find(std::uint32_t glyphIndex) const;
    [[nodiscard]] std::size_t Size() const noexcept { return glyphs_.size(); }

private:
    friend class GlyphCellStore;
    std::unordered_map<std::uint32_t, CachedGlyph> glyphs_;
};

// The packed atlas page as the saving run left it: every glyph already placed, the pixels, and the
// shelf packer's cursor. Restoring it in one step gives RmlUi a single atlas texture instead of one
// upload per glyph added during first layout. Valid only for the key it was saved under.
struct CachedPage {
    struct Entry {
        std::uint32_t glyphIndex = 0;
        float advance = 0.0f;
        float bearingX = 0.0f;
        float bearingY = 0.0f;
        float padding = 0.0f;
        std::int32_t width = 0;
        std::int32_t height = 0;
        std::int32_t atlasX = 0;
        std::int32_t atlasY = 0;
    };
    std::uint32_t placementX = 1;
    std::uint32_t placementY = 1;
    std::uint32_t shelfHeight = 0;
    std::uint32_t usedRows = 0;          // rows of pageDimension RGBA texels stored in `pixels`
    std::vector<Entry> glyphs;
    std::vector<std::uint8_t> pixels;    // usedRows * pageDimension * 4
};

class GlyphCellStore {
public:
    // The user cache directory shared by every build tree (same root as the pipeline cache).
    [[nodiscard]] static std::filesystem::path PathFor(const GlyphCacheKey& key);

    // A rejected, missing or mismatched file yields an empty snapshot; never partial content.
    // `reason` receives why a present file was rejected.
    [[nodiscard]] static std::shared_ptr<const GlyphCellSnapshot> Load(const std::filesystem::path& path,
                                                                       const GlyphCacheKey& key,
                                                                       std::string* reason = nullptr);

    // Re-reads the file, merges `added` over it and publishes the result by atomic rename.
    static bool Merge(const std::filesystem::path& path, const GlyphCacheKey& key,
                      const std::vector<CachedGlyph>& added, std::string* reason = nullptr);

    [[nodiscard]] static std::filesystem::path PageFor(const GlyphCacheKey& key);
    // Null for a missing, rejected or inconsistent file; never partial content.
    [[nodiscard]] static std::shared_ptr<const CachedPage> LoadPage(const std::filesystem::path& path,
                                                                    const GlyphCacheKey& key,
                                                                    std::string* reason = nullptr);
    static bool SavePage(const std::filesystem::path& path, const GlyphCacheKey& key, const CachedPage& page,
                         std::string* reason = nullptr);

    // Whole-page persistence; also off when VIXEN_UI_CACHE=off (the UI startup caches, for measurement).
    [[nodiscard]] static bool PageEnabled();

    // False when VIXEN_GLYPH_CACHE=off (used to measure uncached generation).
    [[nodiscard]] static bool Enabled();

    // Hash of a font file's bytes; zero when the file cannot be read.
    [[nodiscard]] static std::uint64_t HashFile(const std::filesystem::path& path);
};

} // namespace Vixen::Text
