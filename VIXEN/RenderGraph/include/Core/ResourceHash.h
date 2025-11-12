#pragma once

#include <cstdint>
#include <cstring>

namespace Vixen::RenderGraph {

/**
 * @brief Persistent hash computation for resource identification
 *
 * Phase H: Hash-based resource identification eliminates string overhead
 * and provides compile-time persistent identifiers for URM resource lookup.
 *
 * Design:
 * - FNV-1a hash algorithm (fast, good distribution)
 * - Two-part hash structure: (nodeInstance+bundle) + memberName
 * - 64-bit hash space (virtually collision-free)
 * - Compile-time string hashing where possible
 * - Supports automatic cleanup of temporary resources by scope
 *
 * Hash Structure:
 * - Scope Hash (nodeInstanceId + bundleIndex): Identifies allocation scope
 * - Member Hash (variableName): Identifies specific resource
 * - Full Hash (combination): Unique resource identifier
 *
 * This two-part structure enables:
 * - Querying all resources from a specific node+bundle
 * - Automatic cleanup at phase boundaries (end of Compile/Execute)
 * - No manual release calls needed for temporary resources
 *
 * Usage Pattern:
 * @code
 * // In node's ExecuteImpl:
 * uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, "tempCmdBuffer");
 * auto handle = RequestStackResource<VkCommandBuffer, 1>(hash);
 * // ... use resource ...
 * // Automatically cleaned up at end of ExecuteImpl
 * @endcode
 */

/**
 * @brief FNV-1a hash constants
 */
namespace Detail {
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;

    /**
     * @brief Compile-time FNV-1a string hash
     */
    constexpr uint64_t HashString(const char* str, uint64_t hash = FNV_OFFSET_BASIS) {
        return (*str == '\0') ? hash : HashString(str + 1, (hash ^ static_cast<uint64_t>(*str)) * FNV_PRIME);
    }

