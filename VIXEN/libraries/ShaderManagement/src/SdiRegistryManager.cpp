#include "SdiRegistryManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ShaderManagement {

namespace {

/**
 * @brief Sanitize name for C++ identifier
 */
std::string SanitizeName(const std::string& name) {
    std::string sanitized = name;

    // Replace invalid characters with underscores
    for (char& c : sanitized) {
        if (!std::isalnum(c) && c != '_') {
            c = '_';
        }
    }

    // Ensure doesn't start with digit
    if (!sanitized.empty() && std::isdigit(sanitized[0])) {
        sanitized = "_" + sanitized;
    }

    return sanitized;
}

/**
 * @brief Get relative path from one path to another
 */
std::string GetRelativePath(
    const std::filesystem::path& from,
    const std::filesystem::path& to
) {
    // Simple implementation - just use filename if in same directory
    if (from.parent_path() == to.parent_path()) {
        return to.filename().string();
    }

    // Otherwise use absolute path
    return to.string();
}

} // anonymous namespace

// ===== SdiRegistryManager Implementation =====

SdiRegistryManager::SdiRegistryManager()
    : SdiRegistryManager(Config{})
{
}

SdiRegistryManager::SdiRegistryManager(const Config& config)
    : config_(config)
    , lastRegeneration_(std::chrono::system_clock::now())
{
    // Create SDI directory
    std::filesystem::create_directories(config_.sdiDirectory);

    // Create registry header directory
    if (config_.registryHeaderPath.has_parent_path()) {
        std::filesystem::create_directories(config_.registryHeaderPath.parent_path());
    }

    // Load existing registry
    LoadRegistry();
}

SdiRegistryManager::~SdiRegistryManager() {
    // Save registry on destruction
    SaveRegistry();
}

bool SdiRegistryManager::RegisterShader(const SdiRegistryEntry& entry) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Validate entry
    if (entry.uuid.empty()) {
        return false;
    }

    // Check if already registered
    auto it = entries_.find(entry.uuid);
    if (it != entries_.end()) {
        // Update existing entry
        it->second = entry;
        it->second.isActive = true;
        it->second.lastAccessedAt = std::chrono::system_clock::now();
    } else {
        // Add new entry
        SdiRegistryEntry newEntry = entry;
        newEntry.isActive = true;
        newEntry.registeredAt = std::chrono::system_clock::now();
        newEntry.lastAccessedAt = newEntry.registeredAt;

        // Sanitize alias
        newEntry.aliasName = SanitizeAlias(entry.aliasName.empty() ?
            entry.programName : entry.aliasName);

        // Ensure alias is unique
        if (!ValidateAliasUnique(newEntry.aliasName)) {
            // Append UUID prefix to make unique
            newEntry.aliasName += "_" + entry.uuid.substr(0, 8);
        }

        entries_[entry.uuid] = newEntry;
        aliasToUuid_[newEntry.aliasName] = entry.uuid;
    }

    // Track change
    changesSinceRegeneration_++;

    // Auto-regenerate if enabled and threshold reached
    if (config_.autoRegenerate && NeedsRegeneration()) {
        RegenerateRegistry();
    }

    // Save registry
    SaveRegistry();

    return true;
}

bool SdiRegistryManager::UnregisterShader(const std::string& uuid, bool deleteFromDisk) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = entries_.find(uuid);
    if (it == entries_.end()) {
        return false;
    }

    // Mark as inactive (keep in registry for potential cleanup)
    it->second.isActive = false;

    // Remove from alias map
    aliasToUuid_.erase(it->second.aliasName);

    // Delete from disk if requested
    if (deleteFromDisk && std::filesystem::exists(it->second.sdiHeaderPath)) {
        std::filesystem::remove(it->second.sdiHeaderPath);
    }

    // Track change
    changesSinceRegeneration_++;

    // Auto-regenerate if enabled
    if (config_.autoRegenerate && NeedsRegeneration()) {
        RegenerateRegistry();
    }

    // Save registry
    SaveRegistry();

    return true;
}

