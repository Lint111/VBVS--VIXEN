#include "CashSystem/DeviceIdentifier.h"
#include "VulkanResources/VulkanDevice.h"
#include "Hash.h"
#include <sstream>
#include <iostream>
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
        std::cerr << "[DeviceRegistry] Failed to create manifest file" << std::endl;
        return false;
    }

    for (const auto& cacher : m_deviceCachers) {
        if (cacher) {
            manifest << cacher->name() << "\n";
        }
    }
    manifest.close();
    std::cout << "[DeviceRegistry] Saved cacher manifest with " << m_deviceCachers.size() << " entries" << std::endl;

    // Launch parallel save for each cacher
    std::vector<std::future<bool>> futures;

    for (const auto& cacher : m_deviceCachers) {
        if (cacher) {
            auto cacheName = std::string(cacher->name());
            auto cacheFile = directory / (cacheName + ".cache");

            futures.push_back(std::async(std::launch::async,
                [&cacher, cacheName, cacheFile]() {
                    std::cout << "[DeviceRegistry] Saving " << cacheName << " to " << cacheFile << std::endl;
                    bool saved = cacher->SerializeToFile(cacheFile);
                    if (!saved) {
                        std::cerr << "[DeviceRegistry] Failed to save " << cacheName << std::endl;
                    }
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
        std::cout << "[DeviceRegistry] No cache directory found at " << directory << std::endl;
        return true;  // Not an error - just no caches to load
    }

    // Read cacher registry manifest to pre-register cachers
    auto manifestPath = directory / "cacher_registry.txt";
    if (std::filesystem::exists(manifestPath)) {
        std::ifstream manifest(manifestPath);
        if (manifest) {
            std::string cacherName;
            std::cout << "[DeviceRegistry] Pre-registering cachers from manifest..." << std::endl;

            while (std::getline(manifest, cacherName)) {
                // Trim whitespace
                cacherName.erase(0, cacherName.find_first_not_of(" \t\r\n"));
                cacherName.erase(cacherName.find_last_not_of(" \t\r\n") + 1);

                if (!cacherName.empty()) {
                    // Pre-register cacher by name through MainCacher's typed API
                    // This will be handled by MainCacher's GetCacher<T>() which creates if not exists
                    std::cout << "[DeviceRegistry] Found cacher in manifest: " << cacherName << std::endl;
                }
            }
            manifest.close();
        }
    } else {
        std::cout << "[DeviceRegistry] No manifest found (legacy or first run)" << std::endl;
    }

    // Launch parallel load for each cacher
    std::vector<std::future<bool>> futures;

    for (auto& cacher : m_deviceCachers) {
        if (cacher) {
            auto cacheName = std::string(cacher->name());
            auto cacheFile = directory / (cacheName + ".cache");

            futures.push_back(std::async(std::launch::async,
                [&cacher, cacheName, cacheFile, this]() {
                    if (std::filesystem::exists(cacheFile)) {
                        std::cout << "[DeviceRegistry] Loading " << cacheName << " from " << cacheFile << std::endl;
                        bool loaded = cacher->DeserializeFromFile(cacheFile, m_device);
                        if (!loaded) {
                            std::cerr << "[DeviceRegistry] Failed to load " << cacheName << std::endl;
                        }
                        return loaded;
                    } else {
                        std::cout << "[DeviceRegistry] No cache file for " << cacheName << " (first run)" << std::endl;
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
