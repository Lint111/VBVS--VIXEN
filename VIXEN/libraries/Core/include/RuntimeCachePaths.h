#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>

namespace Vixen {

/**
 * Return the root for persistent runtime caches.
 *
 * VIXEN builds default this to a directory under CMAKE_BINARY_DIR. Hosts can choose another
 * build- or user-cache location with VIXEN_CACHE_DIR. The relative fallback keeps consumers
 * built outside VIXEN's CMake project compatible with the previous behavior.
 *
 * Defined in src/RuntimeCachePaths.cpp, the only translation unit compiled with the absolute
 * VIXEN_RUNTIME_CACHE_DIR definition, so every other TU stays cacheable across build trees.
 */
std::filesystem::path RuntimeCacheDirectory();

/// Return the current process identifier on the host platform.
std::uint64_t CurrentProcessId() noexcept;

/// Return an application-specific temporary directory unique to this process.
std::filesystem::path ProcessTemporaryDirectory(std::string_view applicationName);

/// Write bytes to a unique sibling file and atomically replace the destination.
bool AtomicWriteFile(
    const std::filesystem::path& destination,
    const std::function<bool(std::ostream&)>& writer,
    std::string* error = nullptr
);

/// Byte convenience wrapper over AtomicWriteFile.
bool AtomicWriteBytes(
    const std::filesystem::path& destination,
    const char* bytes,
    std::size_t size,
    std::string* error = nullptr
);

/// Text convenience wrapper over AtomicWriteBytes.
bool AtomicWriteText(
    const std::filesystem::path& destination,
    std::string_view text,
    std::string* error = nullptr
);

/// FNV-1a checksum used to detect accidental cache-file tears and content mismatches.
std::uint64_t Checksum64(const void* data, std::size_t size) noexcept;

} // namespace Vixen
