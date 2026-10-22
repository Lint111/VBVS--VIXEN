// std::getenv (VIXEN_GLYPH_CACHE) is C4996 under MSVC /WX; Core opts out the same way (Headers.h).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "Text/GlyphCellStore.h"

#include "RuntimeCachePaths.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <type_traits>

#ifndef MSDFGEN_VERSION
#define MSDFGEN_VERSION unknown
#endif
#define VIXEN_TEXT_STRINGIFY2(x) #x
#define VIXEN_TEXT_STRINGIFY(x) VIXEN_TEXT_STRINGIFY2(x)

namespace Vixen::Text {

namespace {

constexpr char kMagic[8] = {'V', 'X', 'G', 'L', 'Y', 'P', 'H', '1'};
constexpr std::uint32_t kEnvelopeVersion = 1;
constexpr std::uint64_t kMaxFileBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaxGlyphs = 1U << 20;
constexpr std::uint32_t kMaxIdentityBytes = 1024;

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t reserved;
    std::uint64_t identityHash;
    std::uint64_t payloadSize;
    std::uint64_t payloadChecksum;
};
static_assert(sizeof(Header) == 40);

void SetReason(std::string* reason, const char* text) {
    if (reason)
        *reason = text;
}

// Bounds-checked reader: every length is validated against the bytes left before use.
class Reader {
public:
    Reader(const char* data, std::size_t size) : data_(data), size_(size) {}

