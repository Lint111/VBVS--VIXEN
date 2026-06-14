#include "pch.h"
#include "TextureCacher.h"
#include "SamplerCacher.h"
#include "VixenHash.h"
#include "TextureHandling/Loading/TextureLoader.h"
#include "TextureHandling/Loading/STBTextureLoader.h"
#include "VulkanDevice.h"
#include "error/VulkanError.h"
#include <fstream>
#include <sstream>

using namespace Vixen::Hash;

namespace CashSystem {

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreate(const TextureCreateParams& ci) {
    // Call base class implementation
    auto wrapper = TypedCacher<TextureWrapper, TextureCreateParams>::GetOrCreate(ci);

    if (wrapper) {
        LOG_INFO("[TextureCacher::GetOrCreate] Cache hit for texture: " + ci.filePath);
    } else {
        LOG_INFO("[TextureCacher::GetOrCreate] Cache miss for texture: " + ci.filePath);
    }

    return wrapper;
}

std::shared_ptr<TextureWrapper> TextureCacher::GetOrCreateTexture(
    const std::string& filePath,
    std::shared_ptr<SamplerWrapper> samplerWrapper,
    VkFormat format,
    bool generateMipmaps
) {
    TextureCreateParams params{};
    params.filePath = filePath;
    params.format = format;
    params.generateMipmaps = generateMipmaps;
    params.samplerWrapper = samplerWrapper;
    params.fileChecksum = ComputeFileChecksum(filePath);

    return GetOrCreate(params);
}

std::shared_ptr<TextureWrapper> TextureCacher::Create(const TextureCreateParams& ci) {
    auto wrapper = std::make_shared<TextureWrapper>();
    wrapper->filePath = ci.filePath;
    wrapper->format = ci.format;
    wrapper->generateMipmaps = ci.generateMipmaps;

    // Handle sampler via composition pattern
    if (ci.samplerWrapper) {
        // Runtime path: use provided sampler
        wrapper->samplerWrapper = ci.samplerWrapper;
    } else if (ci.samplerParams.has_value()) {
        // Deserialization path: get sampler from SamplerCacher via our owning MainCacher
        // (AR#8: was MainCacher::Instance())
        MainCacher* owner = GetMainCacher();
        if (!owner) {
            throw std::runtime_error("[TextureCacher] no owning MainCacher");
        }
        auto& mainCacher = *owner;
        auto* samplerCacher = mainCacher.GetCacher<SamplerCacher, SamplerWrapper, SamplerCreateParams>(
            std::type_index(typeid(SamplerWrapper)),
            GetDevice()
        );

        if (samplerCacher) {
            wrapper->samplerWrapper = samplerCacher->GetOrCreate(ci.samplerParams.value());
        }
    }

    // Load texture from file (also caches pixel data in wrapper)
    LoadTextureFromFile(ci, *wrapper);

    return wrapper;
}

std::uint64_t TextureCacher::ComputeKey(const TextureCreateParams& ci) const {
    // Build key string from file path + format + mip settings + file checksum
    std::ostringstream keyStream;
    keyStream << ci.filePath << "|"
              << static_cast<uint32_t>(ci.format) << "|"
              << (ci.generateMipmaps ? "1" : "0") << "|"
              << ci.fileChecksum;

    const std::string keyString = keyStream.str();
    return std::hash<std::string>{}(keyString);
}

std::string TextureCacher::ComputeFileChecksum(const std::string& filePath) const {
    try {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return "";
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return "";
        }

        return ComputeSHA256Hex(buffer.data(), buffer.size());
    } catch (const std::exception&) {
        return "";
    }
}

std::vector<uint8_t> TextureCacher::GenerateCheckerboard(uint32_t& outSize) {
    // 256x256 RGBA8 checkerboard with 32x32-pixel cells in two visible greys.
    // Square dimensions keep mip/extent math trivial and the pattern obvious.
    constexpr uint32_t kSize = 256;
    constexpr uint32_t kCell = 32;
    outSize = kSize;

    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4);
    const uint8_t light[4] = {200, 200, 200, 255};  // light grey
    const uint8_t dark[4]  = { 60,  60,  60, 255};  // dark grey

    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const bool isLight = (((x / kCell) + (y / kCell)) & 1u) == 0u;
            const uint8_t* c = isLight ? light : dark;
            const size_t idx = (static_cast<size_t>(y) * kSize + x) * 4;
            pixels[idx + 0] = c[0];
            pixels[idx + 1] = c[1];
            pixels[idx + 2] = c[2];
            pixels[idx + 3] = c[3];
        }
    }
    return pixels;
}