bool SdiRegistryManager::IsRegistered(const std::string& uuid) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = entries_.find(uuid);
    return it != entries_.end() && it->second.isActive;
}

std::optional<SdiRegistryEntry> SdiRegistryManager::GetEntry(const std::string& uuid) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = entries_.find(uuid);
    if (it != entries_.end() && it->second.isActive) {
        // Return a copy to avoid race conditions
        // Lock protects during copy, caller can safely use returned value
        return it->second;
    }

    return std::nullopt;
}

bool SdiRegistryManager::UpdateAlias(const std::string& uuid, const std::string& aliasName) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = entries_.find(uuid);
    if (it == entries_.end()) {
        return false;
    }

    std::string sanitized = SanitizeAlias(aliasName);

    // Validate uniqueness
    if (!ValidateAliasUnique(sanitized, uuid)) {
        return false;
    }

    // Remove old alias mapping
    aliasToUuid_.erase(it->second.aliasName);

    // Update entry
    it->second.aliasName = sanitized;
    aliasToUuid_[sanitized] = uuid;

    // Track change
    changesSinceRegeneration_++;

    // Auto-regenerate if enabled
    if (config_.autoRegenerate && NeedsRegeneration()) {
        RegenerateRegistry();
    }

    SaveRegistry();

    return true;
}

std::vector<std::string> SdiRegistryManager::GetRegisteredUuids(bool activeOnly) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::vector<std::string> uuids;
    uuids.reserve(entries_.size());

    for (const auto& [uuid, entry] : entries_) {
        if (!activeOnly || entry.isActive) {
            uuids.push_back(uuid);
        }
    }

    return uuids;
}

std::vector<SdiRegistryEntry> SdiRegistryManager::GetAllEntries(bool activeOnly) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::vector<SdiRegistryEntry> result;
    result.reserve(entries_.size());

    for (const auto& [uuid, entry] : entries_) {
        if (!activeOnly || entry.isActive) {
            result.push_back(entry);
        }
    }

    return result;
}

size_t SdiRegistryManager::GetRegisteredCount(bool activeOnly) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!activeOnly) {
        return entries_.size();
    }

    return std::count_if(entries_.begin(), entries_.end(),
        [](const auto& pair) { return pair.second.isActive; });
}

std::string SdiRegistryManager::FindByAlias(const std::string& aliasName) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = aliasToUuid_.find(aliasName);
    return it != aliasToUuid_.end() ? it->second : "";
}

bool SdiRegistryManager::RegenerateRegistry() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    try {
        // Generate code
        std::string code = GenerateRegistryToString();

        // Write to file
        std::ofstream file(config_.registryHeaderPath);
        if (!file.is_open()) {
            return false;
        }

        file << code;
        file.close();

        // Reset change tracking
        changesSinceRegeneration_ = 0;
        lastRegeneration_ = std::chrono::system_clock::now();

        return true;

    } catch (const std::exception&) {
        return false;
    }
}

