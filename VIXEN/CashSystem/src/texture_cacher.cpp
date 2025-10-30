#include "CashSystem/TextureCacher.h"
#include "Hash.h"
#include <fstream>
#include <stdexcept>
#include <typeindex>

// Helper function to compute SHA256 - BEFORE including Hash to ensure proper namespace
inline std::string ComputeFileChecksum_Helper_Impl(const void* data, size_t len) {
    // TODO: Fix Hash namespace issue - for now return empty string
    // return ::Vixen::Hash::ComputeSHA256Hex(data, len);
    return "";
}

// Helper function to compute file checksum outside of namespace context
static std::string ComputeFileChecksum_Helper(const std::string& filePath) {
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        return ComputeFileChecksum_Helper_Impl(buffer.data(), buffer.size());
    } catch (const std::exception&) {
        return "";
    }
}

namespace CashSystem {

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreateTexture(
    const std::string& filePath,
    VkFormat format,
    bool generateMipmaps,
    VkFilter minFilter,
    VkFilter magFilter,
    VkSamplerAddressMode addressMode)
{
    TextureCreateParams params;
    params.filePath = filePath;
    params.format = format;
    params.generateMipmaps = generateMipmaps;
    params.minFilter = minFilter;
    params.magFilter = magFilter;
    params.addressModeU = addressMode;
    params.addressModeV = addressMode;
    params.addressModeW = addressMode;
    params.fileChecksum = ComputeFileChecksum(filePath);
    
    return GetOrCreate(params);
}

std::shared_ptr<TextureWrapper> TextureCacher::Create(const TextureCreateParams& ci) {
    auto wrapper = std::make_shared<TextureWrapper>();
    wrapper->filePath = ci.filePath;
    wrapper->format = ci.format;
    wrapper->generateMipmaps = ci.generateMipmaps;
    wrapper->minFilter = ci.minFilter;
    wrapper->magFilter = ci.magFilter;
    wrapper->addressModeU = ci.addressModeU;
    wrapper->addressModeV = ci.addressModeV;
    wrapper->addressModeW = ci.addressModeW;
    
    // Load texture from file
    LoadTextureFromFile(ci, *wrapper);
    
    return wrapper;
}

std::uint64_t TextureCacher::ComputeKey(const TextureCreateParams& ci) const {
    // Combine all parameters into a unique key
    std::ostringstream keyStream;
    keyStream << ci.filePath << "|"
              << ci.format << "|"
              << ci.generateMipmaps << "|"
              << ci.minFilter << "|"
              << ci.magFilter << "|"
              << ci.addressModeU << "|"
              << ci.addressModeV << "|"
              << ci.addressModeW << "|"
              << ci.fileChecksum;
    
    // Use hash function to create 64-bit key
    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

std::string TextureCacher::ComputeFileChecksum(const std::string& filePath) const {
    return ComputeFileChecksum_Helper(filePath);
}

void TextureCacher::LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    // TODO: Integrate with existing TextureLoader from TextureHandling library
    // For now, this is a placeholder implementation
    
    // Placeholder: Create a basic texture
    wrapper.width = 512;
    wrapper.height = 512;
    wrapper.mipLevels = ci.generateMipmaps ? 10 : 1;
    
    // In a real implementation, this would:
    // 1. Load image data using stb_image or similar
    // 2. Create VkImage with proper format and usage
    // 3. Allocate VkDeviceMemory
    // 4. Create VkImageView
    // 5. Create VkSampler with specified filters and address modes
    
    // For MVP, set placeholder handles
    wrapper.image = VK_NULL_HANDLE;
    wrapper.view = VK_NULL_HANDLE;
    wrapper.sampler = VK_NULL_HANDLE;
    wrapper.memory = VK_NULL_HANDLE;
}

bool TextureCacher::SerializeToFile(const std::filesystem::path& path) const {
    // TODO: Implement serialization of texture metadata (not binary data)
    // For now, return true (no-op)
    (void)path;
    return true;
}

bool TextureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // TODO: Implement deserialization and recreate Vulkan objects
    // For now, return true (no-op)
    (void)path;
    (void)device;
    return true;
}

} // namespace CashSystem