#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <mutex>
#include <unordered_map>

namespace ShaderManagement {

/**
 * @brief Shader cache configuration
 */
struct ShaderCacheConfig {
    std::filesystem::path cacheDirectory = "./shader_cache";
    bool enabled = true;
    bool validateCache = true;  // Verify cache integrity on load
    size_t maxCacheSizeMB = 512;  // Maximum cache size in megabytes
};

/**
 * @brief Cache statistics
 */
struct ShaderCacheStats {
    size_t totalCacheHits = 0;
    size_t totalCacheMisses = 0;
    size_t totalBytesRead = 0;
    size_t totalBytesWritten = 0;
    size_t currentCacheSizeBytes = 0;
    uint32_t cachedShaderCount = 0;

    float GetHitRate() const {
        uint64_t total = totalCacheHits + totalCacheMisses;
        return total > 0 ? (static_cast<float>(totalCacheHits) / total) : 0.0f;
    }
};

/**
 * @brief Shader cache manager (device-agnostic)
 *
 * Manages persistent caching of compiled SPIR-V bytecode to disk.
 * Thread-safe: All operations are internally synchronized.
 *
 * Design:
 * - Stores SPIR-V bytecode only (no Vulkan objects)
 * - Cache keys are content-addressable (hash of source + metadata)
 * - Automatic cache validation and cleanup
 * - Size-based eviction policy (LRU)
 */
class ShaderCacheManager {
public:
    explicit ShaderCacheManager(const ShaderCacheConfig& config = {});
    ~ShaderCacheManager();

    // Disable copy/move
    ShaderCacheManager(const ShaderCacheManager&) = delete;
    ShaderCacheManager& operator=(const ShaderCacheManager&) = delete;

    // ===== Cache Operations =====

    /**
     * @brief Lookup SPIR-V in cache by key
     * @param cacheKey Content hash (generated externally)
     * @return SPIR-V bytecode if found, nullopt otherwise
     */
    std::optional<std::vector<uint32_t>> Lookup(const std::string& cacheKey);

    /**
     * @brief Store SPIR-V in cache
     * @param cacheKey Content hash
     * @param spirv SPIR-V bytecode to cache
     * @return true if stored successfully, false otherwise
     */
    bool Store(const std::string& cacheKey, const std::vector<uint32_t>& spirv);

    /**
     * @brief Check if cache contains key
     * @param cacheKey Content hash to check
     * @return true if cached, false otherwise
     */
    bool Contains(const std::string& cacheKey) const;

    /**
     * @brief Remove specific entry from cache
     * @param cacheKey Entry to remove
     * @return true if removed, false if not found
     */
    bool Remove(const std::string& cacheKey);

    // ===== Cache Management =====

    /**
     * @brief Clear all cached entries
     */
    void Clear();

    /**
     * @brief Validate cache integrity (check file corruption)
     * @return Number of corrupted entries removed
     */
    uint32_t ValidateCache();

    /**
     * @brief Evict old entries to meet size limit
     * @return Number of entries evicted
     */
    uint32_t EvictOldEntries();

    /**
     * @brief Rebuild cache metadata from disk
     * Useful after manual cache directory modifications
     */
    void RebuildMetadata();

    // ===== Configuration =====

    /**
     * @brief Enable or disable caching
     * @param enabled If false, all operations become no-ops
     */
    void SetEnabled(bool enabled);

    /**
     * @brief Check if caching is enabled
     */
    bool IsEnabled() const;

    /**
     * @brief Set maximum cache size
     * @param sizeMB Maximum size in megabytes
     */
    void SetMaxCacheSize(size_t sizeMB);

    /**
     * @brief Get cache directory path
     */
    const std::filesystem::path& GetCacheDirectory() const;

    // ===== Statistics =====

    /**
     * @brief Get cache statistics
     */
    ShaderCacheStats GetStatistics() const;

    /**
     * @brief Reset statistics counters
     */
    void ResetStatistics();

private:
    // Internal structures
    struct CacheEntry {
        std::filesystem::path filePath;
        size_t sizeBytes;
        std::filesystem::file_time_type lastAccessed;
    };

    // Internal helpers
    std::filesystem::path GetCacheFilePath(const std::string& cacheKey) const;
    bool ValidateCacheEntry(const std::filesystem::path& path) const;
    void UpdateAccessTime(const std::string& cacheKey);
    void EnsureCacheDirectoryExists();
    std::vector<CacheEntry> GetEntriesSortedByAccessTime() const;

    // Configuration
    ShaderCacheConfig config;

    // Metadata tracking (in-memory)
    std::unordered_map<std::string, CacheEntry> entries;

    // Statistics
    mutable ShaderCacheStats stats;

    // Thread safety
    mutable std::mutex cacheMutex;
};

/**
 * @brief Generate cache key from source and metadata
 *
 * Utility function to create content-addressable cache keys.
 * Hashes: source code + stage + defines + entry point
 *
 * @param source Shader source code (GLSL or empty if using SPIR-V path)
 * @param sourcePath Path to source file (used if source is empty)
 * @param stage Shader stage
 * @param defines Preprocessor defines (name=value pairs)
 * @param entryPoint Entry point function name
 * @return Hex string cache key (64 characters)
 */
std::string GenerateCacheKey(
    const std::string& source,
    const std::filesystem::path& sourcePath,
    uint32_t stage,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const std::string& entryPoint
);

} // namespace ShaderManagement
