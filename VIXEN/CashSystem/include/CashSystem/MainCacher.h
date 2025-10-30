#pragma once

#include "Headers.h"

#include "CacherBase.h"
#include "TypeRegistry.h"
#include "TypedCacher.h"
#include "DeviceIdentifier.h"

#include <typeindex>
#include <typeinfo>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::EventBus {
    class MessageBus;
    using EventSubscriptionID = uint32_t;
}

namespace CashSystem {

// Forward declarations for cacher types
class ShaderModuleCacher;
class TextureCacher;
class DescriptorCacher;
class PipelineCacher;

/**
 * @brief MainCacher - Hybrid caching system supporting both device-dependent and device-independent caching
 * 
 * Manages multiple caching modes:
 * 1. Device-dependent: Device-specific registries for Vulkan resources
 * 2. Device-independent: Global shared registry for non-Vulkan resources
 * 
 * Benefits:
 * - Shader compilation caching can be shared across all devices
 * - Device-specific resources remain isolated per device
 * - Single unified API for both caching types
 */
class MainCacher {
public:
    static MainCacher& Instance();

    /**
     * @brief Initialize the MainCacher and subscribe to device invalidation events
     *
     * @param messageBus Optional MessageBus to subscribe to device invalidation events
     */
    void Initialize(Vixen::EventBus::MessageBus* messageBus = nullptr);

    /**
     * @brief Register a new cacher factory for a specific resource type
     * 
     * @tparam CacherT The specific cacher type (e.g., ShaderModuleCacher, ShaderCompilationCacher)
     * @tparam ResourceT The resource type being cached
     * @tparam CreateInfoT The creation parameters type
     * @param typeIndex std::type_index of the resource type
     * @param name Human-readable name for diagnostics
     * @param isDeviceDependent Whether this cacher requires device context
     * 
     * @note This registration applies to appropriate registries based on device dependency
     */
    template<typename CacherT, typename ResourceT, typename CreateInfoT>
    void RegisterCacher(
        std::type_index typeIndex,
        std::string_view name,
        bool isDeviceDependent = true  // Default: device-dependent for Vulkan resources
    ) {
        static_assert(std::is_base_of_v<TypedCacher<ResourceT, CreateInfoT>, CacherT>,
                      "CacherT must inherit from TypedCacher");
                      
        std::lock_guard lock(m_globalRegistryMutex);
        
        // Check if already registered
        auto existingIt = m_globalFactories.find(typeIndex);
        if (existingIt != m_globalFactories.end()) {
            throw std::runtime_error("Type already registered: " + std::string(name));
        }
        
        m_globalFactories[typeIndex] = [this]() -> std::unique_ptr<CacherBase> {
            return std::make_unique<CacherT>();
        };
        m_globalNames[typeIndex] = name;
        m_deviceDependency[typeIndex] = isDeviceDependent;
    }

    /**
     * @brief Get or create a device-dependent cacher
     * 
     * @tparam CacherT The specific cacher type
     * @tparam ResourceT The resource type being cached  
     * @tparam CreateInfoT The creation parameters type
     * @param typeIndex std::type_index of the resource type
     * @param device VulkanDevice pointer - determines which device registry to use
     * @return Pointer to the cacher, or nullptr if not registered or not device-dependent
     */
    template<typename CacherT, typename ResourceT, typename CreateInfoT>
    CacherT* GetDeviceDependentCacher(std::type_index typeIndex, Vixen::Vulkan::Resources::VulkanDevice* device) {
        static_assert(std::is_base_of_v<TypedCacher<ResourceT, CreateInfoT>, CacherT>,
                      "CacherT must inherit from TypedCacher");
                      
        if (!device) {
            return nullptr;
        }
        
        // Check if this type is registered as device-dependent
        std::shared_lock dependencyLock(m_globalRegistryMutex);
        auto dependencyIt = m_deviceDependency.find(typeIndex);
        if (dependencyIt == m_deviceDependency.end() || !dependencyIt->second) {
            return nullptr;  // Not registered or not device-dependent
        }
        dependencyLock.unlock();
        
        // Get or create device registry
        auto& deviceRegistry = GetOrCreateDeviceRegistry(device);
        
        // Get or create the specific cacher within this device registry
        auto* typedCacher = deviceRegistry.GetOrCreateCacher<CacherT>(
            typeIndex,
            [this, &deviceRegistry, typeIndex](std::unique_ptr<CacherT> newCacher) -> CacherT* {
                // Initialize device for the new cacher
                newCacher->Initialize(deviceRegistry.GetDevice());
                
                std::lock_guard lock(m_deviceRegistriesMutex);
                auto& cachedCacher = deviceRegistry.m_deviceCachers.emplace_back(std::move(newCacher));
                return dynamic_cast<CacherT*>(cachedCacher.get());
            }
        );
        
        if (typedCacher && !typedCacher->IsInitialized()) {
            typedCacher->Initialize(deviceRegistry.GetDevice());
        }
        
        return typedCacher;
    }

