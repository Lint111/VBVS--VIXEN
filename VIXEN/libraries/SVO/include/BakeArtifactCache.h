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

#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace Vixen::SVO {

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

inline void writeBytesVec(std::ofstream& f, const std::vector<uint8_t>& v) {
    const uint64_t size = v.size();
    f.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}

inline bool readBytesVec(std::ifstream& f, std::vector<uint8_t>& v) {
    uint64_t size = 0;
    f.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!f) return false;
    v.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

template <class T>
void writePodVec(std::ofstream& f, const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>, "writePodVec requires a POD element type");
    const uint64_t size = v.size();
    f.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
}

template <class T>
bool readPodVec(std::ifstream& f, std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>, "readPodVec requires a POD element type");
    uint64_t size = 0;
    f.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!f) return false;
    v.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(size * sizeof(T)));
    return static_cast<bool>(f);
}

}  // namespace detail

// Default cache root: cache/global/BakeArtifactCache/, mirroring the existing
// cache/global/ + cache/devices/<id>/*.cache convention already in this repo
// (ShaderCacheManager's own cacheDirectory idiom) -- global, not per-device,
// because a bake artifact has no GPU-specific content.
inline std::filesystem::path DefaultBakeArtifactCacheDir() {
    return std::filesystem::path("cache") / "global" / "BakeArtifactCache";
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
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    if (ec) return false;

    const std::filesystem::path path = BakeArtifactCacheFilePath(hexKey, cacheDir);
    // Write to a temp file then rename -- avoids a reader ever observing a
    // partially-written cache file (e.g. if the process is killed mid-write).
    const std::filesystem::path tmpPath = path.string() + ".tmp";
    std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const ConcatenatedOctrees& cat = bundle.cat;
    detail::writeBytesVec(f, cat.nodes);
    detail::writeBytesVec(f, cat.bricks);
    detail::writeBytesVec(f, cat.materials);
    detail::writeBytesVec(f, cat.channelPool);
    detail::writeBytesVec(f, cat.brickGridLookup);
    detail::writeBytesVec(f, cat.mipPool);
    detail::writePodVec(f, cat.tierRefTable);
    detail::writePodVec(f, cat.configs);
    detail::writePodVec(f, cat.nodeCounts);
    detail::writePodVec(f, cat.brickCounts);
    detail::writePodVec(f, cat.tierRefCounts);
    detail::writePodVec(f, cat.occupiedVoxelCounts);
    f.write(reinterpret_cast<const char*>(&cat.count), sizeof(cat.count));
    detail::writePodVec(f, bundle.lightTreeCut);

    const bool ok = static_cast<bool>(f);
    f.close();
    if (!ok) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return false;
    }
    std::filesystem::rename(tmpPath, path, ec);
    return !ec;
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
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;

    BakeArtifactBundle bundle;
    ConcatenatedOctrees& cat = bundle.cat;
    bool ok = true;
    ok = ok && detail::readBytesVec(f, cat.nodes);
    ok = ok && detail::readBytesVec(f, cat.bricks);
    ok = ok && detail::readBytesVec(f, cat.materials);
    ok = ok && detail::readBytesVec(f, cat.channelPool);
    ok = ok && detail::readBytesVec(f, cat.brickGridLookup);
    ok = ok && detail::readBytesVec(f, cat.mipPool);
    ok = ok && detail::readPodVec(f, cat.tierRefTable);
    ok = ok && detail::readPodVec(f, cat.configs);
    ok = ok && detail::readPodVec(f, cat.nodeCounts);
    ok = ok && detail::readPodVec(f, cat.brickCounts);
    ok = ok && detail::readPodVec(f, cat.tierRefCounts);
    ok = ok && detail::readPodVec(f, cat.occupiedVoxelCounts);
    if (ok) {
        f.read(reinterpret_cast<char*>(&cat.count), sizeof(cat.count));
        ok = static_cast<bool>(f);
    }
    ok = ok && detail::readPodVec(f, bundle.lightTreeCut);

    if (!ok) return std::nullopt;
    return bundle;
}

}  // namespace Vixen::SVO