std::string SdiRegistryManager::GenerateRegistryToString() const {
    // NOTE: This method assumes mutex_ is already locked by the caller
    // DO NOT call public methods that lock mutex_ from here

    std::ostringstream code;

    // Count active shaders without locking (caller already has lock)
    size_t activeCount = std::count_if(entries_.begin(), entries_.end(),
        [](const auto& pair) { return pair.second.isActive; });

    // Header
    code << "// ============================================================================\n";
    code << "// SDI Central Registry\n";
    code << "// ============================================================================\n";
    code << "//\n";
    code << "// Auto-generated central registry for Shader Descriptor Interfaces (SDI).\n";
    code << "// This file includes ONLY currently registered/active shaders.\n";
    code << "//\n";
    code << "// Benefits:\n";
    code << "//   - Single include for all shader interfaces\n";
    code << "//   - Convenient namespace aliases\n";
    code << "//   - Reduced compilation time (only active shaders)\n";
    code << "//\n";
    code << "// Active Shaders: " << activeCount << "\n";
    code << "//\n";
    code << "// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.\n";
    code << "//\n";
    code << "// ============================================================================\n";
    code << "\n";
    code << "#pragma once\n";
    code << "\n";

    // Collect active entries and sort by alias
    std::vector<SdiRegistryEntry> activeEntries;
    for (const auto& [uuid, entry] : entries_) {
        if (entry.isActive) {
            activeEntries.push_back(entry);
        }
    }

    std::sort(activeEntries.begin(), activeEntries.end(),
        [](const SdiRegistryEntry& a, const SdiRegistryEntry& b) {
            return a.aliasName < b.aliasName;
        });

    if (activeEntries.empty()) {
        code << "// No shaders currently registered\n";
        code << "\n";
        return code.str();
    }

    // Include individual SDI headers
    code << "// ============================================================================\n";
    code << "// Include Active Shader SDI Headers\n";
    code << "// ============================================================================\n";
    code << "\n";

    for (const auto& entry : activeEntries) {
        if (config_.generateComments) {
            code << "// " << entry.programName << " (" << entry.uuid << ")\n";
        }

        // Get relative path from registry to SDI header
        std::string includePath = GetRelativePath(
            config_.registryHeaderPath,
            entry.sdiHeaderPath
        );

        code << "#include \"" << includePath << "\"\n";
    }

    code << "\n";

    // Generate namespace aliases
    if (config_.generateAliases) {
        code << "// ============================================================================\n";
        code << "// Convenient Namespace Aliases\n";
        code << "// ============================================================================\n";
        code << "//\n";
        code << "// Usage:\n";
        code << "//   using namespace " << config_.registryNamespace << ";\n";
        code << "//   binding.binding = YourShader::Set0::SomeBinding::BINDING;\n";
        code << "//\n";
        code << "// ============================================================================\n";
        code << "\n";

        code << "namespace " << config_.registryNamespace << " {\n";
        code << "\n";

        for (const auto& entry : activeEntries) {
            if (config_.generateComments) {
                code << "    // " << entry.programName << "\n";
            }

            code << "    namespace " << entry.aliasName
                 << " = " << entry.sdiNamespace << ";\n";
        }

        code << "\n";
        code << "} // namespace " << config_.registryNamespace << "\n";
        code << "\n";
    }

    // Generate shader list for runtime introspection (optional)
    code << "// ============================================================================\n";
    code << "// Shader Metadata (for runtime introspection)\n";
    code << "// ============================================================================\n";
    code << "\n";
    code << "namespace " << config_.registryNamespace << " {\n";
    code << "namespace Registry {\n";
    code << "\n";
    code << "    struct ShaderInfo {\n";
    code << "        const char* uuid;\n";
    code << "        const char* name;\n";
    code << "        const char* alias;\n";
    code << "    };\n";
    code << "\n";
    code << "    constexpr ShaderInfo SHADERS[] = {\n";

    for (size_t i = 0; i < activeEntries.size(); ++i) {
        const auto& entry = activeEntries[i];
        code << "        {\"" << entry.uuid << "\", "
             << "\"" << entry.programName << "\", "
             << "\"" << entry.aliasName << "\"}";

        if (i < activeEntries.size() - 1) {
            code << ",";
        }
        code << "\n";
    }

    code << "    };\n";
    code << "\n";
    code << "    constexpr size_t SHADER_COUNT = " << activeEntries.size() << ";\n";
    code << "\n";
    code << "} // namespace Registry\n";
    code << "} // namespace " << config_.registryNamespace << "\n";
    code << "\n";

    return code.str();
}

bool SdiRegistryManager::NeedsRegeneration() const {
    return changesSinceRegeneration_ >= config_.regenerationThreshold;
}

void SdiRegistryManager::MarkDirty() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    changesSinceRegeneration_ = config_.regenerationThreshold;
}

uint32_t SdiRegistryManager::CleanupInactive(std::chrono::hours olderThan) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    uint32_t count = 0;
    auto now = std::chrono::system_clock::now();

    auto it = entries_.begin();
    while (it != entries_.end()) {
        if (!it->second.isActive) {
            auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - it->second.lastAccessedAt);

            if (age >= olderThan) {
                aliasToUuid_.erase(it->second.aliasName);
                it = entries_.erase(it);
                ++count;
                continue;
            }
        }
        ++it;
    }

    if (count > 0) {
        SaveRegistry();
    }

    return count;
}