    template <typename T>
    bool Read(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (size_ - pos_ < sizeof(T))
            return false;
        std::memcpy(&value, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool ReadBytes(void* out, std::size_t count) {
        if (size_ - pos_ < count)
            return false;
        std::memcpy(out, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    [[nodiscard]] bool AtEnd() const noexcept { return pos_ == size_; }

private:
    const char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

template <typename T>
void Append(std::vector<char>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const char*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

bool ParsePayload(const std::vector<char>& payload, const std::string& identity,
                  std::unordered_map<std::uint32_t, CachedGlyph>& out, std::string* reason) {
    Reader reader(payload.data(), payload.size());
    std::uint32_t identityLength = 0;
    if (!reader.Read(identityLength) || identityLength > kMaxIdentityBytes) {
        SetReason(reason, "identity length out of bounds");
        return false;
    }
    std::string stored(identityLength, '\0');
    if (!reader.ReadBytes(stored.data(), identityLength) || stored != identity) {
        SetReason(reason, "generation parameters or versions differ");
        return false;
    }
    std::uint32_t count = 0;
    if (!reader.Read(count) || count > kMaxGlyphs) {
        SetReason(reason, "glyph count out of bounds");
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        CachedGlyph glyph;
        if (!reader.Read(glyph.glyphIndex) || !reader.Read(glyph.advance) || !reader.Read(glyph.bearingX) ||
            !reader.Read(glyph.bearingY) || !reader.Read(glyph.padding) || !reader.Read(glyph.width) ||
            !reader.Read(glyph.height)) {
            SetReason(reason, "truncated glyph record");
            return false;
        }
        if (glyph.width < 0 || glyph.height < 0 || glyph.width > 16384 || glyph.height > 16384 ||
            (glyph.width == 0) != (glyph.height == 0)) {
            SetReason(reason, "glyph cell dimensions out of bounds");
            return false;
        }
        const std::size_t cellBytes = static_cast<std::size_t>(glyph.width) * glyph.height * 3;
        glyph.rgb.resize(cellBytes);
        if (cellBytes != 0 && !reader.ReadBytes(glyph.rgb.data(), cellBytes)) {
            SetReason(reason, "truncated glyph cell");
            return false;
        }
        const std::uint32_t index = glyph.glyphIndex;
        if (!out.emplace(index, std::move(glyph)).second) {
            SetReason(reason, "duplicate glyph record");
            return false;
        }
    }
    if (!reader.AtEnd()) {
        SetReason(reason, "trailing bytes after glyph records");
        return false;
    }
    return true;
}

// Returns true and fills `glyphs` only for a fully valid file for this key.
bool ReadFile(const std::filesystem::path& path, const GlyphCacheKey& key,
              std::unordered_map<std::uint32_t, CachedGlyph>& glyphs, std::string* reason) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        SetReason(reason, "cannot open store file");
        return false;
    }
    const auto end = in.tellg();
    if (end < static_cast<std::streamoff>(sizeof(Header)) ||
        static_cast<std::uint64_t>(end) > sizeof(Header) + kMaxFileBytes) {
        SetReason(reason, "file size outside bounds");
        return false;
    }
    in.seekg(0, std::ios::beg);
    Header header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    const std::string identity = key.Identity();
    if (!in || std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kEnvelopeVersion) {
        SetReason(reason, "magic or envelope version mismatch");
        return false;
    }
    if (header.identityHash != Vixen::Checksum64(identity.data(), identity.size())) {
        SetReason(reason, "store belongs to different generation parameters");
        return false;
    }
    if (header.payloadSize != static_cast<std::uint64_t>(end) - sizeof(Header)) {
        SetReason(reason, "payload size mismatch");
        return false;
    }
    std::vector<char> payload(static_cast<std::size_t>(header.payloadSize));
    if (!payload.empty()) {
        in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (!in) {
            SetReason(reason, "truncated payload");
            return false;
        }
    }
    if (Vixen::Checksum64(payload.data(), payload.size()) != header.payloadChecksum) {
        SetReason(reason, "payload checksum mismatch");
        return false;
    }
    std::unordered_map<std::uint32_t, CachedGlyph> parsed;
    if (!ParsePayload(payload, identity, parsed, reason))
        return false;
    glyphs = std::move(parsed);
    return true;
}

} // namespace

std::string GlyphCacheKey::Identity() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "font=%016llx;face=%d;size=%d;page=%u;range=%.6f;msdfgen=%s;freetype=%d.%d.%d;gen=%u;edge=simple3.0",
                  static_cast<unsigned long long>(fontContentHash), faceIndex, fontSize, pageDimension,
                  static_cast<double>(distanceRange), VIXEN_TEXT_STRINGIFY(MSDFGEN_VERSION), FREETYPE_MAJOR,
                  FREETYPE_MINOR, FREETYPE_PATCH, kGlyphGeneratorVersion);
    return buffer;
}

const CachedGlyph* GlyphCellSnapshot::Find(std::uint32_t glyphIndex) const {
    const auto it = glyphs_.find(glyphIndex);
    return it == glyphs_.end() ? nullptr : &it->second;
}

std::filesystem::path GlyphCellStore::PathFor(const GlyphCacheKey& key) {
    char name[160];
    std::snprintf(name, sizeof(name), "%016llx_f%d_s%d_p%u_r%.3f.bin",
                  static_cast<unsigned long long>(key.fontContentHash), key.faceIndex, key.fontSize,
                  key.pageDimension, static_cast<double>(key.distanceRange));
    return Vixen::SharedDeviceCacheDirectory() / "glyph-cache" / name;
}

std::shared_ptr<const GlyphCellSnapshot> GlyphCellStore::Load(const std::filesystem::path& path,
                                                              const GlyphCacheKey& key, std::string* reason) {
    auto snapshot = std::make_shared<GlyphCellSnapshot>();
    ReadFile(path, key, snapshot->glyphs_, reason);
    return snapshot;
}

bool GlyphCellStore::Merge(const std::filesystem::path& path, const GlyphCacheKey& key,
                           const std::vector<CachedGlyph>& added, std::string* reason) {
    std::unordered_map<std::uint32_t, CachedGlyph> merged;
    ReadFile(path, key, merged, nullptr); // another process may have appended since our load
    for (const CachedGlyph& glyph : added)
        merged[glyph.glyphIndex] = glyph;

    const std::string identity = key.Identity();
    std::vector<char> payload;
    Append(payload, static_cast<std::uint32_t>(identity.size()));
    payload.insert(payload.end(), identity.begin(), identity.end());
    Append(payload, static_cast<std::uint32_t>(merged.size()));
    for (const auto& [index, glyph] : merged) {
        Append(payload, glyph.glyphIndex);
        Append(payload, glyph.advance);
        Append(payload, glyph.bearingX);
        Append(payload, glyph.bearingY);
        Append(payload, glyph.padding);
        Append(payload, glyph.width);
        Append(payload, glyph.height);
        payload.insert(payload.end(), reinterpret_cast<const char*>(glyph.rgb.data()),
                       reinterpret_cast<const char*>(glyph.rgb.data()) + glyph.rgb.size());
    }
    if (payload.size() > kMaxFileBytes) {
        SetReason(reason, "store would exceed size limit");
        return false;
    }

    Header header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kEnvelopeVersion;
    header.identityHash = Vixen::Checksum64(identity.data(), identity.size());
    header.payloadSize = payload.size();
    header.payloadChecksum = Vixen::Checksum64(payload.data(), payload.size());
    std::vector<char> file(sizeof(header) + payload.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(header), payload.data(), payload.size());
    return Vixen::AtomicWriteBytes(path, file.data(), file.size(), reason);
}

bool GlyphCellStore::Enabled() {
    const char* value = std::getenv("VIXEN_GLYPH_CACHE");
    return !(value && std::string_view(value) == "off");
}

std::uint64_t GlyphCellStore::HashFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = 14695981039346656037ULL;
    char buffer[1 << 16];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= prime;
        }
    }
    return hash == 0 ? 1 : hash;
}

} // namespace Vixen::Text
