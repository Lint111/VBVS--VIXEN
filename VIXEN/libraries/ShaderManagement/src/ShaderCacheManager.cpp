#include "ShaderManagement/ShaderCacheManager.h"
#include "ShaderManagement/Hash.h"

namespace ShaderManagement {

// Cache file format:
// [4 bytes] Magic number (SPVC)
// [4 bytes] SPIR-V size in bytes
// [N bytes] SPIR-V data

static const uint32_t CACHE_MAGIC = 0x43565053; // 'SPVC' in little-endian

ShaderCacheManager::ShaderCacheManager(const ShaderCacheConfig& cfg)
    : config(cfg)
{
    // Create cache directory if it doesn't exist
    if (config.enabled && !std::filesystem::exists(config.cacheDirectory)) {
        try {
            std::filesystem::create_directories(config.cacheDirectory);
        } catch (const std::filesystem::filesystem_error&) {
            // If directory creation fails, disable caching
            config.enabled = false;
        }
    }
}

ShaderCacheManager::~ShaderCacheManager() {
    // Save cache metadata if needed
}

std::optional<std::vector<uint32_t>> ShaderCacheManager::Lookup(const std::string& cacheKey) {
    if (!config.enabled) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    std::filesystem::path cachePath = config.cacheDirectory / (cacheKey + ".spv");

    if (!std::filesystem::exists(cachePath)) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    // Read cache file
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    // Read magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file.good() || magic != CACHE_MAGIC) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    // Read size
    uint32_t size;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!file.good()) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    // Read SPIR-V data
    std::vector<uint32_t> spirv(size / 4);
    file.read(reinterpret_cast<char*>(spirv.data()), size);
    if (!file.good()) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    // Validate if requested
    if (config.validateCache) {
        // Basic validation: check SPIR-V magic number
        if (spirv.empty() || spirv[0] != 0x07230203) {
            stats.totalCacheMisses++;
            return std::nullopt;
        }
    }

    stats.totalCacheHits++;
    stats.totalBytesRead += size;

    return spirv;
}

bool ShaderCacheManager::Store(const std::string& cacheKey, const std::vector<uint32_t>& spirv) {
    if (!config.enabled) {
        return false;
    }

    // Validate inputs
    if (cacheKey.empty()) {
        return false;
    }

    if (spirv.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    std::filesystem::path cachePath = config.cacheDirectory / (cacheKey + ".spv");

    // Write cache file
    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Write magic number
    uint32_t magic = CACHE_MAGIC;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

    // Write size
    uint32_t size = static_cast<uint32_t>(spirv.size() * sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));

    // Write SPIR-V data
    file.write(reinterpret_cast<const char*>(spirv.data()), size);

    if (!file.good()) {
        return false;
    }

    stats.totalBytesWritten += size + sizeof(magic) + sizeof(size);
    stats.cachedShaderCount++;

    // Check cache size limit (simple check, full eviction done on demand)
    if (stats.currentCacheSizeBytes > config.maxCacheSizeMB * 1024 * 1024) {
        EvictOldEntries();
    }

    return true;
}

bool ShaderCacheManager::Contains(const std::string& cacheKey) const {
    if (!config.enabled) {
        return false;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    std::filesystem::path cachePath = config.cacheDirectory / (cacheKey + ".spv");
    return std::filesystem::exists(cachePath);
}

bool ShaderCacheManager::Remove(const std::string& cacheKey) {
    if (!config.enabled) {
        return false;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    std::filesystem::path cachePath = config.cacheDirectory / (cacheKey + ".spv");
    if (!std::filesystem::exists(cachePath)) {
        return false;
    }

    std::filesystem::remove(cachePath);
    stats.cachedShaderCount--;

    return true;
}

void ShaderCacheManager::Clear() {
    if (!config.enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    // Remove all .spv files in cache directory
    for (const auto& entry : std::filesystem::directory_iterator(config.cacheDirectory)) {
        if (entry.path().extension() == ".spv") {
            std::filesystem::remove(entry.path());
        }
    }

    stats.cachedShaderCount = 0;
    stats.currentCacheSizeBytes = 0;
}

uint32_t ShaderCacheManager::ValidateCache() {
    if (!config.enabled) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    uint32_t corruptedCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(config.cacheDirectory)) {
        if (entry.path().extension() != ".spv") {
            continue;
        }

        // Try to read and validate
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) {
            std::filesystem::remove(entry.path());
            corruptedCount++;
            continue;
        }

        // Read magic
        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (!file.good() || magic != CACHE_MAGIC) {
            std::filesystem::remove(entry.path());
            corruptedCount++;
            continue;
        }

        // Read size
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (!file.good() || size == 0) {
            std::filesystem::remove(entry.path());
            corruptedCount++;
            continue;
        }

        // Read first word of SPIR-V (should be magic number)
        uint32_t spirvMagic;
        file.read(reinterpret_cast<char*>(&spirvMagic), sizeof(spirvMagic));
        if (!file.good() || spirvMagic != 0x07230203) {
            std::filesystem::remove(entry.path());
            corruptedCount++;
            continue;
        }
    }

    return corruptedCount;
}

void ShaderCacheManager::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    config.enabled = enabled;
}

