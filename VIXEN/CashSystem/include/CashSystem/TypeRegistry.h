#pragma once

#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <memory>
#include <string_view>
#include <stdexcept>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

class CacherBase;

/**
 * @brief Type-based registry for managing cachers within a device context
 * 
 * This registry is designed to be embedded within DeviceRegistry and provides
 * the same dynamic registration capabilities but within a single device context.
 * 
 * Unlike the previous design, this is device-agnostic and gets its device
 * context from the containing DeviceRegistry.
 */
class TypeRegistry {
public:
    using CacherFactory = std::function<std::unique_ptr<CacherBase>()>;
    
    TypeRegistry() = default;
    
    /**
     * @brief Register a factory for a specific cached resource type
     * 
     * @param typeIndex std::type_index of the resource type being cached
     * @param factory Function that creates the appropriate cacher instance
     * @param name Human-readable name for diagnostics
     */
    void Register(
        std::type_index typeIndex,
        CacherFactory factory,
        std::string_view name
    ) {
        std::lock_guard lock(m_mutex);
        
        // Check if already registered
        if (m_factories.find(typeIndex) != m_factories.end()) {
            throw std::runtime_error("Type already registered: " + std::string(name));
        }
        
        m_factories[typeIndex] = std::move(factory);
        m_names[typeIndex] = name;
    }
    
    /**
     * @brief Create or retrieve an existing cacher for a given type within this device
     * 
     * @param typeIndex std::type_index of the resource type
     * @param device VulkanDevice pointer for this registry
     * @param getOrCreate Function to get existing or create new cacher
     * @return Pointer to the cacher, or nullptr if type not registered
     */
    template<typename CacherT>
    CacherT* GetOrCreateCacher(
        std::type_index typeIndex,
        Vixen::Vulkan::Resources::VulkanDevice* device,
        std::function<CacherT*(std::unique_ptr<CacherT>)> getOrCreate
    ) {
        std::lock_guard lock(m_mutex);
        
        auto it = m_cachers.find(typeIndex);
        if (it != m_cachers.end()) {
            return dynamic_cast<CacherT*>(it->second.get());
        }
        
        auto factoryIt = m_factories.find(typeIndex);
        if (factoryIt == m_factories.end()) {
            return nullptr; // Type not registered
        }
        
        // Create new cacher using factory
        auto newCacher = factoryIt->second();
        auto* typedCacher = dynamic_cast<CacherT*>(newCacher.get());
        
        if (!typedCacher) {
            throw std::runtime_error(
                "Factory returned wrong type for: " + GetName(typeIndex)
            );
        }
        
        // Store the created cacher
        m_cachers[typeIndex] = std::move(newCacher);
        return typedCacher;
    }
    
    /**
     * @brief Get cached cacher without creating (only if exists)
     */
    CacherBase* GetCacher(std::type_index typeIndex) const {
        std::shared_lock lock(m_mutex);
        
        auto it = m_cachers.find(typeIndex);
        return (it != m_cachers.end()) ? it->second.get() : nullptr;
    }
    
    /**
     * @brief Get human-readable name for a registered type
     */
    std::string GetName(std::type_index typeIndex) const {
        std::shared_lock lock(m_mutex);
        
        auto it = m_names.find(typeIndex);
        return (it != m_names.end()) ? std::string(it->second) : "UnknownType";
    }
    
    /**
     * @brief Check if a type is registered
     */
    bool IsRegistered(std::type_index typeIndex) const {
        std::shared_lock lock(m_mutex);
        return m_factories.find(typeIndex) != m_factories.end();
    }
    
    /**
     * @brief Clear all registrations and caches for this device
     */
    void Clear() {
        std::lock_guard lock(m_mutex);
        m_factories.clear();
        m_cachers.clear();
        m_names.clear();
    }
    
    /**
     * @brief Get all registered type names (for diagnostics)
     */
    std::vector<std::string> GetRegisteredTypes() const {
        std::shared_lock lock(m_mutex);
        std::vector<std::string> types;
        
        for (const auto& [typeIndex, name] : m_names) {
            types.push_back(std::string(name));
        }
        
        return types;
    }
    
    /**
     * @brief Get count of cached items for this device
     */
    size_t GetCacheSize() const {
        std::shared_lock lock(m_mutex);
        return m_cachers.size();
    }

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::type_index, CacherFactory> m_factories;
    std::unordered_map<std::type_index, std::unique_ptr<CacherBase>> m_cachers;
    std::unordered_map<std::type_index, std::string_view> m_names;
};

} // namespace CashSystem