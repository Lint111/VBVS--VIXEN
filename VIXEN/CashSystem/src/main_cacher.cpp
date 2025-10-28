#include "MainCacher.h"

#include <cassert>

namespace CashSystem {

MainCacher& MainCacher::Instance() {
    static MainCacher instance;
    return instance;
}

void MainCacher::RegisterCacher(std::unique_ptr<CacherBase> cacher) {
    std::lock_guard lock(m_registryLock);
    m_cachers.push_back(std::move(cacher));
}

bool MainCacher::SaveAll(const std::filesystem::path& dir) const {
    std::lock_guard lock(m_registryLock);
    bool ok = true;
    for (const auto& c : m_cachers) {
        auto path = dir / (std::string(c->name()) + ".cache");
        ok = ok && c->SerializeToFile(path);
    }
    return ok;
}

bool MainCacher::LoadAll(const std::filesystem::path& dir, void* device) {
    std::lock_guard lock(m_registryLock);
    bool ok = true;
    for (const auto& c : m_cachers) {
        auto path = dir / (std::string(c->name()) + ".cache");
        ok = ok && c->DeserializeFromFile(path, device);
    }
    return ok;
}

void MainCacher::ClearAll() {
    std::lock_guard lock(m_registryLock);
    for (auto& c : m_cachers) c->Clear();
}

} // namespace CashSystem