bool ShaderCacheManager::IsEnabled() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return config.enabled;
}

void ShaderCacheManager::SetMaxCacheSize(size_t maxSizeMB) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    config.maxCacheSizeMB = maxSizeMB;
}

const std::filesystem::path& ShaderCacheManager::GetCacheDirectory() const {
    return config.cacheDirectory;
}

ShaderCacheStats ShaderCacheManager::GetStatistics() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return stats;
}

void ShaderCacheManager::ResetStatistics() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    stats.totalCacheHits = 0;
    stats.totalCacheMisses = 0;
    stats.totalBytesRead = 0;
    stats.totalBytesWritten = 0;
}

void ShaderCacheManager::RebuildMetadata() {
    std::lock_guard<std::mutex> lock(cacheMutex);

    // Clear existing metadata
    entries.clear();

    // Rebuild from disk
    if (!std::filesystem::exists(config.cacheDirectory)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(config.cacheDirectory)) {
        if (entry.path().extension() == ".spv") {
            // Extract cache key from filename (without .spv extension)
            std::string cacheKey = entry.path().stem().string();

            CacheEntry cacheEntry;
            cacheEntry.filePath = entry.path();
            cacheEntry.sizeBytes = std::filesystem::file_size(entry.path());
            cacheEntry.lastAccessed = std::filesystem::last_write_time(entry.path());

            entries[cacheKey] = cacheEntry;
        }
    }
}

std::filesystem::path ShaderCacheManager::GetCacheFilePath(const std::string& cacheKey) const {
    return config.cacheDirectory / (cacheKey + ".spv");
}

bool ShaderCacheManager::ValidateCacheEntry(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file.good() || magic != CACHE_MAGIC) {
        return false;
    }

    return true;
}

void ShaderCacheManager::UpdateAccessTime(const std::string& cacheKey) {
    auto it = entries.find(cacheKey);
    if (it != entries.end()) {
        it->second.lastAccessed = std::filesystem::file_time_type::clock::now();
    }
}

void ShaderCacheManager::EnsureCacheDirectoryExists() {
    if (!std::filesystem::exists(config.cacheDirectory)) {
        std::filesystem::create_directories(config.cacheDirectory);
    }
}

std::vector<ShaderCacheManager::CacheEntry> ShaderCacheManager::GetEntriesSortedByAccessTime() const {
    std::vector<CacheEntry> sorted;
    sorted.reserve(entries.size());

    for (const auto& [key, entry] : entries) {
        sorted.push_back(entry);
    }

    std::sort(sorted.begin(), sorted.end(),
        [](const CacheEntry& a, const CacheEntry& b) {
            return a.lastAccessed < b.lastAccessed;
        });

    return sorted;
}

uint32_t ShaderCacheManager::EvictOldEntries() {
    // Get all cache files with their modification times
    struct LocalCacheEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type lastModified;
        size_t size;
    };

    std::vector<LocalCacheEntry> fileEntries;
    for (const auto& entry : std::filesystem::directory_iterator(config.cacheDirectory)) {
        if (entry.path().extension() == ".spv") {
            fileEntries.push_back({
                entry.path(),
                std::filesystem::last_write_time(entry.path()),
                std::filesystem::file_size(entry.path())
            });
        }
    }

    // Sort by last modified time (oldest first)
    std::sort(fileEntries.begin(), fileEntries.end(),
        [](const LocalCacheEntry& a, const LocalCacheEntry& b) {
            return a.lastModified < b.lastModified;
        });

    // Remove oldest until we're under the limit
    size_t targetSize = config.maxCacheSizeMB * 1024 * 1024 * 9 / 10; // 90% of limit
    size_t currentSize = stats.currentCacheSizeBytes;
    uint32_t evicted = 0;

    for (const auto& entry : fileEntries) {
        if (currentSize <= targetSize) {
            break;
        }

        std::filesystem::remove(entry.path);
        currentSize -= entry.size;
        stats.cachedShaderCount--;
        evicted++;
    }

    stats.currentCacheSizeBytes = currentSize;
    return evicted;
}

std::string GenerateCacheKey(
    const std::string& source,
    const std::filesystem::path& sourcePath,
    uint32_t targetVulkanVersion,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const std::string& entryPoint)
{
    // Create a hash from all inputs
    std::stringstream keyStream;
    keyStream << source;
    keyStream << sourcePath.string();
    keyStream << targetVulkanVersion;
    keyStream << entryPoint;

    for (const auto& [name, value] : defines) {
        keyStream << name << "=" << value << ";";
    }

    std::string keyString = keyStream.str();
    return ComputeSHA256Hex(reinterpret_cast<const uint8_t*>(keyString.data()), keyString.size());
}

} // namespace ShaderManagement
