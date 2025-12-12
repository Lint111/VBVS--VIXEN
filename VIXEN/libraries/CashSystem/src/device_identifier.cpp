#include "pch.h"
#include "DeviceIdentifier.h"
#include "MainCacher.h"
#include "VulkanDevice.h"
#include "VixenHash.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <future>
#include <vector>
#include <cstring>

namespace CashSystem {

DeviceIdentifier::DeviceIdentifier(const Vixen::Vulkan::Resources::VulkanDevice* device)
    : m_deviceHash(GenerateDeviceHash(device)), m_deviceIndex(0) {
}

DeviceIdentifier::DeviceIdentifier(uint32_t deviceIndex)
    : m_deviceHash(deviceIndex), m_deviceIndex(deviceIndex) {
}

DeviceIdentifier::DeviceIdentifier(std::uint64_t deviceHash)
    : m_deviceHash(deviceHash), m_deviceIndex(static_cast<uint32_t>(deviceHash & 0xFFFFFFFF)) {
}

std::string DeviceIdentifier::GetDescription() const {
    std::ostringstream oss;
    oss << "Device_0x" << std::hex << m_deviceHash;
    return oss.str();
}

DeviceIdentifier DeviceIdentifier::FromDirectoryName(const std::string& dirName) {
    // Parse from format "Device_0x<hex_hash>"
    std::string prefix = "Device_0x";
    if (dirName.find(prefix) == 0) {
        try {
            std::string hashStr = dirName.substr(prefix.length());
            uint64_t hash = std::stoull(hashStr, nullptr, 16);
            return DeviceIdentifier(hash);
        } catch (...) {
            return DeviceIdentifier(std::uint64_t(0));  // Invalid
        }
    }
    return DeviceIdentifier(std::uint64_t(0));  // Invalid
}

std::uint64_t DeviceIdentifier::GenerateDeviceHash(const Vixen::Vulkan::Resources::VulkanDevice* device) {
    if (!device) {
        return 0;
    }

    // Use stable device properties for persistent cache identification
    // Combine: vendorID + deviceID + driverVersion for a stable hash
    const auto& props = device->gpuProperties;

    std::uint64_t hash = 0;
    // High 32 bits: vendorID
    hash |= (static_cast<std::uint64_t>(props.vendorID) << 32);
    // Low 32 bits: deviceID
    hash |= static_cast<std::uint64_t>(props.deviceID);

    // XOR with driverVersion for additional uniqueness
    hash ^= static_cast<std::uint64_t>(props.driverVersion);

    return hash;
}

// DeviceRegistry implementation
DeviceRegistry::DeviceRegistry(const DeviceIdentifier& deviceId)
    : m_deviceId(deviceId), m_device(nullptr), m_initialized(false) {
    InitializeLogger("DeviceRegistry");
}

void DeviceRegistry::Initialize(Vixen::Vulkan::Resources::VulkanDevice* device) {
    m_device = device;
    m_initialized = (device != nullptr);
    OnInitialize();
}

void DeviceRegistry::ClearAll() {
    m_deviceCachers.clear();
}

bool DeviceRegistry::SaveAll(const std::filesystem::path& directory) const {
    std::filesystem::create_directories(directory);

    // Save cacher registry manifest first (list of active cachers)
    auto manifestPath = directory / "cacher_registry.txt";
    std::ofstream manifest(manifestPath);
    if (!manifest) {
        LOG_ERROR("Failed to create manifest file");
        return false;
    }

    for (const auto& cacher : m_deviceCachers) {
        if (cacher) {
            manifest << cacher->name() << "\n";
        }
    }
    manifest.close();
    LOG_INFO("Saved cacher manifest with " + std::to_string(m_deviceCachers.size()) + " entries");

    // Launch parallel save for each cacher
    std::vector<std::future<bool>> futures;

    for (const auto& cacher : m_deviceCachers) {
        if (cacher) {
            auto cacheName = std::string(cacher->name());
            auto cacheFile = directory / (cacheName + ".cache");

            futures.push_back(std::async(std::launch::async,
                [&cacher, cacheFile]() {
                    bool saved = cacher->SerializeToFile(cacheFile);
                    return saved;
                }
            ));
        }
    }

    // Wait for all saves to complete
    bool success = true;
    for (auto& future : futures) {
        success &= future.get();
    }

    return success;
}

bool DeviceRegistry::LoadAll(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        LOG_INFO("No cache directory found at " + directory.string());
        return true;  // Not an error - just no caches to load
    }

    // Read cacher registry manifest to pre-create cachers BEFORE deserialization
    // Note: Cachers must be registered (via RegisterCacher) before this can succeed
    // If registration hasn't happened yet, deserialization will be deferred until
    // first GetCacher() call (lazy deserialization)
    auto manifestPath = directory / "cacher_registry.txt";
    if (std::filesystem::exists(manifestPath)) {
        std::ifstream manifest(manifestPath);
        if (manifest) {
            std::string cacherName;
            LOG_DEBUG("Pre-creating cachers from manifest...");

            while (std::getline(manifest, cacherName)) {
                // Trim whitespace
                cacherName.erase(0, cacherName.find_first_not_of(" \t\r\n"));
                cacherName.erase(cacherName.find_last_not_of(" \t\r\n") + 1);

                if (!cacherName.empty()) {
                    LOG_DEBUG("Found cacher in manifest: " + cacherName);

                    // Create cacher instance using MainCacher's factory system
                    auto& mainCacher = MainCacher::Instance();
                    auto* createdCacher = mainCacher.CreateCacherByName(cacherName, m_device, *this);

                    if (!createdCacher) {
                        // Not an error - cacher may not be registered yet
                        // Deserialization will happen lazily on first GetCacher() call
                        LOG_DEBUG("Cacher not registered yet (will lazy-load): " + cacherName);
                    }
                }
            }
            manifest.close();

            LOG_INFO("Pre-created " + std::to_string(m_deviceCachers.size()) + " cachers from manifest");
        }
    } else {
        LOG_INFO("No manifest found (legacy or first run)");
    }

    // Launch parallel load for each cacher
    std::vector<std::future<bool>> futures;

    for (auto& cacher : m_deviceCachers) {
        if (cacher) {
            auto cacheFile = directory / (std::string(cacher->name()) + ".cache");

            futures.push_back(std::async(std::launch::async,
                [&cacher, cacheFile, this]() {
                    if (std::filesystem::exists(cacheFile)) {
                        bool loaded = cacher->DeserializeFromFile(cacheFile, m_device);
                        return loaded;
                    } else {
                        return true;  // Not an error
                    }
                }
            ));
        }
    }

    // Wait for all loads to complete
    bool success = true;
    for (auto& future : futures) {
        success &= future.get();
    }

    return success;
}

void DeviceRegistry::OnInitialize() {
    // Hook for derived classes - default no-op
}

} // namespace CashSystem
