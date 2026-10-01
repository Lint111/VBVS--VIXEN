#pragma once

#include <filesystem>

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

} // namespace Vixen
