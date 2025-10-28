#pragma once

#include "CacherBase.h"

#include <vector>
#include <memory>
#include <mutex>
#include <filesystem>

namespace CashSystem {

class MainCacher {
public:
    static MainCacher& Instance();

    // Register a cacher for a type name; called during engine init
    void RegisterCacher(std::unique_ptr<CacherBase> cacher);

    // Serialization across all registered caches
    bool SaveAll(const std::filesystem::path& dir) const;
    bool LoadAll(const std::filesystem::path& dir, void* device);

    void ClearAll();

    template<typename CacherT, typename CI>
    std::shared_ptr<typename CacherT::ResourceT> GetOrCreate(const CI& ci) {
        std::lock_guard lock(m_registryLock);
        for (auto& c : m_cachers) {
            auto casted = dynamic_cast<CacherT*>(c.get());
            if (casted) {
                return casted->GetOrCreate(ci);
            }
        }
        return nullptr;
    }

private:
    MainCacher() = default;
    std::vector<std::unique_ptr<CacherBase>> m_cachers;
    mutable std::mutex m_registryLock;
};

} // namespace CashSystem
