#pragma once

#include <cstdint>

namespace ResourceManagement {

/**
 * @brief Resource state flags for fine-grained lifecycle tracking
 * 
 * Bitfield design allows combining multiple states.
 * 
 * State combinations:
 * - Ready: Resource initialized and usable
 * - Outdated: Resource needs update/reload
 * - Locked: In use by GPU, cannot modify
 * - Stale: Cached data invalid, needs refresh
 * - Pending: Async operation in progress
 */
enum class ResourceState : uint32_t {
    Uninitialized = 0,        // Not yet created
    Ready         = 1 << 0,   // Valid and usable
    Outdated      = 1 << 1,   // Needs reload/recompilation
    Locked        = 1 << 2,   // In use, cannot modify
    Stale         = 1 << 3,   // Cache invalid, needs refresh
    Pending       = 1 << 4,   // Async operation in progress
    Failed        = 1 << 5,   // Creation/loading failed
    Transient     = 1 << 6,   // Temporary, delete after use
};

// Bitwise operators for state combinations
inline ResourceState operator|(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ResourceState operator&(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ResourceState operator~(ResourceState a) {
    return static_cast<ResourceState>(~static_cast<uint32_t>(a));
}

inline bool HasState(ResourceState flags, ResourceState check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}

/**
 * @brief Resource lifecycle stage
 * 
 * Simplified state machine for common resource patterns.
 */
enum class LifecycleStage {
    Creating,    // Resource being initialized
    Active,      // Resource ready for use
    Updating,    // Resource being modified
    Destroying,  // Resource being cleaned up
};

} // namespace ResourceManagement
