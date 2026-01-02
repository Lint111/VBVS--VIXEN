#pragma once

#include "Headers.h"

#include "CacherBase.h"
#include "TypeRegistry.h"
#include "TypedCacher.h"
#include "DeviceIdentifier.h"
#include "ILoggable.h"

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
#include <fstream>
#include <future>
#include <optional>
#include <future>

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace Vixen::EventBus {
    class MessageBus;
    using EventSubscriptionID = uint32_t;
}

namespace ResourceManagement {
    class DeviceBudgetManager;
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
 * - Integrated logging via ILoggable (sub-cachers can register as children)
 */
class MainCacher : public ILoggable {
public:
    static MainCacher& Instance();

    /**
     * @brief Initialize the MainCacher and subscribe to device invalidation events
     *
     * @param messageBus Optional MessageBus to subscribe to device invalidation events
     */
    void Initialize(Vixen::EventBus::MessageBus* messageBus = nullptr);

       /**
     * @brief Shutdown and unsubscribe from message bus
     * Call this before MessageBus is destroyed to prevent use-after-free
     */
    void Shutdown();

    /**
     * @brief Cleanup device-independent (global) caches
     *
     * Called during RenderGraph shutdown to cleanup global shared resources.
     * Note: Device-dependent caches are cleaned up by DeviceNode::CleanupImpl().
     */
    void CleanupGlobalCaches();

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
            return;  // Already registered, skip
        }

        // Register by type_index
        m_globalFactories[typeIndex] = []() -> std::unique_ptr<CacherBase> {
            return std::make_unique<CacherT>();
        };
        m_globalNames[typeIndex] = name;
        m_deviceDependency[typeIndex] = isDeviceDependent;

        // ALSO register by name for future deserialization
        std::string nameStr(name);
        m_nameToFactory[nameStr] = []() -> std::unique_ptr<CacherBase> {
            return std::make_unique<CacherT>();
        };
        m_nameToDeviceDependency[nameStr] = isDeviceDependent;

        LOG_INFO("[MainCacher::RegisterCacher] Registered " + std::string(name) + (isDeviceDependent ? " (device-dependent)" : " (global)"));
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

        // Check if cacher already exists in device registry
        auto* typedCacher = deviceRegistry.GetOrCreateCacher<CacherT>(typeIndex, nullptr);

