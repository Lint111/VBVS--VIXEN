#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>

// Project-wide hash abstraction. Use OpenSSL when available, otherwise try a
// header-only hash library fetched by CMake, else fall back to a deterministic
// non-cryptographic hash. This header is intentionally small and header-only.

namespace Vixen {
namespace Hash {

// FNV-1a hash constants (standard 64-bit)
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

/**
 * @brief Compute 64-bit FNV-1a hash
 *
 * Fast, deterministic, non-cryptographic hash suitable for cache keys.
 * Use this for uint64_t keys (e.g., CashSystem cachers).
 *
 * @param data Pointer to data
 * @param len Length in bytes
 * @return 64-bit hash
 */
inline uint64_t ComputeHash64(const void* data, size_t len) {
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

inline uint64_t ComputeHash64(const std::vector<uint8_t>& data) {
    return ComputeHash64(data.data(), data.size());
}

inline std::string ToHex(const unsigned char* bytes, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

#if defined(HAS_OPENSSL)
#include <openssl/sha.h>

inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data), len, hash);
    return ToHex(hash, SHA256_DIGEST_LENGTH);
}

#elif defined(HAS_STBRUMME_HASH)
#if defined(__has_include)
#  if __has_include("sha256.h")
#    include "sha256.h"
#    define SHADER_HAVE_STBRUMME_SHA256 1
#  elif __has_include(<sha256.h>)
#    include <sha256.h>
#    define SHADER_HAVE_STBRUMME_SHA256 1
#  elif __has_include("hash/sha256.h")
#    include "hash/sha256.h"
#    define SHADER_HAVE_STBRUMME_SHA256 1
#  endif
#endif

#if defined(SHADER_HAVE_STBRUMME_SHA256)
inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    unsigned char hash[32];
    // Best-effort call to stbrumme header's expected API; if unavailable,
    // fall back to deterministic FNV expansion below.
#if defined(sha256)
    sha256(reinterpret_cast<const unsigned char*>(data), static_cast<size_t>(len), hash);
#else
    uint64_t h = 1469598103934665603ull;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) { h ^= static_cast<uint64_t>(p[i]); h *= 1099511628211ull; }
    for (int i = 0; i < 32; ++i) hash[i] = static_cast<unsigned char>((h >> ((i % 8) * 8)) & 0xFF);
#endif
    return ToHex(hash, 32);
}
#else
inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    uint64_t h = 1469598103934665603ull;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) { h ^= static_cast<uint64_t>(p[i]); h *= 1099511628211ull; }
    unsigned char out[32];
    for (int i = 0; i < 32; ++i) out[i] = static_cast<unsigned char>((h >> ((i % 8) * 8)) & 0xFF);
    return ToHex(out, 32);
}
#endif

#else
inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    uint64_t h = 1469598103934665603ull;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) { h ^= static_cast<uint64_t>(p[i]); h *= 1099511628211ull; }
    unsigned char out[32];
    for (int i = 0; i < 32; ++i) out[i] = static_cast<unsigned char>((h >> ((i % 8) * 8)) & 0xFF);
    return ToHex(out, 32);
}
#endif

inline std::string ComputeSHA256Hex(const std::vector<uint8_t>& data) {
    return ComputeSHA256Hex(data.data(), data.size());
}

inline std::string ComputeSHA256HexFromUint32Vec(const std::vector<uint32_t>& data) {
    if (data.empty()) return ComputeSHA256Hex(nullptr, 0);
    return ComputeSHA256Hex(reinterpret_cast<const void*>(data.data()), data.size() * sizeof(uint32_t));
}

} // namespace Hash
} // namespace Vixen
