#include "CashSystem/MainCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "EventBus/MessageBus.h"
#include "EventBus/Message.h"
#include "VulkanDevice.h"
#include <cassert>

namespace CashSystem {

MainCacher& MainCacher::Instance() {
    static MainCacher instance;
    return instance;
}

void MainCacher::CleanupGlobalCaches() {
    // Cleanup all device-independent cachers
    std::lock_guard lock(m_globalRegistryMutex);

    // Check if m_globalCachers itself is in a valid state (avoid static deinitialization issues)
    if (reinterpret_cast<uintptr_t>(&m_globalCachers) == 0xFFFFFFFFFFFFFFFF) {
        return;  // Already destroyed during static deinitialization
    }

    for (auto& [typeIndex, cacher] : m_globalCachers) {
        // Extra safety: check if cacher unique_ptr is valid before dereferencing
        try {
            if (cacher && cacher.get() != nullptr &&
                reinterpret_cast<uintptr_t>(cacher.get()) != 0xFFFFFFFFFFFFFFFF) {
                cacher->Cleanup();
            }
        } catch (...) {
            // Ignore exceptions during cleanup (likely due to static deinitialization)
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
    // Initialize logger (from ILoggable)
    InitializeLogger("CashSystem", false);  // Disabled by default, enable as needed

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
        // Ensure registry is initialized with the device pointer
        if (device && !it->second.IsInitialized()) {
            it->second.Initialize(device);
        }
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

CacherBase* MainCacher::CreateCacherByName(
    const std::string& name,
    ::Vixen::Vulkan::Resources::VulkanDevice* device,
    DeviceRegistry& registry
) {
    std::cout << "[MainCacher::CreateCacherByName] Creating cacher: " << name << std::endl;

    std::lock_guard lock(m_globalRegistryMutex);

    // Debug: print all registered names
    std::cout << "[MainCacher::CreateCacherByName] Registered cachers: ";
    for (const auto& [regName, factory] : m_nameToFactory) {
        std::cout << regName << " ";
    }
    std::cout << std::endl;

    // Look up factory by name
    auto factoryIt = m_nameToFactory.find(name);
    if (factoryIt == m_nameToFactory.end()) {
        std::cerr << "[MainCacher::CreateCacherByName] Unknown cacher name: " << name << std::endl;
        std::cerr << "[MainCacher::CreateCacherByName] Available names: " << m_nameToFactory.size() << std::endl;
        return nullptr;
    }

    // Check if device-dependent
    auto dependencyIt = m_nameToDeviceDependency.find(name);
    if (dependencyIt == m_nameToDeviceDependency.end()) {
        std::cerr << "[MainCacher::CreateCacherByName] No dependency info for: " << name << std::endl;
        return nullptr;
    }

    bool isDeviceDependent = dependencyIt->second;

    // Verify device availability for device-dependent cachers
    if (isDeviceDependent && !device) {
        std::cerr << "[MainCacher::CreateCacherByName] Device-dependent cacher requires device: " << name << std::endl;
        return nullptr;
    }

    // Create cacher instance
    auto newCacher = factoryIt->second();
    if (!newCacher) {
        std::cerr << "[MainCacher::CreateCacherByName] Factory returned null for: " << name << std::endl;
        return nullptr;
    }

    // Initialize with device (required before DeserializeFromFile)
    newCacher->Initialize(device);
    std::cout << "[MainCacher::CreateCacherByName] Initialized cacher: " << name << std::endl;

    // Store in device registry
    CacherBase* cacherPtr = newCacher.get();
    registry.m_deviceCachers.emplace_back(std::move(newCacher));

    std::cout << "[MainCacher::CreateCacherByName] Created and registered: " << name << std::endl;
    return cacherPtr;
}

} // namespace CashSystem
