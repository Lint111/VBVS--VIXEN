#pragma once

#include "CacheCodec.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <filesystem>
#include <any>

// Forward declarations (must be outside CashSystem namespace)
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

class MainCacher;  // owning cacher, set at creation (AR#8 — replaces the process-wide singleton)

class CacherBase {
public:
    virtual ~CacherBase() = default;

    // Initialize the cacher with device context (optional - device-independent cachers can ignore)
    virtual void Initialize(Vixen::Vulkan::Resources::VulkanDevice* device) = 0;

    // Check if the cacher has been initialized
    virtual bool IsInitialized() const noexcept = 0;

    // Return true if an entry exists for key
    virtual bool Has(std::uint64_t key) const noexcept = 0;

    // Get shared pointer to cached object or nullptr
    virtual std::shared_ptr<void> Get(std::uint64_t key) = 0;

    // Insert a new entry given key and creation params; returns shared_ptr to cached object
    virtual std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) = 0;

    // Remove, clear, or prune
    virtual void Erase(std::uint64_t key) = 0;
    virtual void Clear() = 0;

    // Cleanup all Vulkan resources owned by this cacher
    // Called by MainCacher during device cleanup or application shutdown
    virtual void Cleanup() = 0;

    // Persist the existing cacher payload inside a versioned, checksummed envelope, then publish
    // it with a same-directory atomic replacement. A true no-op payload serializer remains a
    // successful no-op and does not create a cache file.
    bool SerializeToFile(const std::filesystem::path& path) const {
        std::string error;
        const bool saved = SerializeCacheFileAtomically(
            path,
            cacheDeviceIdentity_,
            name(),
            [this](const std::filesystem::path& payloadPath) {
                return SerializePayloadToFile(payloadPath);
            },
            &error);
        if (!saved) {
            std::cerr << "[CacheCodec] Failed to save " << name() << " at " << path.string()
                      << ": " << error << '\n';
        }
        return saved;
    }

    // Validate the complete envelope before handing its bytes to the cacher-specific parser.
    // A rejected payload is cleared so partially parsed entries can never be used.
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) {
        std::string error;
        const auto status = DeserializeCacheFile(
            path,
            cacheDeviceIdentity_,
            name(),
            [this, device](const std::filesystem::path& payloadPath) {
                return DeserializePayloadFromFile(payloadPath, device);
            },
            &error);
        if (status == CacheFileStatus::Rejected || status == CacheFileStatus::IoError) {
            std::cerr << "[CacheCodec] Rejected " << name() << " at " << path.string()
                      << ": " << error << " (will rebuild)\n";
            Clear();
        }
        return status == CacheFileStatus::Loaded;
    }

    // Set by DeviceRegistry for device-scoped cachers. Global cachers retain identity zero.
    void SetCacheDeviceIdentity(std::uint64_t identity) noexcept { cacheDeviceIdentity_ = identity; }

    // Return human readable name for diagnostics
    virtual std::string_view name() const noexcept = 0;

    // Owning MainCacher, set by MainCacher when it creates this cacher via a factory.
    // Lets a cacher reach sibling cachers (e.g. a pipeline cacher fetching its layout cacher)
    // without reaching the former process-wide MainCacher::Instance() singleton (AR#8).
    void SetMainCacher(MainCacher* owner) noexcept { mainCacher_ = owner; }
    MainCacher* GetMainCacher() const noexcept { return mainCacher_; }

protected:
    // Cacher-specific byte format. These methods only see unique temporary payload paths; callers
    // persist through SerializeToFile/DeserializeFromFile above.
    virtual bool SerializePayloadToFile(const std::filesystem::path& path) const = 0;
    virtual bool DeserializePayloadFromFile(const std::filesystem::path& path, void* device) = 0;

    MainCacher* mainCacher_ = nullptr;  // non-owning

private:
    std::uint64_t cacheDeviceIdentity_ = 0;
};

} // namespace CashSystem
