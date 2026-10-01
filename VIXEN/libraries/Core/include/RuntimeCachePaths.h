#pragma once

#include <cstdlib>
#include <filesystem>

namespace Vixen {

/**
 * Return the root for persistent runtime caches.
 *
 * VIXEN builds default this to a directory under CMAKE_BINARY_DIR. Hosts can choose another
 * build- or user-cache location with VIXEN_CACHE_DIR. The relative fallback keeps consumers
 * built outside VIXEN's CMake project compatible with the previous behavior.
 */
inline std::filesystem::path RuntimeCacheDirectory() {
    if (const char* overridePath = std::getenv("VIXEN_CACHE_DIR");
        overridePath && *overridePath) {
        return std::filesystem::path(overridePath);
    }
#ifdef VIXEN_RUNTIME_CACHE_DIR
    return std::filesystem::path(VIXEN_RUNTIME_CACHE_DIR);
#else
    return std::filesystem::path("cache");
#endif
}

} // namespace Vixen