    /**
     * @brief Runtime FNV-1a hash for dynamic strings
     */
    inline uint64_t HashStringRuntime(const char* str, size_t length) {
        uint64_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < length; ++i) {
            hash ^= static_cast<uint64_t>(str[i]);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    /**
     * @brief Combine two hashes
     */
    constexpr uint64_t CombineHash(uint64_t hash1, uint64_t hash2) {
        // Boost-style hash_combine
        return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }
}

/**
 * @brief Compute scope hash (node instance + bundle)
 *
 * Computes a hash identifying the allocation scope (node+bundle).
 * Used for querying all resources allocated within a specific scope
 * to enable automatic cleanup of temporary resources.
 *
 * @param nodeInstanceId Unique node instance identifier
 * @param bundleIndex Bundle index (0 for non-variadic nodes, 0-N for variadic)
 * @return uint64_t Scope hash for resource queries
 *
 * Example:
 * @code
 * // Get scope hash for cleanup
 * uint64_t scopeHash = ComputeScopeHash(GetInstanceId(), 0);
 * tracker.ReleaseTemporaryResources(scopeHash);
 * @endcode
 */
constexpr uint64_t ComputeScopeHash(uint32_t nodeInstanceId, uint32_t bundleIndex) {
    uint64_t combined = Detail::CombineHash(
        static_cast<uint64_t>(nodeInstanceId),
        static_cast<uint64_t>(bundleIndex)
    );
    return combined;
}

/**
 * @brief Compute member hash (variable name only)
 *
 * Computes a hash of just the member variable name.
 * Used internally for the two-part hash structure.
 *
 * @param variableName Variable name string
 * @return uint64_t Member hash
 */
constexpr uint64_t ComputeMemberHash(const char* variableName) {
    return Detail::HashString(variableName);
}

/**
 * @brief Compute persistent resource hash (full)
 *
 * Combines node instance ID, bundle index, and variable name into a
 * persistent 64-bit hash suitable for URM resource identification.
 *
 * This is the full hash = CombineHash(scopeHash, memberHash)
 *
 * @param nodeInstanceId Unique node instance identifier
 * @param bundleIndex Bundle index (0 for non-variadic nodes, 0-N for variadic)
 * @param variableName Compile-time constant string identifying the resource
 * @return uint64_t Persistent hash for resource lookup
 *
 * @note variableName should be a compile-time constant for consistent hashing
 * @note Same inputs ALWAYS produce the same hash (deterministic)
 *
 * Example:
 * @code
 * // Non-variadic node, single resource
 * uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, "framebuffers");
 *
 * // Variadic node, multiple bundles
 * for (uint32_t i = 0; i < bundleCount; ++i) {
 *     uint64_t hash = ComputeResourceHash(GetInstanceId(), i, "descriptors");
 *     // ... request resource with hash
 * }
 * @endcode
 */
constexpr uint64_t ComputeResourceHash(uint32_t nodeInstanceId, uint32_t bundleIndex, const char* variableName) {
    // Two-part structure: scope hash + member hash
    uint64_t scopeHash = ComputeScopeHash(nodeInstanceId, bundleIndex);
    uint64_t memberHash = ComputeMemberHash(variableName);

    // Combine both parts
    return Detail::CombineHash(scopeHash, memberHash);
}

/**
 * @brief Type-safe nameOf() macro for variable name stringification
 *
 * Converts a variable identifier to its string representation at compile-time.
 * This prevents typos and enables refactoring tools to track variable renames.
 *
 * @note This is a preprocessor macro, not a function
 *
 * Example:
 * @code
 * std::optional<StackResourceHandle<VkSemaphore, 4>> imageAvailableSemaphores_;
 *
 * // Without nameOf (error-prone):
 * uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, "imageAvailableSemaphores_");
 *
 * // With nameOf (type-safe):
 * uint64_t hash = ComputeResourceHash(GetInstanceId(), 0, nameOf(imageAvailableSemaphores_));
 *
 * // Or use the convenience macro:
 * uint64_t hash = ComputeResourceHashFor(GetInstanceId(), 0, imageAvailableSemaphores_);
 * @endcode
 */
#define nameOf(var) #var

/**
 * @brief Convenience macro combining ComputeResourceHash with nameOf()
 *
 * Automatically stringifies the variable name and computes the hash.
 * Can be used standalone or with context helper methods.
 *
 * @param nodeId Node instance ID (usually GetInstanceId())
 * @param bundleIdx Bundle index (0 for non-variadic nodes)
 * @param var Variable identifier (NOT a string, will be stringified)
 * @return uint64_t Persistent hash
 *
 * Example:
 * @code
 * // Define member variable
 * std::optional<StackResourceHandle<VkSemaphore, 4>> imageAvailableSemaphores_;
 *
 * // Request from URM using type-safe macro
 * uint64_t hash = ComputeResourceHashFor(GetInstanceId(), 0, imageAvailableSemaphores_);
 * auto result = RequestStackResource<VkSemaphore, 4>(hash);
 * if (result) {
 *     imageAvailableSemaphores_ = std::move(result.value());
 * }
 * @endcode
 */
#define ComputeResourceHashFor(nodeId, bundleIdx, var) \
    ComputeResourceHash(nodeId, bundleIdx, #var)

/**
 * @brief Context-aware member hash computation (RECOMMENDED PATTERN)
 *
 * Automatically uses node instance ID and bundle index from context.
 * This is the cleanest way to compute resource hashes within node methods.
 *
 * RECOMMENDED: Use the context method `ctx.GetMemberHash(nameOf(member))` for better
 * overridability and C++-idiomatic code. This macro is maintained for legacy compatibility.
 *
 * @param ctx Context object (TypedCompileContext, VariadicCompileContext, etc.)
 * @param member Member variable identifier (NOT a string, will be stringified)
 * @return uint64_t Persistent hash
 *
 * Example (LEGACY - use ctx.GetMemberHash() instead):
 * @code
 * void CompileImpl(TypedCompileContext& ctx) {
 *     // Define member variable
 *     std::optional<StackResourceHandle<VkSemaphore, 4>> imageAvailableSemaphores_;
 *
 *     // Legacy macro pattern (still supported)
 *     uint64_t hash = GetMemberHash(ctx, imageAvailableSemaphores_);
 *     auto result = ctx.RequestStackResource<VkSemaphore, 4>(hash);
 * }
 * @endcode
 *
 * NEW RECOMMENDED PATTERN:
 * @code
 * void CompileImpl(TypedCompileContext& ctx) {
 *     std::optional<StackResourceHandle<VkSemaphore, 4>> imageAvailableSemaphores_;
 *
 *     // Method-based pattern (RECOMMENDED)
 *     uint64_t hash = ctx.GetMemberHash(nameOf(imageAvailableSemaphores_));
 *     auto result = ctx.RequestStackResource<VkSemaphore, 4>(hash);
 *     if (result) {
 *         imageAvailableSemaphores_ = std::move(result.value());
 *     }
 * }
 * @endcode
 *
 * @note Requires context to provide GetNodeInstanceId() and GetBundleIndex() methods
 */
#define GetMemberHash(ctx, member) \
    ComputeResourceHashFor((ctx).GetNodeInstanceId(), (ctx).GetBundleIndex(), member)

/**
 * @brief Runtime version for dynamic variable names (use sparingly)
 *
 * Prefer the constexpr version with compile-time string literals.
 * This version is provided for rare cases where variable names are dynamic.
 *
 * @param nodeInstanceId Unique node instance identifier
 * @param bundleIndex Bundle index
 * @param variableName Runtime string (not compile-time constant)
 * @param nameLength Length of variableName
 * @return uint64_t Persistent hash for resource lookup
 */
inline uint64_t ComputeResourceHashRuntime(uint32_t nodeInstanceId, uint32_t bundleIndex,
                                            const char* variableName, size_t nameLength) {
    uint64_t nameHash = Detail::HashStringRuntime(variableName, nameLength);
    uint64_t combined = Detail::CombineHash(static_cast<uint64_t>(nodeInstanceId), nameHash);
    combined = Detail::CombineHash(combined, static_cast<uint64_t>(bundleIndex));
    return combined;
}

/**
 * @brief Compile-time hash literal operator (C++20 style)
 *
 * Allows direct hash computation of string literals:
 * @code
 * constexpr uint64_t hash = "variableName"_hash;
 * @endcode
 *
 * @note This is a utility for future use, not required for current pattern
 */
namespace Literals {
    constexpr uint64_t operator""_hash(const char* str, size_t) {
        return Detail::HashString(str);
    }
}

} // namespace Vixen::RenderGraph
