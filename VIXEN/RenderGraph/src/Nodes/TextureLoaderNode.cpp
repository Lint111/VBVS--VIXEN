#include "Nodes/TextureLoaderNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "TextureHandling/Loading/STBTextureLoader.h"
#include "VulkanResources/VulkanDevice.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/SamplerCacher.h"

namespace Vixen::RenderGraph {

// ====== TextureLoaderNodeType ======

TextureLoaderNodeType::TextureLoaderNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Transfer;
    requiredCapabilities = DeviceCapability::Transfer;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Populate schemas from Config
    TextureLoaderNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024 * 1024 * 4; // ~4MB for 1K texture
    workloadMetrics.estimatedComputeCost = 0.5f; // Loading is mostly I/O
    workloadMetrics.estimatedBandwidthCost = 2.0f; // High bandwidth for upload
    workloadMetrics.canRunInParallel = true; // Can load multiple textures in parallel
}

std::unique_ptr<NodeInstance> TextureLoaderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<TextureLoaderNode>(
        instanceName,
        const_cast<TextureLoaderNodeType*>(this)
    );
}

// ====== TextureLoaderNode ======

TextureLoaderNode::TextureLoaderNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<TextureLoaderNodeConfig>(instanceName, nodeType)
{
}

TextureLoaderNode::~TextureLoaderNode() {
    Cleanup();
}

