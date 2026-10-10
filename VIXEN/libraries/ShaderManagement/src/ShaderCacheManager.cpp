#include "ShaderCacheManager.h"
#include "Hash.h"
#include "RuntimeCachePaths.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace ShaderManagement {

// Cache file format:
// [4 bytes] Magic number (SPVC), [4 bytes] version,
// [8 bytes] cache-key hash, [8 bytes] payload length, [8 bytes] payload checksum,
// [N bytes] SPIR-V data.

static const uint32_t CACHE_MAGIC = 0x43565053; // 'SPVC' in little-endian
static constexpr uint32_t CACHE_VERSION = 1;
static constexpr uint64_t MAX_SHADER_CACHE_BYTES = 512ULL * 1024ULL * 1024ULL;

struct ShaderCacheFileHeader {
    uint32_t magic = CACHE_MAGIC;
    uint32_t version = CACHE_VERSION;
    uint64_t keyHash = 0;
    uint64_t payloadSize = 0;
    uint64_t payloadChecksum = 0;
};
static_assert(sizeof(ShaderCacheFileHeader) == 32);

static uint64_t HashCacheKey(const std::string& cacheKey) {
    return Vixen::Checksum64(cacheKey.data(), cacheKey.size());
}

static std::optional<std::vector<uint32_t>> ReadShaderCacheFile(
    const std::filesystem::path& cachePath,
    const std::string& cacheKey,
    bool validateSpirv
) {
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(cachePath, sizeError);
    if (sizeError || fileSize < sizeof(ShaderCacheFileHeader) ||
        fileSize > sizeof(ShaderCacheFileHeader) + MAX_SHADER_CACHE_BYTES) {
        return std::nullopt;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file) return std::nullopt;

    ShaderCacheFileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != CACHE_MAGIC || header.version != CACHE_VERSION ||
        header.keyHash != HashCacheKey(cacheKey)) {
        return std::nullopt;
    }

    const auto payloadSize = fileSize - sizeof(header);
    if (header.payloadSize != payloadSize || payloadSize == 0 ||
        payloadSize % sizeof(uint32_t) != 0 || payloadSize > MAX_SHADER_CACHE_BYTES) {
        return std::nullopt;
    }

    std::vector<uint32_t> spirv(static_cast<std::size_t>(payloadSize / sizeof(uint32_t)));
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(payloadSize));
    if (!file || Vixen::Checksum64(spirv.data(), static_cast<std::size_t>(payloadSize)) != header.payloadChecksum) {
        return std::nullopt;
    }
    if (validateSpirv && (spirv.empty() || spirv[0] != 0x07230203)) {
        return std::nullopt;
    }
    return spirv;
}

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

    // A cache miss rebuilds on mismatch; no reader removes the path because another process may
    // have atomically replaced it after this reader opened the previous file.
    auto spirv = ReadShaderCacheFile(cachePath, cacheKey, config.validateCache);
    if (!spirv) {
        stats.totalCacheMisses++;
        return std::nullopt;
    }

    stats.totalCacheHits++;
    stats.totalBytesRead += spirv->size() * sizeof(uint32_t);

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

    if (spirv.size() > MAX_SHADER_CACHE_BYTES / sizeof(uint32_t)) {
        return false;
    }
    const auto payloadSize = spirv.size() * sizeof(uint32_t);

    ShaderCacheFileHeader header{};
    header.keyHash = HashCacheKey(cacheKey);
    header.payloadSize = payloadSize;
    header.payloadChecksum = Vixen::Checksum64(spirv.data(), payloadSize);
    std::vector<char> bytes(sizeof(header) + payloadSize);
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), spirv.data(), payloadSize);
    std::string writeError;
    if (!Vixen::AtomicWriteBytes(cachePath, bytes.data(), bytes.size(), &writeError)) {
        return false;
    }

    stats.totalBytesWritten += bytes.size();
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

        if (!ReadShaderCacheFile(entry.path(), entry.path().stem().string(), true)) {
            // Leave the invalid path in place. A concurrent process may have replaced it after
            // validation began; Lookup will miss and Store will atomically rebuild the entry.
            corruptedCount++;
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
    return ReadShaderCacheFile(path, path.stem().string(), true).has_value();
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
