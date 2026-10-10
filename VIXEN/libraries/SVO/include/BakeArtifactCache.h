#pragma once
// BakeArtifactCache.h — Baked-Perf M7 Task 7.4: bake-artifact disk cache.
//
// Design note: Vixen-Docs/01-Architecture/Baked-Perf-Fix-Pipeline-Plan-2026-07.md,
// "Task 7.4 design note" (written before this file, per this milestone's
// design-first requirement). Summary: there is no bake-skip cache anywhere in the
// engine (the 87-190s Cornell bake re-runs every boot); this header lets a caller
// hash its bake inputs into a content-addressed key, then load/store the resulting
// ConcatenatedOctrees + light-tree-cut bundle as a single file under
// cache/global/BakeArtifactCache/<hex-key>.bake — mirroring the existing
// cache/global/ + cache/devices/<id>/*.cache convention (ShaderCacheManager's own
// cacheDirectory idiom). A cache HIT must reproduce byte-identical
// ConcatenatedOctrees to a cold bake; that guard is the caller's responsibility
// (verify via a hash compare + the same_path parity gate on a warm boot), this
// header only handles the mechanical save/load.
//
// Header-only, same idiom as SdfBake.h/ShellOctreeGpu.h/MipBake.h.

#include "ShellOctreeGpu.h"   // ConcatenatedOctrees, SerializedOctree
#include "LightTree.h"        // LightTreeNode
#include "Recipe/SdfInstruction.h"  // SdfInstruction (132B POD, hashed verbatim)
#include "RuntimeCachePaths.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace Vixen::SVO {

inline constexpr std::array<char, 8> kBakeArtifactMagic{'V', 'X', 'B', 'A', 'K', 'E', '0', '1'};
inline constexpr std::uint32_t kBakeArtifactEnvelopeVersion = 1;
inline constexpr std::uint64_t kMaxBakeArtifactBytes = 1024ULL * 1024ULL * 1024ULL;

struct BakeArtifactFileHeader {
    char magic[8]{};
    std::uint32_t version = kBakeArtifactEnvelopeVersion;
    std::uint32_t reserved = 0;
    std::uint64_t keyHash = 0;
    std::uint64_t payloadSize = 0;
    std::uint64_t payloadChecksum = 0;
};
static_assert(sizeof(BakeArtifactFileHeader) == 40);

inline std::uint64_t BakeArtifactKeyHash(const std::string& key) noexcept {
    return Vixen::Checksum64(key.data(), key.size());
}

// ===========================================================================
// Key derivation — FNV-1a 64-bit over a caller-assembled byte stream.
// ===========================================================================
// Matches the existing GaiaVoxelWorld::BlockQueryKeyHash FNV-1a idiom already in
// this codebase. Non-cryptographic; fine for a single-machine dev cache (see the
// design note's own collision-risk discussion). A caller builds the key stream
// itself (BakeArtifactCache::KeyBuilder below) so the exact set of hashed bytes
// stays visible/auditable at the call site rather than hidden in this header.
class BakeArtifactKeyBuilder {
public:
    // Bump this if SerializedOctree/ConcatenatedOctrees's layout changes in a way
    // that would make an old cached file misread as a new one -- included as the
    // FIRST bytes hashed so any format change invalidates every existing cache
    // entry (a version bump alone is enough; no explicit clear step needed since
    // the key changes and old files simply become permanently-orphaned misses).
    // v2 (Task 7.6): the CONTENT stored under a given key changed -- callers now
    // run DedupBricks on each body before handing it to the cache, so what a HIT
    // loads is a deduplicated ConcatenatedOctrees, not the raw one v1 cached. Same
    // recipe/params could otherwise hash to a v1 file that predates dedup and is
    // silently stale (correct pixels, but NOT deduplicated, and NOT what a fresh
    // bake under the current code would produce) -- bump forces a clean re-bake.
    static constexpr uint32_t kFormatVersion = 2u;

    BakeArtifactKeyBuilder() { addU32(kFormatVersion); }

    void addBytes(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_bytes.insert(m_bytes.end(), bytes, bytes + size);
    }
    void addU32(uint32_t v)  { addBytes(&v, sizeof(v)); }
    void addI32(int32_t v)   { addBytes(&v, sizeof(v)); }
    void addFloat(float v)   { addBytes(&v, sizeof(v)); }
    void addVec3(const glm::vec3& v) { addBytes(&v, sizeof(v)); }

