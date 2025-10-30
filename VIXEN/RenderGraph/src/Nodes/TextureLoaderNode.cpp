#include "Nodes/TextureLoaderNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "TextureHandling/Loading/STBTextureLoader.h"
#include "VulkanResources/VulkanDevice.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/TextureCacher.h"

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

void TextureLoaderNode::Setup() {
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

void TextureLoaderNode::Compile() {
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

    // Cache the cacher reference for use throughout node lifetime
    textureCacher = mainCacher.GetCacher<
        CashSystem::TextureCacher,
        CashSystem::TextureWrapper,
        CashSystem::TextureCreateParams
    >(typeid(CashSystem::TextureWrapper), device);

    bool textureLoaded = false;
    if (textureCacher) {
        NODE_LOG_INFO("TextureLoaderNode: Texture cache ready");
        // TODO: Implement texture caching
        // auto cachedTexture = textureCacher->GetOrCreate(params);
        textureLoaded = false;  // Not yet implemented
    }

    // Fallback to direct loading if cache not available or not yet implemented
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
            textureSampler = textureData.sampler;
            textureMemory = textureData.mem;
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

    // === REGISTER CLEANUP ===
    // Automatically resolves dependency on DeviceNode via VULKAN_DEVICE_IN input
    RegisterCleanup();
}

void TextureLoaderNode::Execute(VkCommandBuffer commandBuffer) {
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

    // Destroy texture resources
    if (isLoaded) {
        if (textureView != VK_NULL_HANDLE) {
            vkDestroyImageView(device->device, textureView, nullptr);
            textureView = VK_NULL_HANDLE;
        }

        if (textureSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device->device, textureSampler, nullptr);
            textureSampler = VK_NULL_HANDLE;
        }

        if (textureImage != VK_NULL_HANDLE) {
            vkDestroyImage(device->device, textureImage, nullptr);
            textureImage = VK_NULL_HANDLE;
        }

        if (textureMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->device, textureMemory, nullptr);
            textureMemory = VK_NULL_HANDLE;
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
