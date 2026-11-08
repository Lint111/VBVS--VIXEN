#include "pch.h"
#include "MainCacher.h"
#include "ShaderModuleCacher.h"
#include "DescriptorCacher.h"
#include "PipelineCacher.h"
#include "MessageBus.h"
#include "Message.h"
#include "VulkanDevice.h"
#include "KernelDispatch/TaskExecutor.h"
#include <cassert>

namespace CashSystem {

void MainCacher::CleanupGlobalCaches() {
    // Cleanup all device-independent cachers (owner phase: shutdown on the owner thread)
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

MainCacher::MainCacher()
    : m_taskExecutor(std::make_unique<::Vixen::KernelDispatch::TaskExecutor>()) {}

MainCacher::~MainCacher() {
    m_taskExecutor->WaitForBlocking();

    // DeviceNode normally performs this phase before destroying VulkanDevice. The
    // owner also covers standalone/fixture contexts that have a live device registry
    // but no DeviceNode to drive the explicit cleanup callback.
    ClearAll();

    // Cleanup global caches if not already done
    CleanupGlobalCaches();

    // Unsubscribe from message bus - ScopedSubscriptions handles this via RAII
    // No manual Unsubscribe() calls needed
}

void MainCacher::Shutdown() {
    // Unsubscribe from message bus BEFORE it destructs - ScopedSubscriptions handles this via RAII
    subscriptions_.UnsubscribeAll();
    m_messageBus = nullptr;
}

void MainCacher::SetBudgetManager(ResourceManagement::DeviceBudgetManager* /*manager*/) {
    // No-op: Budget manager is now owned by VulkanDevice (Sprint 5 Phase 2.5.3)
    // Access via VulkanDevice::GetBudgetManager() instead
}

void MainCacher::Initialize(::Vixen::EventBus::MessageBus* messageBus) {
    // Initialize logger (from ILoggable)
    InitializeLogger("CashSystem", false);  // Disabled by default, enable as needed

    if (messageBus) {
        m_messageBus = messageBus;

        // Subscribe to device invalidation events using RAII
        subscriptions_.SetBus(messageBus);
        subscriptions_.SubscribeWithResult<::Vixen::EventBus::DeviceInvalidationEvent>(
            [this](const ::Vixen::EventBus::DeviceInvalidationEvent& event) -> bool {
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

    // Owner phase: see m_registryWriter
    WriterScope scope(m_registryWriter);

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
    newIt->second.SetOwner(this);  // AR#8: registry creates cachers-by-name via its owner, not Instance()

    // Initialize the new registry with the device pointer
    if (inserted && device) {
        newIt->second.Initialize(device);
        // Note: Budget manager is now accessed via VulkanDevice (Sprint 5 Phase 2.5.3)
    }

    return newIt->second;
}

DeviceRegistry& MainCacher::GetOrCreateDeviceRegistry(const DeviceIdentifier& deviceId) {
    WriterScope scope(m_registryWriter);

    // Check if registry already exists for this device
    auto it = m_deviceRegistries.find(deviceId);
    if (it != m_deviceRegistries.end()) {
        return it->second;
    }

    // Create new device registry
    // Note: This overload doesn't have the device pointer, so registry won't be initialized
    // Only the VulkanDevice* overload above initializes the registry
    auto [newIt, inserted] = m_deviceRegistries.try_emplace(deviceId, deviceId);
    newIt->second.SetOwner(this);  // AR#8: registry creates cachers-by-name via its owner, not Instance()

    return newIt->second;
}

CacherBase* MainCacher::CreateCacherByName(
    const std::string& name,
    ::Vixen::Vulkan::Resources::VulkanDevice* device,
    DeviceRegistry& registry
) {
    // Note: Creating cacher (log message removed - no logging infrastructure in this context)

    // Reads the factory maps only (written during the registration phase); the registry's own
    // cacher list is touched by exactly one blocking-lane task (its device's load).
    // Look up factory by name
    auto factoryIt = m_nameToFactory.find(name);
    if (factoryIt == m_nameToFactory.end()) {
        return nullptr;
    }

    // Check if device-dependent
    auto dependencyIt = m_nameToDeviceDependency.find(name);
    if (dependencyIt == m_nameToDeviceDependency.end()) {
        return nullptr;
    }

    bool isDeviceDependent = dependencyIt->second;

    // Verify device availability for device-dependent cachers
    if (isDeviceDependent && !device) {
        return nullptr;
    }

    // Create cacher instance
    auto newCacher = factoryIt->second();
    if (!newCacher) {
        return nullptr;
    }
    newCacher->SetMainCacher(this);  // AR#8: cacher reaches siblings via its owner, not Instance()
    newCacher->SetCacheDeviceIdentity(registry.GetDeviceIdentifier().GetHash());

    // Initialize with device (required before DeserializeFromFile)
    newCacher->Initialize(device);

    // Store in device registry
    CacherBase* cacherPtr = newCacher.get();
    registry.m_deviceCachers.emplace_back(std::move(newCacher));

    return cacherPtr;
}

std::future<bool> MainCacher::SaveAllAsync(const std::filesystem::path& directory) const {
    std::vector<std::function<bool()>> tasks;
    tasks.reserve(m_deviceRegistries.size() + 1);
    for (const auto& [deviceId, registry] : m_deviceRegistries) {
        auto deviceDir = directory / "devices" / deviceId.GetDescription();
        auto* registryPtr = &registry;
        tasks.emplace_back([deviceDir, registryPtr]() {
            std::filesystem::create_directories(deviceDir);
            return registryPtr->SaveAll(deviceDir);
        });
    }

    tasks.emplace_back([this, directory]() {
        auto globalDir = directory / "global";
        std::filesystem::create_directories(globalDir);
        return SaveGlobalCaches(globalDir);
    });
    return m_taskExecutor->SubmitBlockingBatch(std::move(tasks));
}

std::future<bool> MainCacher::LoadAllAsync(const std::filesystem::path& directory) {
    std::vector<std::function<bool()>> tasks;
    auto devicesDir = directory / "devices";
    if (std::filesystem::exists(devicesDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(devicesDir)) {
            if (!entry.is_directory()) continue;
            auto deviceDir = entry.path();
            auto deviceId = DeviceIdentifier::FromDirectoryName(deviceDir.filename().string());
            if (!deviceId.IsValid()) continue;
            // Membership is owner-phase: create the registry here, on the caller. The blocking
            // lane then touches only this registry (map nodes are address-stable).
            auto* registryPtr = &GetOrCreateDeviceRegistry(deviceId);
            tasks.emplace_back([registryPtr, deviceDir]() {
                return registryPtr->LoadAll(deviceDir);
            });
        }
    }

    auto globalDir = directory / "global";
    if (std::filesystem::exists(globalDir)) {
        tasks.emplace_back([this, globalDir]() { return LoadGlobalCaches(globalDir); });
    }
    return m_taskExecutor->SubmitBlockingBatch(std::move(tasks));
}

bool MainCacher::SaveGlobalCaches(const std::filesystem::path& directory) const {
    LOG_INFO("[MainCacher] Saving global caches to " + directory.string());

    // Save the manifest atomically with the same envelope checks as cacher payloads.
    auto manifestPath = directory / "manifest.txt";
    std::ostringstream manifest;
    for (const auto& [typeIndex, cacher] : m_globalCachers) {
        if (cacher) {
            manifest << cacher->name() << "\n";
        }
    }
    const auto manifestText = manifest.str();
    const std::vector<char> manifestBytes(manifestText.begin(), manifestText.end());
    std::string manifestError;
    if (!WriteCacheEnvelopeBytesAtomically(
            manifestPath, 0, "global-cacher-registry", manifestBytes, &manifestError)) {
        LOG_ERROR("[MainCacher] Failed to atomically save global cacher manifest: " + manifestError);
        return false;
    }
    LOG_INFO("[MainCacher] Saved global cacher manifest with " + std::to_string(m_globalCachers.size()) + " entries");

    std::vector<std::function<bool()>> tasks;

    for (const auto& [typeIndex, cacher] : m_globalCachers) {
        if (cacher) {
            auto cacheName = std::string(cacher->name());
            auto cacheFile = directory / (cacheName + ".cache");

            // Capture raw pointer for thread safety (cacher is owned by m_globalCachers which is stable)
            CacherBase* cacherPtr = cacher.get();

            LOG_DEBUG("[MainCacher] Queueing blocking save for " + cacheName);
            tasks.emplace_back([cacherPtr, cacheFile]() {
                return cacherPtr->SerializeToFile(cacheFile);
            });
        }
    }

    // Wait for all saves to complete. The batch future preserves this method's synchronous
    // contract while the actual file operations run only on the blocking lane.
    bool success = m_taskExecutor->SubmitBlockingBatch(std::move(tasks)).get();

    LOG_INFO("[MainCacher] Global cache save " + std::string(success ? "succeeded" : "failed"));
    return success;
}

bool MainCacher::LoadGlobalCaches(const std::filesystem::path& directory) {
    LOG_INFO("[MainCacher] Loading global caches from " + directory.string());

    if (!std::filesystem::exists(directory)) {
        LOG_DEBUG("[MainCacher] No global cache directory found");
        return true;  // Not an error
    }

    std::vector<std::function<bool()>> tasks;

    for (const auto& [typeIndex, cacher] : m_globalCachers) {
        if (cacher) {
            auto cacheName = std::string(cacher->name());
            auto cacheFile = directory / (cacheName + ".cache");

            if (std::filesystem::exists(cacheFile)) {
                // Capture raw pointer for thread safety (cacher is owned by m_globalCachers which is stable)
                CacherBase* cacherPtr = cacher.get();

                LOG_DEBUG("[MainCacher] Queueing blocking load for " + cacheName);
                tasks.emplace_back([cacherPtr, cacheFile]() {
                    return cacherPtr->DeserializeFromFile(cacheFile, nullptr);
                });
            }
        }
    }

    bool success = m_taskExecutor->SubmitBlockingBatch(std::move(tasks)).get();

    LOG_INFO("[MainCacher] Global cache load " + std::string(success ? "succeeded" : "failed"));
    return success;
}

} // namespace CashSystem
