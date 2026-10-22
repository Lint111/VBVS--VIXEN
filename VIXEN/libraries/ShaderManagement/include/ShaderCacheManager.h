#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <atomic>
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
 * Lock-free: stats and knobs are relaxed atomics; entry files are written by rename, and the
 * in-memory `entries` map (RebuildMetadata) is owner-phase, touched only by the owning thread.
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
     * @return Number of corrupted entries rejected by validation
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

    // Statistics and runtime knobs: relaxed atomics, no lock (GetStatistics returns a snapshot)
    struct AtomicStats {
        std::atomic<size_t> totalCacheHits{0};
        std::atomic<size_t> totalCacheMisses{0};
        std::atomic<size_t> totalBytesRead{0};
        std::atomic<size_t> totalBytesWritten{0};
        std::atomic<size_t> currentCacheSizeBytes{0};
        std::atomic<uint32_t> cachedShaderCount{0};
    };
    mutable AtomicStats stats;
    std::atomic<bool> enabled_;
    std::atomic<size_t> maxCacheSizeMB_;
};

/**
 * @brief Config of the default SPIR-V cache: <Vixen::SharedDeviceCacheDirectory()>/spirv.
 *
 * SPIR-V is device-independent and its key hashes the post-preprocess source, every
 * compile-affecting option and ShaderCompiler::CompilerIdentity(), so an entry is valid for any
 * build tree and lives in the shared cache root. VIXEN_SHADER_CACHE_DIR overrides the directory;
 * VIXEN_SHADER_CACHE_DISABLE=1 disables caching without deleting the directory.
 */
ShaderCacheConfig DefaultShaderCacheConfig();

/**
 * @brief The process-wide SPIR-V cache every ShaderBundleBuilder uses unless the builder names
 * another one (EnableCaching) or opts out (DisableCaching). Created on first use.
 */
ShaderCacheManager& DefaultShaderCacheManager();

} // namespace ShaderManagement
