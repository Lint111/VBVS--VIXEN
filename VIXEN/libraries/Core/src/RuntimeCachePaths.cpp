#include "RuntimeCachePaths.h"

#include <cstdlib>

namespace Vixen {

std::filesystem::path RuntimeCacheDirectory() {
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