    /**
     * @brief Get or create a device-independent cacher
     * 
     * @tparam CacherT The specific cacher type
     * @tparam ResourceT The resource type being cached  
     * @tparam CreateInfoT The creation parameters type
     * @param typeIndex std::type_index of the resource type
     * @return Pointer to the global shared cacher, or nullptr if not registered or device-dependent
     */
    template<typename CacherT, typename ResourceT, typename CreateInfoT>
    CacherT* GetDeviceIndependentCacher(std::type_index typeIndex) {
        static_assert(std::is_base_of_v<TypedCacher<ResourceT, CreateInfoT>, CacherT>,
                      "CacherT must inherit from TypedCacher");
                      
        std::shared_lock dependencyLock(m_globalRegistryMutex);
        auto dependencyIt = m_deviceDependency.find(typeIndex);
        if (dependencyIt == m_deviceDependency.end() || dependencyIt->second) {
            return nullptr;  // Not registered or device-dependent
        }
        dependencyLock.unlock();
        
        // Get or create in global shared registry
        std::lock_guard globalLock(m_globalRegistryMutex);
        
        auto it = m_globalCachers.find(typeIndex);
        if (it != m_globalCachers.end()) {
            return dynamic_cast<CacherT*>(it->second.get());
        }
        
        auto factoryIt = m_globalFactories.find(typeIndex);
        if (factoryIt == m_globalFactories.end()) {
            return nullptr;  // Type not registered
        }
        
        // Create new global cacher
        auto newCacher = factoryIt->second();
        auto* typedCacher = dynamic_cast<CacherT*>(newCacher.get());
        
        if (!typedCacher) {
            throw std::runtime_error(
                "Factory returned wrong type for: " + GetTypeName(typeIndex)
            );
        }
        
        // Device-independent cachers don't need device initialization
        m_globalCachers[typeIndex] = std::move(newCacher);
        return typedCacher;
    }

    /**
     * @brief Convenience method that automatically chooses device-dependent vs independent
     * 
     * @tparam CacherT The specific cacher type
     * @tparam ResourceT The resource type being cached  
     * @tparam CreateInfoT The creation parameters type
     * @param typeIndex std::type_index of the resource type
     * @param device Optional VulkanDevice pointer (ignored for device-independent cachers)
     * @return Pointer to the appropriate cacher
     */
    template<typename CacherT, typename ResourceT, typename CreateInfoT>
    CacherT* GetCacher(
        std::type_index typeIndex,
        Vixen::Vulkan::Resources::VulkanDevice* device = nullptr
    ) {
        static_assert(std::is_base_of_v<TypedCacher<ResourceT, CreateInfoT>, CacherT>,
                      "CacherT must inherit from TypedCacher");
                      
        // Check registration and dependency
        std::shared_lock dependencyLock(m_globalRegistryMutex);
        auto dependencyIt = m_deviceDependency.find(typeIndex);
        if (dependencyIt == m_deviceDependency.end()) {
            return nullptr;  // Not registered
        }
        dependencyLock.unlock();
        
        if (dependencyIt->second) {
            // Device-dependent
            return GetDeviceDependentCacher<CacherT, ResourceT, CreateInfoT>(typeIndex, device);
        } else {
            // Device-independent
            return GetDeviceIndependentCacher<CacherT, ResourceT, CreateInfoT>(typeIndex);
        }
    }

