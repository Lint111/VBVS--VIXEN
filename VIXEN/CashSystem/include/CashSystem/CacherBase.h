#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <filesystem>
#include <any>

namespace CashSystem {

class CacherBase {
public:
    virtual ~CacherBase() = default;

    // Return true if an entry exists for key
    virtual bool Has(std::uint64_t key) const noexcept = 0;

    // Get shared pointer to cached object or nullptr
    virtual std::shared_ptr<void> Get(std::uint64_t key) = 0;

    // Insert a new entry given key and creation params; returns shared_ptr to cached object
    virtual std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) = 0;

    // Remove, clear, or prune
    virtual void Erase(std::uint64_t key) = 0;
    virtual void Clear() = 0;

    // Persist in-memory cache to disk at path
    virtual bool SerializeToFile(const std::filesystem::path& path) const = 0;

    // Load cache from disk; recreate live objects where possible. "device" is opaque here
    virtual bool DeserializeFromFile(const std::filesystem::path& path, void* device) = 0;

    // Return human readable name for diagnostics
    virtual std::string_view name() const noexcept = 0;
};

} // namespace CashSystem
