#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

// ShaderManagement standalone hash implementation (avoids global Hash.h namespace conflicts)
// Uses simple FNV-1a hash for cache keys - adequate for shader cache invalidation

namespace ShaderManagement {

// FNV-1a hash constants
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

inline uint64_t ComputeFNV1a(const void* data, size_t len) {
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    // Use FNV-1a hash (non-cryptographic but sufficient for cache keys)
    uint64_t hash = ComputeFNV1a(data, len);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

inline std::string ComputeSHA256Hex(const std::vector<uint8_t>& data) {
    return ComputeSHA256Hex(data.data(), data.size());
}

inline std::string ComputeSHA256HexFromUint32Vec(const std::vector<uint32_t>& data) {
    return ComputeSHA256Hex(data.data(), data.size() * sizeof(uint32_t));
}

} // namespace ShaderManagement
