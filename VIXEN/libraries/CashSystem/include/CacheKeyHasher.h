#pragma once

#include "VixenHash.h"  // Vixen::Hash::ComputeHash64

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <type_traits>

namespace CashSystem {

/**
 * @brief Binary hash builder for cache keys
 *
 * Mirrors the BinaryHashBuilder pattern from ShaderDataBundle.cpp.
 * Appends data as binary for deterministic, fast hashing.
 * Uses Vixen::Hash::ComputeHash64 (FNV-1a) for uint64_t keys.
 *
 * Usage:
 * @code
 * CacheKeyHasher hasher;
 * hasher.Add(someUint32)
 *       .Add(someString)
 *       .Add(someFloat);
 * uint64_t key = hasher.Finalize();
 * @endcode
 *
 * @note For float values, consider quantizing to avoid floating-point instability:
 * @code
 * hasher.Add(static_cast<uint32_t>(myFloat * 10000.0f));
 * @endcode
 */
class CacheKeyHasher {
public:
    CacheKeyHasher() {
        buffer_.reserve(256);  // Pre-allocate for typical cache key sizes
    }

    /**
     * @brief Add trivially copyable type (POD, integers, floats, enums)
     */
    template<typename T>
    std::enable_if_t<std::is_trivially_copyable_v<T>, CacheKeyHasher&>
    Add(const T& value) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T));
        return *this;
    }

    /**
     * @brief Add string (length-prefixed for unambiguous parsing)
     */
    CacheKeyHasher& Add(std::string_view str) {
        uint32_t len = static_cast<uint32_t>(str.size());
        Add(len);  // Length prefix
        buffer_.insert(buffer_.end(), str.begin(), str.end());
        return *this;
    }

    /**
     * @brief Add raw byte data
     */
    CacheKeyHasher& AddBytes(const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + len);
        return *this;
    }

    /**
     * @brief Compute final hash using centralized Vixen::Hash
     * @return 64-bit hash suitable for cache key
     */
    [[nodiscard]] uint64_t Finalize() const {
        return ::Vixen::Hash::ComputeHash64(buffer_.data(), buffer_.size());
    }

    /**
     * @brief Get current buffer size (for debugging)
     */
    [[nodiscard]] size_t GetBufferSize() const { return buffer_.size(); }

    /**
     * @brief Reset hasher for reuse
     */
    void Reset() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

} // namespace CashSystem