uint32_t SdiRegistryManager::ValidateRegistry() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    uint32_t invalidCount = 0;

    for (auto& [uuid, entry] : entries_) {
        if (entry.isActive && !std::filesystem::exists(entry.sdiHeaderPath)) {
            entry.isActive = false;
            aliasToUuid_.erase(entry.aliasName);
            ++invalidCount;
        }
    }

    if (invalidCount > 0) {
        SaveRegistry();

        if (config_.autoRegenerate) {
            RegenerateRegistry();
        }
    }

    return invalidCount;
}

uint32_t SdiRegistryManager::ClearAll(bool deleteFromDisk) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    uint32_t count = static_cast<uint32_t>(entries_.size());

    if (deleteFromDisk) {
        for (const auto& [uuid, entry] : entries_) {
            if (std::filesystem::exists(entry.sdiHeaderPath)) {
                std::filesystem::remove(entry.sdiHeaderPath);
            }
        }
    }

    entries_.clear();
    aliasToUuid_.clear();

    SaveRegistry();

    if (config_.autoRegenerate) {
        RegenerateRegistry();
    }

    return count;
}

SdiRegistryManager::Stats SdiRegistryManager::GetStats() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    Stats stats;
    stats.totalRegistered = entries_.size();
    stats.lastRegeneration = lastRegeneration_;
    stats.changesSinceRegeneration = changesSinceRegeneration_;

    for (const auto& [uuid, entry] : entries_) {
        if (entry.isActive) {
            ++stats.activeShaders;

            if (!std::filesystem::exists(entry.sdiHeaderPath)) {
                ++stats.orphanedFiles;
            }
        } else {
            ++stats.inactiveShaders;
        }
    }

    return stats;
}

// ===== Private Helpers =====

void SdiRegistryManager::SaveRegistry() {
    std::filesystem::path registryPath = config_.sdiDirectory / "sdi_registry.dat";

    std::ofstream file(registryPath);
    if (!file.is_open()) {
        return;
    }

    for (const auto& [uuid, entry] : entries_) {
        file << uuid << "|"
             << entry.programName << "|"
             << entry.sdiHeaderPath.string() << "|"
             << entry.sdiNamespace << "|"
             << entry.aliasName << "|"
             << (entry.isActive ? "1" : "0") << "\n";
    }

    file.close();
}

void SdiRegistryManager::LoadRegistry() {
    std::filesystem::path registryPath = config_.sdiDirectory / "sdi_registry.dat";

    if (!std::filesystem::exists(registryPath)) {
        return;
    }

    std::ifstream file(registryPath);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string uuid, name, path, ns, alias, active;

        if (std::getline(iss, uuid, '|') &&
            std::getline(iss, name, '|') &&
            std::getline(iss, path, '|') &&
            std::getline(iss, ns, '|') &&
            std::getline(iss, alias, '|') &&
            std::getline(iss, active, '|'))
        {
            SdiRegistryEntry entry;
            entry.uuid = uuid;
            entry.programName = name;
            entry.sdiHeaderPath = path;
            entry.sdiNamespace = ns;
            entry.aliasName = alias;
            entry.isActive = (active == "1");
            entry.registeredAt = std::chrono::system_clock::now();
            entry.lastAccessedAt = entry.registeredAt;

            entries_[uuid] = entry;

            if (entry.isActive) {
                aliasToUuid_[alias] = uuid;
            }
        }
    }

    file.close();
}

std::string SdiRegistryManager::SanitizeAlias(const std::string& name) const {
    return SanitizeName(name);
}

bool SdiRegistryManager::ValidateAliasUnique(
    const std::string& alias,
    const std::string& excludeUuid
) const {
    auto it = aliasToUuid_.find(alias);

    // Not found - unique
    if (it == aliasToUuid_.end()) {
        return true;
    }

    // Found but it's the excluded UUID - unique
    if (it->second == excludeUuid) {
        return true;
    }

    // Found and different UUID - not unique
    return false;
}

} // namespace ShaderManagement