void TextureCacher::LoadTextureFromFile(const TextureCreateParams& ci, TextureWrapper& wrapper) {
    using namespace Vixen::TextureHandling;

    if (!m_device) {
        throw std::runtime_error("[TextureCacher] no device bound; cannot upload texture");
    }

    // Lazily create a transient one-shot command pool for uploads (same pattern as
    // AccelerationStructureCacher). Destroyed in Cleanup().
    if (m_uploadCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_device->graphicsQueueIndex;
        VK_CHECK_LOG(
            vkCreateCommandPool(m_device->device, &poolInfo, nullptr, &m_uploadCommandPool),
            "[TextureCacher] create upload command pool"
        );
    }

    TextureLoadConfig config{};
    config.uploadMode = TextureLoadConfig::UploadMode::Optimal;
    config.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    config.format = ci.format;

    // STBTextureLoader implements file decode; LoadFromMemory feeds the same GPU
    // upload path with generated bytes when no file is provided.
    STBTextureLoader loader(m_device, m_uploadCommandPool);

    VulkanResult<TextureData> loadResult = std::unexpected(
        VulkanError{VK_ERROR_INITIALIZATION_FAILED, "uninitialised"});

    std::vector<uint8_t> generatedPixels;  // kept alive across the upload call
    uint32_t generatedDim = 0;

    if (ci.filePath.empty()) {
        // No asset file: generate an in-memory default checkerboard and upload it
        // through the identical GPU staging path the file branch uses.
        generatedPixels = GenerateCheckerboard(generatedDim);
        LOG_INFO("[TextureCacher::LoadTextureFromFile] filePath empty - generated default checkerboard ("
                 + std::to_string(generatedDim) + "x" + std::to_string(generatedDim) + ")");
        loadResult = loader.LoadFromMemory(
            generatedPixels.data(), generatedDim, generatedDim, config);
    } else {
        LOG_INFO("[TextureCacher::LoadTextureFromFile] Loading texture from file: " + ci.filePath);
        loadResult = loader.Load(ci.filePath.c_str(), config);
    }

    if (!loadResult.has_value()) {
        throw std::runtime_error(
            "[TextureCacher] failed to load/upload texture '"
            + (ci.filePath.empty() ? std::string("<default checkerboard>") : ci.filePath)
            + "': " + loadResult.error().message);
    }

    TextureData data = loadResult.value();

    // TextureLoader also creates an internal sampler and leaves its upload command
    // buffer allocated. The cacher uses SamplerCacher's sampler (composition) and
    // owns image/view/memory only, so release the loader-owned sampler + cmd buffer
    // here to avoid leaks. The transient pool itself is freed in Cleanup().
    if (data.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device->device, data.sampler, nullptr);
        data.sampler = VK_NULL_HANDLE;
    }
    if (data.cmdTexture != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device->device, m_uploadCommandPool, 1, &data.cmdTexture);
        data.cmdTexture = VK_NULL_HANDLE;
    }

    // Adopt the real GPU resources into the wrapper.
    wrapper.image  = data.image;
    wrapper.view   = data.view;
    wrapper.memory = data.mem;
    wrapper.width  = data.textureWidth;
    wrapper.height = data.textureHeight;
    wrapper.mipLevels = data.minMapLevels;
    wrapper.arrayLayers = 1;

    // Cache the CPU-side pixels (key benefit of TextureCacher) for the generated
    // case; for files we keep the decoded bytes unavailable here (STB frees them
    // inside Load) and leave pixelData empty, which is acceptable for caching.
    if (ci.filePath.empty() && !generatedPixels.empty()) {
        wrapper.pixelData = std::move(generatedPixels);
    }

    LOG_INFO("[TextureCacher::LoadTextureFromFile] Texture uploaded successfully ("
             + std::to_string(wrapper.width) + "x" + std::to_string(wrapper.height) + ")");
}

bool TextureCacher::SerializeToFile(const std::filesystem::path& path) const {
    // Serialize texture metadata and cached pixel data

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("SerializeToFile: Failed to open file: " + path.string());
        return false;
    }

    // Write version header
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write number of cached textures
    std::shared_lock lock(m_lock);
    uint32_t cacheSize = static_cast<uint32_t>(m_entries.size());
    file.write(reinterpret_cast<const char*>(&cacheSize), sizeof(cacheSize));

    // Serialize each texture
    for (const auto& [key, entry] : m_entries) {
        const auto& wrapper = entry.resource;

        // Write file path
        uint32_t pathLen = static_cast<uint32_t>(wrapper->filePath.size());
        file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        file.write(wrapper->filePath.data(), pathLen);

        // Write format and dimensions
        file.write(reinterpret_cast<const char*>(&wrapper->format), sizeof(wrapper->format));
        file.write(reinterpret_cast<const char*>(&wrapper->width), sizeof(wrapper->width));
        file.write(reinterpret_cast<const char*>(&wrapper->height), sizeof(wrapper->height));
        file.write(reinterpret_cast<const char*>(&wrapper->mipLevels), sizeof(wrapper->mipLevels));
        file.write(reinterpret_cast<const char*>(&wrapper->arrayLayers), sizeof(wrapper->arrayLayers));
        file.write(reinterpret_cast<const char*>(&wrapper->generateMipmaps), sizeof(wrapper->generateMipmaps));

        // Write cached pixel data (key benefit - avoids reloading/decoding)
        uint32_t pixelDataSize = static_cast<uint32_t>(wrapper->pixelData.size());
        file.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(pixelDataSize));
        if (pixelDataSize > 0) {
            file.write(reinterpret_cast<const char*>(wrapper->pixelData.data()), pixelDataSize);
        }

        // Write sampler parameters (if available)
        if (wrapper->samplerWrapper) {
            uint8_t hasSampler = 1;
            file.write(reinterpret_cast<const char*>(&hasSampler), sizeof(hasSampler));

            // Serialize sampler create params for recreation
            // TODO: Serialize actual SamplerCreateParams once available
            // For now, just write a placeholder
            uint32_t samplerDataSize = 0;
            file.write(reinterpret_cast<const char*>(&samplerDataSize), sizeof(samplerDataSize));
        } else {
            uint8_t hasSampler = 0;
            file.write(reinterpret_cast<const char*>(&hasSampler), sizeof(hasSampler));
        }
    }

    LOG_INFO("SerializeToFile: Serialized " + std::to_string(cacheSize) + " textures to " + path.string());
    return true;
}