void TextureLoaderNode::SetupImpl() {
    // Read and validate device input
    VulkanDevicePtr devicePtr = In(TextureLoaderNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("TextureLoaderNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Create temporary command pool for texture loading
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(device->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool for texture loading");
    }

    // Create texture loader (using STB for common formats)
    textureLoader = std::make_unique<Vixen::TextureHandling::STBTextureLoader>(
        device,
        commandPool
    );
}

void TextureLoaderNode::CompileImpl() {
    // Get parameters using config constants
    std::string filePath = GetParameterValue<std::string>(TextureLoaderNodeConfig::FILE_PATH, "");
    if (filePath.empty()) {
        throw std::runtime_error("TextureLoaderNode: filePath parameter is required");
    }

    std::string uploadModeStr = GetParameterValue<std::string>(TextureLoaderNodeConfig::UPLOAD_MODE, "Optimal");
    bool generateMipmaps = GetParameterValue<bool>(TextureLoaderNodeConfig::GENERATE_MIPMAPS, false);

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register TextureCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::TextureWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::TextureCacher,
            CashSystem::TextureWrapper,
            CashSystem::TextureCreateParams
        >(
            typeid(CashSystem::TextureWrapper),
            "Texture",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("TextureLoaderNode: Registered TextureCacher");
    }

    // Register SamplerCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::SamplerWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::SamplerCacher,
            CashSystem::SamplerWrapper,
            CashSystem::SamplerCreateParams
        >(
            typeid(CashSystem::SamplerWrapper),
            "Sampler",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("TextureLoaderNode: Registered SamplerCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    textureCacher = mainCacher.GetCacher<
        CashSystem::TextureCacher,
        CashSystem::TextureWrapper,
        CashSystem::TextureCreateParams
    >(typeid(CashSystem::TextureWrapper), device);

    // Use TextureCacher to get or create cached texture
    bool textureLoaded = false;

    if (textureCacher) {
        NODE_LOG_INFO("TextureLoaderNode: Using TextureCacher for " + filePath);

        // Build cache parameters
        CashSystem::TextureCreateParams cacheParams{};
        cacheParams.filePath = filePath;
        cacheParams.format = VK_FORMAT_R8G8B8A8_UNORM;
        cacheParams.generateMipmaps = generateMipmaps;
        cacheParams.minFilter = VK_FILTER_LINEAR;
        cacheParams.magFilter = VK_FILTER_LINEAR;
        cacheParams.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        cacheParams.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        cacheParams.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        // Get or create cached texture
        try {
            cachedTextureWrapper = textureCacher->GetOrCreate(cacheParams);

            if (cachedTextureWrapper && cachedTextureWrapper->image != VK_NULL_HANDLE) {
                // Use cached resources
                textureImage = cachedTextureWrapper->image;
                textureView = cachedTextureWrapper->view;
                textureSampler = cachedTextureWrapper->sampler;
                textureMemory = cachedTextureWrapper->memory;
                isLoaded = true;
                textureLoaded = true;

                NODE_LOG_INFO("TextureLoaderNode: Texture loaded from cache successfully");
            }
        } catch (const std::exception& e) {
            NODE_LOG_WARNING("TextureLoaderNode: Cache failed, falling back to direct load: " + std::string(e.what()));
            textureLoaded = false;
        }
    }

    // Fallback to direct loading if cache not available or failed
    if (!textureLoaded) {
        // Configure load settings
        Vixen::TextureHandling::TextureLoadConfig config;
        if (uploadModeStr == "Linear") {
            config.uploadMode = Vixen::TextureHandling::TextureLoadConfig::UploadMode::Linear;
        } else {
            config.uploadMode = Vixen::TextureHandling::TextureLoadConfig::UploadMode::Optimal;
        }

        // Load the texture directly
        try {
            Vixen::TextureHandling::TextureData textureData = textureLoader->Load(filePath.c_str(), config);

            // Store resources for output
            textureImage = textureData.image;
            textureView = textureData.view;
            textureMemory = textureData.mem;

            // Use cached sampler instead of the one created by TextureLoader
            // Get SamplerCacher
            auto* samplerCacher = mainCacher.GetCacher<
                CashSystem::SamplerCacher,
                CashSystem::SamplerWrapper,
                CashSystem::SamplerCreateParams
            >(typeid(CashSystem::SamplerWrapper), device);

            if (samplerCacher) {
                // Build sampler parameters matching TextureLoader defaults (from TextureLoader.cpp:391-412)
                CashSystem::SamplerCreateParams samplerParams{};
                samplerParams.minFilter = VK_FILTER_LINEAR;
                samplerParams.magFilter = VK_FILTER_LINEAR;
                samplerParams.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerParams.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerParams.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerParams.maxAnisotropy = 16.0f;
                samplerParams.compareEnable = VK_FALSE;
                samplerParams.compareOp = VK_COMPARE_OP_NEVER;
                samplerParams.mipLodBias = 0.0f;
                samplerParams.minLod = 0.0f;
                samplerParams.maxLod = static_cast<float>(textureData.minMapLevels);
                samplerParams.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                samplerParams.unnormalizedCoordinates = VK_FALSE;

                // Get or create cached sampler
                cachedSamplerWrapper = samplerCacher->GetOrCreate(samplerParams);
                if (cachedSamplerWrapper && cachedSamplerWrapper->resource != VK_NULL_HANDLE) {
                    // Destroy the inline sampler created by TextureLoader
                    if (textureData.sampler != VK_NULL_HANDLE) {
                        vkDestroySampler(device->device, textureData.sampler, nullptr);
                    }
                    // Use cached sampler
                    textureSampler = cachedSamplerWrapper->resource;
                    NODE_LOG_INFO("TextureLoaderNode: Using cached sampler");
                } else {
                    // Fallback to inline sampler if caching failed
                    textureSampler = textureData.sampler;
                    NODE_LOG_INFO("TextureLoaderNode: Fallback to inline sampler");
                }
            } else {
                // Fallback to inline sampler if cacher unavailable
                textureSampler = textureData.sampler;
                NODE_LOG_INFO("TextureLoaderNode: No sampler cacher available, using inline sampler");
            }

            isLoaded = true;
            textureLoaded = true;

        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load texture: " + std::string(e.what()));
        }
    }

    // Set typed outputs
    Out(TextureLoaderNodeConfig::TEXTURE_IMAGE, textureImage);
    Out(TextureLoaderNodeConfig::TEXTURE_VIEW, textureView);
    Out(TextureLoaderNodeConfig::TEXTURE_SAMPLER, textureSampler);
    Out(TextureLoaderNodeConfig::VULKAN_DEVICE_OUT, device);
}

void TextureLoaderNode::ExecuteImpl() {
    // Texture loading happens in Compile phase
    // Execute phase is a no-op for this node since the texture is already loaded
    
    // The texture is in SHADER_READ_ONLY_OPTIMAL layout after loading
    // If we need to transition to a different layout, we would do it here
}

void TextureLoaderNode::CleanupImpl() {
    // Validate device is still valid before attempting cleanup
    if (device == VK_NULL_HANDLE || device->device == VK_NULL_HANDLE) {
        // Device already destroyed - mark resources as invalid but don't try to destroy them
        textureView = VK_NULL_HANDLE;
        textureSampler = VK_NULL_HANDLE;
        textureImage = VK_NULL_HANDLE;
        textureMemory = VK_NULL_HANDLE;
        commandPool = VK_NULL_HANDLE;
        isLoaded = false;
        textureLoader.reset();
        return;
    }

    // Release cached texture wrapper - cacher owns resources and destroys when appropriate
    if (cachedTextureWrapper) {
        NODE_LOG_DEBUG("TextureLoaderNode: Releasing cached texture wrapper (cacher owns resources)");
        cachedTextureWrapper.reset();
        textureImage = VK_NULL_HANDLE;
        textureView = VK_NULL_HANDLE;
        textureMemory = VK_NULL_HANDLE;
    }

    // Release cached sampler wrapper - cacher owns VkSampler and destroys when appropriate
    if (cachedSamplerWrapper) {
        NODE_LOG_DEBUG("TextureLoaderNode: Releasing cached sampler wrapper (cacher owns resource)");
        cachedSamplerWrapper.reset();
        textureSampler = VK_NULL_HANDLE;
    }

    // Destroy texture resources ONLY if NOT cached (fallback path owns resources)
    if (isLoaded && !cachedTextureWrapper) {
        if (textureView != VK_NULL_HANDLE) {
            vkDestroyImageView(device->device, textureView, nullptr);
            textureView = VK_NULL_HANDLE;
        }

        if (textureImage != VK_NULL_HANDLE) {
            vkDestroyImage(device->device, textureImage, nullptr);
            textureImage = VK_NULL_HANDLE;
        }

        if (textureMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->device, textureMemory, nullptr);
            textureMemory = VK_NULL_HANDLE;
        }

        // Only destroy sampler if NOT cached (fallback path)
        if (textureSampler != VK_NULL_HANDLE && !cachedSamplerWrapper) {
            vkDestroySampler(device->device, textureSampler, nullptr);
            textureSampler = VK_NULL_HANDLE;
        }

        isLoaded = false;
    }

    // Destroy command pool
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    textureLoader.reset();
}

} // namespace Vixen::RenderGraph
