#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "EventBus/MessageBus.h"
#include "EventBus/Message.h"
#include "VulkanResources/VulkanDevice.h"
#include <cassert>

namespace CashSystem {

MainCacher& MainCacher::Instance() {
    static MainCacher instance;
    return instance;
}

void MainCacher::CleanupGlobalCaches() {
    // Cleanup all device-independent cachers
    std::lock_guard lock(m_globalRegistryMutex);
    for (auto& [typeIndex, cacher] : m_globalCachers) {
        if (cacher) {
            cacher->Cleanup();
        }
    }
}

MainCacher::~MainCacher() {
    // Cleanup global caches if not already done
    CleanupGlobalCaches();

    // Unsubscribe from message bus
    if (m_messageBus && m_deviceInvalidationSubscription != 0) {
        m_messageBus->Unsubscribe(m_deviceInvalidationSubscription);
    }
}

void MainCacher::Initialize(::Vixen::EventBus::MessageBus* messageBus) {
    if (messageBus) {
        m_messageBus = messageBus;

        // Subscribe to device invalidation events
        m_deviceInvalidationSubscription = m_messageBus->Subscribe(
            ::Vixen::EventBus::DeviceInvalidationEvent::TYPE,
            [this](const ::Vixen::EventBus::BaseEventMessage& msg) -> bool {
                const auto& event = static_cast<const ::Vixen::EventBus::DeviceInvalidationEvent&>(msg);

                // Cast deviceHandle back to VulkanDevice*
                auto* device = static_cast<::Vixen::Vulkan::Resources::VulkanDevice*>(event.deviceHandle);

                if (device) {
                    // Clear all device-dependent caches for this device
                    ClearDeviceCaches(device);

                    // Log the invalidation (optional)
                    // Logger could be integrated here if needed
                }

                return true; // Event handled
            }
        );
    }
}

DeviceRegistry& MainCacher::GetOrCreateDeviceRegistry(::Vixen::Vulkan::Resources::VulkanDevice* device) {
    DeviceIdentifier deviceId(const_cast<const ::Vixen::Vulkan::Resources::VulkanDevice*>(device));

    std::lock_guard lock(m_deviceRegistriesMutex);

    // Check if registry already exists for this device
    auto it = m_deviceRegistries.find(deviceId);
    if (it != m_deviceRegistries.end()) {
        return it->second;
    }

    // Create new device registry
    auto [newIt, inserted] = m_deviceRegistries.try_emplace(deviceId, deviceId);

    // Initialize the new registry with the device pointer
    if (inserted && device) {
        newIt->second.Initialize(device);
    }

    return newIt->second;
}

DeviceRegistry& MainCacher::GetOrCreateDeviceRegistry(const DeviceIdentifier& deviceId) {
    std::lock_guard lock(m_deviceRegistriesMutex);

    // Check if registry already exists for this device
    auto it = m_deviceRegistries.find(deviceId);
    if (it != m_deviceRegistries.end()) {
        return it->second;
    }

    // Create new device registry
    // Note: This overload doesn't have the device pointer, so registry won't be initialized
    // Only the VulkanDevice* overload above initializes the registry
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