        if (!typedCacher) {
            // Cacher doesn't exist - create using global factory
            std::lock_guard globalLock(m_globalRegistryMutex);
            auto factoryIt = m_globalFactories.find(typeIndex);
            if (factoryIt == m_globalFactories.end()) {
                return nullptr;  // Factory not registered (shouldn't happen after registration check)
            }

            // Create new cacher instance using global factory
            auto newCacher = factoryIt->second();
            typedCacher = dynamic_cast<CacherT*>(newCacher.get());

            if (!typedCacher) {
                return nullptr;  // Factory returned wrong type
            }

            // Initialize with device
            typedCacher->Initialize(deviceRegistry.GetDevice());

            // Store in device registry
            std::lock_guard deviceLock(m_deviceRegistriesMutex);
            deviceRegistry.m_deviceCachers.emplace_back(std::move(newCacher));

            // Lazy deserialization: Load from disk if cache file exists
            std::string cacheName(typedCacher->name());
            std::filesystem::path cacheDir = std::filesystem::path("cache") / "devices" / deviceRegistry.GetDeviceIdentifier().GetDescription();
            std::filesystem::path cacheFile = cacheDir / (cacheName + ".cache");

            if (std::filesystem::exists(cacheFile)) {
                LOG_DEBUG("[MainCacher] Lazy-loading cache for " + cacheName + " from " + cacheFile.string());
                bool loaded = typedCacher->DeserializeFromFile(cacheFile, deviceRegistry.GetDevice());
                if (loaded) {
                    LOG_DEBUG("[MainCacher] Successfully lazy-loaded " + cacheName);
                } else {
                    LOG_DEBUG("[MainCacher] Failed to lazy-load " + cacheName + " (will recreate)");
                }
            }
        }

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
     *
     * Calls Cleanup() on all device-dependent cachers for this device,
     * then removes the device registry.
     */
    void ClearDeviceCaches(Vixen::Vulkan::Resources::VulkanDevice* device) {
        if (!device) {
            return;
        }

        std::lock_guard lock(m_deviceRegistriesMutex);
        auto deviceId = DeviceIdentifier(device);

        auto it = m_deviceRegistries.find(deviceId);
        if (it != m_deviceRegistries.end()) {
            // Call Cleanup() on all cachers in this device registry
            for (auto& cacher : it->second.m_deviceCachers) {
                if (cacher) {
                    cacher->Cleanup();
                }
            }
            // Now erase the registry
            m_deviceRegistries.erase(it);
        }
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
     * @note Synchronous version - blocks until complete
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
     * @brief Save all caches to disk asynchronously (non-blocking)
     * @return Future that resolves to true if all saves succeeded
     * @note Launches parallel tasks for each device registry and global caches
     */
    std::future<bool> SaveAllAsync(const std::filesystem::path& directory) const {
        return std::async(std::launch::async, [this, directory]() {
            std::vector<std::future<bool>> futures;

            // Launch parallel save tasks for each device registry
            {
                std::shared_lock deviceLock(m_deviceRegistriesMutex);
                for (const auto& [deviceId, registry] : m_deviceRegistries) {
                    auto deviceDir = directory / "devices" / deviceId.GetDescription();

                    // Capture by value to avoid dangling references
                    futures.push_back(std::async(std::launch::async,
                        [deviceDir, &registry]() {
                            std::filesystem::create_directories(deviceDir);
                            return registry.SaveAll(deviceDir);
                        }
                    ));
                }
            }

            // Launch global cache save
            futures.push_back(std::async(std::launch::async, [this, directory]() {
                auto globalDir = directory / "global";
                std::filesystem::create_directories(globalDir);
                return SaveGlobalCaches(globalDir);
            }));

            // Wait for all saves to complete
            bool success = true;
            for (auto& future : futures) {
                success &= future.get();
            }

            return success;
        });
    }

    /**
     * @brief Load all caches from disk (synchronous)
     * @note Blocks until all caches are loaded
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
     * @brief Load all caches from disk asynchronously (non-blocking)
     * @return Future that resolves to true if all loads succeeded
     * @note Launches parallel tasks for each device registry and global caches
     */
    std::future<bool> LoadAllAsync(const std::filesystem::path& directory) {
        return std::async(std::launch::async, [this, directory]() {
            std::vector<std::future<bool>> futures;

            // Collect device directories to load
            auto devicesDir = directory / "devices";
            std::vector<std::pair<DeviceIdentifier, std::filesystem::path>> deviceDirs;

            if (std::filesystem::exists(devicesDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(devicesDir)) {
                    if (entry.is_directory()) {
                        auto deviceDir = entry.path();
                        auto deviceId = DeviceIdentifier::FromDirectoryName(deviceDir.filename().string());

                        if (deviceId.IsValid()) {
                            deviceDirs.emplace_back(deviceId, deviceDir);
                        }
                    }
                }
            }

            // Launch parallel load tasks for each device registry
            for (const auto& [deviceId, deviceDir] : deviceDirs) {
                futures.push_back(std::async(std::launch::async,
                    [this, deviceId, deviceDir]() {
                        auto& registry = GetOrCreateDeviceRegistry(deviceId);
                        return registry.LoadAll(deviceDir);
                    }
                ));
            }

            // Launch global cache load
            auto globalDir = directory / "global";
            if (std::filesystem::exists(globalDir)) {
                futures.push_back(std::async(std::launch::async, [this, globalDir]() {
                    return LoadGlobalCaches(globalDir);
                }));
            }

            // Wait for all loads to complete
            bool success = true;
            for (auto& future : futures) {
                success &= future.get();
            }

            return success;
        });
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

    /**
     * @brief Set budget manager for GPU allocation tracking
     *
     * Propagates the budget manager to all existing device registries.
     * Cachers can access this via DeviceRegistry::GetBudgetManager().
     *
     * @param manager DeviceBudgetManager pointer (externally owned)
     */
    void SetBudgetManager(ResourceManagement::DeviceBudgetManager* manager);

    /**
     * @brief Get current budget manager
     * @return DeviceBudgetManager pointer, or nullptr if not configured
     */
    ResourceManagement::DeviceBudgetManager* GetBudgetManager() const { return m_budgetManager; }

    /**
     * @brief Create a cacher instance by name (for manifest-based deserialization)
     *
     * @param name Cacher name from manifest file
     * @param device VulkanDevice pointer (required for device-dependent cachers, ignored otherwise)
     * @param registry DeviceRegistry reference to add the cacher to (for device-dependent cachers)
     * @return Pointer to created cacher, or nullptr if name not registered
     *
     * @note This enables LoadAll to create cachers before deserializing from disk
     */
    CacherBase* CreateCacherByName(
        const std::string& name,
        ::Vixen::Vulkan::Resources::VulkanDevice* device,
        DeviceRegistry& registry
    );

    /**
     * @brief Get or create a device registry for the specified device
     *
     * This should be called by DeviceNode during Compile() to register the device
     * with the caching system. It creates a device registry that will hold all
     * device-dependent cachers for this device.
     *
     * @param device VulkanDevice pointer
     * @return Reference to the device registry
     */
    DeviceRegistry& GetOrCreateDeviceRegistry(::Vixen::Vulkan::Resources::VulkanDevice* device);

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

    // Sprint 4 Phase D: Budget manager for GPU allocation tracking
    ResourceManagement::DeviceBudgetManager* m_budgetManager = nullptr;

    // Device registry management (private overload)
    DeviceRegistry& GetOrCreateDeviceRegistry(const DeviceIdentifier& deviceId);
    
    // Global cache management
    bool SaveGlobalCaches(const std::filesystem::path& directory) const {
        LOG_INFO("[MainCacher] Saving global caches to " + directory.string());

        // Save manifest
        auto manifestPath = directory / "manifest.txt";
        std::ofstream manifest(manifestPath);
        if (!manifest) {
            LOG_ERROR("[MainCacher] Failed to create global cacher manifest");
            return false;
        }

        std::shared_lock lock(m_globalRegistryMutex);
        for (const auto& [typeIndex, cacher] : m_globalCachers) {
            if (cacher) {
                manifest << cacher->name() << "\n";
            }
        }
        manifest.close();
        LOG_INFO("[MainCacher] Saved global cacher manifest with " + std::to_string(m_globalCachers.size()) + " entries");

        // Launch parallel save for each global cacher
        std::vector<std::future<bool>> futures;

        for (const auto& [typeIndex, cacher] : m_globalCachers) {
            if (cacher) {
                auto cacheName = std::string(cacher->name());
                auto cacheFile = directory / (cacheName + ".cache");

                // Capture raw pointer for thread safety (cacher is owned by m_globalCachers which is stable)
                CacherBase* cacherPtr = cacher.get();

                LOG_DEBUG("[MainCacher] Launching async save for " + cacheName);
                futures.push_back(std::async(std::launch::async,
                    [cacherPtr, cacheFile]() {
                        return cacherPtr->SerializeToFile(cacheFile);
                    }
                ));
            }
        }

        // Wait for all saves to complete
        bool success = true;
        for (auto& future : futures) {
            success &= future.get();
        }

        LOG_INFO("[MainCacher] Global cache save " + std::string(success ? "succeeded" : "failed"));
        return success;
    }

    bool LoadGlobalCaches(const std::filesystem::path& directory) {
        LOG_INFO("[MainCacher] Loading global caches from " + directory.string());

        if (!std::filesystem::exists(directory)) {
            LOG_DEBUG("[MainCacher] No global cache directory found");
            return true;  // Not an error
        }

        // Launch parallel load for each global cacher
        std::vector<std::future<bool>> futures;

        std::shared_lock lock(m_globalRegistryMutex);
        for (const auto& [typeIndex, cacher] : m_globalCachers) {
            if (cacher) {
                auto cacheName = std::string(cacher->name());
                auto cacheFile = directory / (cacheName + ".cache");

                if (std::filesystem::exists(cacheFile)) {
                    // Capture raw pointer for thread safety (cacher is owned by m_globalCachers which is stable)
                    CacherBase* cacherPtr = cacher.get();

                    LOG_DEBUG("[MainCacher] Launching async load for " + cacheName);
                    futures.push_back(std::async(std::launch::async,
                        [cacherPtr, cacheFile]() {
                            return cacherPtr->DeserializeFromFile(cacheFile, nullptr);
                        }
                    ));
                }
            }
        }

        // Wait for all loads to complete
        bool success = true;
        for (auto& future : futures) {
            success &= future.get();
        }

        LOG_INFO("[MainCacher] Global cache load " + std::string(success ? "succeeded" : "failed"));
        return success;
    }
    
    // Global type registration (shared across all devices)
    using GlobalFactory = std::function<std::unique_ptr<CacherBase>()>;
    std::unordered_map<std::type_index, GlobalFactory> m_globalFactories;
    std::unordered_map<std::type_index, std::string_view> m_globalNames;
    std::unordered_map<std::type_index, bool> m_deviceDependency;  // true = device-dependent, false = device-independent

    // Name-based registration (enables manifest-based deserialization)
    std::unordered_map<std::string, GlobalFactory> m_nameToFactory;
    std::unordered_map<std::string, bool> m_nameToDeviceDependency;
    
    // Global device-independent caches
    std::unordered_map<std::type_index, std::unique_ptr<CacherBase>> m_globalCachers;
    
    // Device-specific registries
    mutable std::shared_mutex m_deviceRegistriesMutex;
    std::unordered_map<DeviceIdentifier, DeviceRegistry, DeviceIdentifier::DeviceHasher> m_deviceRegistries;
    
    // Thread safety for global registry
    mutable std::shared_mutex m_globalRegistryMutex;
};

} // namespace CashSystem
