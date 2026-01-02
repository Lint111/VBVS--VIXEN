#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <filesystem>
#include <vector>
#include <typeindex>
#include <typeinfo>
#include "CacherBase.h"
#include "ILoggable.h"

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace ResourceManagement {
    class DeviceBudgetManager;
}

namespace CashSystem {

/**
 * @brief Device identifier for multi-device caching systems
 * 
 * Provides a consistent way to identify and hash Vulkan devices
 * for device-specific cache registries.
 */
class DeviceIdentifier {
public:
    /**
     * @brief Create device identifier from VulkanDevice
     */
    explicit DeviceIdentifier(const Vixen::Vulkan::Resources::VulkanDevice* device);
    
    /**
     * @brief Create device identifier from device index (legacy/multi-GPU systems)
     */
    explicit DeviceIdentifier(uint32_t deviceIndex);
    
    /**
     * @brief Create device identifier from custom hash
     */
    explicit DeviceIdentifier(std::uint64_t deviceHash);
    
    /**
     * @brief Get the hash identifier for this device
     */
    std::uint64_t GetHash() const noexcept { return m_deviceHash; }
    
    /**
     * @brief Get device index (if applicable)
     */
    uint32_t GetDeviceIndex() const noexcept { return m_deviceIndex; }
    
    /**
     * @brief Get human-readable device info
     */
    std::string GetDescription() const;
    
    /**
     * @brief Check if this is a valid device identifier
     */
    bool IsValid() const noexcept { return m_deviceHash != 0; }
    
    /**
     * @brief Equality comparison
     */
    bool operator==(const DeviceIdentifier& other) const noexcept {
        return m_deviceHash == other.m_deviceHash;
    }
    
    /**
     * @brief Inequality comparison
     */
    bool operator!=(const DeviceIdentifier& other) const noexcept {
        return !(*this == other);
    }
    
    /**
     * @brief Less-than comparison for use in containers
     */
    bool operator<(const DeviceIdentifier& other) const noexcept {
        return m_deviceHash < other.m_deviceHash;
    }
    
    /**
     * @brief Hash function for use in unordered containers
     */
    struct DeviceHasher {
        std::size_t operator()(const DeviceIdentifier& deviceId) const noexcept {
            return std::hash<std::uint64_t>{}(deviceId.m_deviceHash);
        }
    };

    /**
     * @brief Create device identifier from directory name
     */
    static DeviceIdentifier FromDirectoryName(const std::string& dirName);

private:
    std::uint64_t m_deviceHash;
    uint32_t m_deviceIndex;
    
    /**
     * @brief Generate device hash from VulkanDevice
     */
    static std::uint64_t GenerateDeviceHash(const Vixen::Vulkan::Resources::VulkanDevice* device);
};

/**
 * @brief Device registry entry for multi-device caching
 *
 * Encapsulates a complete caching system for a single Vulkan device
 */
class DeviceRegistry : public ILoggable {
    friend class MainCacher;  // MainCacher needs access to m_deviceCachers

public:
    DeviceRegistry() = delete;
    DeviceRegistry(const DeviceIdentifier& deviceId);
    
    /**
     * @brief Initialize the registry for a specific device
     */
    void Initialize(Vixen::Vulkan::Resources::VulkanDevice* device);
    
    /**
     * @brief Check if this registry is initialized
     */
    bool IsInitialized() const noexcept { return m_initialized; }
    
    /**
     * @brief Get the device identifier for this registry
     */
    const DeviceIdentifier& GetDeviceIdentifier() const noexcept { return m_deviceId; }
    
    /**
     * @brief Get the Vulkan device for this registry
     */
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const noexcept { return m_device; }
    
    /**
     * @brief Clear all caches for this device
     */
    void ClearAll();
    
    /**
     * @brief Save all caches to disk for this device
     */
    bool SaveAll(const std::filesystem::path& directory) const;
    
    /**
     * @brief Load all caches from disk for this device
     */
    bool LoadAll(const std::filesystem::path& directory);

    /**
     * @brief Get or create a typed cacher within this device registry
     */
    template<typename CacherT>
    CacherT* GetOrCreateCacher(
        [[maybe_unused]] std::type_index typeIndex,
        [[maybe_unused]] std::function<CacherT*(std::unique_ptr<CacherT>)> factory
    ) {
        // Check if already cached
        for (auto& cacher : m_deviceCachers) {
            if (auto* typed = dynamic_cast<CacherT*>(cacher.get())) {
                return typed;
            }
        }
        return nullptr;  // Not found - caller must create
    }

    /**
     * @brief Get the total cache size for this device
     */
    size_t GetCacheSize() const noexcept { return m_deviceCachers.size(); }

    /**
     * @brief Set budget manager for GPU allocation tracking
     * @param manager DeviceBudgetManager pointer (externally owned)
     */
    void SetBudgetManager(ResourceManagement::DeviceBudgetManager* manager) { m_budgetManager = manager; }

    /**
     * @brief Get budget manager for GPU allocation tracking
     * @return DeviceBudgetManager pointer, or nullptr if not configured
     */
    ResourceManagement::DeviceBudgetManager* GetBudgetManager() const { return m_budgetManager; }

private:
    DeviceIdentifier m_deviceId;
    Vixen::Vulkan::Resources::VulkanDevice* m_device;
    bool m_initialized;

    // Sprint 4 Phase D: Budget manager for GPU allocation tracking
    ResourceManagement::DeviceBudgetManager* m_budgetManager = nullptr;

    // TypeRegistry will be embedded here for device-specific type registration
    // This maintains the type-safe caching but per-device
    // For now, we'll use a placeholder until we refactor TypeRegistry
    std::vector<std::unique_ptr<CacherBase>> m_deviceCachers;

    void OnInitialize();
};

} // namespace CashSystem