    // Program bytes: SdfInstruction is a 132B POD (static_assert'd in
    // Recipe/SdfInstruction.h) -- hashing it verbatim covers the authored
    // geometry/primitive-params completely with no per-field enumeration and no
    // drift risk if the recipe's own opcode/data layout changes.
    void addProgram(std::span<const Recipe::SdfInstruction> prog) {
        addU32(static_cast<uint32_t>(prog.size()));
        if (!prog.empty()) {
            addBytes(prog.data(), prog.size() * sizeof(Recipe::SdfInstruction));
        }
    }

    // FNV-1a 64-bit over the accumulated byte stream, rendered as 16 lowercase
    // hex digits (stable, filesystem-safe filename).
    [[nodiscard]] std::string hexKey() const {
        uint64_t h = 0xcbf29ce484222325ULL;  // FNV offset basis
        for (uint8_t b : m_bytes) {
            h ^= static_cast<uint64_t>(b);
            h *= 0x100000001b3ULL;  // FNV prime
        }
        char buf[17];
        for (int i = 15; i >= 0; --i) {
            const uint32_t nibble = static_cast<uint32_t>(h & 0xFu);
            buf[i] = static_cast<char>(nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10));
            h >>= 4;
        }
        buf[16] = '\0';
        return std::string(buf, 16);
    }

private:
    std::vector<uint8_t> m_bytes;
};

// ===========================================================================
// On-disk bundle: ConcatenatedOctrees + the light-tree cut.
// ===========================================================================
struct BakeArtifactBundle {
    ConcatenatedOctrees cat;
    std::vector<LightTreeNode> lightTreeCut;
};

namespace detail {

inline std::uint64_t remainingBytes(std::istream& f) {
    const auto current = f.tellg();
    if (current < 0) return 0;
    f.seekg(0, std::ios::end);
    const auto end = f.tellg();
    f.seekg(current, std::ios::beg);
    if (!f || end < current) return 0;
    return static_cast<std::uint64_t>(end - current);
}

inline void writeBytesVec(std::ostream& f, const std::vector<uint8_t>& v) {
    const uint64_t size = v.size();
    f.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}

inline bool readBytesVec(std::istream& f, std::vector<uint8_t>& v) {
    uint64_t size = 0;
    f.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!f) return false;
    if (size > kMaxBakeArtifactBytes || size > std::numeric_limits<std::size_t>::max() ||
        size > remainingBytes(f)) return false;
    v.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

template <class T>
void writePodVec(std::ostream& f, const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>, "writePodVec requires a POD element type");
    const uint64_t size = v.size();
    f.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
}

template <class T>
bool readPodVec(std::istream& f, std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>, "readPodVec requires a POD element type");
    uint64_t size = 0;
    f.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!f) return false;
    if (size > std::numeric_limits<std::size_t>::max() / sizeof(T)) return false;
    const auto byteSize = size * sizeof(T);
    if (byteSize > remainingBytes(f) || byteSize > kMaxBakeArtifactBytes) return false;
    v.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(byteSize));
    return static_cast<bool>(f);
}

}  // namespace detail

// Default cache root: <runtime-cache>/global/BakeArtifactCache/, mirroring the existing
// global + device cache split without writing runtime data into the source tree.
// (ShaderCacheManager's own cacheDirectory idiom) -- global, not per-device,
// because a bake artifact has no GPU-specific content.
inline std::filesystem::path DefaultBakeArtifactCacheDir() {
    return Vixen::RuntimeCacheDirectory() / "global" / "BakeArtifactCache";
}

inline std::filesystem::path BakeArtifactCacheFilePath(
        const std::string& hexKey,
        const std::filesystem::path& cacheDir = DefaultBakeArtifactCacheDir()) {
    return cacheDir / (hexKey + ".bake");
}