    /**
     * @brief Check if a type is registered
     */
    bool IsRegistered(std::type_index typeIndex) const {
        std::shared_lock lock(m_globalRegistryMutex);
        return m_globalFactories.find(typeIndex) != m_globalFactories.end();
    }

    /**
     * @brief Check if a registered type is device-dependent
     */
    bool IsDeviceDependent(std::type_index typeIndex) const {
        std::shared_lock lock(m_globalRegistryMutex);
        auto it = m_deviceDependency.find(typeIndex);
        return (it != m_deviceDependency.end()) ? it->second : true;  // Default to device-dependent
    }

    /**
     * @brief Get human-readable name for a registered type
     */
    std::string GetTypeName(std::type_index typeIndex) const {
        std::shared_lock lock(m_globalRegistryMutex);
        auto it = m_globalNames.find(typeIndex);
        return (it != m_globalNames.end()) ? std::string(it->second) : "UnknownType";
    }

    /**
     * @brief Clear all caches for a specific device
     */
    void ClearDeviceCaches(Vixen::Vulkan::Resources::VulkanDevice* device) {
        if (!device) {
            return;
        }
        
        std::lock_guard lock(m_deviceRegistriesMutex);
        auto deviceId = DeviceIdentifier(device);
        m_deviceRegistries.erase(deviceId);
    }

    /**
     * @brief Clear all global (device-independent) caches
     */
    void ClearGlobalCaches() {
        std::lock_guard lock(m_globalRegistryMutex);
        m_globalCachers.clear();
    }

    /**
     * @brief Clear all caches for all devices and global caches
     */
    void ClearAll() {
        std::lock_guard deviceLock(m_deviceRegistriesMutex);
        std::lock_guard globalLock(m_globalRegistryMutex);
        
        m_deviceRegistries.clear();
        m_globalCachers.clear();
        // Keep global factories and names for continued registration
    }

    /**
     * @brief Save all caches to disk (organized by device and global)
     */
    bool SaveAll(const std::filesystem::path& directory) const {
        bool success = true;
        
        // Save device-specific caches
        {
            std::shared_lock deviceLock(m_deviceRegistriesMutex);
            for (const auto& [deviceId, registry] : m_deviceRegistries) {
                auto deviceDir = directory / "devices" / deviceId.GetDescription();
                if (!std::filesystem::exists(deviceDir)) {
                    std::filesystem::create_directories(deviceDir);
                }
                success &= registry.SaveAll(deviceDir);
            }
        }
        
        // Save global caches
        {
            auto globalDir = directory / "global";
            if (!std::filesystem::exists(globalDir)) {
                std::filesystem::create_directories(globalDir);
            }
            success &= SaveGlobalCaches(globalDir);
        }
        
        return success;
    }

    /**
     * @brief Load all caches from disk
     */
    bool LoadAll(const std::filesystem::path& directory) {
        bool success = true;
        
        // Load device-specific caches
        auto devicesDir = directory / "devices";
        if (std::filesystem::exists(devicesDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(devicesDir)) {
                if (entry.is_directory()) {
                    auto deviceDir = entry.path();
                    auto deviceId = DeviceIdentifier::FromDirectoryName(deviceDir.filename().string());
                    
                    if (deviceId.IsValid()) {
                        auto& registry = GetOrCreateDeviceRegistry(deviceId);
                        success &= registry.LoadAll(deviceDir);
                    }
                }
            }
        }
        
        // Load global caches
        auto globalDir = directory / "global";
        if (std::filesystem::exists(globalDir)) {
            success &= LoadGlobalCaches(globalDir);
        }
        
        return success;
    }

