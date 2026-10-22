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

/**
 * Return the root for driver-compiled state that is valid for a device and driver, not for a build
 * tree (VkPipelineCache blobs). Order: VIXEN_SHARED_CACHE_DIR; <VIXEN_CACHE_DIR>/shared when the
 * runtime cache is overridden (isolated runs stay isolated); the per-user cache directory
 * (%LOCALAPPDATA%\VIXEN\cache, $XDG_CACHE_HOME/vixen or ~/.cache/vixen); <runtime cache>/shared.
 */
std::filesystem::path SharedDeviceCacheDirectory();

/// Return the current process identifier on the host platform.
std::uint64_t CurrentProcessId() noexcept;

/// Return an application-specific temporary directory unique to this process.
///
/// The default /tmp directory is created on first use, removed at normal process exit, and stale
/// directories from dead processes are swept on the next use after a bounded grace period. The main and editor
/// capture callers keep their artifacts by setting VIXEN_HUD_CAPTURE_DIR or
/// VIXEN_EDITOR_CAPTURE_DIR, respectively; those explicit roots are returned unchanged.
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