// Store `bundle` under `hexKey`. Returns false (and does not throw) on any I/O
// failure -- a cache STORE failure should never be fatal to a bake that already
// succeeded; the caller simply doesn't get a warm boot next time.
inline bool StoreBakeArtifact(
        const std::string& hexKey,
        const BakeArtifactBundle& bundle,
        const std::filesystem::path& cacheDir = DefaultBakeArtifactCacheDir()) {
    const std::filesystem::path path = BakeArtifactCacheFilePath(hexKey, cacheDir);
    std::ostringstream payloadStream(std::ios::binary | std::ios::out);
    const ConcatenatedOctrees& cat = bundle.cat;
    detail::writeBytesVec(payloadStream, cat.nodes);
    detail::writeBytesVec(payloadStream, cat.bricks);
    detail::writeBytesVec(payloadStream, cat.materials);
    detail::writeBytesVec(payloadStream, cat.channelPool);
    detail::writeBytesVec(payloadStream, cat.brickGridLookup);
    detail::writeBytesVec(payloadStream, cat.mipPool);
    detail::writePodVec(payloadStream, cat.tierRefTable);
    detail::writePodVec(payloadStream, cat.configs);
    detail::writePodVec(payloadStream, cat.nodeCounts);
    detail::writePodVec(payloadStream, cat.brickCounts);
    detail::writePodVec(payloadStream, cat.tierRefCounts);
    detail::writePodVec(payloadStream, cat.occupiedVoxelCounts);
    payloadStream.write(reinterpret_cast<const char*>(&cat.count), sizeof(cat.count));
    detail::writePodVec(payloadStream, bundle.lightTreeCut);
    if (!payloadStream) return false;

    const auto payload = payloadStream.str();
    if (payload.size() > kMaxBakeArtifactBytes) return false;
    BakeArtifactFileHeader header{};
    std::memcpy(header.magic, kBakeArtifactMagic.data(), kBakeArtifactMagic.size());
    header.keyHash = BakeArtifactKeyHash(hexKey);
    header.payloadSize = payload.size();
    header.payloadChecksum = Vixen::Checksum64(payload.data(), payload.size());
    return Vixen::AtomicWriteFile(path, [&](std::ostream& out) {
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        return static_cast<bool>(out);
    });
}

// Load a previously-stored bundle for `hexKey`. Returns std::nullopt on any
// miss (file absent) OR read failure (truncated/corrupt file) -- the caller's
// response to nullopt is always "bake fresh", so a corrupt cache file is a
// silent miss, not a hard error, exactly like every other cache in this repo
// (ShaderCacheManager's own ValidateCacheEntry philosophy).
inline std::optional<BakeArtifactBundle> LoadBakeArtifact(
        const std::string& hexKey,
        const std::filesystem::path& cacheDir = DefaultBakeArtifactCacheDir()) {
    const std::filesystem::path path = BakeArtifactCacheFilePath(hexKey, cacheDir);
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError || fileSize < sizeof(BakeArtifactFileHeader) ||
        fileSize > sizeof(BakeArtifactFileHeader) + kMaxBakeArtifactBytes) return std::nullopt;

    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    BakeArtifactFileHeader header{};
    f.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!f || std::memcmp(header.magic, kBakeArtifactMagic.data(), kBakeArtifactMagic.size()) != 0 ||
        header.version != kBakeArtifactEnvelopeVersion || header.keyHash != BakeArtifactKeyHash(hexKey) ||
        header.payloadSize != fileSize - sizeof(header) || header.payloadSize > kMaxBakeArtifactBytes) {
        return std::nullopt;
    }

    std::string payload(static_cast<std::size_t>(header.payloadSize), '\0');
    if (!payload.empty()) f.read(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!f || Vixen::Checksum64(payload.data(), payload.size()) != header.payloadChecksum) return std::nullopt;
    std::istringstream payloadStream(std::move(payload), std::ios::binary | std::ios::in);

    BakeArtifactBundle bundle;
    ConcatenatedOctrees& cat = bundle.cat;
    bool ok = true;
    ok = ok && detail::readBytesVec(payloadStream, cat.nodes);
    ok = ok && detail::readBytesVec(payloadStream, cat.bricks);
    ok = ok && detail::readBytesVec(payloadStream, cat.materials);
    ok = ok && detail::readBytesVec(payloadStream, cat.channelPool);
    ok = ok && detail::readBytesVec(payloadStream, cat.brickGridLookup);
    ok = ok && detail::readBytesVec(payloadStream, cat.mipPool);
    ok = ok && detail::readPodVec(payloadStream, cat.tierRefTable);
    ok = ok && detail::readPodVec(payloadStream, cat.configs);
    ok = ok && detail::readPodVec(payloadStream, cat.nodeCounts);
    ok = ok && detail::readPodVec(payloadStream, cat.brickCounts);
    ok = ok && detail::readPodVec(payloadStream, cat.tierRefCounts);
    ok = ok && detail::readPodVec(payloadStream, cat.occupiedVoxelCounts);
    if (ok) {
        payloadStream.read(reinterpret_cast<char*>(&cat.count), sizeof(cat.count));
        ok = static_cast<bool>(payloadStream);
    }
    ok = ok && detail::readPodVec(payloadStream, bundle.lightTreeCut);

    if (!ok || payloadStream.peek() != std::char_traits<char>::eof()) return std::nullopt;
    return bundle;
}

}  // namespace Vixen::SVO
