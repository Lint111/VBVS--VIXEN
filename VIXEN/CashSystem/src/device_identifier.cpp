#include "CashSystem/DeviceIdentifier.h"
#include "Hash.h"
#include <sstream>

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
    // Simple hash: use pointer address
    return reinterpret_cast<std::uint64_t>(device);
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
    // TODO: Implement serialization
    return true;
}

bool DeviceRegistry::LoadAll(const std::filesystem::path& directory) {
    // TODO: Implement deserialization
    return true;
}

void DeviceRegistry::OnInitialize() {
    // Hook for derived classes - default no-op
}

} // namespace CashSystem
