#pragma once

#include "CacherBase.h"
#include "ILoggable.h"

#include <shared_mutex>
#include <unordered_map>
#include <future>
#include <optional>
#include <vector>
#include <cstdint>
#include <typeindex>

// Forward declarations for type safety
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {

// TypedCacher<D, CI>
// D: resource wrapper type (e.g., PipelineWrapper)
// CI: creation-info struct used to create D
template<typename D, typename CI>
class TypedCacher : public CacherBase, public ILoggable {
public:
    using ResourceT = D;
    using CreateInfoT = CI;
    using PtrT = std::shared_ptr<ResourceT>;

    TypedCacher() : m_device(nullptr), m_initialized(false) {}
    virtual ~TypedCacher() = default;

    /**
     * @brief Initialize the cacher with device context
     */
    virtual void Initialize(Vixen::Vulkan::Resources::VulkanDevice* device) {
        m_device = device;
        m_initialized = true;
        OnInitialize();
    }

    /**
     * @brief Check if the cacher has been initialized
     * @note For device-dependent cachers, both flag and device pointer must be valid
     */
    bool IsInitialized() const noexcept { return m_initialized && m_device != nullptr; }

    /**
     * @brief Get the device context
     */
    Vixen::Vulkan::Resources::VulkanDevice* GetDevice() const noexcept { return m_device; }

    // Typed convenience API — callers should use this
    PtrT GetOrCreate(const CreateInfoT& ci) {
        auto key = ComputeKey(ci);

        { // fast path read
            std::shared_lock rlock(m_lock);
            auto it = m_entries.find(key);
            if (it != m_entries.end()) {
                std::cout << "[" << name() << "] CACHE HIT (key=" << key << ", entries=" << m_entries.size() << ")" << std::endl;
                return it->second.resource;
            }
            auto pit = m_pending.find(key);
            if (pit != m_pending.end()) return pit->second.get();
        }

        // try to create
        std::unique_lock wlock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            std::cout << "[" << name() << "] CACHE HIT (key=" << key << ", entries=" << m_entries.size() << ")" << std::endl;
            return it->second.resource;
        }
        auto pit = m_pending.find(key);
        if (pit != m_pending.end()) return pit->second.get();

        std::cout << "[" << name() << "] CACHE MISS (key=" << key << ", entries=" << m_entries.size() << ")" << std::endl;

        // create a promise to signal others
        auto prom = std::make_shared<std::promise<PtrT>>();
        std::shared_future<PtrT> fut = prom->get_future().share();
        m_pending.emplace(key, fut);

        // unlock during heavy create work
        wlock.unlock();

        PtrT created = Create(ci);

        // re-lock and insert
        wlock.lock();
        CacheEntry e; e.key = key; e.ci = ci; e.resource = created;
        m_entries.emplace(key, std::move(e));

        // fulfill promise and remove pending
        prom->set_value(created);
        m_pending.erase(key);
        return created;
    }

    // CacherBase overrides (typed mapping)
    bool Has(std::uint64_t key) const noexcept override {
        std::shared_lock lock(m_lock);
        return m_entries.find(key) != m_entries.end();
    }

    std::shared_ptr<void> Get(std::uint64_t key) override {
        std::shared_lock lock(m_lock);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) return it->second.resource;
        return nullptr;
    }

    std::shared_ptr<void> Insert(std::uint64_t key, const std::any& creationParams) override {
        // try to cast creationParams to CI
        try {
            auto ci = std::any_cast<CreateInfoT>(creationParams);
            PtrT created = Create(ci);
            std::unique_lock lock(m_lock);
            CacheEntry e; e.key = key; e.ci = ci; e.resource = created;
            m_entries.emplace(key, std::move(e));
            return created;
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

    void Erase(std::uint64_t key) override {
        std::unique_lock lock(m_lock);
        m_entries.erase(key);
    }

    void Clear() override {
        std::unique_lock lock(m_lock);
        m_entries.clear();
        m_pending.clear();
    }

    void Cleanup() override {
        // Default implementation: just clear the cache
        // Derived classes should override to destroy Vulkan resources before clearing
        Clear();
    }

    bool SerializeToFile(const std::filesystem::path& path) const override {
        // default stub: derived classes should override if they need real serialization
        (void)path;
        return true;
    }

    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override {
        (void)path; (void)device;
        return true;
    }

    std::string_view name() const noexcept override { return "TypedCacher"; }

protected:
    // Derived must implement how to create and how to compute keys
    virtual PtrT Create(const CreateInfoT& ci) = 0;
    virtual std::uint64_t ComputeKey(const CreateInfoT& ci) const = 0;

    struct CacheEntry {
        std::uint64_t key;
        CreateInfoT ci;
        PtrT resource;
    };

    mutable std::shared_mutex m_lock;
    std::unordered_map<std::uint64_t, CacheEntry> m_entries;
    std::unordered_map<std::uint64_t, std::shared_future<PtrT>> m_pending;

    // Device context and initialization tracking
    Vixen::Vulkan::Resources::VulkanDevice* m_device;
    bool m_initialized;
    
    // Hook for derived classes to perform initialization
    virtual void OnInitialize() {}
};

} // namespace CashSystem