    /**
     * @brief Get list of all registered cache types
     */
    std::vector<std::string> GetRegisteredTypes() const {
        std::shared_lock lock(m_globalRegistryMutex);
        std::vector<std::string> types;
        
        for (const auto& [typeIndex, name] : m_globalNames) {
            types.push_back(std::string(name));
        }
        
        return types;
    }

    /**
     * @brief Get list of active device registries
     */
    std::vector<std::string> GetActiveDevices() const {
        std::shared_lock lock(m_deviceRegistriesMutex);
        std::vector<std::string> devices;
        
        for (const auto& [deviceId, registry] : m_deviceRegistries) {
            if (registry.IsInitialized()) {
                devices.push_back(deviceId.GetDescription());
            }
        }
        
        return devices;
    }

    /**
     * @brief Get cache statistics
     */
    struct CacheStats {
        size_t globalCaches = 0;
        size_t deviceRegistries = 0;
        size_t totalDeviceCaches = 0;
    };

    CacheStats GetStats() const {
        std::shared_lock deviceLock(m_deviceRegistriesMutex);
        std::shared_lock globalLock(m_globalRegistryMutex);
        
        CacheStats stats;
        stats.globalCaches = m_globalCachers.size();
        stats.deviceRegistries = m_deviceRegistries.size();
        
        for (const auto& [deviceId, registry] : m_deviceRegistries) {
            stats.totalDeviceCaches += registry.GetCacheSize();
        }
        
        return stats;
    }

    /* Legacy compatibility - use GetCacher() template instead
    ShaderModuleCacher* GetShaderModuleCacher(Vixen::Vulkan::Resources::VulkanDevice* device = nullptr);
    TextureCacher* GetTextureCacher(Vixen::Vulkan::Resources::VulkanDevice* device = nullptr);
    DescriptorCacher* GetDescriptorCacher(Vixen::Vulkan::Resources::VulkanDevice* device = nullptr);
    PipelineCacher* GetPipelineCacher(Vixen::Vulkan::Resources::VulkanDevice* device = nullptr);
    */

private:
    MainCacher() = default;
    ~MainCacher();
    MainCacher(const MainCacher&) = delete;
    MainCacher& operator=(const MainCacher&) = delete;

    // Event bus integration for device invalidation
    Vixen::EventBus::MessageBus* m_messageBus = nullptr;
    Vixen::EventBus::EventSubscriptionID m_deviceInvalidationSubscription = 0;
    
    // Device registry management
    DeviceRegistry& GetOrCreateDeviceRegistry(::Vixen::Vulkan::Resources::VulkanDevice* device);
    DeviceRegistry& GetOrCreateDeviceRegistry(const DeviceIdentifier& deviceId);
    
    // Global cache management
    bool SaveGlobalCaches(const std::filesystem::path& directory) const;
    bool LoadGlobalCaches(const std::filesystem::path& directory);
    
    // Global type registration (shared across all devices)
    using GlobalFactory = std::function<std::unique_ptr<CacherBase>()>;
    std::unordered_map<std::type_index, GlobalFactory> m_globalFactories;
    std::unordered_map<std::type_index, std::string_view> m_globalNames;
    std::unordered_map<std::type_index, bool> m_deviceDependency;  // true = device-dependent, false = device-independent
    
    // Global device-independent caches
    std::unordered_map<std::type_index, std::unique_ptr<CacherBase>> m_globalCachers;
    
    // Device-specific registries
    mutable std::shared_mutex m_deviceRegistriesMutex;
    std::unordered_map<DeviceIdentifier, DeviceRegistry, DeviceIdentifier::DeviceHasher> m_deviceRegistries;
    
    // Thread safety for global registry
    mutable std::shared_mutex m_globalRegistryMutex;
};

} // namespace CashSystem
