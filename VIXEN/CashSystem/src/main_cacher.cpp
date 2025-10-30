#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/PipelineCacher.h"
#include <cassert>

namespace CashSystem {

MainCacher& MainCacher::Instance() {
    static MainCacher instance;
    return instance;
}

DeviceRegistry& MainCacher::GetOrCreateDeviceRegistry(::Vixen::Vulkan::Resources::VulkanDevice* device) {
    DeviceIdentifier deviceId(const_cast<const ::Vixen::Vulkan::Resources::VulkanDevice*>(device));
    return GetOrCreateDeviceRegistry(deviceId);
}

DeviceRegistry& MainCacher::GetOrCreateDeviceRegistry(const DeviceIdentifier& deviceId) {
    std::lock_guard lock(m_deviceRegistriesMutex);

    // Check if registry already exists for this device
    auto it = m_deviceRegistries.find(deviceId);
    if (it != m_deviceRegistries.end()) {
        return it->second;
    }

    // Create new device registry
    auto [newIt, inserted] = m_deviceRegistries.try_emplace(deviceId, deviceId);
    return newIt->second;
}

bool MainCacher::SaveGlobalCaches(const std::filesystem::path& directory) const {
    std::shared_lock lock(m_globalRegistryMutex);
    // TODO: Implement global cache serialization
    return true;
}

bool MainCacher::LoadGlobalCaches(const std::filesystem::path& directory) {
    std::lock_guard lock(m_globalRegistryMutex);
    // TODO: Implement global cache deserialization
    return true;
}

} // namespace CashSystem
