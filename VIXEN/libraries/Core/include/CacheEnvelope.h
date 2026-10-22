#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Vixen {

// The one outer file envelope for persistent caches: magic, version, an identity (device or
// generation parameters), a cache type, payload size and checksum. Its metadata is validated before
// any payload parser sees a byte. The payload layout belongs to each cache.
inline constexpr std::array<char, 8> kCacheEnvelopeMagic{'V', 'X', 'C', 'A', 'C', 'H', 'E', '1'};
inline constexpr std::uint32_t kCacheEnvelopeVersion = 1;
inline constexpr std::uint64_t kMaxCachePayloadBytes = 512ULL * 1024ULL * 1024ULL;

struct CacheEnvelopeHeader {
    char magic[8]{};
    std::uint32_t version = kCacheEnvelopeVersion;
    std::uint32_t reserved = 0;
    std::uint64_t deviceIdentity = 0;
    std::uint64_t cacherTypeHash = 0;
    std::uint64_t payloadSize = 0;
    std::uint64_t payloadChecksum = 0;
};
static_assert(sizeof(CacheEnvelopeHeader) == 48);

enum class CacheFileStatus {
    Loaded,
    Missing,
    Rejected,
    IoError,
};

std::uint64_t CacheTypeHash(std::string_view value) noexcept;

/// Wrap `payload` in the envelope and publish it by atomic rename.
bool WriteCacheEnvelopeBytesAtomically(
    const std::filesystem::path& destination,
    std::uint64_t deviceIdentity,
    std::string_view cacherType,
    const std::vector<char>& payload,
    std::string* error = nullptr);

/// Read and validate an envelope file. `payload` holds the verified bytes only for Loaded; a
/// mismatched identity or type, wrong size, or bad checksum is Rejected and never partially returned.
CacheFileStatus ReadCacheEnvelopeBytes(
    const std::filesystem::path& path,
    std::uint64_t expectedDeviceIdentity,
    std::string_view expectedCacherType,
    std::vector<char>& payload,
    std::string* error = nullptr);

} // namespace Vixen