bool TextureCacher::DeserializeFromFile(const std::filesystem::path& path, void* device) {
    // Deserialize texture metadata and cached pixel data
    // Vulkan resources are recreated on-demand

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("DeserializeFromFile: Failed to open file: " + path.string());
        return false;
    }

    // Read version header
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        LOG_ERROR("DeserializeFromFile: Unsupported version: " + std::to_string(version));
        return false;
    }

    // Read number of cached textures
    uint32_t cacheSize = 0;
    file.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));

    LOG_INFO("DeserializeFromFile: Loading " + std::to_string(cacheSize) + " textures from " + path.string());

    // Deserialize each texture metadata
    for (uint32_t i = 0; i < cacheSize; ++i) {
        // Read file path
        uint32_t pathLen = 0;
        file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        std::string filePath(pathLen, '\0');
        file.read(filePath.data(), pathLen);

        // Read format and dimensions
        VkFormat format;
        uint32_t width, height, mipLevels, arrayLayers;
        bool generateMipmaps;
        file.read(reinterpret_cast<char*>(&format), sizeof(format));
        file.read(reinterpret_cast<char*>(&width), sizeof(width));
        file.read(reinterpret_cast<char*>(&height), sizeof(height));
        file.read(reinterpret_cast<char*>(&mipLevels), sizeof(mipLevels));
        file.read(reinterpret_cast<char*>(&arrayLayers), sizeof(arrayLayers));
        file.read(reinterpret_cast<char*>(&generateMipmaps), sizeof(generateMipmaps));

        // Read cached pixel data
        uint32_t pixelDataSize = 0;
        file.read(reinterpret_cast<char*>(&pixelDataSize), sizeof(pixelDataSize));
        std::vector<uint8_t> pixelData(pixelDataSize);
        if (pixelDataSize > 0) {
            file.read(reinterpret_cast<char*>(pixelData.data()), pixelDataSize);
        }

        // Read sampler data
        uint8_t hasSampler = 0;
        file.read(reinterpret_cast<char*>(&hasSampler), sizeof(hasSampler));
        if (hasSampler) {
            uint32_t samplerDataSize = 0;
            file.read(reinterpret_cast<char*>(&samplerDataSize), sizeof(samplerDataSize));
            // Skip sampler data for now
        }

        // Note: Actual texture resources are recreated on-demand via GetOrCreate
        // This deserialization validates the file format and prepares metadata
    }

    LOG_INFO("DeserializeFromFile: Texture metadata validated (resources will be recreated on-demand)");

    (void)device;  // Device used when recreating resources on-demand
    return true;
}

void TextureCacher::Cleanup() {
    // Clean up Vulkan resources before clearing cache

    std::unique_lock lock(m_lock);

    for (auto& [key, entry] : m_entries) {
        auto& wrapper = entry.resource;

        if (!wrapper) continue;

        VkDevice device = GetDevice() ? GetDevice()->device : VK_NULL_HANDLE;

        if (device != VK_NULL_HANDLE) {
            // Destroy Vulkan resources
            if (wrapper->view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, wrapper->view, nullptr);
                wrapper->view = VK_NULL_HANDLE;
            }

            if (wrapper->image != VK_NULL_HANDLE) {
                vkDestroyImage(device, wrapper->image, nullptr);
                wrapper->image = VK_NULL_HANDLE;
            }

            if (wrapper->memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, wrapper->memory, nullptr);
                wrapper->memory = VK_NULL_HANDLE;
            }
        }

        // Clear cached pixel data
        wrapper->pixelData.clear();
    }

    // Destroy the transient upload command pool (frees any command buffers still
    // allocated from it as a side effect).
    if (m_uploadCommandPool != VK_NULL_HANDLE) {
        VkDevice device = GetDevice() ? GetDevice()->device : VK_NULL_HANDLE;
        if (device != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, m_uploadCommandPool, nullptr);
        }
        m_uploadCommandPool = VK_NULL_HANDLE;
    }

    // Clear the cache
    m_entries.clear();
    m_pending.clear();

    LOG_INFO("[TextureCacher::Cleanup] Cleaned up all texture resources");
}

} // namespace CashSystem
